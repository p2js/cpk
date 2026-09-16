/*
 * write.c
 * Implementation of a stringifier and formatter for tomlc17-parsed TOML files.
 */
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
} StringBuffer;

static int sbuf_reserve(StringBuffer* buffer, size_t add) {
    size_t need = buffer->len + add + 1;
    if (need <= buffer->cap) return 0;
    size_t new_cap = buffer->cap ? buffer->cap : 256;
    while (new_cap < need) new_cap *= 2;
    char* p = (char*)realloc(buffer->buf, new_cap);
    if (!p) return 1;
    buffer->buf = p;
    buffer->cap = new_cap;
    return 0;
}

static int sbuf_putc(StringBuffer* buffer, char c) {
    if (sbuf_reserve(buffer, 1)) return 1;
    buffer->buf[buffer->len++] = c;
    buffer->buf[buffer->len] = '\0';
    return 0;
}

static int sbuf_puts(StringBuffer* buffer, const char* str) {
    size_t str_length = strlen(str);
    if (sbuf_reserve(buffer, str_length)) return 1;
    memcpy(buffer->buf + buffer->len, str, str_length);
    buffer->len += str_length;
    buffer->buf[buffer->len] = '\0';
    return 0;
}

static int sbuf_printf(StringBuffer* buffer, const char* format_string, ...) {
    va_list arg_list;
    va_start(arg_list, format_string);
    va_list arg_list_2;
    va_copy(arg_list_2, arg_list);
    int print_length = vsnprintf(NULL, 0, format_string, arg_list);
    va_end(arg_list);
    if (print_length < 0) {
        va_end(arg_list_2);
        return 1;
    }
    if (sbuf_reserve(buffer, (size_t)print_length)) {
        va_end(arg_list_2);
        return 1;
    }
    vsnprintf(buffer->buf + buffer->len, buffer->cap - buffer->len, format_string, arg_list_2);
    va_end(arg_list_2);
    buffer->len += (size_t)print_length;
    return 0;
}

static int is_bare_key_char(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

static int emit_key(StringBuffer* buffer, const char* key, int len) {
    int bare = 1;
    for (int i = 0; i < len; i++) {
        if (!is_bare_key_char((unsigned char)key[i])) {
            bare = 0;
            break;
        }
    }
    if (bare && len > 0) {
        return sbuf_printf(buffer, "%.*s", len, key);
    }

    if (sbuf_putc(buffer, '"')) return 1;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)key[i];
        switch (c) {
            case '\\':
                if (sbuf_puts(buffer, "\\\\")) return 1;
                break;
            case '"':
                if (sbuf_puts(buffer, "\\\"")) return 1;
                break;
            case '\b':
                if (sbuf_puts(buffer, "\\b")) return 1;
                break;
            case '\f':
                if (sbuf_puts(buffer, "\\f")) return 1;
                break;
            case '\n':
                if (sbuf_puts(buffer, "\\n")) return 1;
                break;
            case '\r':
                if (sbuf_puts(buffer, "\\r")) return 1;
                break;
            case '\t':
                if (sbuf_puts(buffer, "\\t")) return 1;
                break;
            default:
                if (c < 0x20) {
                    if (sbuf_printf(buffer, "\\u%04x", c)) return 1;
                } else {
                    if (sbuf_putc(buffer, (char)c)) return 1;
                }
        }
    }
    return sbuf_putc(buffer, '"');
}

static int emit_string_value(StringBuffer* buffer, const char* str, int len) {
    if (sbuf_putc(buffer, '"')) return 1;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
            case '\\':
                if (sbuf_puts(buffer, "\\\\")) return 1;
                break;
            case '"':
                if (sbuf_puts(buffer, "\\\"")) return 1;
                break;
            case '\b':
                if (sbuf_puts(buffer, "\\b")) return 1;
                break;
            case '\f':
                if (sbuf_puts(buffer, "\\f")) return 1;
                break;
            case '\n':
                if (sbuf_puts(buffer, "\\n")) return 1;
                break;
            case '\r':
                if (sbuf_puts(buffer, "\\r")) return 1;
                break;
            case '\t':
                if (sbuf_puts(buffer, "\\t")) return 1;
                break;
            default:
                if (c < 0x20) {
                    if (sbuf_printf(buffer, "\\u%04x", c)) return 1;
                } else {
                    if (sbuf_putc(buffer, (char)c)) return 1;
                }
        }
    }
    return sbuf_putc(buffer, '"');
}

static int emit_timestamp(StringBuffer* buffer, const toml_datum_t* datum) {
    int year = datum->u.ts.year, month = datum->u.ts.month, day = datum->u.ts.day;
    int hour = datum->u.ts.hour, minute = datum->u.ts.minute, second = datum->u.ts.second;
    int microsecond = datum->u.ts.usec;
    int timezone = datum->u.ts.tz;

    if (datum->type == TOML_DATE) {
        return sbuf_printf(buffer, "%04d-%02d-%02d", year, month, day);
    } else if (datum->type == TOML_TIME) {
        if (microsecond)
            return sbuf_printf(buffer, "%02d:%02d:%02d.%06d", hour, minute, second, microsecond);
        return sbuf_printf(buffer, "%02d:%02d:%02d", hour, minute, second);
    } else {
        if (datum->type == TOML_DATETIME) {
            if (microsecond)
                return sbuf_printf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d.%06d", year, month, day,
                                   hour, minute, second, microsecond);
            return sbuf_printf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hour,
                               minute, second);
        }
        char sign = '+';
        int off = timezone;
        if (off < 0) {
            sign = '-';
            off = -off;
        }
        if (microsecond) {
            return sbuf_printf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d.%06d%c%02d:%02d", year, month,
                               day, hour, minute, second, microsecond, sign, off / 60, off % 60);
        }
        return sbuf_printf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d", year, month, day,
                           hour, minute, second, sign, off / 60, off % 60);
    }
}

static int emit_value(StringBuffer* buffer, const toml_datum_t* datum, int indent);

static int emit_array(StringBuffer* buffer, const toml_datum_t* datum, int indent) {
    if (sbuf_puts(buffer, "[\n")) return 1;
    for (int i = 0; i < datum->u.arr.size; i++) {
        for (int j = 0; j < indent + 4; j++) {
            if (sbuf_putc(buffer, ' ')) return 1;
        }
        if (emit_value(buffer, &datum->u.arr.elem[i], indent + 4)) return 1;
        if (sbuf_puts(buffer, ",\n")) return 1;
    }
    for (int j = 0; j < indent; j++) {
        if (sbuf_putc(buffer, ' ')) return 1;
    }
    return sbuf_putc(buffer, ']');
}

static int emit_inline_table(StringBuffer* buffer, const toml_datum_t* datum, int indent) {
    (void)indent;
    if (sbuf_puts(buffer, "{")) return 1;
    bool first = true;
    for (int i = 0; i < datum->u.tab.size; i++) {
        if (datum->u.tab.value[i].type == TOML_UNKNOWN) continue;
        if (!first && sbuf_puts(buffer, ", ")) return 1;
        first = false;
        if (emit_key(buffer, datum->u.tab.key[i], datum->u.tab.len[i])) return 1;
        if (sbuf_puts(buffer, " = ")) return 1;
        if (emit_value(buffer, &datum->u.tab.value[i], indent)) return 1;
    }
    return sbuf_putc(buffer, '}');
}

static int emit_value(StringBuffer* buffer, const toml_datum_t* datum, int indent) {
    switch (datum->type) {
        case TOML_UNKNOWN:
            return 0;
        case TOML_STRING:
            return emit_string_value(buffer, datum->u.str.ptr, datum->u.str.len);
        case TOML_INT64:
            return sbuf_printf(buffer, "%" PRId64, datum->u.int64);
        case TOML_FP64:
            return sbuf_printf(buffer, "%.17g", datum->u.fp64);
        case TOML_BOOLEAN:
            return sbuf_puts(buffer, datum->u.boolean ? "true" : "false");
        case TOML_DATE:
        case TOML_TIME:
        case TOML_DATETIME:
        case TOML_DATETIMETZ:
            return emit_timestamp(buffer, datum);
        case TOML_ARRAY:
            return emit_array(buffer, datum, indent);
        case TOML_TABLE:
            return emit_inline_table(buffer, datum, indent);
        default:
            return 1;
    }
}

static int emit_path(StringBuffer* buffer, const char** path, int* pathlen, int depth) {
    for (int i = 0; i < depth; i++) {
        if (i && sbuf_putc(buffer, '.')) return 1;
        if (emit_key(buffer, path[i], pathlen[i])) return 1;
    }
    return 0;
}

static int emit_table_recursive(
    StringBuffer* buffer, const toml_datum_t* table, const char** path, int* path_len, int depth);

static int emit_table_body(
    StringBuffer* buffer, const toml_datum_t* table, const char** path, int* path_len, int depth) {
    bool has_values = false;

    /* Emit all non-table values first. */
    for (int i = 0; i < table->u.tab.size; i++) {
        const toml_datum_t* v = &table->u.tab.value[i];

        if (v->type == TOML_UNKNOWN || v->type == TOML_TABLE) continue;

        has_values = true;
        if (emit_key(buffer, table->u.tab.key[i], table->u.tab.len[i])) return 1;
        if (sbuf_puts(buffer, " = ")) return 1;
        if (emit_value(buffer, v, 0)) return 1;
        if (sbuf_putc(buffer, '\n')) return 1;
    }

    /* Separate this table's values from its subtables. */
    bool need_blank = has_values;

    for (int i = 0; i < table->u.tab.size; i++) {
        const toml_datum_t* value = &table->u.tab.value[i];

        if (value->type != TOML_TABLE) continue;

        path[depth] = table->u.tab.key[i];
        path_len[depth] = table->u.tab.len[i];

        if (need_blank)
            if (sbuf_putc(buffer, '\n')) return 1;

        if (emit_table_recursive(buffer, value, path, path_len, depth + 1)) return 1;

        need_blank = true;
    }

    return 0;
}

static int table_has_values(const toml_datum_t* table) {
    for (int i = 0; i < table->u.tab.size; i++) {
        const toml_datum_t* value = &table->u.tab.value[i];

        if (value->type != TOML_UNKNOWN && value->type != TOML_TABLE) return 1;
    }

    return 0;
}

static int emit_table_recursive(
    StringBuffer* buffer, const toml_datum_t* table, const char** path, int* path_len, int depth) {
    bool is_dependencies_table = (depth == 1 && path[0] && strcmp(path[0], "dependencies") == 0);

    if (depth > 0 && (table_has_values(table) || is_dependencies_table)) {
        if (sbuf_putc(buffer, '[')) return 1;
        if (emit_path(buffer, path, path_len, depth)) return 1;
        if (sbuf_puts(buffer, "]\n")) return 1;
    }

    return emit_table_body(buffer, table, path, path_len, depth);
}

char* toml_stringify_result(const toml_result_t* result) {
    if (!result || !result->ok) return NULL;

    StringBuffer buffer = {0};
    const char* path[64];
    int pathlen[64];

    if (emit_table_recursive(&buffer, &result->toptab, path, pathlen, 0)) {
        free(buffer.buf);
        return NULL;
    }

    return buffer.buf ? buffer.buf : strdup("");
}

void toml_stringify_free(char* str) {
    free(str);
}
