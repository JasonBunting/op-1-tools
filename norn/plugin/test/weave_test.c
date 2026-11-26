/* Host test of Norn's weave (norn_weave.h): the integer threads against the mockup's point() in floating point
 * (norn/design/norn-weave-comparison.html), every model across the knobs; the threads start at the hands and end at
 * their right ends; nothing leaves the area below the top strip; crossings are real. Writes an SVG of each model's
 * weave at a few settings to look at.
 *   gcc -O2 -Wall -I norn/plugin norn/plugin/test/weave_test.c -lm -o /tmp/norn_weave && /tmp/norn_weave build
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "norn_weave.h"

static int16_t tab[8192];
static int failures;

/* the mockup's point(), y only, t = 0, no reduced motion */
static double ref_y(int m, int i, double u, double g, double w, double o, int note, double t)
{
    const double hx[3] = { 79.72, 84.13, 87.44 }, hy[3] = { 73.31, 104.57, 136.20 };
    const double a = (o - .5) * 2, p = u * (8 + 11 * g) + i * 2.0944;
    double wave;
    if (m == 0) {
        const double f = u * (7 + 10 * w) + i * 2.0944;
        wave = (8 + 7 * fabs(g - .5) * 2) * (sin(f) + (g - .5) * .85 * cos(2 * f) + w * .45 * sin(3 * f)) + a * (u - .35) * 19;
    } else if (m == 1) {
        const double warp = a < 0 ? (-a) * sin(u * 12 + i) * 2.4 : 0, grit = a > 0 ? a * (2 + 4 * w) * sin(p * 4) : 0;
        wave = (10 + 13 * w) * sin(p + warp + w * 1.15 * sin(2 * p)) + grit;
    } else if (m == 2) wave = 8 * sin(u * (8 + 20 * w) + i * 2.0944) + (g - .5) * (i - 1) * 25;
    else {
        const double q = u * (5 + 12 * w) + i * 2.0944, terrain = g * 7;
        wave = (7 + 15 * w) * (sin(q + a * 2.4) + .36 * sin(q * (1 + terrain * .35) + terrain));
    }
    const double ease = u * u * (3 - 2 * u), env = pow(sin(M_PI * u), 2);
    const double drift = note ? 1.1 * sin(u * 18 - t * 3 + i) : 0;
    (void)hx;
    return hy[i] + (108 + (1 - i) * 12 - hy[i]) * ease + env * (wave + drift) * .88;
}

static void svg(const char *dir, int m)
{
    if (!dir) return;
    static const char *names[4] = { "shaper", "fm", "formant", "terrain" }, *col[3] = { "#62e6a4", "#eee9dc", "#ff9859" };
    char path[512];
    snprintf(path, sizeof path, "%s/norn_weave_%s.svg", dir, names[m]);
    FILE *f = fopen(path, "w");
    if (!f) return;
    const int32_t set[4][3] = { { 0, 0, 0 }, { 16384, 16384, 16384 }, { 32767, 32767, 32767 }, { 8000, 26000, 4000 } };
    fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 320 %d' width='960' height='%d'>"
               "<rect width='320' height='%d' fill='#000'/>\n", 160 * 4, 480 * 4, 160 * 4);
    for (int s = 0; s < 4; s++) {
        nw_in in = { m, set[s][0], set[s][1], set[s][2], 0, 0 };
        int32_t ys[3][NW_GRID_N], cx[NW_CROSS_MAX], cy[NW_CROSS_MAX];
        nw_threads(tab, &in, ys);
        fprintf(f, "<g transform='translate(0 %d)'><rect y='0' width='320' height='160' fill='none' stroke='#333'/>"
                   "<text x='4' y='10' fill='#878c94' font-size='7' font-family='monospace'>%s g %d w %d o %d</text>\n",
                s * 160, names[m], set[s][0] * 100 / 32767, set[s][1] * 100 / 32767, set[s][2] * 100 / 32767);
        for (int i = 0; i < 3; i++) {
            fprintf(f, "<polyline fill='none' stroke='%s' stroke-width='0.85' points='%.2f,%.2f", col[i],
                    nw_hand_x[i] / 1024.0, nw_hand_y[i] / 1024.0);
            for (int k = 0; k < NW_GRID_N; k++) fprintf(f, " %d,%.2f", NW_GRID_X0 + 2 * k, ys[i][k] / 1024.0);
            fprintf(f, "'/>\n");
        }
        const int n = nw_crossings(ys, cx, cy);
        for (int j = 0; j < n; j++)
            fprintf(f, "<rect x='%.2f' y='%.2f' width='1.6' height='1.6' fill='#eee9dc' transform='rotate(45 %.2f %.2f)'/>\n",
                    cx[j] / 1024.0 - .8, cy[j] / 1024.0 - .8, cx[j] / 1024.0, cy[j] / 1024.0);
        fprintf(f, "</g>\n");
    }
    fprintf(f, "</svg>\n");
    fclose(f);
    printf("wrote %s\n", path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : 0;
    for (int k = 0; k < 8192; k++) tab[k] = (int16_t)lrint(32767 * sin(2 * M_PI * k / 8192));

    /* against the mockup, every model, knobs in tenths, u in 1/200, with and without the drift */
    double worst = 0; int wm = 0;
    int32_t top = NW_Q(160), bottom = 0;
    for (int m = 0; m < 4; m++)
        for (int gi = 0; gi <= 10; gi++) for (int wi = 0; wi <= 10; wi++) for (int oi = 0; oi <= 10; oi++)
            for (int note = 0; note < 2; note++) {
                nw_in in = { m, gi * 32767 / 10, wi * 32767 / 10, oi * 32767 / 10, 1234, note };
                for (int i = 0; i < 3; i++)
                    for (int s = 0; s <= 200; s++) {
                        const int32_t u = s * 32768 / 200;
                        const int32_t y = nw_y(tab, &in, i, u);
                        const double r = ref_y(m, i, s / 200.0, in.g / 32767.0, in.w / 32767.0, in.o / 32767.0, note, 1.234);
                        const double e = fabs(y / 1024.0 - r);
                        if (e > worst) { worst = e; wm = m; }
                        if (y < top) top = y;
                        if (y > bottom) bottom = y;
                    }
            }
    printf("weave: largest difference from the mockup %.3f px (model %d); threads span y %.1f .. %.1f\n",
           worst, wm, top / 1024.0, bottom / 1024.0);
    if (worst > 0.5) { printf("FAIL: weave differs from the mockup\n"); failures++; }
    if (top < NW_Q(31) || bottom > NW_Q(150)) { printf("FAIL: weave leaves its area\n"); failures++; }

    /* the ends: at the hand and at the right end, exactly */
    for (int i = 0; i < 3; i++) {
        nw_in in = { 1, 32767, 32767, 32767, 0, 1 };
        const int32_t y0 = nw_y(tab, &in, i, 0), y1 = nw_y(tab, &in, i, 32768);
        if (y0 != nw_hand_y[i] || abs(y1 - nw_end_y[i]) > 2 || nw_x_at(i, 0) != nw_hand_x[i]
            || abs(nw_x_at(i, 32768) - NW_END_X) > 64) {
            printf("FAIL: thread %d ends: start %d (hand %d), end %d (%d)\n", i, y0, nw_hand_y[i], y1, nw_end_y[i]);
            failures++;
        }
    }

    /* crossings: each one between two threads that really swap sides there */
    int most = 0;
    for (int m = 0; m < 4; m++) for (int gi = 0; gi <= 4; gi++) for (int wi = 0; wi <= 4; wi++) for (int oi = 0; oi <= 4; oi++) {
        nw_in in = { m, gi * 8191, wi * 8191, oi * 8191, 0, 0 };
        int32_t ys[3][NW_GRID_N], cx[NW_CROSS_MAX], cy[NW_CROSS_MAX];
        nw_threads(tab, &in, ys);
        const int n = nw_crossings(ys, cx, cy);
        if (n > most) most = n;
        for (int j = 0; j < n; j++) {
            int ok = 0;
            const int k = (cx[j] - NW_Q(NW_GRID_X0)) / NW_Q(2);
            for (int a = 0; a < 3 && !ok; a++) for (int b = a + 1; b < 3; b++)
                if (k + 1 < NW_GRID_N && (int64_t)(ys[a][k] - ys[b][k]) * (ys[a][k + 1] - ys[b][k + 1]) <= 0) ok = 1;
            if (!ok || cx[j] < NW_Q(NW_CROSS_X0)) { printf("FAIL: a crossing where no threads cross\n"); failures++; break; }
        }
    }
    printf("crossings: at most %d at once (cap %d)\n", most, NW_CROSS_MAX);
    for (int m = 0; m < 4; m++) svg(dir, m);
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
