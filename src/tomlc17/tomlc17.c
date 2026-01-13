/*
 * tomlc17.c
 * Implementation of a TOML 1.1 parser and tree navigator.
 *
 * Modified from https://github.com/cktan/tomlc17
 *
 * Copyright (c) 2024-2025, CK Tan.
 * https://github.com/cktan/tomlc17/blob/main/LICENSE
 */
#include "tomlc17.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const toml_datum_t DATUM_ZERO = {0};

static toml_option_t toml_option = {0, realloc, free};

#define TOML_DO(x) \
    if (x)         \
        return -1; \
    else           \
        (void)0

/*
 *  Error buffer
 */
typedef struct ebuf_t {
    char *ptr;
    int len;
} toml_ebuf_t;

/*
 *  Format an error into ebuf[]. Always return -1.
 */
static int toml_RETERROR(toml_ebuf_t ebuf, int lineno, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char *p = ebuf.ptr;
    char *q = p + ebuf.len;
    if (lineno) {
        snprintf(p, q - p, "(line %d) ", lineno);
        p += strlen(p);
    }
    vsnprintf(p, q - p, fmt, args);
    return -1;
}

/*
 *  Memory pool. Allocated a big block once and hand out piecemeal.
 */
typedef struct {
    int top, max;
    char buf[1];  // first byte starts here
} toml_pool_t;

/**
 *  Create a memory pool of N bytes. Return the memory pool on
 *  success, or NULL if out of memory.
 */
static toml_pool_t *toml_pool_create(int N) {
    if (N <= 0) {
        N = 100;  // minimum
    }
    int totalsz = sizeof(toml_pool_t) + N;
    toml_pool_t *pool = toml_option.mem_realloc(0, totalsz);
    if (!pool) {
        return NULL;
    }
    memset(pool, 0, totalsz);
    pool->max = N;
    return pool;
}

/**
 *  Destroy a memory pool.
 */
static void toml_pool_destroy(toml_pool_t *pool) {
    toml_option.mem_free(pool);
}

/**
 *  Allocate n bytes from pool. Return the memory allocated on
 *  success, or NULL if out of memory.
 */
static char *toml_pool_alloc(toml_pool_t *pool, int n) {
    if (pool->top + n > pool->max) {
        return NULL;
    }
    char *ret = pool->buf + pool->top;
    pool->top += n;
    return ret;
}

/* This is a string view. */
typedef struct {
    const char *ptr;
    int len;
} toml_span_t;

/* Represents a multi-part key */
#define KEYPARTMAX 10
typedef struct {
    int nspan;
    toml_span_t span[KEYPARTMAX];
} toml_keypart_t;

static int toml_utf8_to_ucs(const char *s, int len, uint32_t *ret);
static int toml_ucs_to_utf8(uint32_t code, char buf[4]);

// flags for toml_datum_t::flag.
#define FLAG_INLINED 1
#define FLAG_STDEXPR 2
#define FLAG_EXPLICIT 4

// Maximum levels of brackets and braces to prevent
// stack overflow during recursive descent of the parser.
#define BRACKET_LEVEL_MAX 30
#define BRACE_LEVEL_MAX 30

static inline size_t align8(size_t x) {
    return (((x) + 7) & ~7);
}

typedef enum {
    TOK_DOT = 1,
    TOK_EQUAL,
    TOK_COMMA,
    TOK_LBRACK,
    TOK_LLBRACK,
    TOK_RBRACK,
    TOK_RRBRACK,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LIT,
    TOK_STRING,
    TOK_MLSTRING,
    TOK_LITSTRING,
    TOK_MLLITSTRING,
    TOK_TIME,
    TOK_DATE,
    TOK_DATETIME,
    TOK_DATETIMETZ,
    TOK_INTEGER,
    TOK_FLOAT,
    TOK_BOOL,
    TOK_ENDL,
    TOK_FIN = -5000,  // EOF
} toml_toktyp_t;

// Scanner object
typedef struct {
    const char *src;   // src[] is a NUL-terminated string
    const char *endp;  // end of src[]. always pointing at a NUL char.
    const char *cur;   // current char in src[]
    int lineno;        // line number of current char
    char *errmsg;      // point to errbuf if there was an error
    toml_ebuf_t ebuf;

    int bracket_level;  // count depth of [ ]
    int brace_level;    // count depth of { }
} toml_scanner_t;

/* Remember the current state of a scanner */
typedef struct {
    toml_scanner_t *sp;
    const char *cur;  // points into scanner_t::src[]
    int lineno;       // current line number
} toml_scanner_state_t;

// A scan token
typedef struct {
    toml_toktyp_t toktyp;
    int lineno;
    toml_span_t str;

    // values represented by str
    union {
        int64_t int64;
        double fp64;
        bool b1;
        struct {
            // validity depends on toktyp for TIME, DATE, DATETIME, DATETIMETZ
            int year, month, day, hour, minute, sec, usec;
            int tz;  // +- minutes
        } tsval;
    } u;
} toml_token_t;

static void toml_scan_init(toml_scanner_t *sp, const char *src, int len, char *errbuf,
    int errbufsz);
static int toml_scan_key(toml_scanner_t *sp, toml_token_t *tok);
static int toml_scan_value(toml_scanner_t *sp, toml_token_t *tok);
// restore scanner to state before tok was returned
static toml_scanner_state_t toml_scan_mark(toml_scanner_t *sp);
static void toml_scan_restore(toml_scanner_t *sp, toml_scanner_state_t state);

// Parser object
typedef struct parser_t parser_t;
struct parser_t {
    toml_scanner_t scanner;
    toml_datum_t toptab;   // top table
    toml_datum_t *curtab;  // current table
    toml_pool_t *pool;     // memory pool for strings
    toml_ebuf_t ebuf;
};

// Put key into tab dictionary. Return a place to
// the datum for the key on success, or NULL otherwise.
static toml_datum_t *tab_emplace(toml_datum_t *tab, toml_span_t key,
    const char **reason) {
    assert(tab->type == TOML_TABLE);
    int N = tab->u.tab.size;
    for (int i = 0; i < N; i++) {
        if (tab->u.tab.len[i] == key.len &&
            0 == memcmp(tab->u.tab.key[i], key.ptr, key.len)) {
            return &tab->u.tab.value[i];
        }
    }
    // Expand pkey[], plen[] and value[]
    {
        char **pkey = toml_option.mem_realloc(tab->u.tab.key, sizeof(*pkey) * align8(N + 1));
        if (!pkey) {
            *reason = "out of memory";
            return NULL;
        }
        tab->u.tab.key = (const char **)pkey;
    }

    {
        int *plen = toml_option.mem_realloc(tab->u.tab.len, sizeof(*plen) * align8(N + 1));
        if (!plen) {
            *reason = "out of memory";
            return NULL;
        }
        tab->u.tab.len = plen;
    }

    {
        toml_datum_t *value =
            toml_option.mem_realloc(tab->u.tab.value, sizeof(*value) * align8(N + 1));
        if (!value) {
            *reason = "out of memory";
            return NULL;
        }
        tab->u.tab.value = value;
    }

    // Append the new key/value
    tab->u.tab.size = N + 1;
    tab->u.tab.key[N] = (char *)key.ptr;
    tab->u.tab.len[N] = key.len;
    tab->u.tab.value[N] = DATUM_ZERO;
    return &tab->u.tab.value[N];
}

// Find key in tab and return its index. If not found, return -1.
static int toml_tab_find(toml_datum_t *tab, toml_span_t key) {
    assert(tab->type == TOML_TABLE);
    for (int i = 0, top = tab->u.tab.size; i < top; i++) {
        if (tab->u.tab.len[i] == key.len &&
            0 == memcmp(tab->u.tab.key[i], key.ptr, key.len)) {
            return i;
        }
    }
    return -1;
}

// Add a new key in tab. Return 0 on success, -1 otherwise.
// On error, *reason will point to an error message.
static int toml_tab_add(toml_datum_t *tab, toml_span_t newkey, toml_datum_t newvalue,
    const char **reason) {
    assert(tab->type == TOML_TABLE);
    if (-1 != toml_tab_find(tab, newkey)) {
        *reason = "duplicate key";
        return -1;
    }
    toml_datum_t *pvalue = tab_emplace(tab, newkey, reason);
    if (!pvalue) {
        return -1;
    }
    *pvalue = newvalue;
    return 0;
}

// Add a new element into an array. Return 0 on success, -1 otherwise.
// On error, *reason will point to an error message.
static toml_datum_t *toml_arr_emplace(toml_datum_t *arr, const char **reason) {
    assert(arr->type == TOML_ARRAY);
    int n = arr->u.arr.size;
    toml_datum_t *elem = toml_option.mem_realloc(arr->u.arr.elem, sizeof(*elem) * align8(n + 1));
    if (!elem) {
        *reason = "out of memory";
        return NULL;
    }
    arr->u.arr.elem = elem;
    arr->u.arr.size = n + 1;
    elem[n] = DATUM_ZERO;
    return &elem[n];
}

// ------------------- parser section
static int toml_parse_norm(parser_t *pp, toml_token_t tok, toml_span_t *ret_span);
static int toml_parse_val(parser_t *pp, toml_token_t tok, toml_datum_t *ret);
static int toml_parse_keyvalue_expr(parser_t *pp, toml_token_t tok);
static int toml_parse_std_table_expr(parser_t *pp, toml_token_t tok);
static int toml_parse_array_table_expr(parser_t *pp, toml_token_t tok);

static toml_datum_t toml_mkdatum(toml_type_t ty) {
    toml_datum_t ret = {0};
    ret.type = ty;
    if (ty == TOML_DATE || ty == TOML_TIME || ty == TOML_DATETIME ||
        ty == TOML_DATETIMETZ) {
        ret.u.ts.year = -1;
        ret.u.ts.month = -1;
        ret.u.ts.day = -1;
        ret.u.ts.hour = -1;
        ret.u.ts.minute = -1;
        ret.u.ts.second = -1;
        ret.u.ts.usec = -1;
        ret.u.ts.tz = -1;
    }
    return ret;
}

// Recursively free any dynamically allocated memory in the datum tree
static void toml_datum_free(toml_datum_t *datum) {
    if (datum->type == TOML_TABLE) {
        for (int i = 0, top = datum->u.tab.size; i < top; i++) {
            toml_datum_free(&datum->u.tab.value[i]);
        }
        toml_option.mem_free(datum->u.tab.key);
        toml_option.mem_free(datum->u.tab.len);
        toml_option.mem_free(datum->u.tab.value);
    } else if (datum->type == TOML_ARRAY) {
        for (int i = 0, top = datum->u.arr.size; i < top; i++) {
            toml_datum_free(&datum->u.arr.elem[i]);
        }
        toml_option.mem_free(datum->u.arr.elem);
    }
    // other types do not allocate memory
    *datum = DATUM_ZERO;
}

// Make a deep copy of src to dst.
// Return 0 on success, -1 otherwise.
static int toml_datum_copy(toml_datum_t *dst, toml_datum_t src, toml_pool_t *pool,
    const char **reason) {
    *dst = toml_mkdatum(src.type);
    switch (src.type) {
        case TOML_STRING:
            dst->u.str.ptr = toml_pool_alloc(pool, src.u.str.len + 1);
            if (!dst->u.str.ptr) {
                *reason = "out of memory";
                goto bail;
            }
            dst->u.str.len = src.u.str.len;
            memcpy((char *)dst->u.str.ptr, src.u.str.ptr, src.u.str.len + 1);
            break;
        case TOML_TABLE:
            for (int i = 0; i < src.u.tab.size; i++) {
                toml_span_t newkey = {src.u.tab.key[i], src.u.tab.len[i]};
                toml_datum_t *pvalue = tab_emplace(dst, newkey, reason);
                if (!pvalue) {
                    goto bail;
                }
                if (toml_datum_copy(pvalue, src.u.tab.value[i], pool, reason)) {
                    goto bail;
                }
            }
            break;
        case TOML_ARRAY:
            for (int i = 0; i < src.u.arr.size; i++) {
                toml_datum_t *pelem = toml_arr_emplace(dst, reason);
                if (!pelem) {
                    goto bail;
                }
                if (toml_datum_copy(pelem, src.u.arr.elem[i], pool, reason)) {
                    goto bail;
                }
            }
            break;
        default:
            *dst = src;
            break;
    }

    return 0;

bail:
    toml_datum_free(dst);
    return -1;
}

// Check if datum is an array of tables.
static inline bool toml_is_array_of_tables(toml_datum_t datum) {
    bool ret = (datum.type == TOML_ARRAY);
    for (int i = 0; ret && i < datum.u.arr.size; i++) {
        ret = (datum.u.arr.elem[i].type == TOML_TABLE);
    }
    return ret;
}

// Merge src into dst. Return 0 on success, -1 otherwise.
static int toml_datum_merge(toml_datum_t *dst, toml_datum_t src, toml_pool_t *pool,
    const char **reason) {
    if (dst->type != src.type) {
        toml_datum_free(dst);
        return toml_datum_copy(dst, src, pool, reason);
    }
    switch (src.type) {
        case TOML_TABLE:
            // for key-value in src:
            //    override key-value in dst.
            for (int i = 0; i < src.u.tab.size; i++) {
                toml_span_t key;
                key.ptr = src.u.tab.key[i];
                key.len = src.u.tab.len[i];
                toml_datum_t *pvalue = tab_emplace(dst, key, reason);
                if (!pvalue) {
                    return -1;
                }
                if (pvalue->type) {
                    TOML_DO(toml_datum_merge(pvalue, src.u.tab.value[i], pool, reason));
                } else {
                    toml_datum_free(pvalue);
                    TOML_DO(toml_datum_copy(pvalue, src.u.tab.value[i], pool, reason));
                }
            }
            return 0;
        case TOML_ARRAY:
            if (toml_is_array_of_tables(src)) {
                // append src array to dst
                for (int i = 0; i < src.u.arr.size; i++) {
                    toml_datum_t *pelem = toml_arr_emplace(dst, reason);
                    if (!pelem) {
                        return -1;
                    }
                    TOML_DO(toml_datum_copy(pelem, src.u.arr.elem[i], pool, reason));
                }
                return 0;
            }
            // fallthru
        default:
            break;
    }
    toml_datum_free(dst);
    return toml_datum_copy(dst, src, pool, reason);
}

// Compare the content of a and b.
static bool toml_datum_equiv(toml_datum_t a, toml_datum_t b) {
    if (a.type != b.type) {
        return false;
    }
    int N;
    switch (a.type) {
        case TOML_STRING:
            return a.u.str.len == b.u.str.len &&
                   0 == memcmp(a.u.str.ptr, b.u.str.ptr, a.u.str.len);
        case TOML_INT64:
            return a.u.int64 == b.u.int64;
        case TOML_FP64:
            return a.u.fp64 == b.u.fp64;
        case TOML_BOOLEAN:
            return !!a.u.boolean == !!b.u.boolean;
        case TOML_DATE:
            return a.u.ts.year == b.u.ts.year && a.u.ts.month == b.u.ts.month &&
                   a.u.ts.day == b.u.ts.day;
        case TOML_TIME:
            return a.u.ts.hour == b.u.ts.hour && a.u.ts.minute == b.u.ts.minute &&
                   a.u.ts.second == b.u.ts.second && a.u.ts.usec == b.u.ts.usec;
        case TOML_DATETIME:
            return a.u.ts.year == b.u.ts.year && a.u.ts.month == b.u.ts.month &&
                   a.u.ts.day == b.u.ts.day && a.u.ts.hour == b.u.ts.hour &&
                   a.u.ts.minute == b.u.ts.minute && a.u.ts.second == b.u.ts.second &&
                   a.u.ts.usec == b.u.ts.usec;
        case TOML_DATETIMETZ:
            return a.u.ts.year == b.u.ts.year && a.u.ts.month == b.u.ts.month &&
                   a.u.ts.day == b.u.ts.day && a.u.ts.hour == b.u.ts.hour &&
                   a.u.ts.minute == b.u.ts.minute && a.u.ts.second == b.u.ts.second &&
                   a.u.ts.usec == b.u.ts.usec && a.u.ts.tz == b.u.ts.tz;
        case TOML_ARRAY:
            N = a.u.arr.size;
            if (N != b.u.arr.size) {
                return false;
            }
            for (int i = 0; i < N; i++) {
                if (!toml_datum_equiv(a.u.arr.elem[i], b.u.arr.elem[i])) {
                    return false;
                }
            }
            return true;
        case TOML_TABLE:
            N = a.u.tab.size;
            if (N != b.u.tab.size) {
                return false;
            }
            for (int i = 0; i < N; i++) {
                int len = a.u.tab.len[i];
                if (len != b.u.tab.len[i]) {
                    return false;
                }
                if (0 != memcmp(a.u.tab.key[i], b.u.tab.key[i], len)) {
                    return false;
                }
                if (!toml_datum_equiv(a.u.tab.value[i], b.u.tab.value[i])) {
                    return false;
                }
            }
            return true;
        default:
            break;
    }
    return false;
}

/**
 *  Override values in r1 using r2. Return a new result. All results
 *  (i.e., r1, r2 and the returned result) must be freed using toml_free()
 *  after use.
 *
 *  LOGIC:
 *   ret = copy of r1
 *   for each item x in r2:
 *     if x is not in ret:
 *          override
 *     elif x in ret is NOT of the same type:
 *         override
 *     elif x is an array of tables:
 *         append r2.x to ret.x
 *     elif x is a table:
 *         merge r2.x to ret.x
 *     else:
 *         override
 */
toml_result_t toml_merge(const toml_result_t *r1, const toml_result_t *r2) {
    const char *reason = "";
    toml_result_t ret = {0};
    toml_pool_t *pool = 0;
    if (!r1->ok) {
        reason = "param error: r1 not ok";
        goto bail;
    }
    if (!r2->ok) {
        reason = "param error: r2 not ok";
        goto bail;
    }
    {
        toml_pool_t *r1pool = (toml_pool_t *)r1->__internal;
        toml_pool_t *r2pool = (toml_pool_t *)r2->__internal;
        pool = toml_pool_create(r1pool->top + r2pool->top);
        if (!pool) {
            reason = "out of memory";
            goto bail;
        }
    }

    // Make a copy of r1
    if (toml_datum_copy(&ret.toptab, r1->toptab, pool, &reason)) {
        goto bail;
    }

    // Merge r2 into the result
    if (toml_datum_merge(&ret.toptab, r2->toptab, pool, &reason)) {
        goto bail;
    }

    ret.ok = 1;
    ret.__internal = pool;
    return ret;

bail:
    toml_pool_destroy(pool);
    snprintf(ret.errmsg, sizeof(ret.errmsg), "%s", reason);
    return ret;
}

bool toml_equiv(const toml_result_t *r1, const toml_result_t *r2) {
    if (!(r1->ok && r2->ok)) {
        return false;
    }
    return toml_datum_equiv(r1->toptab, r2->toptab);
}

/**
 * Find a key in a toml_table. Return the value of the key if found,
 * or a TOML_UNKNOWN otherwise.
 */
toml_datum_t toml_get(toml_datum_t datum, const char *key) {
    toml_datum_t ret = {0};
    if (datum.type == TOML_TABLE) {
        int n = datum.u.tab.size;
        const char **pkey = datum.u.tab.key;
        toml_datum_t *pvalue = datum.u.tab.value;
        for (int i = 0; i < n; i++) {
            if (0 == strcmp(pkey[i], key)) {
                return pvalue[i];
            }
        }
    }
    return ret;
}

/**
 * Locate a value starting from a toml_table. Return the value of the key if
 * found, or a TOML_UNKNOWN otherwise.
 *
 * Note: the multipart-key is separated by DOT, and must not have any escape
 * chars.
 */
toml_datum_t toml_seek(toml_datum_t table, const char *multipart_key) {
    if (table.type != TOML_TABLE) {
        return DATUM_ZERO;
    }

    char buf[128];
    int bufsz = strlen(multipart_key) + 1;
    if (bufsz >= (int)sizeof(buf)) {
        return DATUM_ZERO;
    }
    memcpy(buf, multipart_key, bufsz);

    // go through the multipart name part by part.
    char *p = buf;
    char *q = strchr(p, '.');
    toml_datum_t datum = table;
    while (q && datum.type == TOML_TABLE) {
        *q = 0;
        datum = toml_get(datum, p);
        if (datum.type == TOML_TABLE) {
            p = q + 1;
            q = strchr(p, '.');
        }
    }

    if (!q && datum.type == TOML_TABLE) {
        return toml_get(datum, p);
    }

    return DATUM_ZERO;
}

/**
 *  Return the default options.
 */
toml_option_t toml_default_option(void) {
    toml_option_t opt = {0, realloc, free};
    return opt;
}

/**
 *  Override the current options.
 */
void toml_set_option(toml_option_t opt) {
    toml_option = opt;
}

/**
 *  Free the result returned by toml_parse().
 */
void toml_free(toml_result_t result) {
    toml_datum_free(&result.toptab);
    toml_pool_destroy((toml_pool_t *)result.__internal);
}

/**
 *  Parse a toml document.
 */
toml_result_t toml_parse_file_ex(const char *fname) {
    toml_result_t result = {0};
    FILE *fp = fopen(fname, "r");
    if (!fp) {
        snprintf(result.errmsg, sizeof(result.errmsg), "fopen: %s", fname);
        return result;
    }
    result = toml_parse_file(fp);
    fclose(fp);
    return result;
}

/**
 *  Parse a toml document.
 */
toml_result_t toml_parse_file(FILE *fp) {
    toml_result_t result = {0};
    char *buf = 0;
    int top, max;  // index into buf[]
    top = max = 0;

    // Read file into memory
    while (!feof(fp)) {
        assert(top <= max);
        if (top == max) {
            // need to extend buf[]
            int tmpmax = (max * 1.5) + 1000;
            if (tmpmax < 0) {
                // the real max is INT_MAX - 1 to account for terminating NUL.
                if (max < INT_MAX - 1) {
                    tmpmax = INT_MAX - 1;
                } else {
                    snprintf(result.errmsg, sizeof(result.errmsg),
                        "file is bigger than %d bytes", INT_MAX - 1);
                    toml_option.mem_free(buf);
                    return result;
                }
            }
            // add an extra byte for terminating NUL
            char *tmp = toml_option.mem_realloc(buf, tmpmax + 1);
            if (!tmp) {
                snprintf(result.errmsg, sizeof(result.errmsg), "out of memory");
                toml_option.mem_free(buf);
                return result;
            }
            buf = tmp;
            max = tmpmax;
        }

        errno = 0;
        top += fread(buf + top, 1, max - top, fp);
        if (ferror(fp)) {
            snprintf(result.errmsg, sizeof(result.errmsg), "%s",
                errno ? strerror(errno) : "Error reading file");
            toml_option.mem_free(buf);
            return result;
        }
    }
    buf[top] = 0;  // NUL terminator

    result = toml_parse(buf, top);
    toml_option.mem_free(buf);
    return result;
}

/**
 *  Parse a toml document.
 */
toml_result_t toml_parse(const char *src, int len) {
    toml_result_t result = {0};
    parser_t parser = {0};
    parser_t *pp = &parser;

    // Check that src is NUL terminated.
    if (src[len]) {
        snprintf(result.errmsg, sizeof(result.errmsg),
            "src[] must be NUL terminated");
        goto bail;
    }

    // If user insists, check that src[] is a valid utf8 string.
    if (toml_option.check_utf8) {
        int line = 1;  // keeps track of line number
        for (int i = 0; i < len;) {
            uint32_t ch;
            int n = toml_utf8_to_ucs(src + i, len - i, &ch);
            if (n < 0) {
                snprintf(result.errmsg, sizeof(result.errmsg),
                    "invalid UTF8 char on line %d", line);
                goto bail;
            }
            if (0xD800 <= ch && ch <= 0xDFFF) {
                // explicitly prohibit surrogates (non-scalar unicode code point)
                snprintf(result.errmsg, sizeof(result.errmsg),
                    "invalid UTF8 char \\u%04x on line %d", ch, line);
                goto bail;
            }
            line += (ch == '\n' ? 1 : 0);
            i += n;
        }
    }

    // Initialize parser
    pp->toptab = toml_mkdatum(TOML_TABLE);
    pp->curtab = &pp->toptab;
    pp->ebuf.ptr = result.errmsg;
    pp->ebuf.len = sizeof(result.errmsg);

    // Alloc memory pool
    pp->pool =
        toml_pool_create(len + 10);  // add some extra bytes for NUL term and safety
    if (!pp->pool) {
        snprintf(result.errmsg, sizeof(result.errmsg), "out of memory");
        goto bail;
    }

    // Initialize scanner.
    toml_scan_init(&pp->scanner, src, len, pp->ebuf.ptr, pp->ebuf.len);

    // Keep parsing until FIN
    for (;;) {
        toml_token_t tok;
        if (toml_scan_key(&pp->scanner, &tok)) {
            goto bail;
        }
        // break on FIN
        if (tok.toktyp == TOK_FIN) {
            break;
        }
        switch (tok.toktyp) {
            case TOK_ENDL:  // skip blank lines
                continue;
            case TOK_LBRACK:
                if (toml_parse_std_table_expr(pp, tok)) {
                    goto bail;
                }
                break;
            case TOK_LLBRACK:
                if (toml_parse_array_table_expr(pp, tok)) {
                    goto bail;
                }
                break;
            default:
                // non-blank line: parse an expression
                if (toml_parse_keyvalue_expr(pp, tok)) {
                    goto bail;
                }
                break;
        }
        // each expression must be followed by newline
        if (toml_scan_key(&pp->scanner, &tok)) {
            goto bail;
        }
        if (tok.toktyp == TOK_FIN || tok.toktyp == TOK_ENDL) {
            continue;
        }
        toml_RETERROR(pp->ebuf, tok.lineno, "ENDL expected");
        goto bail;
    }

    // return result
    result.ok = true;
    result.toptab = pp->toptab;
    result.__internal = (void *)pp->pool;
    return result;

bail:
    // return error
    toml_datum_free(&pp->toptab);
    toml_pool_destroy(pp->pool);
    result.ok = false;
    assert(result.errmsg[0]);  // make sure there is an errmsg
    return result;
}

// Convert a (LITSTRING, LIT, MLLITSTRING, MLSTRING, or STRING) token to a
// datum.
static int toml_token_to_string(parser_t *pp, toml_token_t tok, toml_datum_t *ret) {
    *ret = toml_mkdatum(TOML_STRING);
    toml_span_t span;
    TOML_DO(toml_parse_norm(pp, tok, &span));
    ret->u.str.ptr = (char *)span.ptr;
    ret->u.str.len = span.len;
    return 0;
}

// Convert TIME token to a datum.
static int toml_token_to_time(parser_t *pp, toml_token_t tok, toml_datum_t *ret) {
    (void)pp;
    *ret = toml_mkdatum(TOML_TIME);
    ret->u.ts.hour = tok.u.tsval.hour;
    ret->u.ts.minute = tok.u.tsval.minute;
    ret->u.ts.second = tok.u.tsval.sec;
    ret->u.ts.usec = tok.u.tsval.usec;
    return 0;
}

// Convert a DATE token to a datum.
static int toml_token_to_date(parser_t *pp, toml_token_t tok, toml_datum_t *ret) {
    (void)pp;
    *ret = toml_mkdatum(TOML_DATE);
    ret->u.ts.year = tok.u.tsval.year;
    ret->u.ts.month = tok.u.tsval.month;
    ret->u.ts.day = tok.u.tsval.day;
    return 0;
}

// Convert a DATETIME token to a datum.
static int toml_token_to_datetime(parser_t *pp, toml_token_t tok, toml_datum_t *ret) {
    (void)pp;
    *ret = toml_mkdatum(TOML_DATETIME);
    ret->u.ts.year = tok.u.tsval.year;
    ret->u.ts.month = tok.u.tsval.month;
    ret->u.ts.day = tok.u.tsval.day;
    ret->u.ts.hour = tok.u.tsval.hour;
    ret->u.ts.minute = tok.u.tsval.minute;
    ret->u.ts.second = tok.u.tsval.sec;
    ret->u.ts.usec = tok.u.tsval.usec;
    return 0;
}

// Convert a DATETIMETZ token to a datum.
static int toml_token_to_datetimetz(parser_t *pp, toml_token_t tok, toml_datum_t *ret) {
    (void)pp;
    *ret = toml_mkdatum(TOML_DATETIMETZ);
    ret->u.ts.year = tok.u.tsval.year;
    ret->u.ts.month = tok.u.tsval.month;
    ret->u.ts.day = tok.u.tsval.day;
    ret->u.ts.hour = tok.u.tsval.hour;
    ret->u.ts.minute = tok.u.tsval.minute;
    ret->u.ts.second = tok.u.tsval.sec;
    ret->u.ts.usec = tok.u.tsval.usec;
    ret->u.ts.tz = tok.u.tsval.tz;
    return 0;
}

// Convert an int64 token to a datum.
static int toml_token_to_int64(parser_t *pp, toml_token_t tok, toml_datum_t *ret) {
    (void)pp;
    assert(tok.toktyp == TOK_INTEGER);
    *ret = toml_mkdatum(TOML_INT64);
    ret->u.int64 = tok.u.int64;
    return 0;
}

// Convert a fp64 token to a datum.
static int toml_token_to_fp64(parser_t *pp, toml_token_t tok, toml_datum_t *ret) {
    (void)pp;
    assert(tok.toktyp == TOK_FLOAT);
    *ret = toml_mkdatum(TOML_FP64);
    ret->u.fp64 = tok.u.fp64;
    return 0;
}

// Convert a boolean token to a datum.
static int toml_token_to_boolean(parser_t *pp, toml_token_t tok, toml_datum_t *ret) {
    (void)pp;
    assert(tok.toktyp == TOK_BOOL);
    *ret = toml_mkdatum(TOML_BOOLEAN);
    ret->u.boolean = tok.u.b1;
    return 0;
}

// Parse a multipart key. Return 0 on success, -1 otherwise.
static int toml_parse_key(parser_t *pp, toml_token_t tok, toml_keypart_t *ret_keypart) {
    ret_keypart->nspan = 0;
    // key = simple-key | dotted_key
    // simple-key = STRING | LITSTRING | LIT
    // dotted-key = simple-key (DOT simple-key)+
    if (tok.toktyp != TOK_STRING && tok.toktyp != TOK_LITSTRING &&
        tok.toktyp != TOK_LIT) {
        return toml_RETERROR(pp->ebuf, tok.lineno, "missing key");
    }

    int n = 0;
    toml_span_t *kpspan = ret_keypart->span;

    // Normalize the first keypart
    if (toml_parse_norm(pp, tok, &kpspan[n])) {
        return toml_RETERROR(pp->ebuf, tok.lineno,
            "unable to normalize string; probably a unicode issue");
    }
    n++;

    // Scan and normalize the second to last keypart
    while (1) {
        toml_scanner_state_t mark = toml_scan_mark(&pp->scanner);

        // Eat the dot if it is there
        TOML_DO(toml_scan_key(&pp->scanner, &tok));

        // If not a dot, we are done with keyparts.
        if (tok.toktyp != TOK_DOT) {
            toml_scan_restore(&pp->scanner, mark);
            break;
        }

        // Scan the n-th key
        TOML_DO(toml_scan_key(&pp->scanner, &tok));

        if (tok.toktyp != TOK_STRING && tok.toktyp != TOK_LITSTRING &&
            tok.toktyp != TOK_LIT) {
            return toml_RETERROR(pp->ebuf, tok.lineno, "expects a string in dotted-key");
        }

        if (n >= KEYPARTMAX) {
            return toml_RETERROR(pp->ebuf, tok.lineno, "too many key parts");
        }

        // Normalize the n-th key.
        TOML_DO(toml_parse_norm(pp, tok, &kpspan[n]));
        n++;
    }

    // This key has n parts.
    ret_keypart->nspan = n;
    return 0;
}

// Starting at toptab, descend following keypart[]. If a key does not
// exist in the current table, create a new table entry for the
// key. Returns the final table represented by the key.
static toml_datum_t *toml_descend_keypart(parser_t *pp, int lineno,
    toml_datum_t *toptab, toml_keypart_t *keypart,
    bool stdtabexpr) {
    toml_datum_t *tab = toptab;  // current tab

    for (int i = 0; i < keypart->nspan; i++) {
        const char *reason;
        // Find the i-th keypart
        int j = toml_tab_find(tab, keypart->span[i]);
        // Not found: add a new (key, tab) pair.
        if (j < 0) {
            toml_datum_t newtab = toml_mkdatum(TOML_TABLE);
            newtab.flag |= stdtabexpr ? FLAG_STDEXPR : 0;
            if (toml_tab_add(tab, keypart->span[i], newtab, &reason)) {
                toml_RETERROR(pp->ebuf, lineno, "%s", reason);
                return NULL;
            }
            tab = &tab->u.tab.value[tab->u.tab.size - 1];  // descend
            continue;
        }

        // Found: extract the value of the key.
        toml_datum_t *value = &tab->u.tab.value[j];

        // If the value is a table, descend.
        if (value->type == TOML_TABLE) {
            tab = value;  // descend
            continue;
        }

        // If the value is an array: locate the last entry and descend.
        if (value->type == TOML_ARRAY) {
            // If empty: error.
            if (value->u.arr.size <= 0) {
                toml_RETERROR(pp->ebuf, lineno, "array %s has no elements",
                    keypart->span[i].ptr);
                return NULL;
            }

            // Extract the last element of the array.
            value = &value->u.arr.elem[value->u.arr.size - 1];

            // It must be a table!
            if (value->type != TOML_TABLE) {
                toml_RETERROR(pp->ebuf, lineno, "array %s must be array of tables",
                    keypart->span[i].ptr);
                return NULL;
            }
            tab = value;  // descend
            continue;
        }

        // key not found
        toml_RETERROR(pp->ebuf, lineno, "cannot locate table at key %s",
            keypart->span[i].ptr);
        return NULL;
    }

    // Return the table corresponding to the keypart[].
    return tab;
}

// Recursively set flags on datum
static void toml_set_flag_recursive(toml_datum_t *datum, uint32_t flag) {
    datum->flag |= flag;
    switch (datum->type) {
        case TOML_ARRAY:
            for (int i = 0, top = datum->u.arr.size; i < top; i++) {
                toml_set_flag_recursive(&datum->u.arr.elem[i], flag);
            }
            break;
        case TOML_TABLE:
            for (int i = 0, top = datum->u.tab.size; i < top; i++) {
                toml_set_flag_recursive(&datum->u.tab.value[i], flag);
            }
            break;
        default:
            break;
    }
}

// Parse an inline array.
static int toml_parse_inline_array(parser_t *pp, toml_token_t tok,
    toml_datum_t *ret_datum) {
    assert(tok.toktyp == TOK_LBRACK);
    *ret_datum = toml_mkdatum(TOML_ARRAY);
    int need_comma = 0;

    // loop until RBRACK
    for (;;) {
        // skip ENDL
        do {
            TOML_DO(toml_scan_value(&pp->scanner, &tok));
        } while (tok.toktyp == TOK_ENDL);

        // If got an RBRACK: done!
        if (tok.toktyp == TOK_RBRACK) {
            break;
        }

        // If got a COMMA: check if it is expected.
        if (tok.toktyp == TOK_COMMA) {
            if (need_comma) {
                need_comma = 0;
                continue;
            }
            return toml_RETERROR(pp->ebuf, tok.lineno,
                "syntax error while parsing array: unexpected comma");
        }

        // Not a comma, but need a comma: error!
        if (need_comma) {
            return toml_RETERROR(pp->ebuf, tok.lineno,
                "syntax error while parsing array: missing comma");
        }

        // This is a valid value!

        // Add the value to the array.
        const char *reason;
        toml_datum_t *pelem = toml_arr_emplace(ret_datum, &reason);
        if (!pelem) {
            return toml_RETERROR(pp->ebuf, tok.lineno, "while parsing array: %s", reason);
        }

        // Parse the value and save into array.
        TOML_DO(toml_parse_val(pp, tok, pelem));

        // Need comma before the next value.
        need_comma = 1;
    }

    // Set the INLINE flag for all things in this array.
    toml_set_flag_recursive(ret_datum, FLAG_INLINED);
    return 0;
}

// Parse an inline table.
static int toml_parse_inline_table(parser_t *pp, toml_token_t tok,
    toml_datum_t *ret_datum) {
    assert(tok.toktyp == TOK_LBRACE);
    *ret_datum = toml_mkdatum(TOML_TABLE);
    bool need_comma = 0;
    bool was_comma = 0;

    // loop until RBRACE
    for (;;) {
        TOML_DO(toml_scan_key(&pp->scanner, &tok));

        // Got an RBRACE: done!
        if (tok.toktyp == TOK_RBRACE) {
            if (was_comma) {
                /*
                return RETERROR(pp->ebuf, tok.lineno,
                                "extra comma before closing brace");
                */
                // extra comma before RBRACE is allowed for v1.1
                (void)0;
            }
            break;
        }

        // Got a comma: check if it is expected.
        if (tok.toktyp == TOK_COMMA) {
            if (need_comma) {
                need_comma = 0, was_comma = 1;
                continue;
            }
            return toml_RETERROR(pp->ebuf, tok.lineno, "unexpected comma");
        }

        // Newline not allowed in inline table.
        // newline is allowed in v1.1
        if (tok.toktyp == TOK_ENDL) {
            // return RETERROR(pp->ebuf, tok.lineno, "unexpected newline");
            continue;
        }

        // Not a comma, but need a comma: error!
        if (need_comma) {
            return toml_RETERROR(pp->ebuf, tok.lineno, "missing comma");
        }

        // Get the keyparts
        toml_keypart_t keypart = {0};
        int keylineno = tok.lineno;
        TOML_DO(toml_parse_key(pp, tok, &keypart));

        // Descend to one keypart before last
        toml_span_t lastkeypart = keypart.span[--keypart.nspan];
        toml_datum_t *tab =
            toml_descend_keypart(pp, keylineno, ret_datum, &keypart, false);
        if (!tab) {
            return -1;
        }

        // If tab is a previously declared inline table: error.
        if (tab->flag & FLAG_INLINED) {
            return toml_RETERROR(pp->ebuf, tok.lineno, "inline table cannot be extended");
        }

        // We are explicitly defining it now.
        tab->flag |= FLAG_EXPLICIT;

        // match EQUAL
        TOML_DO(toml_scan_value(&pp->scanner, &tok));

        if (tok.toktyp != TOK_EQUAL) {
            if (tok.toktyp == TOK_ENDL) {
                return toml_RETERROR(pp->ebuf, tok.lineno, "unexpected newline");
            } else {
                return toml_RETERROR(pp->ebuf, tok.lineno, "missing '='");
            }
        }

        // obtain the value
        toml_datum_t value;
        TOML_DO(toml_scan_value(&pp->scanner, &tok));
        TOML_DO(toml_parse_val(pp, tok, &value));

        // Add the value to tab.
        const char *reason;
        if (toml_tab_add(tab, lastkeypart, value, &reason)) {
            return toml_RETERROR(pp->ebuf, tok.lineno, "%s", reason);
        }
        need_comma = 1, was_comma = 0;
    }

    toml_set_flag_recursive(ret_datum, FLAG_INLINED);
    return 0;
}

// Parse a value.
static int toml_parse_val(parser_t *pp, toml_token_t tok, toml_datum_t *ret) {
    // val = string / boolean / array / inline-table / date-time / float / integer
    switch (tok.toktyp) {
        case TOK_STRING:
        case TOK_MLSTRING:
        case TOK_LITSTRING:
        case TOK_MLLITSTRING:
            return toml_token_to_string(pp, tok, ret);
        case TOK_TIME:
            return toml_token_to_time(pp, tok, ret);
        case TOK_DATE:
            return toml_token_to_date(pp, tok, ret);
        case TOK_DATETIME:
            return toml_token_to_datetime(pp, tok, ret);
        case TOK_DATETIMETZ:
            return toml_token_to_datetimetz(pp, tok, ret);
        case TOK_INTEGER:
            return toml_token_to_int64(pp, tok, ret);
        case TOK_FLOAT:
            return toml_token_to_fp64(pp, tok, ret);
        case TOK_BOOL:
            return toml_token_to_boolean(pp, tok, ret);
        case TOK_LBRACK:  // inline-array
            return toml_parse_inline_array(pp, tok, ret);
        case TOK_LBRACE:  // inline-table
            return toml_parse_inline_table(pp, tok, ret);
        default:
            break;
    }
    return toml_RETERROR(pp->ebuf, tok.lineno, "missing value");
}

// Parse a standard table expression, and set the curtab of the parser
// to the table referenced.  A standard table expression is a line
// like [a.b.c.d].
static int toml_parse_std_table_expr(parser_t *pp, toml_token_t tok) {
    // std-table = [ key ]
    // Eat the [
    assert(tok.toktyp == TOK_LBRACK);  // [ ate by caller

    // Read the first keypart
    TOML_DO(toml_scan_key(&pp->scanner, &tok));

    // Extract the keypart[]
    int keylineno = tok.lineno;
    toml_keypart_t keypart;
    TOML_DO(toml_parse_key(pp, tok, &keypart));

    // Eat the ]
    TOML_DO(toml_scan_key(&pp->scanner, &tok));
    if (tok.toktyp != TOK_RBRACK) {
        return toml_RETERROR(pp->ebuf, tok.lineno, "missing right-bracket");
    }

    // Descend to one keypart before last.
    toml_span_t lastkeypart = keypart.span[--keypart.nspan];

    // Descend keypart from the toptab.
    toml_datum_t *tab =
        toml_descend_keypart(pp, keylineno, &pp->toptab, &keypart, true);
    if (!tab) {
        return -1;
    }

    // Look for the last keypart in the final tab
    int j = toml_tab_find(tab, lastkeypart);
    if (j < 0) {
        // If not found: add it.
        if (tab->flag & FLAG_INLINED) {
            return toml_RETERROR(pp->ebuf, keylineno, "inline table cannot be extended");
        }
        const char *reason;
        toml_datum_t newtab = toml_mkdatum(TOML_TABLE);
        newtab.flag |= FLAG_STDEXPR;
        if (toml_tab_add(tab, lastkeypart, newtab, &reason)) {
            return toml_RETERROR(pp->ebuf, keylineno, "%s", reason);
        }
        // this is the new tab
        tab = &tab->u.tab.value[tab->u.tab.size - 1];
    } else {
        // Found: check for errors
        tab = &tab->u.tab.value[j];
        if (tab->flag & FLAG_EXPLICIT) {
            /*
              This is not OK:
              [x.y.z]
              [x.y.z]

              but this is OK:
              [x.y.z]
              [x]
            */
            return toml_RETERROR(pp->ebuf, keylineno, "table defined more than once");
        }
        if (!(tab->flag & FLAG_STDEXPR)) {
            /*
            [t1]			# OK
            t2.t3.v = 0		# OK
            [t1.t2]   		# should FAIL  - t2 was non-explicit but was not
            created by std-table-expr
            */
            return toml_RETERROR(pp->ebuf, keylineno, "table defined before");
        }
    }

    // Set explicit flag on tab
    tab->flag |= FLAG_EXPLICIT;

    // Set tab as curtab of the parser
    pp->curtab = tab;
    return 0;
}

// Parse an array table expression, and set the curtab of the parser
// to the table referenced. A standard array table expresison is a line
// like [[a.b.c.d]].
static int toml_parse_array_table_expr(parser_t *pp, toml_token_t tok) {
    // array-table = [[ key ]]
    assert(tok.toktyp == TOK_LLBRACK);  // [[ ate by caller

    // Read the first keypart
    TOML_DO(toml_scan_key(&pp->scanner, &tok));

    int keylineno = tok.lineno;
    toml_keypart_t keypart;
    TOML_DO(toml_parse_key(pp, tok, &keypart));

    // eat the ]]
    toml_token_t rrb;
    TOML_DO(toml_scan_key(&pp->scanner, &rrb));
    if (rrb.toktyp != TOK_RRBRACK) {
        return toml_RETERROR(pp->ebuf, rrb.lineno, "missing ']]'");
    }

    // remove the last keypart from keypart[]
    toml_span_t lastkeypart = keypart.span[--keypart.nspan];

    // descend the key from the toptab
    toml_datum_t *tab = &pp->toptab;
    for (int i = 0; i < keypart.nspan; i++) {
        toml_span_t curkey = keypart.span[i];
        int j = toml_tab_find(tab, curkey);
        if (j < 0) {
            // If not found: add a new (key,tab) pair
            const char *reason;
            toml_datum_t newtab = toml_mkdatum(TOML_TABLE);
            newtab.flag |= FLAG_STDEXPR;
            if (toml_tab_add(tab, curkey, newtab, &reason)) {
                return toml_RETERROR(pp->ebuf, keylineno, "%s", reason);
            }
            tab = &tab->u.tab.value[tab->u.tab.size - 1];
            continue;
        }

        // Found: get the value
        toml_datum_t *value = &tab->u.tab.value[j];

        // If value is table, then point to that table and continue descent.
        if (value->type == TOML_TABLE) {
            tab = value;
            continue;
        }

        // If value is an array of table, point to the last element of the array and
        // continue descent.
        if (value->type == TOML_ARRAY) {
            if (value->flag & FLAG_INLINED) {
                return toml_RETERROR(pp->ebuf, keylineno, "cannot expand array %s",
                    curkey.ptr);
            }
            if (value->u.arr.size <= 0) {
                return toml_RETERROR(pp->ebuf, keylineno, "array %s has no elements",
                    curkey.ptr);
            }
            value = &value->u.arr.elem[value->u.arr.size - 1];
            if (value->type != TOML_TABLE) {
                return toml_RETERROR(pp->ebuf, keylineno, "array %s must be array of tables",
                    curkey.ptr);
            }
            tab = value;
            continue;
        }

        // keypart not found
        return toml_RETERROR(pp->ebuf, keylineno, "cannot locate table at key %s",
            curkey.ptr);
    }

    // For the final keypart, make sure entry at key is an array of tables
    const char *reason;
    int idx = toml_tab_find(tab, lastkeypart);
    if (idx == -1) {
        // If not found, add an array of table.
        if (toml_tab_add(tab, lastkeypart, toml_mkdatum(TOML_ARRAY), &reason)) {
            return toml_RETERROR(pp->ebuf, keylineno, "%s", reason);
        }
        idx = toml_tab_find(tab, lastkeypart);
        assert(idx >= 0);
    }
    // Check that this is an array.
    if (tab->u.tab.value[idx].type != TOML_ARRAY) {
        return toml_RETERROR(pp->ebuf, keylineno, "entry must be an array");
    }
    // Add an empty table to the array
    toml_datum_t *arr = &tab->u.tab.value[idx];
    if (arr->flag & FLAG_INLINED) {
        return toml_RETERROR(pp->ebuf, keylineno, "cannot extend a static array");
    }
    toml_datum_t *pelem = toml_arr_emplace(arr, &reason);
    if (!pelem) {
        return toml_RETERROR(pp->ebuf, keylineno, "%s", reason);
    }
    *pelem = toml_mkdatum(TOML_TABLE);

    // Set the last element of this array as curtab of the parser
    pp->curtab = &arr->u.arr.elem[arr->u.arr.size - 1];
    assert(pp->curtab->type == TOML_TABLE);

    return 0;
}

// Parse an expression. A toml doc is just a list of expressions.
static int toml_parse_keyvalue_expr(parser_t *pp, toml_token_t tok) {
    // Obtain the key
    int keylineno = tok.lineno;
    toml_keypart_t keypart;
    TOML_DO(toml_parse_key(pp, tok, &keypart));

    // match the '='
    TOML_DO(toml_scan_key(&pp->scanner, &tok));
    if (tok.toktyp != TOK_EQUAL) {
        return toml_RETERROR(pp->ebuf, tok.lineno, "expect '='");
    }

    // Obtain the value
    toml_datum_t val;
    TOML_DO(toml_scan_value(&pp->scanner, &tok));
    TOML_DO(toml_parse_val(pp, tok, &val));

    // Locate the last table using keypart[]
    const char *reason;
    toml_datum_t *tab = pp->curtab;
    for (int i = 0; i < keypart.nspan - 1; i++) {
        int j = toml_tab_find(tab, keypart.span[i]);
        if (j < 0) {
            if (i > 0 && (tab->flag & FLAG_EXPLICIT)) {
                return toml_RETERROR(
                    pp->ebuf, keylineno,
                    "cannot extend a previously defined table using dotted expression");
            }
            toml_datum_t newtab = toml_mkdatum(TOML_TABLE);
            if (toml_tab_add(tab, keypart.span[i], newtab, &reason)) {
                return toml_RETERROR(pp->ebuf, keylineno, "%s", reason);
            }
            tab = &tab->u.tab.value[tab->u.tab.size - 1];
            continue;
        }
        toml_datum_t *value = &tab->u.tab.value[j];
        if (value->type == TOML_TABLE) {
            tab = value;
            continue;
        }
        if (value->type == TOML_ARRAY) {
            return toml_RETERROR(pp->ebuf, keylineno,
                "encountered previously declared array '%s'",
                keypart.span[i].ptr);
        }
        return toml_RETERROR(pp->ebuf, keylineno, "cannot locate table at '%s'",
            keypart.span[i].ptr);
    }

    // Check for disallowed situations.
    if (tab->flag & FLAG_INLINED) {
        return toml_RETERROR(pp->ebuf, keylineno, "inline table cannot be extended");
    }
    if (keypart.nspan > 1 && (tab->flag & FLAG_EXPLICIT)) {
        return toml_RETERROR(
            pp->ebuf, keylineno,
            "cannot extend a previously defined table using dotted expression");
    }

    // Add a new key/value for tab.
    if (toml_tab_add(tab, keypart.span[keypart.nspan - 1], val, &reason)) {
        return toml_RETERROR(pp->ebuf, keylineno, "%s", reason);
    }

    return 0;
}

// Normalize a LIT/STRING/MLSTRING/LITSTRING/MLLITSTRING
// -> unescape all escaped chars
// The returned string is allocated out of pp->sbuf[]
static int toml_parse_norm(parser_t *pp, toml_token_t tok, toml_span_t *ret_span) {
    // Allocate a buffer to store the normalized string. Add one
    // extra-byte for terminating NUL.
    char *p = toml_pool_alloc(pp->pool, tok.str.len + 1);
    if (!p) {
        return toml_RETERROR(pp->ebuf, tok.lineno, "out of memory");
    }

    // Copy from token string into buffer
    memcpy(p, tok.str.ptr, tok.str.len);
    p[tok.str.len] = 0;  // additional NUL term for safety

    ret_span->ptr = p;
    ret_span->len = tok.str.len;

    switch (tok.toktyp) {
        case TOK_LIT:
        case TOK_LITSTRING:
        case TOK_MLLITSTRING:
            // no need to handle escape chars
            return 0;

        case TOK_STRING:
        case TOK_MLSTRING:
            // need to handle escape chars
            break;

        default:
            return toml_RETERROR(pp->ebuf, 0, "internal: arg must be a string");
    }

    // if there is no escape char, then done!
    p = memchr(ret_span->ptr, '\\', ret_span->len);
    if (!p) {
        return 0;  // success
    }

    // Normalize the escaped chars
    char *dst = p;
    while (*p) {
        if (*p != '\\') {
            *dst++ = *p++;
            continue;
        }
        switch (p[1]) {
            case '"':
            case '\\':
                *dst++ = p[1];
                p += 2;
                continue;
            case 'b':
                *dst++ = '\b';
                p += 2;
                continue;
            case 't':
                *dst++ = '\t';
                p += 2;
                continue;
            case 'n':
                *dst++ = '\n';
                p += 2;
                continue;
            case 'f':
                *dst++ = '\f';
                p += 2;
                continue;
            case 'r':
                *dst++ = '\r';
                p += 2;
                continue;
            case 'e':
                *dst++ = '\e';
                p += 2;
                continue;
            case 'x': {
                char buf[3];
                memcpy(buf, p + 2, 2);
                buf[2] = 0;
                int32_t ucs = strtol(buf, 0, 16);
                int n = toml_ucs_to_utf8(ucs, dst);
                if (n < 0) {
                    return toml_RETERROR(pp->ebuf, tok.lineno, "error converting UCS %s to UTF8",
                        buf);
                }
                dst += n;
                p += 4;
                continue;
            }
            case 'u':
            case 'U': {
                char buf[9];
                int sz = (p[1] == 'u' ? 4 : 8);
                memcpy(buf, p + 2, sz);
                buf[sz] = 0;
                int32_t ucs = strtol(buf, 0, 16);
                if (0xD800 <= ucs && ucs <= 0xDFFF) {
                    // explicitly prohibit surrogates (non-scalar unicode code point)
                    return toml_RETERROR(pp->ebuf, tok.lineno, "invalid UTF8 char \\u%04x", ucs);
                }
                int n = toml_ucs_to_utf8(ucs, dst);
                if (n < 0) {
                    return toml_RETERROR(pp->ebuf, tok.lineno, "error converting UCS %s to UTF8",
                        buf);
                }
                dst += n;
                p += 2 + sz;
                continue;
            }

            case ' ':
            case '\t':
            case '\r':
                // line-ending backslash
                // --- allow for extra whitespace chars after backslash
                // --- skip until newline
                p++;
                p += strspn(p, " \t\r");
                if (*p != '\n') {
                    return toml_RETERROR(pp->ebuf, tok.lineno, "internal error");
                }
                // fallthru
            case '\n':
                // skip all whitespaces including newline
                p++;
                p += strspn(p, " \t\r\n");
                continue;
            default:
                *dst++ = *p++;
                continue;
        }
    }
    *dst = 0;
    ret_span->len = dst - ret_span->ptr;
    return 0;
}

// -------------- scanner functions

// Get the next char
static int scan_get(toml_scanner_t *sp) {
    int ret = TOK_FIN;
    const char *p = sp->cur;
    if (p < sp->endp) {
        ret = *p++;
        if (ret == '\r' && p < sp->endp && *p == '\n') {
            ret = *p++;
        }
    }
    sp->cur = p;
    sp->lineno += (ret == '\n' ? 1 : 0);
    return ret;
}

// Check if the next char matches ch.
static inline bool scan_match(toml_scanner_t *sp, int ch) {
    const char *p = sp->cur;
    if (p < sp->endp && *p == ch) {
        return true;
    }
    if (ch == '\n' && p + 1 < sp->endp) {
        return p[0] == '\r' && p[1] == '\n';
    }
    return false;
}

// Check if the next char is in accept[].
static bool scan_matchany(toml_scanner_t *sp, const char *accept) {
    for (; *accept; accept++) {
        if (scan_match(sp, *accept)) {
            return true;
        }
    }
    return false;
}

// Check if the next n chars match ch.
static inline bool scan_nmatch(toml_scanner_t *sp, int ch, int n) {
    assert(ch != '\n');  // not handled
    if (sp->cur + n > sp->endp) {
        return false;
    }
    const char *p = sp->cur;
    int i;
    for (i = 0; i < n && p[i] == ch; i++);
    return i == n;
}

// Initialize a token.
static inline toml_token_t mktoken(toml_scanner_t *sp, toml_toktyp_t typ) {
    toml_token_t tok = {0};
    tok.toktyp = typ;
    tok.str.ptr = sp->cur;
    tok.lineno = sp->lineno;
    return tok;
}

#define S_GET() scan_get(sp)
#define S_MATCH(ch) scan_match(sp, (ch))
#define S_MATCH3(ch) scan_nmatch(sp, (ch), 3)
#define S_MATCH4(ch) scan_nmatch(sp, (ch), 4)
#define S_MATCH6(ch) scan_nmatch(sp, (ch), 6)

static inline bool toml_is_valid_char(int ch) {
    // i.e. (0x20 <= ch && ch <= 0x7e) || (ch & 0x80);
    return isprint(ch) || (ch & 0x80);
}

static inline bool toml_is_hex_char(int ch) {
    ch = toupper(ch);
    return ('0' <= ch && ch <= '9') || ('A' <= ch && ch <= 'F');
}

// Initialize a scanner
static void toml_scan_init(toml_scanner_t *sp, const char *src, int len, char *errbuf,
    int errbufsz) {
    memset(sp, 0, sizeof(*sp));
    sp->src = src;
    sp->endp = src + len;
    assert(*sp->endp == '\0');
    sp->cur = src;
    sp->lineno = 1;
    sp->ebuf.ptr = errbuf;
    sp->ebuf.len = errbufsz;
}

static int toml_scan_multiline_string(toml_scanner_t *sp, toml_token_t *tok) {
    assert(S_MATCH3('"'));
    S_GET(), S_GET(), S_GET();  // skip opening """

    // According to spec: trim first newline after """
    if (S_MATCH('\n')) {
        S_GET();
    }

    *tok = mktoken(sp, TOK_MLSTRING);
    // scan until terminating """
    while (1) {
        if (S_MATCH3('"')) {
            if (S_MATCH4('"')) {
                // special case... """abcd """" -> (abcd ")
                // but sequences of 3 or more double quotes are not allowed
                if (S_MATCH6('"')) {
                    return toml_RETERROR(sp->ebuf, sp->lineno,
                        "detected sequences of 3 or more double quotes");
                } else {
                    ;  // no problem
                }
            } else {
                break;  // found terminating """
            }
        }
        int ch = S_GET();
        if (ch == TOK_FIN) {
            return toml_RETERROR(sp->ebuf, sp->lineno, "unterminated \"\"\"");
        }
        // If non-escaped char ...
        if (ch != '\\') {
            if (!(toml_is_valid_char(ch) || (ch && strchr(" \t\n", ch)))) {
                return toml_RETERROR(sp->ebuf, sp->lineno, "invalid char in string");
            }
            continue;
        }
        // ch is backslash; handle escape char
        ch = S_GET();
        if (ch && strchr("btnfre\"\\", ch)) {
            // skip \", \\, \b, \f, \n, \r, \t
            continue;
        }
        int top = 0;
        switch (ch) {
            case 'x':
                top = 2;
                break;
            case 'u':
                top = 4;
                break;
            case 'U':
                top = 8;
                break;
            default:
                break;
        }
        if (top) {
            for (int i = 0; i < top; i++) {
                if (!toml_is_hex_char(S_GET())) {
                    return toml_RETERROR(sp->ebuf, sp->lineno,
                        "expect %d hex digits after \\%c", top, ch);
                }
            }
            continue;
        }
        // handle line-ending backslash
        if (ch == ' ' || ch == '\t') {
            // Although the spec does not allow for whitespace following a
            // line-ending backslash, some standard tests expect it.
            // Skip whitespace till EOL.
            while (ch != TOK_FIN && ch && strchr(" \t", ch)) {
                ch = S_GET();
            }
            if (ch != '\n') {
                // Got a backslash followed by whitespace, followed by some char
                // before newline
                return toml_RETERROR(sp->ebuf, sp->lineno, "bad escape char in string");
            }
            // fallthru
        }
        if (ch == '\n') {
            // got a line-ending backslash
            // - skip all whitespaces
            while (scan_matchany(sp, " \t\n")) {
                S_GET();
            }
            continue;
        }
        return toml_RETERROR(sp->ebuf, sp->lineno, "bad escape char in string");
    }
    tok->str.len = sp->cur - tok->str.ptr;

    assert(S_MATCH3('"'));
    S_GET(), S_GET(), S_GET();
    return 0;
}

static int toml_scan_string(toml_scanner_t *sp, toml_token_t *tok) {
    assert(S_MATCH('"'));
    if (S_MATCH3('"')) {
        return toml_scan_multiline_string(sp, tok);
    }
    S_GET();  // skip opening "

    // scan until closing "
    *tok = mktoken(sp, TOK_STRING);
    while (!S_MATCH('"')) {
        int ch = S_GET();
        if (ch == TOK_FIN) {
            return toml_RETERROR(sp->ebuf, sp->lineno, "unterminated string");
        }
        // If non-escaped char ...
        if (ch != '\\') {
            if (!(toml_is_valid_char(ch) || ch == ' ' || ch == '\t')) {
                return toml_RETERROR(sp->ebuf, sp->lineno, "invalid char in string");
            }
            continue;
        }
        // ch is backslash; handle escape char
        ch = S_GET();
        if (ch && strchr("btnfre\"\\", ch)) {
            // skip \b, \t, \n, \f, \r, \e, \", \\  .
            continue;
        }
        int top = 0;
        switch (ch) {
            case 'x':
                top = 2;
                break;
            case 'u':
                top = 4;
                break;
            case 'U':
                top = 8;
                break;
            default:
                return toml_RETERROR(sp->ebuf, sp->lineno, "bad escape char in string");
        }
        for (int i = 0; i < top; i++) {
            if (!toml_is_hex_char(S_GET())) {
                return toml_RETERROR(sp->ebuf, sp->lineno, "expect %d hex digits after \\%c",
                    top, ch);
            }
        }
    }
    tok->str.len = sp->cur - tok->str.ptr;

    assert(S_MATCH('"'));
    S_GET();  // skip the terminating "
    return 0;
}

static int toml_scan_multiline_litstring(toml_scanner_t *sp, toml_token_t *tok) {
    assert(S_MATCH3('\''));
    S_GET(), S_GET(), S_GET();  // skip opening '''

    // According to spec: trim first newline after '''
    if (S_MATCH('\n')) {
        S_GET();
    }

    // scan until terminating '''
    *tok = mktoken(sp, TOK_MLLITSTRING);
    while (1) {
        if (S_MATCH3('\'')) {
            if (S_MATCH4('\'')) {
                // special case... '''abcd '''' -> (abcd ')
                // but sequences of 3 or more single quotes are not allowed
                if (S_MATCH6('\'')) {
                    return toml_RETERROR(sp->ebuf, sp->lineno,
                        "sequences of 3 or more single quotes");
                } else {
                    ;  // no problem
                }
            } else {
                break;  // found terminating '''
            }
        }
        int ch = S_GET();
        if (ch == TOK_FIN) {
            return toml_RETERROR(sp->ebuf, sp->lineno,
                "unterminated multiline lit string");
        }
        if (!(toml_is_valid_char(ch) || (ch && strchr(" \t\n", ch)))) {
            return toml_RETERROR(sp->ebuf, sp->lineno, "invalid char in string");
        }
    }
    tok->str.len = sp->cur - tok->str.ptr;

    assert(S_MATCH3('\''));
    S_GET(), S_GET(), S_GET();
    return 0;
}

static int toml_scan_litstring(toml_scanner_t *sp, toml_token_t *tok) {
    assert(S_MATCH('\''));
    if (S_MATCH3('\'')) {
        return toml_scan_multiline_litstring(sp, tok);
    }
    S_GET();  // skip opening '

    // scan until closing '
    *tok = mktoken(sp, TOK_LITSTRING);
    while (!S_MATCH('\'')) {
        int ch = S_GET();
        if (ch == TOK_FIN) {
            return toml_RETERROR(sp->ebuf, sp->lineno, "unterminated string");
        }
        if (!(toml_is_valid_char(ch) || ch == '\t')) {
            return toml_RETERROR(sp->ebuf, sp->lineno, "invalid char in string");
        }
    }
    tok->str.len = sp->cur - tok->str.ptr;
    assert(S_MATCH('\''));
    S_GET();
    return 0;
}

static bool toml_is_valid_date(int year, int month, int day) {
    if (!(1 <= year)) {
        return false;
    }
    if (!(1 <= month && month <= 12)) {
        return false;
    }
    int is_leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    int days_in_month[] = {
        31, 28 + is_leap_year, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (1 <= day && day <= days_in_month[month - 1]);
}

static bool toml_is_valid_time(int hour, int minute, int sec, int usec) {
    if (!(0 <= hour && hour <= 23)) {
        return false;
    }
    if (!(0 <= minute && minute <= 59)) {
        return false;
    }
    if (!(0 <= sec && sec <= 59)) {
        return false;
    }
    if (!(0 <= usec)) {
        return false;
    }
    return true;
}

static bool toml_is_valid_timezone(int minute) {
    minute = (minute < 0 ? -minute : minute);
    int hour = minute / 60;
    minute = minute % 60;
    if (!(0 <= hour && hour <= 23)) {
        return false;
    }
    if (!(0 <= minute && minute < 60)) {
        return false;
    }
    return true;
}

// Read an int (without signs) from the string p.
static int toml_read_int(const char *p, int *ret) {
    const char *pp = p;
    int val = 0;
    for (; isdigit(*p); p++) {
        val = val * 10 + (*p - '0');
        if (val < 0) {
            return 0;  // overflowed
        }
    }
    *ret = val;
    return p - pp;
}

// Read a date as YYYY-MM-DD from p[]. Return #bytes consumed.
static int toml_read_date(const char *p, int *year, int *month, int *day) {
    const char *pp = p;
    int n;
    n = toml_read_int(p, year);
    if (n != 4 || p[4] != '-') {
        return 0;
    }
    n = toml_read_int(p += n + 1, month);
    if (n != 2 || p[2] != '-') {
        return 0;
    }
    n = toml_read_int(p += n + 1, day);
    if (n != 2) {
        return 0;
    }
    p += 2;
    assert(p - pp == 10);
    return p - pp;
}

// Read a time as HH:MM:SS.subsec from p[]. Return #bytes consumed.
static int toml_read_time(const char *p, int *hour, int *minute, int *second,
    int *usec) {
    const char *pp = p;
    int n;
    *hour = *minute = *second = *usec = 0;
    // scan hours
    n = toml_read_int(p, hour);
    if (n != 2 || p[2] != ':') {
        return 0;
    }
    p += 3;

    // scan minutes
    n = toml_read_int(p, minute);
    if (n != 2) {
        return 0;
    }
    if (p[2] != ':') {
        // seconds are optional in v1.1
        p += 2;
        return p - pp;
    }
    p += 3;

    // scan seconds
    n = toml_read_int(p, second);
    if (n != 2) {
        return 0;
    }
    p += 2;

    if (*p != '.') {
        return p - pp;
    }
    p++;  // skip the period
    if (!isdigit(*p)) {
        // trailing period
        return 0;
    }
    int micro_factor = 100000;
    while (isdigit(*p) && micro_factor) {
        *usec += (*p - '0') * micro_factor;
        micro_factor /= 10;
        p++;
    }
    return p - pp;
}

// Reads a timezone from p[]. Return #bytes consumed.
static int toml_read_tzone(const char *p, char *tzsign, int *tzhour, int *tzminute) {
    const char *pp = p;
    *tzhour = *tzminute = 0;
    *tzsign = '+';
    // look for Zulu
    if (*p == 'Z' || *p == 'z') {
        return 1;
    }

    *tzsign = *p++;
    if (!(*tzsign == '+' || *tzsign == '-')) {
        return 0;
    }

    // look for HH:MM
    int n;
    n = toml_read_int(p, tzhour);
    if (n != 2 || p[2] != ':') {
        return 0;
    }
    n = toml_read_int(p += 3, tzminute);
    if (n != 2) {
        return 0;
    }
    p += 2;
    return p - pp;
}

static int toml_scan_time(toml_scanner_t *sp, toml_token_t *tok) {
    int lineno = sp->lineno;
    char buffer[20];
    int len = sp->endp - sp->cur;
    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    memcpy(buffer, sp->cur, len);
    buffer[len] = 0;  // NUL

    char *p = buffer;
    int hour, minute, sec, usec;
    len = toml_read_time(p, &hour, &minute, &sec, &usec);
    if (len == 0) {
        return toml_RETERROR(sp->ebuf, lineno, "invalid time");
    }
    if (!toml_is_valid_time(hour, minute, sec, usec)) {
        return toml_RETERROR(sp->ebuf, lineno, "invalid time");
    }

    *tok = mktoken(sp, TOK_TIME);
    tok->str.len = len;
    sp->cur += len;
    tok->u.tsval.year = -1;
    tok->u.tsval.month = -1;
    tok->u.tsval.day = -1;
    tok->u.tsval.hour = hour;
    tok->u.tsval.minute = minute;
    tok->u.tsval.sec = sec;
    tok->u.tsval.usec = usec;
    tok->u.tsval.tz = -1;
    return 0;
}

static int toml_scan_timestamp(toml_scanner_t *sp, toml_token_t *tok) {
    int year, month, day, hour, minute, sec, usec, tz;
    int n;
    // make a copy of sp->cur into buffer to ensure NUL terminated string
    char buffer[80];
    int len = sp->endp - sp->cur;
    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    memcpy(buffer, sp->cur, len);
    buffer[len] = 0;  // NUL

    toml_toktyp_t toktyp = TOK_FIN;
    int lineno = sp->lineno;
    const char *p = buffer;
    if (isdigit(p[0]) && isdigit(p[1]) && p[2] == ':') {
        year = month = day = hour = minute = sec = usec = tz = -1;
        n = toml_read_time(buffer, &hour, &minute, &sec, &usec);
        if (!n) {
            return toml_RETERROR(sp->ebuf, lineno, "invalid time");
        }
        toktyp = TOK_TIME;
        p += n;
        goto done;
    }

    year = month = day = hour = minute = sec = usec = tz = -1;
    n = toml_read_date(p, &year, &month, &day);
    if (!n) {
        return toml_RETERROR(sp->ebuf, lineno, "invalid date");
    }
    toktyp = TOK_DATE;
    p += n;
    if (!((p[0] == 'T' || p[0] == ' ' || p[0] == 't') && isdigit(p[1]) &&
            isdigit(p[2]) && p[3] == ':')) {
        goto done;  // date only
    }

    n = toml_read_time(p += 1, &hour, &minute, &sec, &usec);
    if (!n) {
        return toml_RETERROR(sp->ebuf, lineno, "invalid timestamp");
    }
    toktyp = TOK_DATETIME;
    p += n;
    char tzsign;
    int tzhour, tzminute;
    n = toml_read_tzone(p, &tzsign, &tzhour, &tzminute);
    if (n == 0) {
        goto done;  // datetime only
    }
    toktyp = TOK_DATETIMETZ;
    p += n;
    if (!(0 <= tzminute && tzminute < 60)) {
        return toml_RETERROR(sp->ebuf, lineno, "invalid timezone");
    }
    tz = (tzhour * 60 + tzminute) * (tzsign == '-' ? -1 : 1);
    goto done;  // datetimetz

done:
    *tok = mktoken(sp, toktyp);
    n = p - buffer;
    tok->str.len = n;
    sp->cur += n;

    tok->u.tsval.year = year;
    tok->u.tsval.month = month;
    tok->u.tsval.day = day;
    tok->u.tsval.hour = hour;
    tok->u.tsval.minute = minute;
    tok->u.tsval.sec = sec;
    tok->u.tsval.usec = usec;
    tok->u.tsval.tz = tz;

    switch (tok->toktyp) {
        case TOK_TIME:
            if (!toml_is_valid_time(hour, minute, sec, usec)) {
                return toml_RETERROR(sp->ebuf, lineno, "invalid time");
            }
            break;
        case TOK_DATE:
            if (!toml_is_valid_date(year, month, day)) {
                return toml_RETERROR(sp->ebuf, lineno, "invalid date");
            }
            break;
        case TOK_DATETIME:
        case TOK_DATETIMETZ:
            if (!toml_is_valid_date(year, month, day)) {
                return toml_RETERROR(sp->ebuf, lineno, "invalid date");
            }
            if (!toml_is_valid_time(hour, minute, sec, usec)) {
                return toml_RETERROR(sp->ebuf, lineno, "invalid time");
            }
            if (tok->toktyp == TOK_DATETIMETZ && !toml_is_valid_timezone(tz)) {
                return toml_RETERROR(sp->ebuf, lineno, "invalid timezone");
            }
            break;
        default:
            assert(0);
            return toml_RETERROR(sp->ebuf, lineno, "internal error");
    }

    return 0;
}

static int toml_process_numstr(char *buffer, int base, const char **reason) {
    // squeeze out _
    char *q = strchr(buffer, '_');
    if (q) {
        for (int i = q - buffer; buffer[i]; i++) {
            if (buffer[i] != '_') {
                *q++ = buffer[i];
                continue;
            }
            int left = (i == 0) ? 0 : buffer[i - 1];
            int right = buffer[i + 1];
            if (!isdigit(left) && !(base == 16 && toml_is_hex_char(left))) {
                *reason = "underscore only allowed between digits";
                return -1;
            }
            if (!isdigit(right) && !(base == 16 && toml_is_hex_char(right))) {
                *reason = "underscore only allowed between digits";
                return -1;
            }
        }
        *q = 0;
    }

    // decimal points must be surrounded by digits. Also, convert to lowercase.
    for (int i = 0; buffer[i]; i++) {
        if (buffer[i] == '.') {
            if (i == 0 || !isdigit(buffer[i - 1]) || !isdigit(buffer[i + 1])) {
                *reason = "decimal point must be surrounded by digits";
                return -1;
            }
        } else if ('A' <= buffer[i] && buffer[i] <= 'Z') {
            buffer[i] = tolower(buffer[i]);
        }
    }

    if (base == 10) {
        // check for leading 0:  '+01' is an error!
        q = buffer;
        q += (*q == '+' || *q == '-') ? 1 : 0;
        if (q[0] == '0' && isdigit(q[1])) {
            *reason = "leading 0 in numbers";
            return -1;
        }

        // 1e+01 is also an error
        if (0 != (q = strchr(buffer, 'e'))) {
            q += (*q == '+' || *q == '-') ? 1 : 0;
            if (q[0] == '0' && isdigit(q[1])) {
                *reason = "leading 0 in numbers";
                return -1;
            }
        }
    }

    return 0;
}

static int toml_scan_float(toml_scanner_t *sp, toml_token_t *tok) {
    char buffer[50];  // need to accomodate "9_007_199_254_740_991.0"
    int len = sp->endp - sp->cur;
    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    memcpy(buffer, sp->cur, len);
    buffer[len] = 0;  // NUL

    int lineno = sp->lineno;
    char *p = buffer;
    p += (*p == '+' || *p == '-') ? 1 : 0;
    if (0 == memcmp(p, "nan", 3) || (0 == memcmp(p, "inf", 3))) {
        p += 3;
    } else {
        p += strspn(p, "_0123456789eE.+-");
    }
    len = p - buffer;
    buffer[len] = 0;

    const char *reason;
    if (toml_process_numstr(buffer, 10, &reason)) {
        return toml_RETERROR(sp->ebuf, lineno, reason);
    }

    errno = 0;
    char *q;
    double fp64 = strtod(buffer, &q);
    if (errno || *q || q == buffer) {
        return toml_RETERROR(sp->ebuf, lineno, "error parsing float");
    }

    *tok = mktoken(sp, TOK_FLOAT);
    tok->u.fp64 = fp64;
    tok->str.len = len;
    sp->cur += len;
    return 0;
}

static int toml_scan_number(toml_scanner_t *sp, toml_token_t *tok) {
    const char *reason;
    char buffer[50];  // need to accomodate "9_007_199_254_740_991.0"
    int len = sp->endp - sp->cur;
    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    memcpy(buffer, sp->cur, len);
    buffer[len] = 0;  // NUL

    char *p = buffer;
    char *q = buffer + len;
    int lineno = sp->lineno;
    // process %0x, %0o or %0b integers
    if (p[0] == '0') {
        const char *span = 0;
        int base = 0;
        switch (p[1]) {
            case 'x':
                base = 16;
                span = "_0123456789abcdefABCDEF";
                break;
            case 'o':
                base = 8;
                span = "_01234567";
                break;
            case 'b':
                base = 2;
                span = "_01";
                break;
        }
        if (base) {
            p += 2;
            p += strspn(p, span);
            len = p - buffer;
            buffer[len] = 0;

            if (toml_process_numstr(buffer + 2, base, &reason)) {
                return toml_RETERROR(sp->ebuf, lineno, reason);
            }

            // use strtoll to obtain the value
            *tok = mktoken(sp, TOK_INTEGER);
            errno = 0;
            tok->u.int64 = strtoll(buffer + 2, &q, base);
            if (errno || *q || q == buffer + 2) {
                return toml_RETERROR(sp->ebuf, lineno, "error parsing integer");
            }
            tok->str.len = len;
            sp->cur += len;
            return 0;
        }
    }

    // handle inf/nan
    if (*p == '+' || *p == '-') {
        p++;
    }
    if (*p == 'i' || *p == 'n') {
        return toml_scan_float(sp, tok);
    }

    // regular int or float
    p = buffer;
    p += strspn(p, "0123456789_+-.eE");
    len = p - buffer;
    buffer[len] = 0;

    if (toml_process_numstr(buffer, 10, &reason)) {
        return toml_RETERROR(sp->ebuf, lineno, reason);
    }

    *tok = mktoken(sp, TOK_INTEGER);
    errno = 0;
    tok->u.int64 = strtoll(buffer, &q, 10);
    if (errno || *q || q == buffer) {
        if (*q && strchr(".eE", *q)) {
            return toml_scan_float(sp, tok);  // try to fit a float
        }
        return toml_RETERROR(sp->ebuf, lineno, "error parsing integer");
    }

    tok->str.len = len;
    sp->cur += len;
    return 0;
}

static int toml_scan_bool(toml_scanner_t *sp, toml_token_t *tok) {
    char buffer[10];
    int len = sp->endp - sp->cur;
    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    memcpy(buffer, sp->cur, len);
    buffer[len] = 0;  // NUL

    int lineno = sp->lineno;
    bool val = false;
    const char *p = buffer;
    if (0 == strncmp(p, "true", 4)) {
        val = true;
        p += 4;
    } else if (0 == strncmp(p, "false", 5)) {
        val = false;
        p += 5;
    } else {
        return toml_RETERROR(sp->ebuf, lineno, "invalid boolean value");
    }
    if (*p && !strchr("# \r\n\t,}]", *p)) {
        return toml_RETERROR(sp->ebuf, lineno, "invalid boolean value");
    }

    len = p - buffer;
    *tok = mktoken(sp, TOK_BOOL);
    tok->u.b1 = val;
    tok->str.len = len;
    sp->cur += len;
    return 0;
}

// Check if the next token may be TIME
static inline bool toml_test_time(const char *p, const char *endp) {
    return &p[2] < endp && isdigit(p[0]) && isdigit(p[1]) && p[2] == ':';
}

// Check if the next token may be DATE
static inline bool toml_test_date(const char *p, const char *endp) {
    return &p[4] < endp && isdigit(p[0]) && isdigit(p[1]) && isdigit(p[2]) &&
           isdigit(p[3]) && p[4] == '-';
}

// Check if the next token may be BOOL
static inline bool toml_test_bool(const char *p, const char *endp) {
    return &p[0] < endp && (*p == 't' || *p == 'f');
}

// Check if the next token may be NUMBER
static bool toml_test_number(const char *p, const char *endp) {
    if (&p[0] < endp && *p && strchr("0123456789+-._", *p)) {
        return true;
    }
    if (&p[3] < endp) {
        if (0 == memcmp(p, "nan", 3) || 0 == memcmp(p, "inf", 3)) {
            return true;
        }
    }
    return false;
}

// Scan a literal that is not a string
static int toml_scan_nonstring_literal(toml_scanner_t *sp, toml_token_t *tok) {
    int lineno = sp->lineno;
    if (toml_test_time(sp->cur, sp->endp)) {
        return toml_scan_time(sp, tok);
    }

    if (toml_test_date(sp->cur, sp->endp)) {
        return toml_scan_timestamp(sp, tok);
    }

    if (toml_test_bool(sp->cur, sp->endp)) {
        return toml_scan_bool(sp, tok);
    }

    if (toml_test_number(sp->cur, sp->endp)) {
        return toml_scan_number(sp, tok);
    }
    return toml_RETERROR(sp->ebuf, lineno, "invalid value");
}

// Scan a literal
static int toml_scan_literal(toml_scanner_t *sp, toml_token_t *tok) {
    *tok = mktoken(sp, TOK_LIT);
    const char *p = sp->cur;
    while (p < sp->endp && (isalnum(*p) || *p == '_' || *p == '-')) {
        p++;
    }
    tok->str.len = p - tok->str.ptr;
    sp->cur = p;
    return 0;
}

// Save the current state of the scanner
static toml_scanner_state_t toml_scan_mark(toml_scanner_t *sp) {
    toml_scanner_state_t mark;
    mark.sp = sp;
    mark.cur = sp->cur;
    mark.lineno = sp->lineno;
    return mark;
}

// Restore the scanner state to a previously saved state
static void toml_scan_restore(toml_scanner_t *sp, toml_scanner_state_t mark) {
    assert(mark.sp == sp);
    sp->cur = mark.cur;
    sp->lineno = mark.lineno;
}

// Return the next token
static int toml_scan_next(toml_scanner_t *sp, bool keymode, toml_token_t *tok) {
again:
    *tok = mktoken(sp, TOK_FIN);
    if (sp->errmsg) {
        return -1;
    }

    int ch = S_GET();
    if (ch == TOK_FIN) {
        return 0;
    }

    tok->str.len = 1;
    switch (ch) {
        case '\n':
            tok->toktyp = TOK_ENDL;
            break;

        case ' ':
        case '\t':
            goto again;  // skip whitespace

        case '#':
            // comment: skip until newline
            while (!S_MATCH('\n')) {
                ch = S_GET();
                if (ch == TOK_FIN)
                    break;
                if ((0 <= ch && ch <= 0x8) || (0x0a <= ch && ch <= 0x1f) ||
                    (ch == 0x7f)) {
                    return toml_RETERROR(sp->ebuf, sp->lineno, "bad control char in comment");
                }
            }
            goto again;  // skip comment

        case '.':
            tok->toktyp = TOK_DOT;
            break;

        case '=':
            tok->toktyp = TOK_EQUAL;
            break;

        case ',':
            tok->toktyp = TOK_COMMA;
            break;

        case '[':
            tok->toktyp = TOK_LBRACK;
            if (keymode && S_MATCH('[')) {
                S_GET();
                tok->toktyp = TOK_LLBRACK;
                tok->str.len = 2;
            }
            break;

        case ']':
            tok->toktyp = TOK_RBRACK;
            if (keymode && S_MATCH(']')) {
                S_GET();
                tok->toktyp = TOK_RRBRACK;
                tok->str.len = 2;
            }
            break;

        case '{':
            tok->toktyp = TOK_LBRACE;
            break;

        case '}':
            tok->toktyp = TOK_RBRACE;
            break;

        case '"':
            sp->cur--;
            TOML_DO(toml_scan_string(sp, tok));
            break;

        case '\'':
            sp->cur--;
            TOML_DO(toml_scan_litstring(sp, tok));
            break;

        default:
            sp->cur--;
            TOML_DO(keymode ? toml_scan_literal(sp, tok) : toml_scan_nonstring_literal(sp, tok));
            break;
    }

    return 0;
}

static int toml_check_overflow(toml_scanner_t *sp, toml_token_t *tok) {
    switch (tok->toktyp) {
        case TOK_LBRACK:
            sp->bracket_level++;
            if (sp->bracket_level > BRACKET_LEVEL_MAX) {
                return toml_RETERROR(sp->ebuf, sp->lineno, "stack overflow");
            }
            break;
        case TOK_RBRACK:
            sp->bracket_level--;
            break;
        case TOK_LBRACE:
            sp->brace_level++;
            if (sp->brace_level > BRACE_LEVEL_MAX) {
                return toml_RETERROR(sp->ebuf, sp->lineno, "stack overflow");
            }
            break;
        case TOK_RBRACE:
            sp->brace_level--;
            break;
        default:
            break;
    }
    return 0;
}

static int toml_scan_key(toml_scanner_t *sp, toml_token_t *tok) {
    TOML_DO(toml_scan_next(sp, true, tok));
    TOML_DO(toml_check_overflow(sp, tok));
    return 0;
}

static int toml_scan_value(toml_scanner_t *sp, toml_token_t *tok) {
    TOML_DO(toml_scan_next(sp, false, tok));
    TOML_DO(toml_check_overflow(sp, tok));
    return 0;
}

/**
 * Convert a char in utf8 into UCS, and store it in *ret.
 * Return #bytes consumed or -1 on failure.
 */
static int toml_utf8_to_ucs(const char *orig, int len, uint32_t *ret) {
    const unsigned char *buf = (const unsigned char *)orig;
    unsigned i = *buf++;
    uint32_t v;

    /* 0x00000000 - 0x0000007F:
       0xxxxxxx
    */
    if (0 == (i >> 7)) {
        if (len < 1)
            return -1;
        v = i;
        return *ret = v, 1;
    }
    /* 0x00000080 - 0x000007FF:
       110xxxxx 10xxxxxx
    */
    if (0x6 == (i >> 5)) {
        if (len < 2)
            return -1;
        v = i & 0x1f;
        for (int j = 0; j < 1; j++) {
            i = *buf++;
            if (0x2 != (i >> 6))
                return -1;
            v = (v << 6) | (i & 0x3f);
        }
        return *ret = v, (const char *)buf - orig;
    }

    /* 0x00000800 - 0x0000FFFF:
       1110xxxx 10xxxxxx 10xxxxxx
    */
    if (0xE == (i >> 4)) {
        if (len < 3)
            return -1;
        v = i & 0x0F;
        for (int j = 0; j < 2; j++) {
            i = *buf++;
            if (0x2 != (i >> 6))
                return -1;
            v = (v << 6) | (i & 0x3f);
        }
        return *ret = v, (const char *)buf - orig;
    }

    /* 0x00010000 - 0x001FFFFF:
       11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    */
    if (0x1E == (i >> 3)) {
        if (len < 4)
            return -1;
        v = i & 0x07;
        for (int j = 0; j < 3; j++) {
            i = *buf++;
            if (0x2 != (i >> 6))
                return -1;
            v = (v << 6) | (i & 0x3f);
        }
        return *ret = v, (const char *)buf - orig;
    }

    if (0) {
        // NOTE: these code points taking more than 4 bytes are not supported

        /* 0x00200000 - 0x03FFFFFF:
           111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
        */
        if (0x3E == (i >> 2)) {
            if (len < 5)
                return -1;
            v = i & 0x03;
            for (int j = 0; j < 4; j++) {
                i = *buf++;
                if (0x2 != (i >> 6))
                    return -1;
                v = (v << 6) | (i & 0x3f);
            }
            return *ret = v, (const char *)buf - orig;
        }

        /* 0x04000000 - 0x7FFFFFFF:
           1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
        */
        if (0x7e == (i >> 1)) {
            if (len < 6)
                return -1;
            v = i & 0x01;
            for (int j = 0; j < 5; j++) {
                i = *buf++;
                if (0x2 != (i >> 6))
                    return -1;
                v = (v << 6) | (i & 0x3f);
            }
            return *ret = v, (const char *)buf - orig;
        }
    }

    return -1;
}

/**
 * Convert a UCS char to utf8 code, and return it in buf.
 * Return #bytes used in buf to encode the char, or
 * -1 on error.
 */
static int toml_ucs_to_utf8(uint32_t code, char buf[4]) {
    /* http://stackoverflow.com/questions/6240055/manually-converting-unicode-codepoints-into-utf-8-and-utf-16
     */
    /* The UCS code values 0xd800–0xdfff (UTF-16 surrogates) as well
     * as 0xfffe and 0xffff (UCS noncharacters) should not appear in
     * conforming UTF-8 streams.
     */
    /*
     *  https://github.com/toml-lang/toml-test/issues/165
     *  [0xd800, 0xdfff] and [0xfffe, 0xffff] are implicitly allowed by TOML, so
     * we disable the check.
     */
    if (0) {
        if (0xd800 <= code && code <= 0xdfff)
            return -1;
        if (0xfffe <= code && code <= 0xffff)
            return -1;
    }

    /* 0x00000000 - 0x0000007F:
       0xxxxxxx
    */
    if (code <= 0x7F) {
        buf[0] = (unsigned char)code;
        return 1;
    }

    /* 0x00000080 - 0x000007FF:
       110xxxxx 10xxxxxx
    */
    if (code <= 0x000007FF) {
        buf[0] = (unsigned char)(0xc0 | (code >> 6));
        buf[1] = (unsigned char)(0x80 | (code & 0x3f));
        return 2;
    }

    /* 0x00000800 - 0x0000FFFF:
       1110xxxx 10xxxxxx 10xxxxxx
    */
    if (code <= 0x0000FFFF) {
        buf[0] = (unsigned char)(0xe0 | (code >> 12));
        buf[1] = (unsigned char)(0x80 | ((code >> 6) & 0x3f));
        buf[2] = (unsigned char)(0x80 | (code & 0x3f));
        return 3;
    }

    /* 0x00010000 - 0x001FFFFF:
       11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    */
    if (code <= 0x001FFFFF) {
        buf[0] = (unsigned char)(0xf0 | (code >> 18));
        buf[1] = (unsigned char)(0x80 | ((code >> 12) & 0x3f));
        buf[2] = (unsigned char)(0x80 | ((code >> 6) & 0x3f));
        buf[3] = (unsigned char)(0x80 | (code & 0x3f));
        return 4;
    }

#ifdef UNDEF
    if (0) {
        // NOTE: these code points taking more than 4 bytes are not supported
        /* 0x00200000 - 0x03FFFFFF:
           111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
        */
        if (code <= 0x03FFFFFF) {
            buf[0] = (unsigned char)(0xf8 | (code >> 24));
            buf[1] = (unsigned char)(0x80 | ((code >> 18) & 0x3f));
            buf[2] = (unsigned char)(0x80 | ((code >> 12) & 0x3f));
            buf[3] = (unsigned char)(0x80 | ((code >> 6) & 0x3f));
            buf[4] = (unsigned char)(0x80 | (code & 0x3f));
            return 5;
        }

        /* 0x04000000 - 0x7FFFFFFF:
           1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
        */
        if (code <= 0x7FFFFFFF) {
            buf[0] = (unsigned char)(0xfc | (code >> 30));
            buf[1] = (unsigned char)(0x80 | ((code >> 24) & 0x3f));
            buf[2] = (unsigned char)(0x80 | ((code >> 18) & 0x3f));
            buf[3] = (unsigned char)(0x80 | ((code >> 12) & 0x3f));
            buf[4] = (unsigned char)(0x80 | ((code >> 6) & 0x3f));
            buf[5] = (unsigned char)(0x80 | (code & 0x3f));
            return 6;
        }
    }
#endif

    return -1;
}