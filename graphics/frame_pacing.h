#ifndef FRAME_PACING_H
#define FRAME_PACING_H

#include <stdint.h>

/*
 * Tiny64 frame pacing.
 *
 * Limits how frequently expensive graphical frames are
 * presented to the framebuffer.
 *
 * This does not guarantee VSync, but it prevents the
 * renderer from generating unnecessary frames.
 */

#define FRAME_TARGET_FPS 60

void frame_pacing_init(void);

/*
 * Returns 1 when enough time has passed to render
 * another frame.
 *
 * Returns 0 when the renderer should wait.
 */
int frame_pacing_ready(void);

#endif