#include "gfx.h"

#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <gccore.h>
#include <ogc/tpl.h>

#include "atlas.h"
#include "hedgehog_tpl.h"

/* The setup follows the gxSprites example that ships with libogc. */

enum {
    CONSOLE_MARGIN = 20,
    FIFO_SIZE = 256 * 1024,
    FIFO_ALIGNMENT = 32,
    CANVAS_WIDTH_4_3 = 640,
    CANVAS_WIDTH_16_9 = 854,
    FAR_PLANE = 300,
    DRAW_DEPTH = 5,
    OPAQUE = 0xFF,
    UNTINTED = 0xFFFFFF,
};

/* A color is 0xRRGGBB. */
static const unsigned RED_SHIFT = 16;
static const unsigned GREEN_SHIFT = 8;

/* The middle of an atlas cell, in cells. */
static const float CELL_MIDDLE = 0.5F;

/* A rectangle on the canvas, or in the atlas as texture coordinates. */
typedef struct {
    float left;
    float top;
    float right;
    float bottom;
} area;

static GXRModeObj *mode;
static void *framebuffers[2];
static unsigned back_buffer;
static GXTexObj atlas;

static void vertex(gfx_point position, uint32_t rgb, gfx_point texel) {
    GX_Position2f32(position.x, position.y);
    GX_Color4u8((u8)(rgb >> RED_SHIFT), (u8)(rgb >> GREEN_SHIFT), (u8)rgb, OPAQUE);
    GX_TexCoord2f32(texel.x, texel.y);
}

static void draw_quad(area screen, uint32_t rgb, area texture) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    vertex((gfx_point){screen.left, screen.top}, rgb, (gfx_point){texture.left, texture.top});
    vertex((gfx_point){screen.right, screen.top}, rgb, (gfx_point){texture.right, texture.top});
    vertex((gfx_point){screen.right, screen.bottom}, rgb, (gfx_point){texture.right, texture.bottom});
    vertex((gfx_point){screen.left, screen.bottom}, rgb, (gfx_point){texture.left, texture.bottom});
    GX_End();
}

/* The heap can hand out memory that still has dirty lines in the data cache.
   The app reaches a display buffer only through its uncached address, so a
   line written back later would show on screen. */
static void *new_framebuffer(void) {
    void *cached = SYS_AllocateFramebuffer(mode);
    DCInvalidateRange(cached, VIDEO_GetFrameBufferSize(mode));
    void *uncached = MEM_K0_TO_K1(cached);
    VIDEO_ClearFrameBuffer(mode, uncached, COLOR_BLACK);
    return uncached;
}

void gfx_console(void) {
    VIDEO_Init();
    mode = VIDEO_GetPreferredMode(NULL);
    void *framebuffer = new_framebuffer();
    console_init(framebuffer, CONSOLE_MARGIN, CONSOLE_MARGIN, mode->fbWidth - CONSOLE_MARGIN,
                 mode->xfbHeight - CONSOLE_MARGIN, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(framebuffer);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }
}

float gfx_init(void) {
    mode = VIDEO_GetPreferredMode(NULL);
    framebuffers[0] = new_framebuffer();
    framebuffers[1] = new_framebuffer();
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(framebuffers[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }
    back_buffer = 1U;

    void *fifo = memalign(FIFO_ALIGNMENT, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    /* The CPU sends commands through the write-gather pipe, which skips the
       data cache. A dirty line left by the memset would later overwrite
       commands in memory that the GPU has not read yet. */
    DCFlushRange(fifo, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);

    GX_SetViewport(0, 0, mode->fbWidth, mode->efbHeight, 0, 1);
    GX_SetDispCopyYScale(GX_GetYScaleFactor(mode->efbHeight, mode->xfbHeight));
    GX_SetScissor(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopySrc(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopyDst(mode->fbWidth, mode->xfbHeight);
    GX_SetCopyFilter(mode->aa, mode->sample_pattern, GX_TRUE, mode->vfilter);
    GX_SetFieldMode(mode->field_rendering, mode->viHeight == 2 * mode->xfbHeight ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(mode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDispCopyGamma(GX_GM_1_0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    /* Each pixel is the texture color times the vertex color. A flat
       rectangle is the white atlas cell tinted by its vertex color. */
    GX_SetNumChans(1);
    GX_SetNumTexGens(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);

    TPLFile tpl;
    TPL_OpenTPLFromMemory(&tpl, (void *)hedgehog_tpl, hedgehog_tpl_size);
    TPL_GetTexture(&tpl, 0, &atlas);
    GX_InitTexObjFilterMode(&atlas, GX_NEAR, GX_NEAR);
    GX_InvalidateTexAll();
    GX_LoadTexObj(&atlas, GX_TEXMAP0);

    /* A 16:9 console stretches the 640-pixel frame sideways on the TV. The
       wider canvas squeezes the drawing by the same factor first. */
    float width = CONF_GetAspectRatio() == CONF_ASPECT_16_9 ? CANVAS_WIDTH_16_9 : CANVAS_WIDTH_4_3;
    Mtx44 projection;
    guOrtho(projection, 0, GFX_HEIGHT, 0, width, 0, FAR_PLANE);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    Mtx view;
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0, 0, -DRAW_DEPTH);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    return width;
}

void gfx_rect(float left, float top, float width, float height, uint32_t rgb) {
    /* Every corner samples the middle of the white cell. */
    unsigned column = ATLAS_WHITE % ATLAS_COLUMNS;
    unsigned row = ATLAS_WHITE / ATLAS_COLUMNS;
    gfx_point white = {
        ((float)column + CELL_MIDDLE) * (float)ATLAS_CELL / (float)ATLAS_WIDTH,
        ((float)row + CELL_MIDDLE) * (float)ATLAS_CELL / (float)ATLAS_HEIGHT,
    };
    draw_quad((area){left, top, left + width, top + height}, rgb, (area){white.x, white.y, white.x, white.y});
}

void gfx_frame(unsigned frame, gfx_point corner, unsigned scale) {
    unsigned column = frame % ATLAS_COLUMNS;
    unsigned row = frame / ATLAS_COLUMNS;
    area texture = {
        .left = (float)(column * ATLAS_CELL) / (float)ATLAS_WIDTH,
        .top = (float)(row * ATLAS_CELL) / (float)ATLAS_HEIGHT,
    };
    texture.right = texture.left + ((float)ATLAS_CELL / (float)ATLAS_WIDTH);
    texture.bottom = texture.top + ((float)ATLAS_CELL / (float)ATLAS_HEIGHT);
    float size = (float)(ATLAS_CELL * scale);
    draw_quad((area){corner.x, corner.y, corner.x + size, corner.y + size}, UNTINTED, texture);
}

void gfx_present(void) {
    GX_DrawDone();
    GX_CopyDisp(framebuffers[back_buffer], GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(framebuffers[back_buffer]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    back_buffer ^= 1U;
}

/* Dolphin writes this buffer to emulated memory only when its graphics hack
   "XFBToTextureEnable" is off. */
gfx_picture gfx_displayed(void) {
    return (gfx_picture){
        .pixels = framebuffers[back_buffer ^ 1U],
        .len = (size_t)mode->fbWidth * mode->xfbHeight * VI_DISPLAY_PIX_SZ,
        .width = mode->fbWidth,
        .height = mode->xfbHeight,
    };
}
