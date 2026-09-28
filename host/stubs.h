/* Just enough ESP-IDF to compile the launcher's UI on a desktop. */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND        0x105
#define ESP_ERR_INVALID_STATE    0x103
#define ESP_ERR_INVALID_ARG      0x102
#define ESP_ERR_INVALID_VERSION  0x10A
#define ESP_ERR_INVALID_SIZE     0x104

#define MALLOC_CAP_DMA  0
#define MALLOC_CAP_8BIT 0
static inline void *heap_caps_malloc(size_t n, int caps)
{
    (void)caps;
    return malloc(n);
}

#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__)

typedef enum { ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1 } esp_partition_type_t;
typedef int esp_partition_subtype_t;
typedef struct { size_t size; } esp_partition_t;

#ifdef __cplusplus
extern "C" {
#endif
const esp_partition_t *esp_partition_find_first(esp_partition_type_t t, esp_partition_subtype_t s, const char *label);
#ifdef __cplusplus
}
#endif
typedef int esp_partition_mmap_handle_t;
#define ESP_PARTITION_MMAP_DATA 0
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_partition_mmap(const esp_partition_t *p, size_t off, size_t n, int kind,
                             const void **out, esp_partition_mmap_handle_t *h);
static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "error"; }
static inline void heap_caps_free(void *p) { free(p); }
#ifdef __cplusplus
}
#endif
