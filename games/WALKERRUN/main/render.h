#pragma once
#include <stdint.h>
#include <stddef.h>
#include "avg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Rendering runs in its own FreeRTOS task so the panel's DMA transfer overlaps
 * with emulation. The emulator hands over completed vector lists; if the
 * renderer is busy the frame is dropped (the game keeps running in real time). */
void render_init(void);
void render_submit(const avg_point_t *points, int npoints);   /* called from the emulator task */
uint32_t render_frames_drawn(void);
uint32_t render_frames_dropped(void);
uint64_t render_busy_us(void);                                /* CPU time spent rasterizing/converting */
/* With no frames being submitted, wait until the render task is
 * idle, then borrow the frame buffer as scratch memory (it is rebuilt from scratch every frame). */
void render_wait_idle(void);
uint8_t *render_scratch(size_t *size);

#ifdef __cplusplus
}
#endif
