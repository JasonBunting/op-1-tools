/* soundprobe: a throwaway feature plugin for issue #5 (sound recall). It answers "can a plugin copy a whole synth
 * sound and put it back later, engine included?" using the randomiser's fixed-address approach. Hundekraft 3.2.3.
 *
 * On a synth page (not the sequencer page or a sound editor):
 *   Shift + scissors  capture: copy the current slot's whole patch JSON into memory
 *   scissors          restore: load that copy into the current slot, as the randomiser applies a step
 *                     (voices reset, patch_set, op1_sound_apply, the save job, a screen refresh)
 * The copy lives in RAM only and is lost at power-off. Restoring into another slot is allowed, to see what happens.
 *
 * Addresses and the apply sequence come from Hundekraft's randomiser (synth_sdk/plugins/randomiser/rn_fw.c, original
 * tool by Lewis-Clayton / pcppcp, kennel port by Hundekraft), valid for the R246 firmware under core 3.2.3.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission. */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns")
#include "op1_feature.h"
#include "op1_feature_sound.h"

#define SLOT          (*(volatile int *)0xFF8016D4 & 7)         /* current synth slot */
#define SHIFT         (*(volatile uint8_t *)0x01DD7F78)
#define CUR_MODE_ID   (*(volatile int *)0xFF907D58)             /* ModeSwitcher: active mode */
#define SYNTH_MODE    0
#define CUR_SCREEN    (*(volatile int *)0x018B65C8)
#define SEQ_SCREEN    0x2D                                      /* sequencer page */
#define KEY_SCISSORS  0x0C
#define CYC_MS        400000u                                   /* CYCLES per ms (400 MHz) */
#define REQ_MS        2000u                                     /* drop a request not done by then */
#define PATCH(s)      ((uint8_t *)0x01DDD7E8 + ((s) & 7) * 0x38)   /* synth patch objects: +0 slot, +4 JSON */
#define VOICES_RESET  ((void (*)(void))0x01A862D4)              /* all voices and held notes */
#define PATCH_SET     ((void (*)(void *, int, const void *))0x01A052DC)
#define PATCH_SAVE    ((void (*)(void *))0x01A00D90)            /* patch vt+0x34: save job */
#define SAVER_JOB     (*(void *const volatile *)0x01BA29D8)    /* the job the saver thread runs, 0: none */
#define SAVER_RD      (*(const volatile uint32_t *)0x01BA33E4) /* its job ring: read / write index */
#define SAVER_WR      (*(const volatile uint32_t *)0x01BA33E8)
#define CUR_CTRL      ((uint32_t *(*)(void))0x019E97FC)         /* ModeSwitcher: current controller (vtable at +4) */
#define LAST_MODE     ((int (*)(void))0x019E9528)
#define SCREEN_REQ    ((void (*)(int, int, int))0x019D7C94)     /* (screen id, slot, mode): the GUI's screen queue */
#define JV_CGET       ((const void *(*)(const void *, const char *))0x019BEB70)   /* const operator[](key) */
#define JV_ASSIGN     ((void *(*)(void *, const void *))0x019BDDC0)               /* operator=, a deep copy */
#define JV_NULL_C     ((void *(*)(void *, int))0x019BFA30)                        /* Value(ValueType) */
enum { JV_NULL = 0, JV_STRING = 4 };
enum { REQ_NONE, REQ_CAPTURE, REQ_RESTORE };

static uint32_t saved[4];                                       /* a Json::Value (16 bytes): the captured patch */
static uint8_t inited, have;
static char msg[40];
static volatile uint8_t req, swallow;
static volatile int req_slot;
static volatile uint32_t req_t0;

static uint32_t cycles(void) { uint32_t c; __asm__ volatile ("%0 = CYCLES;" : "=d"(c)); return c; }
static int synth_page(void) { return CUR_MODE_ID == SYNTH_MODE && !op1_sound_editor_open() && CUR_SCREEN != SEQ_SCREEN; }
static int saver_busy(void) { return SAVER_JOB != 0 || SAVER_RD != SAVER_WR; }   /* the saver writes sounds without a lock */

/* "<prefix> <engine type>, slot <n>" in lower case: op1_toast shows upper case as instrument icons */
static void say(const char *prefix, const void *json, int slot)
{
    const void *v = JV_CGET(json, "type");
    const char *t = v && ((const uint8_t *)v)[8] == JV_STRING ? *(const char *const *)v : "?";
    char *p = msg, *end = msg + sizeof msg - 10;
    while (*prefix && p < end) *p++ = *prefix++;
    *p++ = ' ';
    while (t && *t && p < end) { char c = *t++; *p++ = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
    const char *s = " slot ";
    while (*s) *p++ = *s++;
    *p++ = (char)('1' + slot);
    *p = 0;
    op1_toast(msg);
}
static void page_refresh(int slot)                              /* as the stock key dispatch after a key */
{
    uint32_t *c = CUR_CTRL();
    const uint32_t *vt = (const uint32_t *)c[1];
    int id = ((int (*)(void *))vt[0x44 / 4])((uint8_t *)c + (int16_t)vt[0x40 / 4]);   /* the page's screen id */
    SCREEN_REQ(id, slot, LAST_MODE());
}

/* KeyHandler thread: 1 = consumed */
static int input(const op1_input *in)
{
    if (in->type != OP1_IN_KEY || in->code != KEY_SCISSORS) return 0;
    if (!in->pressed) { if (swallow) { swallow = 0; return 1; } return 0; }
    if (!synth_page()) return 0;
    if (!req) { req_slot = SLOT; req_t0 = cycles(); req = (uint8_t)(SHIFT ? REQ_CAPTURE : REQ_RESTORE); }
    swallow = 1;
    return 1;
}

/* main loop */
static void service(void)
{
    if (!inited) { JV_NULL_C(saved, JV_NULL); inited = 1; }
    int r = req;
    if (!r) return;
    int slot = SLOT;
    if (!synth_page() || slot != req_slot || cycles() - req_t0 > REQ_MS * CYC_MS) {
        op1_sound_fade(0); req = REQ_NONE;
        op1_toast("probe request dropped");
        return;
    }
    if (saver_busy()) return;
    if (r == REQ_CAPTURE) {
        JV_ASSIGN(saved, PATCH(slot) + 4);
        have = 1;
        say("captured", saved, slot);
        req = REQ_NONE;
        return;
    }
    if (!have) { op1_toast("nothing captured yet"); req = REQ_NONE; return; }
    if (op1_sound_fade(1)) return;                              /* wait for the audio thread to confirm silence */
    uint8_t *patch = PATCH(slot);
    VOICES_RESET();
    PATCH_SET(patch, slot, saved);
    op1_sound_apply((int *)patch);
    PATCH_SAVE(patch);
    page_refresh(slot);
    op1_sound_fade(0);
    say("restored", patch + 4, slot);
    req = REQ_NONE;
}
static void event(int id, int32_t arg) { (void)arg; if (id == OP1_EV_SERVICE) service(); }

static const char *const claims[] = { "key:synth:scissors", "key:synth:shift+scissors", 0 };
const op1_feature_t FEATURE_soundprobe = { .api = OP1_FEATURE_API, .priority = 600, .name = "soundprobe",
                                           .input = input, .event = event, .claims = claims };
