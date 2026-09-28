#include "mqart.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "mqart";

#define MQART_MAGIC     "MQ04"
#define MQART_SUBTYPE   0x40
#define DISK_IMG_SZ     12      /* u16 w, u16 h, u32 off, u32 len */
#define DISK_ENTRY_SZ   (12 + 12 + 24 + 24 + 4 * DISK_IMG_SZ)
#define SNAP_HEAD       (MQART_SNAP_COLOURS * 3)

static const uint8_t *s_map;            /* the whole partition */
static size_t s_size;
static mqart_entry_t s_entries[MQART_MAX];
static int s_count;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Where a picture's row table starts, within the picture. */
static uint32_t head_of(const mqart_img_t *im)
{
    for (int i = 0; i < s_count; i++)
        if (im == &s_entries[i].snap) return SNAP_HEAD;
    return 0;
}

/* Read one picture's description. One that would read outside the partition or
 * overflow the panel is dropped - the entry survives, and is drawn without it. */
static void read_img(const uint8_t *d, mqart_img_t *im, uint32_t head, const char *rom, const char *what)
{
    im->w = rd16(d); im->h = rd16(d + 2); im->off = rd32(d + 4); im->len = rd32(d + 8);
    if (im->w == 0) return;
    if (im->h == 0 || im->w > MQART_MAX_W || im->h > MQART_MAX_H ||
        im->off > s_size || im->len > s_size - im->off || im->len < head + 4u * im->h) {
        ESP_LOGW(TAG, "%s: %s is malformed, ignored", rom, what);
        memset(im, 0, sizeof *im);
    }
}

esp_err_t mqart_init(void)
{
    s_count = 0;
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      (esp_partition_subtype_t)MQART_SUBTYPE, "mqart");
    if (!part) {
        ESP_LOGE(TAG, "no 'mqart' partition - flash it with tools/flash_mqart.sh");
        return ESP_ERR_NOT_FOUND;
    }
    if (!s_map) {
        esp_partition_mmap_handle_t h;
        const void *p;
        esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &p, &h);
        if (err != ESP_OK) { ESP_LOGE(TAG, "cannot map the partition: %s", esp_err_to_name(err)); return err; }
        s_map = p;
        s_size = part->size;
    }

    if (memcmp(s_map, MQART_MAGIC, 4) != 0) {
        ESP_LOGE(TAG, "bad magic (want %s) - artwork not flashed, or from an older build", MQART_MAGIC);
        return ESP_ERR_INVALID_STATE;
    }
    int count = rd16(s_map + 4);
    if (rd16(s_map + 6) != DISK_ENTRY_SZ) {
        ESP_LOGE(TAG, "entry size %d, expected %d", rd16(s_map + 6), DISK_ENTRY_SZ);
        return ESP_ERR_INVALID_VERSION;
    }
    if (count > MQART_MAX) {
        ESP_LOGW(TAG, "blob holds %d entries, keeping first %d", count, MQART_MAX);
        count = MQART_MAX;
    }
    if (8 + (size_t)count * DISK_ENTRY_SZ > s_size) return ESP_ERR_INVALID_SIZE;

    for (int i = 0; i < count; i++) {
        const uint8_t *e = s_map + 8 + (size_t)i * DISK_ENTRY_SZ;
        mqart_entry_t *m = &s_entries[i];
        memset(m, 0, sizeof *m);
        memcpy(m->rom, e, 12);
        memcpy(m->boot, e + 12, 12);
        memcpy(m->title, e + 24, 24);
        memcpy(m->by, e + 48, 24);
        if (m->boot[0] == 0) memcpy(m->boot, m->rom, sizeof m->boot);   /* default: boot self */
        for (int k = 0; k < MQART_LOGOS; k++)
            read_img(e + 72 + k * DISK_IMG_SZ, &m->logo[k], 0, m->rom, "a logo");
        read_img(e + 72 + MQART_LOGOS * DISK_IMG_SZ, &m->snap, SNAP_HEAD, m->rom, "the snap");
    }
    s_count = count;

    ESP_LOGI(TAG, "%d entries in %u KB partition", s_count, (unsigned)(s_size / 1024));
    return s_count ? ESP_OK : ESP_ERR_INVALID_STATE;
}

int mqart_count(void) { return s_count; }

const mqart_entry_t *mqart_get(int i)
{
    return (i >= 0 && i < s_count) ? &s_entries[i] : NULL;
}

int mqart_find(const char *rom)
{
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_entries[i].rom, rom) == 0) return i;
    return -1;
}

const char *mqart_boot_label(const char *rom)
{
    int i = mqart_find(rom);
    return (i >= 0 && s_entries[i].boot[0]) ? s_entries[i].boot : rom;
}

const uint8_t *mqart_snap_colours(const mqart_entry_t *e)
{
    return (e && e->snap.w) ? s_map + e->snap.off : NULL;
}

bool mqart_row(const mqart_img_t *im, int y, uint8_t *dst)
{
    if (!s_map || !im || !im->w || y < 0 || y >= im->h) return false;
    const uint8_t *img = s_map + im->off, *end = img + im->len;
    uint32_t at = rd32(img + head_of(im) + 4u * (uint32_t)y);
    if (at >= im->len) return false;

    /* control < 128: that many + 1 bytes follow as they are; otherwise the next
     * byte, control - 126 times. Nothing is taken on trust: a row that runs past
     * the picture or past its own width stops there. */
    const uint8_t *s = img + at;
    int left = im->w;
    while (left > 0) {
        if (s >= end) return false;
        int c = *s++;
        if (c < 128) {
            int n = c + 1;
            if (n > left || n > end - s) return false;
            memcpy(dst, s, (size_t)n);
            s += n; dst += n; left -= n;
        } else {
            int n = c - 126;
            if (n > left || s >= end) return false;
            memset(dst, *s++, (size_t)n);
            dst += n; left -= n;
        }
    }
    return true;
}
