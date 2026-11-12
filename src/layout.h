/*
 * layout.h - the "where do I crop, and where do I paste it" math.
 *
 * This file has NO Windows or Direct3D code in it on purpose, so the same
 * logic can be unit-tested on any machine (see tests/test_layout.c).
 *
 * THE IDEA
 * --------
 * RE1 HD always renders into a 16:9 "canvas" that is centred inside the
 * back buffer. On a 4:3 monitor that canvas is letterboxed (black bars on
 * top and bottom). The game's built-in 4:3 option then draws a 4:3 picture
 * in the middle of that 16:9 canvas (black bars left and right).
 *
 *      back buffer (4:3 monitor, e.g. 1280x960)
 *      +--------------------------------------+
 *      |            black (letterbox)         |
 *      |      +---+--------------+---+        |
 *      |      |   |  4:3 CONTENT |   |  <- 16:9 canvas
 *      |      |   |              |   |        |
 *      |      +---+--------------+---+        |
 *      |            black (letterbox)         |
 *      +--------------------------------------+
 *
 * We cut the 4:3 CONTENT rectangle out of the finished frame and scale it
 * UNIFORMLY (same factor in X and Y) to fill the whole back buffer. Because
 * the factor is uniform, menus, inventory and videos are never stretched or
 * squeezed.
 *
 * If the back buffer is not exactly 4:3 (a 5:4 1280x1024 monitor, say) we
 * can either:
 *   FILL_COVER   - crop a little off the left/right of the content so it
 *                  fills the screen with no bars (default).
 *   FILL_FIT     - keep all of the content and accept thin black bars.
 *   FILL_STRETCH - stretch to fill (distorts slightly; not recommended).
 */
#ifndef RE1_LAYOUT_H
#define RE1_LAYOUT_H

typedef struct {
    int left, top, right, bottom;   /* same memory layout as a Win32 RECT */
} Rect;

typedef enum {
    FILL_FIT     = 0,
    FILL_COVER   = 1,
    FILL_STRETCH = 2
} FillMode;

typedef struct {
    Rect canvas;        /* where we believe the game's own canvas is        */
    Rect content;       /* the part of the canvas we want to show           */
    Rect src;           /* region copied out of the finished frame          */
    Rect dst;           /* region of the back buffer it is pasted into      */
    int  dstHasBars;    /* 1 if dst does not cover the whole back buffer    */
} Layout;

/* Largest w x h that has aspect aspW:aspH and fits inside boxW x boxH. */
static void FitAspect(int boxW, int boxH, int aspW, int aspH, int *w, int *h)
{
    if (boxW * aspH <= boxH * aspW) {       /* box is "narrower": width limits */
        *w = boxW;
        *h = boxW * aspH / aspW;
    } else {                                /* box is "wider": height limits   */
        *h = boxH;
        *w = boxH * aspW / aspH;
    }
}

/* A w x h rectangle centred inside outerW x outerH. */
static Rect CentredRect(int outerW, int outerH, int w, int h)
{
    Rect r;
    r.left   = (outerW - w) / 2;
    r.top    = (outerH - h) / 2;
    r.right  = r.left + w;
    r.bottom = r.top + h;
    return r;
}

static void ComputeLayout(int bbW, int bbH,
                          int canvasAspW,  int canvasAspH,
                          int contentAspW, int contentAspH,
                          FillMode fill, Layout *out)
{
    int cw, ch;     /* canvas size   */
    int tw, th;     /* content size  */
    int dw, dh;     /* destination size */

    /* 1. The game's 16:9 canvas inside the back buffer. */
    FitAspect(bbW, bbH, canvasAspW, canvasAspH, &cw, &ch);
    out->canvas = CentredRect(bbW, bbH, cw, ch);

    /* 2. The 4:3 content inside that canvas (full canvas height). */
    FitAspect(cw, ch, contentAspW, contentAspH, &tw, &th);
    out->content = CentredRect(cw, ch, tw, th);
    out->content.left   += out->canvas.left;   out->content.right  += out->canvas.left;
    out->content.top    += out->canvas.top;    out->content.bottom += out->canvas.top;

    /* 3. Default: paste the whole content rectangle over the whole screen. */
    out->src = out->content;
    out->dst.left = 0;  out->dst.top = 0;
    out->dst.right = bbW;  out->dst.bottom = bbH;
    out->dstHasBars = 0;

    if (fill == FILL_STRETCH)
        return;

    if (fill == FILL_COVER && bbW * th < bbH * tw) {
        /* Screen is narrower than the content (e.g. 5:4 vs 4:3):
         * trim the content's left/right edges so the aspect matches the
         * screen exactly, then fill the whole screen. We never crop
         * top/bottom, so FILL_COVER on a wide screen falls through to FIT. */
        int sw   = th * bbW / bbH;                  /* trimmed content width */
        int trim = (tw - sw) / 2;
        out->src.left  = out->content.left  + trim;
        out->src.right = out->content.right - trim;
        return;
    }

    /* FILL_FIT (or COVER on a screen that is not narrower): keep the whole
     * content and centre it, leaving black bars where it does not reach. */
    FitAspect(bbW, bbH, tw, th, &dw, &dh);
    out->dst = CentredRect(bbW, bbH, dw, dh);
    out->dstHasBars = (dw < bbW || dh < bbH);
}

#endif /* RE1_LAYOUT_H */
