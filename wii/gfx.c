#include "gfx.h"

#include <malloc.h>
#include <string.h>

#include <gccore.h>
#include <ogc/tpl.h>

#include "atlas.h"
#include "hedgehog_tpl.h"

/* The setup follows the gxSprites example that ships with libogc. */

enum { FIFO_SIZE = 256 * 1024 };

static GXRModeObj *mode;
static void *framebuffers[2];
static int back_buffer;
static GXTexObj atlas;
static float canvas_width;

static void vertex(float x, float y, uint32_t rgb, float s, float t) {
    GX_Position2f32(x, y);
    GX_Color4u8((u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb, 0xFF);
    GX_TexCoord2f32(s, t);
}

static void quad(float x, float y, float width, float height, uint32_t rgb, float s0, float t0, float s1, float t1) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    vertex(x, y, rgb, s0, t0);
    vertex(x + width, y, rgb, s1, t0);
    vertex(x + width, y + height, rgb, s1, t1);
    vertex(x, y + height, rgb, s0, t1);
    GX_End();
}

void gfx_init(void) {
    mode = VIDEO_GetPreferredMode(NULL);
    framebuffers[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    framebuffers[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(framebuffers[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }
    back_buffer = 1;

    void *fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
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
    canvas_width = CONF_GetAspectRatio() == CONF_ASPECT_16_9 ? 854 : 640;
    Mtx44 projection;
    guOrtho(projection, 0, GFX_HEIGHT, 0, canvas_width, 0, 300);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    Mtx view;
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0, 0, -5);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
}

float gfx_width(void) {
    return canvas_width;
}

void gfx_rect(float x, float y, float width, float height, uint32_t rgb) {
    float s = (ATLAS_WHITE % ATLAS_COLUMNS + 0.5f) * ATLAS_CELL / ATLAS_WIDTH;
    float t = (ATLAS_WHITE / ATLAS_COLUMNS + 0.5f) * ATLAS_CELL / ATLAS_HEIGHT;
    quad(x, y, width, height, rgb, s, t, s, t);
}

void gfx_frame(float x, float y, unsigned frame) {
    float s0 = (float)(frame % ATLAS_COLUMNS * ATLAS_CELL) / ATLAS_WIDTH;
    float t0 = (float)(frame / ATLAS_COLUMNS * ATLAS_CELL) / ATLAS_HEIGHT;
    float s1 = s0 + (float)ATLAS_CELL / ATLAS_WIDTH;
    float t1 = t0 + (float)ATLAS_CELL / ATLAS_HEIGHT;
    quad(x, y, ATLAS_CELL, ATLAS_CELL, 0xFFFFFF, s0, t0, s1, t1);
}

void gfx_present(void) {
    GX_DrawDone();
    GX_CopyDisp(framebuffers[back_buffer], GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(framebuffers[back_buffer]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    back_buffer ^= 1;
}

/* Dolphin writes this buffer to emulated memory only when its graphics hack
   "XFBToTextureEnable" is off. */
const uint8_t *gfx_displayed(unsigned *width, unsigned *height, size_t *len) {
    *width = mode->fbWidth;
    *height = mode->xfbHeight;
    *len = (size_t)mode->fbWidth * mode->xfbHeight * VI_DISPLAY_PIX_SZ;
    return framebuffers[back_buffer ^ 1];
}
