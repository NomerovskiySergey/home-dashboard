#pragma once

#include <stddef.h>
#include <stdbool.h>

/* Bound recursion before cJSON allocates or visits any child, including unknown fields. */
static inline const char *dashboard_json_object_end(const char *start, const char *end)
{
    enum { MAX_DEPTH = 32, MAX_RECORD_BYTES = 32768 };
    if (!start || start == end || *start != '{') return NULL;
    char closing[MAX_DEPTH];
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (const char *p = start; p < end && (size_t)(p - start) < MAX_RECORD_BYTES; ++p) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (*p == '\\') escaped = true;
            else if (*p == '"') quoted = false;
        } else if (*p == '"') quoted = true;
        else if (*p == '{' || *p == '[') {
            if (depth == MAX_DEPTH) return NULL;
            closing[depth++] = *p == '{' ? '}' : ']';
        } else if (*p == '}' || *p == ']') {
            if (!depth || closing[depth - 1] != *p) return NULL;
            if (!--depth) return p + 1;
        }
    }
    return NULL;
}
