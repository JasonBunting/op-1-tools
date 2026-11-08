/* rastprobe: a throwaway feature plugin that answers "what does the native raster look like, and can we get it out
 * over USB MIDI?". Hundekraft 3.1.4 SDK.
 *
 * Off by default: switch "PROBE" on the settings page turns it on. While on:
 *   - the Gfx overlay latches the raster descriptor every frame and a checksum every 60 frames;
 *   - the main-loop service sends an INFO message every INFO_PASSES passes and, between them, the raw framebuffer
 *     (512-byte rows, 400 rows, RGB565 LE, padding included) in small CHUNK messages, CHUNKS_PER_PASS per service
 *     pass, but only while the descriptor looks like a known buffer (expected()).
 * The service reads the live framebuffer, so a streamed image may tear. That is fine for a probe.
 *
 * SysEx: F0 7D 'O' 'M' <type> <7-bit packed payload, devupd's hi-byte-first layout> F7. 7D = non-commercial ID,
 * apart from devupd's F0 00 20 76 7D.
 *   type 1 INFO   u32 seq, frames, passes, sent, r[0..7], sum, sum_frame
 *   type 2 CHUNK  u32 seq, fb, offset (bytes from fb), then CHUNK_BYTES of pixels
 */
#include "op1_feature.h"
#include "op1_feature_midi.h"

/* usbmidi_tx_enqueue(_, packets, count): what devupd/du_fw.c send_sysex uses; op1_midi_send takes 3 bytes only */
#define TX_ENQ ((void (*)(int, const void *, unsigned))0x019E0D18)

/* Raster descriptor: r[0] RGB565 buffer, r[1] width, r[2] stride in bytes, r[3] height. Measured on 3.1.4 (settings
 * page): r[0] alternates 0x02000000 / 0x03000000 (double buffered), 188 x 376, stride 512; kn_artcache.c's 198 x 400 is
 * another panel. The probe streams 400 rows of 512 bytes from either known buffer, never another address. */
#define PROBE_STRIDE   512u
#define PROBE_H        400u
#define SWEEP_BYTES    (PROBE_STRIDE * PROBE_H)
#define CHUNK_BYTES    40u                /* 12 + 40 bytes -> 60 packed + 6 framing = 66-byte messages */
#define CHUNKS_PER_PASS 2u                /* ~123 passes/s measured -> about 21 s per sweep */
#define INFO_PASSES    200u               /* service passes between INFO messages (pass rate is reported in INFO) */
#define SUM_FRAMES     60u

static volatile uint32_t r_words[8], frames, sum, sum_frame;
static uint32_t passes, sent, seq, offset;

static int on(void) { return op1_switch_on("rastprobe"); }
static int expected(const volatile uint32_t *r)
{
    return (r[0] == 0x02000000u || r[0] == 0x03000000u) && r[2] == PROBE_STRIDE && r[1] && r[1] <= PROBE_STRIDE / 2u
        && r[3] && r[3] <= PROBE_H;
}

static uint32_t checksum(const volatile uint32_t *r)  /* FNV-1a over r[1] x r[3]: multiplies, no division */
{
    const uint16_t *fb = (const uint16_t *)(uintptr_t)r[0];
    uint32_t h = 2166136261u, w = r[1], ht = r[3];
    for (uint32_t y = 0; y < ht; y++) {
        const uint16_t *row = fb + y * (PROBE_STRIDE / 2u);
        for (uint32_t x = 0; x < w; x++) h = (h ^ row[x]) * 16777619u;
    }
    return h;
}

static void overlay(void *raster, int32_t screen)
{
    (void)screen;
    if (!on()) return;
    const uint32_t *r = raster;
    for (int i = 0; i < 8; i++) r_words[i] = r[i];
    frames++;
    if (frames % SUM_FRAMES == 0 && expected(r_words)) { sum = checksum(r_words); sum_frame = frames; }
}

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

static void send_sysex(const uint8_t *m, int n)    /* as devupd/du_fw.c send_sysex */
{
    static uint32_t pk[96];
    int np = 0;
    for (int i = 0; i < n; i += 3) {
        int k = n - i < 3 ? n - i : 3, cin = n - i > 3 ? 4 : 4 + k;
        uint32_t w = (uint32_t)cin;
        for (int j = 0; j < k; j++) w |= (uint32_t)m[i + j] << (8 * (j + 1));
        pk[np++] = w;
    }
    TX_ENQ(0, pk, (unsigned)np);
    sent++;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void send(int type, const uint8_t *payload, int n)
{
    static uint8_t msg[200];
    int o = 0;
    msg[o++] = 0xF0; msg[o++] = 0x7D; msg[o++] = 'O'; msg[o++] = 'M'; msg[o++] = (uint8_t)type;
    o += pack7(payload, n, msg + o);
    msg[o++] = 0xF7;
    send_sysex(msg, o);
}

static void service(void)
{
    if (!on()) return;
    static uint8_t p[16 * 4];
    if (++passes % INFO_PASSES == 1) {
        put32(p, seq++); put32(p + 4, frames); put32(p + 8, passes); put32(p + 12, sent);
        for (int i = 0; i < 8; i++) put32(p + 16 + 4 * i, r_words[i]);
        put32(p + 48, sum); put32(p + 52, sum_frame);
        send(1, p, 56);
        return;
    }
    if (!expected(r_words)) return;               /* unknown layout: INFO only */
    uint32_t fb = r_words[0];
    static uint8_t c[12 + CHUNK_BYTES];
    for (uint32_t k = 0; k < CHUNKS_PER_PASS; k++) {
        put32(c, seq++); put32(c + 4, fb); put32(c + 8, offset);
        const uint8_t *src = (const uint8_t *)(uintptr_t)fb + offset;
        for (uint32_t i = 0; i < CHUNK_BYTES; i++) c[12 + i] = src[i];
        send(2, c, (int)sizeof c);
        offset = (offset + CHUNK_BYTES) % SWEEP_BYTES;
    }
}

static void event(int id, int32_t arg)
{
    (void)arg;
    if (id == OP1_EV_REGISTER) op1_switch("rastprobe", "PROBE");
    else if (id == OP1_EV_SERVICE) service();
}

const op1_feature_t FEATURE_rastprobe = { .api = OP1_FEATURE_API, .priority = 900, .name = "rastprobe", .event = event, .overlay = overlay };
