#pragma once
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct { char kind; int x, y; double t; int order; } ev_t;
static inline int script_number(const char **cursor, int base, long long low, long long high, char separator, int *out) {
    const char *p = *cursor;
    if (!(*p >= '0' && *p <= '9') && !(low < 0 && *p == '-')) return 0;
    char *end; errno = 0;
    long long value = strtoll(p, &end, base);
    if (errno || end == p || *end != separator || value < low || value > high) return 0;
    *out = (int)(uint32_t)value; *cursor = end + 1; return 1;
}
/* 1 parsed, 0 not a script option, -1 invalid. No side effects on failure. */
static inline int civ3_parse_event(const char *option, const char *value, ev_t *result) {
    ev_t event = {0};
    if (!strcmp(option, "--move") || !strcmp(option, "--click") || !strcmp(option, "--open")) event.kind = option[2];
    else if (!strcmp(option, "--autopilot")) event.kind = 'a';
    else if (!strcmp(option, "--wait")) event.kind = 'w';
    else if (!strcmp(option, "--key")) event.kind = 'k';
    else return 0;
    if (!value || !*value) return -1;
    const char *p = value;
    if (event.kind == 'm' || event.kind == 'c' || event.kind == 'o') {
        if (!script_number(&p, 10, -32768, 32767, ',', &event.x) ||
            !script_number(&p, 10, -32768, 32767, '@', &event.y)) return -1;
    } else if (event.kind == 'k') {
        const char *plus = strchr(p, '+');
        if (plus) {
            if (plus == p) return -1;
            for (; p < plus; ++p) {
                int mask = *p == 'c' ? 1 : *p == 's' ? 2 : *p == 'a' ? 4 : 0;
                if (!mask || (event.y & mask)) return -1;
                event.y |= mask;
            }
            ++p;
        }
        if (!script_number(&p, 0, 1, 255, '@', &event.x)) return -1;
    } else if (event.kind == 'a') {
        if (!script_number(&p, 10, 100, 3600000, '@', &event.x)) return -1;
    } else {
        if (!script_number(&p, 0, 65536, 0xFFFFFFFCull, '@', &event.x) || ((uint32_t)event.x & 3u)) return -1;
    }
    if (!(*p >= '0' && *p <= '9')) return -1;
    char *end; errno = 0;
    event.t = strtod(p, &end);
    if (errno || end == p || *end || !isfinite(event.t) || event.t < 0 || event.t > 86400) return -1;
    *result = event; return 1;
}
