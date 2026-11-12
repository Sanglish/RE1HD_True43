/* Native unit test for layout.h:  gcc -I../src test_layout.c -o t && ./t */
#include <stdio.h>
#include "layout.h"

static int failures = 0;

static void Expect(const char *name, Rect got, int l, int t, int r, int b)
{
    int ok = got.left == l && got.top == t && got.right == r && got.bottom == b;
    printf("%s %-34s got (%d,%d)-(%d,%d)  want (%d,%d)-(%d,%d)\n",
           ok ? "PASS" : "FAIL", name,
           got.left, got.top, got.right, got.bottom, l, t, r, b);
    if (!ok) failures++;
}

int main(void)
{
    Layout L;

    /* 1280x960 (true 4:3): canvas 1280x720 at y=120, content 960x720 at x=160.
     * Whole content -> whole screen, scale 1.3333 uniform. */
    ComputeLayout(1280, 960, 16, 9, 4, 3, FILL_COVER, &L);
    Expect("4:3  canvas",  L.canvas,  0, 120, 1280, 840);
    Expect("4:3  src",     L.src,   160, 120, 1120, 840);
    Expect("4:3  dst",     L.dst,     0,   0, 1280, 960);
    if (L.dstHasBars) { puts("FAIL 4:3 should have no bars"); failures++; }

    /* 1280x1024 (5:4): cover trims content width to 900 (x 190..1090),
     * scale 1.4222 uniform in both axes. */
    ComputeLayout(1280, 1024, 16, 9, 4, 3, FILL_COVER, &L);
    Expect("5:4  cover src", L.src,  190, 152, 1090, 872);
    Expect("5:4  cover dst", L.dst,    0,   0, 1280, 1024);

    /* Same screen with FIT: keep all content, 1280x960 centred, 32px bars. */
    ComputeLayout(1280, 1024, 16, 9, 4, 3, FILL_FIT, &L);
    Expect("5:4  fit src", L.src,  160, 152, 1120, 872);
    Expect("5:4  fit dst", L.dst,    0,  32, 1280, 992);
    if (!L.dstHasBars) { puts("FAIL 5:4 fit should have bars"); failures++; }

    /* 1920x1080 widescreen: content is the 1440x1080 centre; cover must not
     * crop top/bottom, so it falls back to FIT -> pillarbox. */
    ComputeLayout(1920, 1080, 16, 9, 4, 3, FILL_COVER, &L);
    Expect("16:9 src", L.src,  240, 0, 1680, 1080);
    Expect("16:9 dst", L.dst,  240, 0, 1680, 1080);

    /* Scale must be uniform in the cover case (no stretching). */
    ComputeLayout(1280, 1024, 16, 9, 4, 3, FILL_COVER, &L);
    {
        int sw = L.src.right - L.src.left, sh = L.src.bottom - L.src.top;
        int dw = L.dst.right - L.dst.left, dh = L.dst.bottom - L.dst.top;
        long lhs = (long)sw * dh, rhs = (long)sh * dw;      /* sw/dw == sh/dh */
        long diff = lhs > rhs ? lhs - rhs : rhs - lhs;
        int ok = diff <= (long)dh + dw;                     /* 1px rounding   */
        printf("%s uniform scale (cross products %ld vs %ld)\n",
               ok ? "PASS" : "FAIL", lhs, rhs);
        if (!ok) failures++;
    }

    /* Stretch mode fills everything. */
    ComputeLayout(1280, 1024, 16, 9, 4, 3, FILL_STRETCH, &L);
    Expect("stretch dst", L.dst, 0, 0, 1280, 1024);

    printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
