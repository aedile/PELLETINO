/*
 * pacman_video.h - Pac-Man Video Rendering
 */

#ifndef PACMAN_VIDEO_H
#define PACMAN_VIDEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Tile dimensions
#define TILE_WIDTH      8
#define TILE_HEIGHT     8
#define TILES_X         28
#define TILES_Y         36

// Sprite dimensions
#define SPRITE_WIDTH    16
#define SPRITE_HEIGHT   16
#define MAX_SPRITES     8

/**
 * Initialize video subsystem
 */
void pacman_video_init(void);

/**
 * Set tile graphics data
 */
void pacman_video_set_tiles(const uint16_t *tiles);

/**
 * Set sprite graphics data
 */
void pacman_video_set_sprites(const uint32_t *sprites);

/**
 * Set color palette
 */
void pacman_video_set_palette(const uint16_t *palette);

/**
 * Render a complete frame
 * @param memory Pointer to video/color/sprite RAM
 */
void pacman_video_render_frame(const uint8_t *memory);

#ifdef __cplusplus
}
#endif

#endif // PACMAN_VIDEO_H
