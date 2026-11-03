/* Host test for mirror.c (Linux/WSL): maps a fake framebuffer at 0x02000000, loads a 256x400 24-bit BMP frame from
 * the listener, runs the plugin's overlay and service passes and writes the SysEx it sends to a file.
 *   gcc -DOP1_HOST -I<synth_sdk> host_test.c -o host_test && ./host_test frame.bmp out.syx
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
    printf("passes %u, bytes %u (stream %u), tiles %u, frames %u, stopped ok\n", passes, bytes_sent, sent_before,
           tiles_sent, frames_sent);
    return 0;
}
