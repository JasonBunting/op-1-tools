/* See pp_player.h: one mode's pages and chain as checked, versioned text. Never raw structs: a build with a different
 * layout must still read old saves or refuse them. Text is hex only, so it is safe inside the SQL the saver builds.
 * Layout, every number two hex digits except as noted:
 *   "PP1" count sel pitch_key chain_len chain[chain_len]
 *   then for each page: start len swing dir notes[16] (six digits each)
 *   then the sum of all the characters before it, four digits.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#pragma GCC optimize("no-tree-loop-distribute-patterns")      /* the firmware has no memcpy / memset */
#include "pp_player.h"

#define PP_HEADER 11
#define PP_PAGE_CHARS (8 + PP_COLS * 6)
#define PP_CHECK_CHARS 4

static char *put_hex(char *p, uint32_t v, int digits)
{
    for (int i = digits - 1; i >= 0; i--) {
        int d = v >> (i * 4) & 15;
        *p++ = (char)(d < 10 ? '0' + d : 'A' + d - 10);
    }
    return p;
}
/* -1 on anything that isn't an upper-case hex digit */
static int32_t get_hex(const char *p, int digits)
{
    int32_t v = 0;
    for (int i = 0; i < digits; i++) {
        char c = p[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return -1;
        v = v << 4 | d;
    }
    return v;
}
static uint32_t sum_chars(const char *s, int n)
{
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) sum += (uint8_t)s[i];
    return sum & 0xFFFF;
}

int pp_serialize(int mode, char *out, int cap)
{
    const pp_bank_t *b = &pp_bank[mode & 1];
    int count = b->count, chain_len = b->chain_len;
    if (cap < PP_HEADER + chain_len * 2 + count * PP_PAGE_CHARS + PP_CHECK_CHARS) return 0;
    char *p = out;
    *p++ = 'P'; *p++ = 'P'; *p++ = '1';
    p = put_hex(p, (uint32_t)count, 2);
    p = put_hex(p, b->sel, 2);
    p = put_hex(p, b->pitch_key, 2);
    p = put_hex(p, (uint32_t)chain_len, 2);
    for (int i = 0; i < chain_len; i++) p = put_hex(p, b->chain[i], 2);
    for (int i = 0; i < count; i++) {
        const pp_page_t *g = &b->pages[i];
        p = put_hex(p, g->start, 2);
        p = put_hex(p, g->len, 2);
        p = put_hex(p, g->swing, 2);
        p = put_hex(p, g->dir, 2);
        for (int c = 0; c < PP_COLS; c++) p = put_hex(p, g->notes[c], 6);
    }
    p = put_hex(p, sum_chars(out, (int)(p - out)), PP_CHECK_CHARS);
    return (int)(p - out);
}

/* Everything is checked before anything changes: a rejected save leaves the current work alone. */
static int valid(const char *s, int len)
{
    if (len < PP_HEADER + PP_CHECK_CHARS || s[0] != 'P' || s[1] != 'P' || s[2] != '1') return 0;
    int count = get_hex(s + 3, 2), sel = get_hex(s + 5, 2), pitch = get_hex(s + 7, 2), chain_len = get_hex(s + 9, 2);
    if (count < 1 || count > PP_PAGES || sel < 0 || sel >= count || pitch < 0 || pitch >= PP_KEYS) return 0;
    if (chain_len < 0 || chain_len > PP_CHAIN_MAX) return 0;
    if (len != PP_HEADER + chain_len * 2 + count * PP_PAGE_CHARS + PP_CHECK_CHARS) return 0;
    if (get_hex(s + len - PP_CHECK_CHARS, PP_CHECK_CHARS) != (int32_t)sum_chars(s, len - PP_CHECK_CHARS)) return 0;
    const char *p = s + PP_HEADER;
    for (int i = 0; i < chain_len; i++, p += 2) {
        int page = get_hex(p, 2);
        if (page < 0 || page >= count) return 0;
    }
    for (int i = 0; i < count; i++) {
        int start = get_hex(p, 2), n = get_hex(p + 2, 2), swing = get_hex(p + 4, 2), dir = get_hex(p + 6, 2);
        p += 8;
        if (start < 0 || n < 1 || start + n > PP_COLS) return 0;
        if (swing < PP_SWING_MIN || swing > PP_SWING_MAX || dir < 0 || dir >= PP_DIRS) return 0;
        for (int c = 0; c < PP_COLS; c++, p += 6) {
            int32_t notes = get_hex(p, 6);
            if (notes < 0 || notes >= 1L << PP_KEYS) return 0;
        }
    }
    return 1;
}

int pp_parse(int mode, const char *s, int len)
{
    if (!s || !valid(s, len)) return -1;
    pp_clear(mode);
    pp_bank_t *b = &pp_bank[mode & 1];
    int count = get_hex(s + 3, 2), chain_len = get_hex(s + 9, 2);
    const char *p = s + PP_HEADER;
    for (int i = 0; i < chain_len; i++, p += 2) b->chain[i] = (uint8_t)get_hex(p, 2);
    for (int i = 0; i < count; i++) {
        pp_page_t *g = &b->pages[i];
        g->start = (uint8_t)get_hex(p, 2);
        g->len = (uint8_t)get_hex(p + 2, 2);
        g->swing = (uint8_t)get_hex(p + 4, 2);
        g->dir = (uint8_t)get_hex(p + 6, 2);
        p += 8;
        for (int c = 0; c < PP_COLS; c++, p += 6) g->notes[c] = (uint32_t)get_hex(p, 6);
    }
    b->count = (uint8_t)count;
    b->sel = (uint8_t)get_hex(s + 5, 2);
    b->play_page = b->sel;
    b->pitch_key = (uint8_t)get_hex(s + 7, 2);
    b->chain_len = (uint8_t)chain_len;
    b->chain_pick = b->sel;
    return 0;
}
