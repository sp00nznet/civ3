/* Strict numeric options shared with synthetic tests. */
#pragma once
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static inline int civ3_unsigned_option(const char *text, uint32_t minimum, uint32_t maximum, uint32_t *result) {
    if (!text || !*text || strspn(text, "0123456789") != strlen(text)) return 0;
    char *end; errno = 0;
    unsigned long long n = strtoull(text, &end, 10);
    if (errno || *end || n < minimum || n > maximum) return 0;
    *result = (uint32_t)n; return 1;
}
