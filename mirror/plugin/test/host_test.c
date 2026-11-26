/* Host test for mirror.c (Linux/WSL): maps a fake framebuffer at 0x02000000, loads a 256x400 24-bit BMP frame from
 * the listener, runs the plugin's overlay and service passes and writes the SysEx it sends to a file.
 *   gcc -O2 -I<hundekraft source>/ext/kennel/synth_sdk mirror/plugin/test/host_test.c -o /tmp/host_test && /tmp/host_test frame.bmp out.syx
 * After the first run it draws a 20 x 30 box into the frame and runs again, so the stream also exercises diffs.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static FILE *syx;
static void host_tx(int cable, const void *packets, unsigned count)
{
    (void)cable;
    if (!syx) return;                                               /* after the recorded part */
    const uint32_t *pk = packets;
    for (unsigned i = 0; i < count; i++) {
        unsigned cin = pk[i] & 0xF, n = cin == 4 || cin == 7 ? 3 : cin == 6 ? 2 : cin == 5 ? 1 : 0;
        for (unsigned j = 0; j < n; j++) fputc((int)(pk[i] >> (8 * (j + 1)) & 0xFF), syx);
    }
}
#define TX_ENQ host_tx
int op1_switch(const char *feature, const char *label) { (void)feature; (void)label; return 0; }
int op1_switch_on(const char *feature) { (void)feature; return 1; }
#include "../mirror.c"

static int record_frames(uint16_t *fa);

static void run(uint32_t *raster, int passes)
{
    static const uint8_t keepalive[3] = { 0xBF, 118, 127 };    /* as the viewer sends it, about once a second */
    for (int i = 0; i < passes; i++) {
        if (i % 123 == 0) midi_raw(keepalive, 3);
        overlay(raster, 0);
        event(OP1_EV_SERVICE, 0);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: host_test frame.bmp out.syx\n"); return 2; }
    uint16_t *fb = mmap((void *)0x02000000, 512 * 400, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 1; }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    static uint8_t bmp[54 + 256 * 3 * 400];
    if (fread(bmp, 1, sizeof bmp, f) != sizeof bmp) { fprintf(stderr, "expected a 256x400 24-bit BMP\n"); return 1; }
    fclose(f);
    for (int y = 0; y < 400; y++)
        for (int x = 0; x < 256; x++) {
            const uint8_t *p = &bmp[54 + (y * 256 + x) * 3];          /* top-down rows, BGR */
            fb[y * 256 + x] = (uint16_t)((p[2] >> 3) << 11 | (p[1] >> 2) << 5 | p[0] >> 3);
        }

    syx = fopen(argv[2], "wb");
    uint32_t raster[8] = { 0x02000000u, 188, 512, 376 };
    event(OP1_EV_REGISTER, 0);
    for (int i = 0; i < 50; i++) event(OP1_EV_SERVICE, 0);         /* no viewer yet: nothing may be sent */
    if (bytes_sent) { fprintf(stderr, "sent %u bytes without a keep-alive\n", bytes_sent); return 1; }
    run(raster, 400);                                               /* the whole screen, then idle refreshes */
    for (int y = 100; y < 130; y++) for (int x = 60; x < 80; x++) fb[y * 256 + x] = 0xF800;   /* red box */
    run(raster, 60);

    static uint8_t snap[SNAP_STRIDE * SNAP_H * 2];                         /* whole frames: the core grants the buffer */
    mirror_snap.ptr = snap; mirror_snap.size = sizeof snap;
    static const uint8_t frames_on[3] = { 0xBF, CC_MODE, MODE_FRAMES | MODE_PALETTE | MODE_RLE };
    midi_raw(frames_on, 3);
    for (int y = 200; y < 260; y++) for (int x = 20; x < 120; x++) fb[y * 256 + x] = 0x001F;  /* blue box */
    run(raster, 200);
    if (frames_sent < 2) { fprintf(stderr, "whole frames: only %u frame markers\n", frames_sent); return 1; }
    fclose(syx);
    syx = NULL;
    uint32_t sent_before = bytes_sent;
    for (uint32_t i = 0; i < ALIVE_PASSES + 10; i++) event(OP1_EV_SERVICE, 0);   /* viewer gone: must stop within ~3 s */
    for (int i = 0; i < 200; i++) event(OP1_EV_SERVICE, 0);
    uint32_t after_stop = bytes_sent;
    for (int i = 0; i < 200; i++) event(OP1_EV_SERVICE, 0);
    if (bytes_sent != after_stop) { fprintf(stderr, "still sending after the keep-alive stopped\n"); return 1; }
    uint32_t frames_before = frames_sent, bytes_before = bytes_sent;
    run(raster, 200);                                               /* the viewer comes back, whole frames still on */
    if (frames_sent == frames_before || bytes_sent == bytes_before) {
        fprintf(stderr, "whole frames stuck after the viewer came back (bytes +%u, frames +%u)\n",
                bytes_sent - bytes_before, frames_sent - frames_before);
        return 1;
    }
    if (record_frames(fb)) return 1;
    printf("passes %u, bytes %u (stream %u), tiles %u, frames %u, stopped ok, record frames ok\n", passes, bytes_sent,
           sent_before, tiles_sent, frames_sent);
    return 0;
}

/* Hundekraft 3.3+ record frames (screens/guiskip.c): a still screen's frames draw into a scratch buffer, and the two
 * display buffers (0x02000000, 0x03000000) keep the current picture. Also checks that whole frames copy the frame
 * before, finished, with what the core draws after the features' overlays (a text entry, a toast). */
static const uint8_t keep[3] = { 0xBF, 118, 127 };
static uint32_t ras_a[4] = { 0x02000000u, 188, 512, 376 }, ras_b[4] = { 0x03000000u, 188, 512, 376 },
                ras_s[4] = { 0x04000000u, 188, 512, 376 };           /* the scratch: never read by the plugin */
static void pass(uint32_t *ras, int i) { if (i % 100 == 0) midi_raw(keep, 3); overlay(ras, 0); event(OP1_EV_SERVICE, 0); }
static void paint(uint16_t *buf, uint16_t c) { for (int y = 0; y < 376; y++) for (int x = 0; x < 188; x++) buf[y * 256 + x] = c; }

static int record_frames(uint16_t *fa)
{
    uint16_t *fbb = mmap((void *)0x03000000, 512 * 400, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (fbb == MAP_FAILED) { perror("mmap"); return 1; }
    static const uint8_t live[3] = { 0xBF, CC_MODE, MODE_PALETTE | MODE_RLE }, whole[3] = { 0xBF, CC_MODE, MODE_FRAMES | MODE_PALETTE | MODE_RLE };

    /* 1. live: one drawn frame with a change, then only record frames: the change still goes out */
    midi_raw(live, 3);
    for (int i = 0; i < 200; i++) pass(ras_a, i);                    /* settled */
    for (int y = 10; y < 40; y++) for (int x = 10; x < 40; x++) fa[y * 256 + x] = 0x07E0;
    pass(ras_a, 1);
    uint32_t t0 = tiles_sent;
    for (int i = 1; i < 200; i++) pass(ras_s, i);
    if (tiles_sent == t0) { fprintf(stderr, "record frames: a change drawn before them was never sent\n"); return 1; }

    /* 2. the viewer connects to a still screen: the last drawn frame (latched while no viewer) streams */
    for (uint32_t i = 0; i < ALIVE_PASSES + 10; i++) event(OP1_EV_SERVICE, 0);    /* viewer gone */
    paint(fa, 0x1234);
    overlay(ras_a, 0);                                                /* drawn while nobody watches */
    t0 = tiles_sent;
    for (int i = 0; i < 300; i++) pass(ras_s, i);                    /* then only record frames */
    if (tiles_sent - t0 < (188 / 8) * (376 / 8)) {
        fprintf(stderr, "record frames: a viewer on a still screen got %u tiles, not the whole screen\n", tiles_sent - t0);
        return 1;
    }

    /* 3. whole frames, drawn frames alternating A and B: each frame is cleared and drawn, the overlay runs, then a
     * "text entry" is drawn after it; the copy must hold the text entry (the frame before, finished) */
    midi_raw(whole, 3);
    uint32_t f0 = frames_sent;
    for (int i = 0; i < 400; i++) {
        uint16_t *buf = i & 1 ? fbb : fa;
        paint(buf, 0x0841);
        pass(i & 1 ? ras_b : ras_a, i);
        for (int y = 300; y < 310; y++) for (int x = 50; x < 150; x++) buf[y * 256 + x] = 0xFFFF;   /* after the overlays */
    }
    if (frames_sent == f0) { fprintf(stderr, "whole frames: no frame with drawn frames alternating\n"); return 1; }
    const uint16_t *snap_px = mirror_snap.ptr;
    if (snap_px[305 * SNAP_STRIDE + 100] != 0xFFFF) {
        fprintf(stderr, "whole frames: the copy misses what was drawn after the overlays\n");
        return 1;
    }

    /* 4. whole frames on a still screen: record frames only, frames keep going out */
    f0 = frames_sent;
    for (int i = 0; i < 400; i++) pass(ras_s, i);
    if (frames_sent == f0) { fprintf(stderr, "whole frames: stuck on record frames\n"); return 1; }
    return 0;
}
