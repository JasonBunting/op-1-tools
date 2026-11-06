/* mirror: streams the OP-1's screen over USB MIDI SysEx as changed 8x8 tiles. Hundekraft 3.1.4 SDK.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
 *
 * The PC drives it with control changes on MIDI channel 16 (heard through midi_raw, which only looks):
 *   CC 118  keep-alive: streams while one arrived in the last ~3 s; the viewer sends it every second
 *   CC 119  budget 0..127 (default 64): 256 + 20 v SysEx bytes per pass
 *   CC 120  scan 0..127 (default 64): 8 + 8 v tiles checked per pass
 *   CC 121  mode bits (default 6): 1 whole frames, 2 palette tiles, 4 RLE tiles
 *   CC 122  refresh 0..16 (default 1): tiles resent per pass regardless, so anything lost heals
 * Without a viewer the plugin sends nothing. A new stream starts with the whole screen.
 *
 * Live mode: the Gfx overlay latches the raster descriptor of the frame just drawn; the main-loop service scans a few
 * tiles of that buffer per pass, compares each with a hash of what was last sent and sends changed ones within the
 * budget. Reads may tear against the next frame; the next scan resends such tiles.
 * Whole frames: the overlay copies one finished frame into the declared buffer (bigbuf), the service sends its changed
 * tiles over as many passes as that takes, then a FRAME marker, and asks the overlay for the next frame. The viewer
 * shows the picture only at FRAME markers, so it never shows parts of two frames. The buffer holds a 188 x 376 screen
 * (as measured) in rows of 192 pixels; without it, or on a larger screen, the plugin stays live.
 *
 * Raster (docs/audit-001-framebuffer.md): r[0] RGB565 LE buffer, 0x02000000 or 0x03000000 (double buffered), r[1]
 * width, r[2] stride 512 bytes, r[3] height; portrait, the visible area is the top-left r[1] x r[3].
 *
 * SysEx: F0 7D 'O' 'M' <type> <7-bit packed payload, devupd's hi-byte-first layout> F7.
 *   0x10 GEOM   u16 seq, u16 width, u16 height                                   on change and about once a second
 *   0x11 TILES  u16 seq, then tiles: u8 tx, u8 ty, u8 n; n = 0: 64 raw pixels (u16 LE, row by row);
 *               n = 1..42: n runs of (u8 count - 1, u16 colour); n = 0x80 | (k - 1): k colours (u16), then 64 indexes
 *               of 1 (k <= 2), 2 (k <= 4) or 4 bits, packed from bit 0 of each byte
 *   0x12 STAT   u16 seq, u32 frames, u32 passes, u32 bytes sent, u32 tiles sent, u32 status    about once a second
 *               status bit 0: the whole-frame buffer exists, bit 1: whole frames are being sent
 *   0x13 FRAME  u16 seq, u32 frames sent                                          after each whole frame
 */
#include "op1_feature.h"

/* usbmidi_tx_enqueue(_, packets, count): what devupd/du_fw.c send_sysex uses; op1_midi_send takes 3 bytes only */
#ifndef TX_ENQ                            /* host tests define their own */
#define TX_ENQ ((void (*)(int, const void *, unsigned))0x019E0D18)
#endif

#define STRIDE_PX      256u               /* r[2] == 512 bytes */
#define MAX_W          256u
#define MAX_H          400u
#define TILE           8u
#define TILES_X        (MAX_W / TILE)
#define TILES_Y        (MAX_H / TILE)
#define SNAP_W         188u               /* the whole-frame buffer: the measured screen size, */
#define SNAP_H         376u
#define SNAP_STRIDE    192u               /* rows padded to whole tiles so the last tile column stays inside */
#define MSG_RAW_MAX    200u               /* payload bytes per message: <= 229 packed + 6 framing */
#define MAX_RUNS       42u                /* runs * 3 < 128: beyond that raw is smaller */
#define BUDGET(v)      (256u + 20u * (v))
#define SCAN_TILES(v)  (8u + 8u * (v))
#define CC_CHANNEL     15u                /* MIDI channel 16 */
#define CC_KEEPALIVE   118u
#define CC_BUDGET      119u
#define CC_SCAN        120u
#define CC_MODE        121u
#define CC_REFRESH     122u
#define MODE_FRAMES    1u
#define MODE_PALETTE   2u
#define MODE_RLE       4u
#define ALIVE_PASSES   370u               /* ~3 s */
#define STAT_PASSES    123u

OP1_BIGBUF(mirror_snap, 0, SNAP_STRIDE * SNAP_H * 2u);   /* plugin.json "bigbuf"; 0 min: without it, live mode only */

static volatile uint32_t r_words[4], frames, heard, budget_v = 64, scan_v = 64, mode = MODE_PALETTE | MODE_RLE,
                         refresh_n = 1;
static volatile uint32_t snap_want, snap_ready, snap_w, snap_h;  /* overlay <-> service handshake */
static uint32_t hashes[TILES_X * TILES_Y];
static uint32_t passes, bytes_sent, tiles_sent, frames_sent, scan_pos, refresh_pos, heard_seen, alive_until;
static uint16_t seq, geom_w, geom_h;
static int active, framing;

static void midi_raw(const uint8_t *msg, int len)              /* timer context: record and return */
{
    if (len != 3 || msg[0] != (0xB0u | CC_CHANNEL)) return;
    uint32_t v = msg[2] & 0x7Fu;
    switch (msg[1]) {
    case CC_KEEPALIVE: heard++; break;
    case CC_BUDGET:    budget_v = v; break;
    case CC_SCAN:      scan_v = v; break;
    case CC_MODE:      mode = v; break;
    case CC_REFRESH:   refresh_n = v > 16u ? 16u : v; break;
    }
}

static int known(const volatile uint32_t *r)
{
    return (r[0] == 0x02000000u || r[0] == 0x03000000u) && r[2] == STRIDE_PX * 2u && r[1] && r[1] <= MAX_W
        && r[3] && r[3] <= MAX_H;
}

static int have_snap(void) { return mirror_snap.ptr && mirror_snap.size >= SNAP_STRIDE * SNAP_H * 2u; }
static int fits_snap(const volatile uint32_t *r) { return known(r) && r[1] <= SNAP_W && r[3] <= SNAP_H; }

static void overlay(void *raster, int32_t screen)
{
    (void)screen;
    if (!active) return;
    const uint32_t *r = raster;
    for (int i = 0; i < 4; i++) r_words[i] = r[i];
    frames++;
    if (snap_want && !snap_ready && fits_snap(r) && have_snap()) {   /* a finished frame: copy its visible part */
        const uint32_t *src = (const uint32_t *)(uintptr_t)r[0];
        uint32_t *dst = mirror_snap.ptr, words = (r[1] + 1u) / 2u;
        for (uint32_t y = 0; y < r[3]; y++, src += STRIDE_PX / 2u, dst += SNAP_STRIDE / 2u)
            for (uint32_t x = 0; x < words; x++) dst[x] = src[x];
        snap_w = r[1]; snap_h = r[3];
        snap_want = 0;
        snap_ready = 1;
    }
}

/* ---- SysEx out ---- */
static int pack7(const uint8_t *in, int n, uint8_t *out)
{
    int o = 0;
    for (int i = 0; i < n; i += 7) {
        uint8_t *hi = &out[o++];
        *hi = 0;
        for (int k = 0; k < 7 && i + k < n; k++) { *hi |= (uint8_t)((in[i + k] >> 7) << k); out[o++] = in[i + k] & 0x7F; }
    }
    return o;
}

static int send(int type, const uint8_t *payload, int n)        /* -> bytes on the wire */
{
    static uint8_t msg[256];
    static uint32_t pk[96];
    int o = 0;
    msg[o++] = 0xF0; msg[o++] = 0x7D; msg[o++] = 'O'; msg[o++] = 'M'; msg[o++] = (uint8_t)type;
    o += pack7(payload, n, msg + o);
    msg[o++] = 0xF7;
    int np = 0;
    for (int i = 0; i < o; i += 3) {                              /* as devupd/du_fw.c send_sysex */
        int k = o - i < 3 ? o - i : 3, cin = o - i > 3 ? 4 : 4 + k;
        uint32_t w = (uint32_t)cin;
        for (int j = 0; j < k; j++) w |= (uint32_t)msg[i + j] << (8 * (j + 1));
        pk[np++] = w;
    }
    TX_ENQ(0, pk, (unsigned)np);
    bytes_sent += (uint32_t)o;
    return o;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

/* ---- tiles ---- */
static uint8_t out[MSG_RAW_MAX];
static uint32_t out_n;

static int flush(void)
{
    if (out_n <= 2) return 0;
    int n = send(0x11, out, (int)out_n);
    out_n = 0;
    return n;
}

/* Copies tile (tx, ty) of a buffer with rows of stride pixels into px[] and returns its hash. */
static uint32_t grab(const uint16_t *fb, uint32_t stride, uint32_t tx, uint32_t ty, uint16_t *px)
{
    uint32_t h = 2166136261u;
    for (uint32_t y = 0; y < TILE; y++) {
        const uint16_t *row = fb + (ty * TILE + y) * stride + tx * TILE;
        for (uint32_t x = 0; x < TILE; x++) { px[y * TILE + x] = row[x]; h = (h ^ row[x]) * 16777619u; }
    }
    return h;
}

/* Encodes the grabbed pixels of tile (tx, ty) into tile[] as the smallest of raw, RLE (if enabled) and palette (if
 * enabled); returns the encoded size (3-byte header included). */
static uint32_t encode(const uint16_t *px, uint32_t tx, uint32_t ty, uint8_t *tile, uint32_t m)
{
    static uint8_t rle[3 + MAX_RUNS * 3u];
    uint32_t best = 3 + TILE * TILE * 2u, runs = 0, o = 3;
    tile[0] = (uint8_t)tx; tile[1] = (uint8_t)ty;

    uint16_t pal[16];
    uint8_t idx[TILE * TILE];
    uint32_t k = 0;
    if (m & MODE_PALETTE)
        for (uint32_t i = 0; i < TILE * TILE && k <= 16u; i++) {
            uint32_t j = 0;
            while (j < k && pal[j] != px[i]) j++;
            if (j == k) { if (k == 16u) { k = 17u; break; } pal[k++] = px[i]; }
            idx[i] = (uint8_t)j;
        }
    uint32_t bits = k <= 2u ? 1u : k <= 4u ? 2u : 4u, pal_size = 3 + 2 * k + TILE * TILE * bits / 8u;
    if (m & MODE_RLE)
        for (uint32_t i = 0; i < TILE * TILE && runs < MAX_RUNS;) {
            uint32_t j = i + 1;
            while (j < TILE * TILE && px[j] == px[i]) j++;
            rle[o++] = (uint8_t)(j - i - 1); put16(&rle[o], px[i]); o += 2;
            runs++; i = j;
            if (i == TILE * TILE) runs |= 0x100u;                   /* complete */
        }
    uint32_t rle_ok = (runs & 0x100u) != 0, rle_size = o;

    if ((m & MODE_PALETTE) && k <= 16u && pal_size < best && (!rle_ok || pal_size < rle_size)) {
        tile[2] = (uint8_t)(0x80u | (k - 1u));
        o = 3;
        for (uint32_t j = 0; j < k; j++, o += 2) put16(&tile[o], pal[j]);
        for (uint32_t i = 0; i < TILE * TILE * bits / 8u; i++) tile[o + i] = 0;
        for (uint32_t i = 0; i < TILE * TILE; i++) tile[o + i * bits / 8u] |= (uint8_t)(idx[i] << (i * bits % 8u));
        return pal_size;
    }
    if (rle_ok && rle_size < best) {
        tile[2] = (uint8_t)(runs & 0xFFu);
        for (uint32_t i = 3; i < rle_size; i++) tile[i] = rle[i];
        return rle_size;
    }
    tile[2] = 0;
    for (uint32_t i = 0, p = 3; i < TILE * TILE; i++, p += 2) put16(&tile[p], px[i]);
    return best;
}

/* Hashes tile (tx, ty) and queues it if changed (or forced). Returns 0 when the budget is spent before it fit. */
static int visit(const uint16_t *fb, uint32_t stride, uint32_t tx, uint32_t ty, int force, uint32_t *budget, uint32_t m)
{
    static uint8_t tile[3 + TILE * TILE * 2];
    static uint16_t px[TILE * TILE];
    uint32_t *slot = &hashes[ty * TILES_X + tx];
    uint32_t h = grab(fb, stride, tx, ty, px);
    if (h == *slot && !force) return 1;
    uint32_t len = encode(px, tx, ty, tile, m);                 /* only changed (or forced) tiles are encoded */
    if (out_n + len > MSG_RAW_MAX) {
        uint32_t n = (uint32_t)flush();
        *budget = n < *budget ? *budget - n : 0;
    }
    if (*budget < len + len / 7u + 8u) return 0;                /* packed size plus framing, roughly */
    if (out_n == 0) { put16(out, seq++); out_n = 2; }
    for (uint32_t i = 0; i < len; i++) out[out_n++] = tile[i];
    *slot = h;
    tiles_sent++;
    return 1;
}

static void service(void)
{
    passes++;
    uint32_t h_now = heard;
    if (h_now != heard_seen) { heard_seen = h_now; alive_until = passes + ALIVE_PASSES; }
    if ((int32_t)(alive_until - passes) <= 0) {                  /* framing too, so a returning viewer asks for a frame */
        active = 0; framing = 0; snap_want = snap_ready = 0;
        return;
    }
    if (!active) { active = 1; geom_w = geom_h = 0; return; }  /* a new stream: the next pass resends everything */

    uint32_t m = mode;
    int want_frames = (m & MODE_FRAMES) && have_snap() && fits_snap(r_words);
    if (want_frames != framing) {                                /* switching: start a fresh sweep */
        framing = want_frames;
        scan_pos = 0;
        snap_ready = 0;
        snap_want = (uint32_t)framing;
    }
    if (framing && !snap_ready) return;                          /* waiting for the overlay's copy */
    if (!framing && !known(r_words)) return;
    const uint16_t *fb = framing ? (const uint16_t *)mirror_snap.ptr : (const uint16_t *)(uintptr_t)r_words[0];
    uint32_t w = framing ? snap_w : r_words[1], h = framing ? snap_h : r_words[3];
    uint32_t stride = framing ? SNAP_STRIDE : STRIDE_PX;
    uint32_t tx_n = (w + TILE - 1) / TILE, ty_n = (h + TILE - 1) / TILE, n = tx_n * ty_n;
    uint8_t p[22];

    if (w != geom_w || h != geom_h || passes % STAT_PASSES == 0) {
        if (w != geom_w || h != geom_h) {                         /* new screen geometry: everything is stale */
            for (uint32_t i = 0; i < TILES_X * TILES_Y; i++) hashes[i] = 0;
            geom_w = (uint16_t)w; geom_h = (uint16_t)h; scan_pos = refresh_pos = 0;
        }
        put16(p, seq++); put16(p + 2, w); put16(p + 4, h);
        send(0x10, p, 6);
    }
    if (passes % STAT_PASSES == 1) {
        put16(p, seq++); put32(p + 2, frames); put32(p + 6, passes); put32(p + 10, bytes_sent); put32(p + 14, tiles_sent);
        put32(p + 18, (have_snap() ? 1u : 0u) | (framing ? 2u : 0u));
        send(0x12, p, 22);
    }

    uint32_t budget = BUDGET(budget_v), scan = SCAN_TILES(scan_v), refresh = refresh_n;
    for (uint32_t k = 0; k < refresh; k++) {
        refresh_pos %= n;
        if (!visit(fb, stride, refresh_pos % tx_n, refresh_pos / tx_n, 1, &budget, m)) break;
        refresh_pos++;
    }
    for (uint32_t k = 0; k < scan; k++) {
        if (framing && scan_pos >= n) break;
        scan_pos %= n;
        if (!visit(fb, stride, scan_pos % tx_n, scan_pos / tx_n, 0, &budget, m)) break;   /* resume here next pass */
        scan_pos++;
    }
    flush();
    if (framing && scan_pos >= n) {                              /* the whole frame is out: mark it, ask for the next */
        put16(p, seq++); put32(p + 2, ++frames_sent);
        send(0x13, p, 6);
        scan_pos = 0;
        snap_ready = 0;
        snap_want = 1;
    }
}

static void event(int id, int32_t arg)
{
    (void)arg;
    if (id == OP1_EV_SERVICE) service();
}

const op1_feature_t FEATURE_mirror = { .api = OP1_FEATURE_API, .priority = 900, .name = "mirror", .event = event,
                                       .overlay = overlay, .midi_raw = midi_raw };
