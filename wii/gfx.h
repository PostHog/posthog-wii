#ifndef GFX_H
#define GFX_H

#include <stddef.h>
#include <stdint.h>

/* The canvas is 480 units tall with y pointing down. Its width follows the
   console's screen setting, 640 for 4:3 and 854 for 16:9, so a square stays
   square on both. */
enum { GFX_HEIGHT = 480 };

/* Takes over the display from the text console. */
void gfx_init(void);

float gfx_width(void);

void gfx_rect(float x, float y, float width, float height, uint32_t rgb);

/* Draws one cell of the sprite atlas with its top-left corner at x, y. */
void gfx_frame(float x, float y, unsigned frame);

/* Shows what was drawn since the last call and waits for the next frame. */
void gfx_present(void);

/* The display buffer that the last gfx_present showed: len bytes, two
   pixels in every four as Y0, Cb, Y1, Cr. For debug builds. */
const uint8_t *gfx_displayed(unsigned *width, unsigned *height, size_t *len);

#endif
