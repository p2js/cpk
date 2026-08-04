#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tomlc17/src/tomlc17.h"

typedef struct {
    char* buf;
    size_t len;
    size_t cap;
} sbuf_t;

static int sbuf_reserve(sbuf_t* b, size_t add) {
    size_t need = b->len + add + 1;
    if (need <= b->cap) return 1;
    size_t ncap = b->cap ? b->cap : 256;
    while (ncap < need) ncap *= 2;
    char* p = (char*)realloc(b->buf, ncap);
    if (!p) return 0;
    b->buf = p;
    b->cap = ncap;
    return 1;
}

static int sbuf_putc(sbuf_t* b, char c) {
    if (!sbuf_reserve(b, 1)) return 0;
    b->buf[b->len++] = c;
    b->buf[b->len] = '\0';
    return 1;
}

static int sbuf_puts(sbuf_t* b, const char* s) {
    size_t n = strlen(s);
    if (!sbuf_reserve(b, n)) return 0;
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
    return 1;
}

static int sbuf_printf(sbuf_t* b, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return 0;
    }
    if (!sbuf_reserve(b, (size_t)n)) {
        va_end(ap2);
        return 0;
    }
    vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
    return 1;
}

static int is_bare_key_char(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

static int emit_key(sbuf_t* b, const char* k, int len) {
    int bare = 1;
    for (int i = 0; i < len; i++) {
        if (!is_bare_key_char((unsigned char)k[i])) {
            bare = 0;
            break;
        }
    }
    if (bare && len > 0) {
        return sbuf_printf(b, "%.*s", len, k);
    }

    if (!sbuf_putc(b, '"')) return 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)k[i];
        switch (c) {
            case '\\':
                if (!sbuf_puts(b, "\\\\")) return 0;
                break;
            case '"':
                if (!sbuf_puts(b, "\\\"")) return 0;
                break;
            case '\b':
                if (!sbuf_puts(b, "\\b")) return 0;
                break;
            case '\f':
                if (!sbuf_puts(b, "\\f")) return 0;
                break;
            case '\n':
                if (!sbuf_puts(b, "\\n")) return 0;
                break;
            case '\r':
                if (!sbuf_puts(b, "\\r")) return 0;
                break;
            case '\t':
                if (!sbuf_puts(b, "\\t")) return 0;
                break;
            default:
                if (c < 0x20) {
                    if (!sbuf_printf(b, "\\u%04x", c)) return 0;
                } else {
                    if (!sbuf_putc(b, (char)c)) return 0;
                }
        }
    }
    return sbuf_putc(b, '"');
}

static int emit_string_value(sbuf_t* b, const char* s, int len) {
    if (!sbuf_putc(b, '"')) return 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '\\':
                if (!sbuf_puts(b, "\\\\")) return 0;
                break;
            case '"':
                if (!sbuf_puts(b, "\\\"")) return 0;
                break;
            case '\b':
                if (!sbuf_puts(b, "\\b")) return 0;
                break;
            case '\f':
                if (!sbuf_puts(b, "\\f")) return 0;
                break;
            case '\n':
                if (!sbuf_puts(b, "\\n")) return 0;
                break;
            case '\r':
                if (!sbuf_puts(b, "\\r")) return 0;
                break;
            case '\t':
                if (!sbuf_puts(b, "\\t")) return 0;
                break;
            default:
                if (c < 0x20) {
                    if (!sbuf_printf(b, "\\u%04x", c)) return 0;
                } else {
                    if (!sbuf_putc(b, (char)c)) return 0;
                }
        }
    }
    return sbuf_putc(b, '"');
}

static int emit_ts(sbuf_t* b, const toml_datum_t* d) {
    int y = d->u.ts.year, mo = d->u.ts.month, da = d->u.ts.day;
    int h = d->u.ts.hour, mi = d->u.ts.minute, se = d->u.ts.second;
    int us = d->u.ts.usec;
    int tz = d->u.ts.tz;

    if (d->type == TOML_DATE) {
        return sbuf_printf(b, "%04d-%02d-%02d", y, mo, da);
    } else if (d->type == TOML_TIME) {
        if (us) return sbuf_printf(b, "%02d:%02d:%02d.%06d", h, mi, se, us);
        return sbuf_printf(b, "%02d:%02d:%02d", h, mi, se);
    } else {
        if (d->type == TOML_DATETIME) {
            if (us)
                return sbuf_printf(b, "%04d-%02d-%02dT%02d:%02d:%02d.%06d", y, mo, da, h, mi, se,
                                   us);
            return sbuf_printf(b, "%04d-%02d-%02dT%02d:%02d:%02d", y, mo, da, h, mi, se);
        }
        char sign = '+';
        int off = tz;
        if (off < 0) {
            sign = '-';
            off = -off;
        }
        if (us) {
            return sbuf_printf(b, "%04d-%02d-%02dT%02d:%02d:%02d.%06d%c%02d:%02d", y, mo, da, h, mi,
                               se, us, sign, off / 60, off % 60);
        }
        return sbuf_printf(b, "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d", y, mo, da, h, mi, se,
                           sign, off / 60, off % 60);
    }
}

static int emit_value(sbuf_t* b, const toml_datum_t* d, int indent);

static int emit_array(sbuf_t* b, const toml_datum_t* d, int indent) {
    if (!sbuf_puts(b, "[\n")) return 0;
    for (int i = 0; i < d->u.arr.size; i++) {
        for (int k = 0; k < indent + 4; k++)
            if (!sbuf_putc(b, ' ')) return 0;
        if (!emit_value(b, &d->u.arr.elem[i], indent + 4)) return 0;
        if (!sbuf_puts(b, ",\n")) return 0;
    }
    for (int k = 0; k < indent; k++)
        if (!sbuf_putc(b, ' ')) return 0;
    return sbuf_putc(b, ']');
}

static int emit_inline_table(sbuf_t* b, const toml_datum_t* d, int indent) {
    (void)indent;
    if (!sbuf_puts(b, "{")) return 0;
    bool first = true;
    for (int i = 0; i < d->u.tab.size; i++) {
        if (d->u.tab.value[i].type == TOML_UNKNOWN) continue;
        if (!first && !sbuf_puts(b, ", ")) return 0;
        first = false;
        if (!emit_key(b, d->u.tab.key[i], d->u.tab.len[i])) return 0;
        if (!sbuf_puts(b, " = ")) return 0;
        if (!emit_value(b, &d->u.tab.value[i], indent)) return 0;
    }
    return sbuf_putc(b, '}');
}

static int emit_value(sbuf_t* b, const toml_datum_t* d, int indent) {
    switch (d->type) {
        case TOML_UNKNOWN:
            return 1;
        case TOML_STRING:
            return emit_string_value(b, d->u.str.ptr, d->u.str.len);
        case TOML_INT64:
            return sbuf_printf(b, "%" PRId64, d->u.int64);
        case TOML_FP64:
            return sbuf_printf(b, "%.17g", d->u.fp64);
        case TOML_BOOLEAN:
            return sbuf_puts(b, d->u.boolean ? "true" : "false");
        case TOML_DATE:
        case TOML_TIME:
        case TOML_DATETIME:
        case TOML_DATETIMETZ:
            return emit_ts(b, d);
        case TOML_ARRAY:
            return emit_array(b, d, indent);
        case TOML_TABLE:
            return emit_inline_table(b, d, indent);
        default:
            return 0;
    }
}

static int emit_path(sbuf_t* b, const char** path, int* pathlen, int depth) {
    for (int i = 0; i < depth; i++) {
        if (i && !sbuf_putc(b, '.')) return 0;
        if (!emit_key(b, path[i], pathlen[i])) return 0;
    }
    return 1;
}

static int emit_table_recursive(
    sbuf_t* b, const toml_datum_t* tab, const char** path, int* pathlen, int depth);

static int emit_table_body(
    sbuf_t* b, const toml_datum_t* tab, const char** path, int* pathlen, int depth) {
    bool has_values = false;

    /* Emit all non-table values first. */
    for (int i = 0; i < tab->u.tab.size; i++) {
        const toml_datum_t* v = &tab->u.tab.value[i];

        if (v->type == TOML_UNKNOWN || v->type == TOML_TABLE) continue;

        has_values = true;

        if (!emit_key(b, tab->u.tab.key[i], tab->u.tab.len[i])) return 0;
        if (!sbuf_puts(b, " = ")) return 0;
        if (!emit_value(b, v, 0)) return 0;
        if (!sbuf_putc(b, '\n')) return 0;
    }

    /* Separate this table's values from its subtables. */
    bool need_blank = has_values;

    for (int i = 0; i < tab->u.tab.size; i++) {
        const toml_datum_t* v = &tab->u.tab.value[i];

        if (v->type != TOML_TABLE) continue;

        path[depth] = tab->u.tab.key[i];
        pathlen[depth] = tab->u.tab.len[i];

        if (need_blank)
            if (!sbuf_putc(b, '\n')) return 0;

        if (!emit_table_recursive(b, v, path, pathlen, depth + 1)) return 0;

        need_blank = true;
    }

    return 1;
}

static int table_has_values(const toml_datum_t* tab) {
    for (int i = 0; i < tab->u.tab.size; i++) {
        const toml_datum_t* v = &tab->u.tab.value[i];

        if (v->type != TOML_UNKNOWN && v->type != TOML_TABLE) return 1;
    }

    return 0;
}

static int emit_table_recursive(
    sbuf_t* b, const toml_datum_t* tab, const char** path, int* pathlen, int depth) {
    if (depth > 0 && table_has_values(tab)) {
        if (!sbuf_putc(b, '[')) return 0;
        if (!emit_path(b, path, pathlen, depth)) return 0;
        if (!sbuf_puts(b, "]\n")) return 0;
    }

    return emit_table_body(b, tab, path, pathlen, depth);
}

char* toml_stringify_result(const toml_result_t* r) {
    if (!r || !r->ok) return NULL;

    sbuf_t b = {0};
    const char* path[64];
    int pathlen[64];

    if (!emit_table_recursive(&b, &r->toptab, path, pathlen, 0)) {
        free(b.buf);
        return NULL;
    }

    return b.buf ? b.buf : strdup("");
}

void toml_stringify_free(char* s) {
    free(s);
}
