/*
 * _cscanner.c — C-accelerated date scanner for metadatez.
 *
 * v3: Unified lookup table + vectorcall + no PyDict for kwargs.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* ── Macros ─────────────────────────────────────────────────── */

#define ISDIG(c)  ((c) >= '0' && (c) <= '9')
#define ISALP(c)  (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define ISWAN(c)  (ISDIG(c) || ISALP(c) || (c) == '_')

#define VALID_DAY(d)    ((d) >= 1 && (d) <= 31)
#define VALID_MONTH(m)  ((m) >= 1 && (m) <= 12)
#define VALID_YEAR(y)   ((y) >= 1900 && (y) <= 2100)
#define VALID_HOUR(h)   ((h) >= 0 && (h) <= 23)

/* ══════════════════════════════════════════════════════════════
 * SECTION 1: Unified Hash Table (FNV-1a, open addressing)
 *
 * One table replaces 15 separate tables.  Each entry carries a
 * word-type tag so classify_word needs only ONE probe per word.
 * ══════════════════════════════════════════════════════════════ */

#define UT_CAP  512
#define UT_MASK (UT_CAP - 1)

/* Word types — priority order used during table build:
 * entries inserted lowest-first; later (higher) overwrites. */
#define WT_NONE       0
#define WT_WHITELIST  1
#define WT_AND        2
#define WT_ORDINAL    3
#define WT_UNIT       4
#define WT_MODIFIER   5
#define WT_WEEKDAY_SHORT 6
#define WT_WEEKDAY    7
#define WT_MONTH_SHORT 8
#define WT_MONTH      9
#define WT_NOON_SA    10
#define WT_QUARTER    11
#define WT_SEASON     12
#define WT_TODAY      13
#define WT_NOW        14

typedef struct {
    char          *key;
    unsigned char  occupied;
    unsigned char  wtype;
    long           ival;        /* primary int value */
    long           ival2;       /* noon second hour  */
    double         dval;        /* ordinal amounts   */
    int            unit_idx;    /* index into uname[] for WT_UNIT */
} UEntry;

typedef struct { UEntry entries[UT_CAP]; } UnifiedTable;

static unsigned int fnv1a(const char *s) {
    unsigned int h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

static void ut_init(UnifiedTable *ut) { memset(ut, 0, sizeof(*ut)); }

/* Return existing entry or create new slot. */
static UEntry *ut_slot(UnifiedTable *ut, const char *key) {
    unsigned int idx = fnv1a(key) & UT_MASK;
    while (ut->entries[idx].occupied) {
        if (strcmp(ut->entries[idx].key, key) == 0) return &ut->entries[idx];
        idx = (idx + 1) & UT_MASK;
    }
    ut->entries[idx].key = strdup(key);
    ut->entries[idx].occupied = 1;
    return &ut->entries[idx];
}

static const UEntry *ut_find(const UnifiedTable *ut, const char *key) {
    unsigned int idx = fnv1a(key) & UT_MASK;
    int probe = 0;
    while (ut->entries[idx].occupied && probe < UT_CAP) {
        if (strcmp(ut->entries[idx].key, key) == 0) return &ut->entries[idx];
        idx = (idx + 1) & UT_MASK;
        probe++;
    }
    return NULL;
}

static void ut_free(UnifiedTable *ut) {
    for (int i = 0; i < UT_CAP; i++)
        if (ut->entries[i].key) { free(ut->entries[i].key); ut->entries[i].key = NULL; }
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 2: Phrase arrays (for multi-word matching)
 * ══════════════════════════════════════════════════════════════ */

typedef struct { char *str; int len; } Phrase;

static void phrases_free(Phrase *p, int n) {
    if (!p) return;
    for (int i = 0; i < n; i++) free(p[i].str);
    free(p);
}

static Phrase *phrases_from_pylist(PyObject *lst, int *out_n) {
    Py_ssize_t n = PyList_GET_SIZE(lst);
    Phrase *p = (Phrase *)calloc(n, sizeof(Phrase));
    for (Py_ssize_t i = 0; i < n; i++) {
        const char *s = PyUnicode_AsUTF8(PyList_GET_ITEM(lst, i));
        p[i].str = strdup(s);
        p[i].len = (int)strlen(s);
    }
    *out_n = (int)n;
    return p;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 3: Scanner object
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    PyObject_HEAD

    /* Single unified word lookup table */
    UnifiedTable words;
    int dd_left_first;

    /* Phrase arrays (sorted longest-first) */
    Phrase *ph_in_the;       int n_in_the;
    Phrase *ph_today_multi;  int n_today_multi;
    Phrase *ph_quarter_multi;int n_quarter_multi;
    Phrase *ph_ordinal_multi;int n_ordinal_multi;

    /* Meta* classes (strong refs) */
    PyObject *cls_Rel, *cls_Ord, *cls_Unit, *cls_Mod, *cls_Rng, *cls_And;

    /* Fast-path: MetaRelative._from_scanner bound method + relativedelta class */
    PyObject *cls_FromScanner;   /* bound classmethod */
    PyObject *cls_rd;            /* dateutil.relativedelta.relativedelta */

    /* Cached Units enum values (strong) */
    PyObject *u_YEAR, *u_SEASON, *u_QUARTER, *u_MONTH, *u_WEEK;
    PyObject *u_DAY, *u_HOUR, *u_MINUTE, *u_SECOND, *u_MICRO;

    /* Cached interned unit-name strings for MetaUnit (strong) */
    PyObject *uname[8]; /* YEAR MONTH WEEK DAY HOUR MINUTE SECOND MICROSECOND */

    /* Pre-built kwnames tuples for MetaRelative vectorcall patterns */
    PyObject *kn_days;            /* ("days",)                                   */
    PyObject *kn_now;             /* ("days","hours","minutes","seconds")         */
    PyObject *kn_month;           /* ("month",)                                  */
    PyObject *kn_month_day;       /* ("month","day")                             */
    PyObject *kn_month_day_lv;    /* ("month","day","levels")                    */
    PyObject *kn_year;            /* ("year",)                                   */
    PyObject *kn_ymd;             /* ("year","month","day")                      */
    PyObject *kn_weekday;         /* ("weekday",)                                */
    PyObject *kn_hour;            /* ("hour",)                                   */
    PyObject *kn_hour_lv;         /* ("hour","levels")                           */
    PyObject *kn_hour_min;        /* ("hour","minute")                           */
    PyObject *kn_hms;             /* ("hour","minute","second")                  */
    PyObject *kn_hmsm;            /* ("hour","minute","second","microsecond")    */
    PyObject *kn_hms_lv;          /* ("hour","minute","second","levels")         */
    PyObject *kn_day;             /* ("day",)                                    */

    /* Cached "SENT" string */
    PyObject *str_SENT;
} ScannerObject;

/* ══════════════════════════════════════════════════════════════
 * SECTION 4: Helpers
 * ══════════════════════════════════════════════════════════════ */

static int wb_start(const char *t, int p) {
    return (p == 0) || !ISWAN((unsigned char)t[p-1]);
}
static int wb_end(const char *t, int p, int n) {
    return (p >= n) || !ISWAN((unsigned char)t[p]);
}
static int ci_starts(const char *t, int p, int n, const char *tgt) {
    for (int i = 0; tgt[i]; i++) {
        if (p+i >= n) return 0;
        if (tolower((unsigned char)t[p+i]) != (unsigned char)tgt[i]) return 0;
    }
    return 1;
}

static inline PyObject *make_span(int s, int e) {
    PyObject *t = PyTuple_New(2);
    if (!t) return NULL;
    PyTuple_SET_ITEM(t, 0, PyLong_FromLong(s));
    PyTuple_SET_ITEM(t, 1, PyLong_FromLong(e));
    return t;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 5: Meta* object factories
 *
 * MetaRelative uses _from_scanner(span, levels, rd) to bypass
 * dict allocation and float-checking in __init__.
 * Other Meta* types use PyObject_CallFunctionObjArgs (no kwargs).
 * ══════════════════════════════════════════════════════════════ */

/* Map a kwname string to the corresponding Units enum value.
 * Returns a borrowed ref, or NULL if the name is "levels". */
static PyObject *kwname_to_unit(ScannerObject *self, PyObject *name) {
    /* Use pointer comparison first (interned strings) */
    const char *s = PyUnicode_AsUTF8(name);
    if (!s) return NULL;
    switch (s[0]) {
    case 'y': return self->u_YEAR;      /* "year" */
    case 'm':
        if (s[1] == 'o') return self->u_MONTH;  /* "month" */
        if (s[1] == 'i') {
            if (s[2] == 'c') return self->u_MICRO; /* "microsecond" */
            return self->u_MINUTE;                  /* "minute", "minutes" */
        }
        return self->u_MONTH;            /* "months" */
    case 'd': return self->u_DAY;        /* "day", "days" */
    case 'h': return self->u_HOUR;       /* "hour", "hours" */
    case 'w':
        if (s[1] == 'e' && s[2] == 'e' && s[3] == 'k') {
            if (s[4] == 'd') return self->u_DAY;   /* "weekday" */
            return self->u_WEEK;                     /* "weeks" */
        }
        return NULL;
    case 's':
        if (s[1] == 'e') return self->u_SECOND;  /* "second", "seconds" */
        return NULL;
    case 'l': return NULL;               /* "levels" — skip */
    default:  return NULL;
    }
}

/* MetaRelative via _from_scanner(span, levels, rd_obj)
 * kv[] = array of PyObject* new refs; kwn = kwnames tuple.
 * kv values are DECREF'd by this function.
 *
 * If the last element of kwn is "levels", that kv[] entry is the
 * pre-built levels set and we use it directly; otherwise we auto-
 * compute levels from the keyword names. */
static PyObject *vrel(ScannerObject *self, int s, int e,
                       int nkw, PyObject **kv, PyObject *kwn) {
    PyObject *sp = NULL, *levels = NULL, *rd_kwargs = NULL;
    PyObject *rd_obj = NULL, *result = NULL;
    int has_explicit_levels = 0;
    int rd_nkw = nkw;  /* number of kwargs for relativedelta */

    sp = make_span(s, e);
    if (!sp) goto error;

    /* Check if last kwname is "levels" */
    {
        PyObject *last_name = PyTuple_GET_ITEM(kwn, nkw - 1);
        const char *ln = PyUnicode_AsUTF8(last_name);
        if (ln && ln[0] == 'l' && ln[1] == 'e') {
            has_explicit_levels = 1;
            levels = kv[nkw - 1];  /* steal ref */
            kv[nkw - 1] = NULL;    /* prevent double-decref in cleanup */
            rd_nkw = nkw - 1;
        }
    }

    /* Build levels set if not explicit */
    if (!has_explicit_levels) {
        levels = PySet_New(NULL);
        if (!levels) goto error;
        for (int i = 0; i < nkw; i++) {
            PyObject *unit = kwname_to_unit(self, PyTuple_GET_ITEM(kwn, i));
            if (unit) PySet_Add(levels, unit);
        }
    }

    /* Build relativedelta kwargs dict */
    rd_kwargs = PyDict_New();
    if (!rd_kwargs) goto error;
    for (int i = 0; i < rd_nkw; i++) {
        PyDict_SetItem(rd_kwargs, PyTuple_GET_ITEM(kwn, i), kv[i]);
    }

    /* Construct relativedelta(**rd_kwargs) */
    {
        PyObject *empty = PyTuple_New(0);
        rd_obj = PyObject_Call(self->cls_rd, empty, rd_kwargs);
        Py_DECREF(empty);
    }
    if (!rd_obj) goto error;

    /* Call _from_scanner(span, levels, rd_obj) */
    result = PyObject_CallFunctionObjArgs(self->cls_FromScanner,
                                          sp, levels, rd_obj, NULL);

    /* Fall through to cleanup */
error:
    for (int i = 0; i < nkw; i++) Py_XDECREF(kv[i]);
    Py_XDECREF(sp);
    Py_XDECREF(levels);
    Py_XDECREF(rd_kwargs);
    Py_XDECREF(rd_obj);
    return result;
}

/* MetaOrdinal(amount, span) */
static PyObject *vord(ScannerObject *self, double amount, int s, int e) {
    PyObject *a = PyFloat_FromDouble(amount);
    PyObject *sp = make_span(s, e);
    PyObject *obj = PyObject_CallFunctionObjArgs(self->cls_Ord, a, sp, NULL);
    Py_DECREF(a); Py_DECREF(sp);
    return obj;
}

/* MetaUnit(unit_name_str, span) — uname is borrowed */
static PyObject *vunit(ScannerObject *self, int uidx, int s, int e) {
    PyObject *sp = make_span(s, e);
    PyObject *obj = PyObject_CallFunctionObjArgs(self->cls_Unit, self->uname[uidx], sp, NULL);
    Py_DECREF(sp);
    return obj;
}

/* MetaModifier(x, value, span) */
static PyObject *vmod(ScannerObject *self, const char *word, long val, int s, int e) {
    PyObject *x = PyUnicode_FromString(word);
    PyObject *v = PyLong_FromLong(val);
    PyObject *sp = make_span(s, e);
    PyObject *obj = PyObject_CallFunctionObjArgs(self->cls_Mod, x, v, sp, NULL);
    Py_DECREF(x); Py_DECREF(v); Py_DECREF(sp);
    return obj;
}

/* MetaRange(x, span) */
static PyObject *vrng(ScannerObject *self, const char *word, int s, int e) {
    PyObject *x = PyUnicode_FromString(word);
    PyObject *sp = make_span(s, e);
    PyObject *obj = PyObject_CallFunctionObjArgs(self->cls_Rng, x, sp, NULL);
    Py_DECREF(x); Py_DECREF(sp);
    return obj;
}

/* MetaAnd(x, span) */
static PyObject *vand(ScannerObject *self, const char *word, int s, int e) {
    PyObject *x = PyUnicode_FromString(word);
    PyObject *sp = make_span(s, e);
    PyObject *obj = PyObject_CallFunctionObjArgs(self->cls_And, x, sp, NULL);
    Py_DECREF(x); Py_DECREF(sp);
    return obj;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 6: Emit helpers
 * ══════════════════════════════════════════════════════════════ */

static int emit(PyObject *list, PyObject *obj) {
    if (!obj) return -1;
    int rc = PyList_Append(list, obj);
    Py_DECREF(obj);
    return rc;
}

static int emit_str(PyObject *list, const char *text, int s, int e) {
    PyObject *str = PyUnicode_FromStringAndSize(text + s, e - s);
    if (!str) return -1;
    int rc = PyList_Append(list, str);
    Py_DECREF(str);
    return rc;
}

static int emit_sent(ScannerObject *self, PyObject *list) {
    return PyList_Append(list, self->str_SENT);
}

/* Build a levels set with one or two elements */
static PyObject *levels1(PyObject *u) {
    PyObject *s = PySet_New(NULL);
    PySet_Add(s, u);
    return s;
}
static PyObject *levels2(PyObject *u1, PyObject *u2) {
    PyObject *s = PySet_New(NULL);
    PySet_Add(s, u1);
    PySet_Add(s, u2);
    return s;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 7: Time parsing
 * ══════════════════════════════════════════════════════════════ */

static int str_has_ci(const char *s, int len, const char *needle) {
    int nlen = (int)strlen(needle);
    for (int i = 0; i + nlen <= len; i++) {
        int ok = 1;
        for (int j = 0; j < nlen; j++)
            if (tolower((unsigned char)s[i+j]) != (unsigned char)needle[j]) { ok = 0; break; }
        if (ok) return 1;
    }
    return 0;
}

static void parse_time_str(const char *raw, int len,
                           int *hour, int *minute, int *second, int *micro) {
    *hour = 0; *minute = 0; *second = 0; *micro = 0;
    int hoffset = 0;
    if (str_has_ci(raw, len, "pm") || str_has_ci(raw, len, "p.m") ||
        str_has_ci(raw, len, "afternoon"))
        hoffset = 12;
    int nums[4] = {0}, nc = 0, i = 0;
    while (i < len && nc < 4) {
        if (ISDIG(raw[i])) {
            int v = 0;
            while (i < len && ISDIG(raw[i])) { v = v*10 + (raw[i]-'0'); i++; }
            nums[nc++] = v;
        } else i++;
    }
    *hour = nums[0];
    if (nc >= 2) *minute = nums[1];
    if (nc >= 3) *second = nums[2];
    if (nc >= 4) *micro  = nums[3];
    if (*hour == 12 && hoffset) hoffset = 0;
    *hour = (hoffset + *hour) % 24;
}

static int classify_time(ScannerObject *self, const char *raw, int rawlen,
                         int s, int e, PyObject *results) {
    int hour, minute, second, micro;
    parse_time_str(raw, rawlen, &hour, &minute, &second, &micro);
    int has_dot = (memchr(raw, '.', rawlen) != NULL);
    PyObject *obj;
    if (has_dot && micro) {
        PyObject *kv[] = {PyLong_FromLong(hour), PyLong_FromLong(minute),
                          PyLong_FromLong(second), PyLong_FromLong(micro)};
        obj = vrel(self, s, e, 4, kv, self->kn_hmsm);
    } else if (second) {
        PyObject *kv[] = {PyLong_FromLong(hour), PyLong_FromLong(minute),
                          PyLong_FromLong(second)};
        obj = vrel(self, s, e, 3, kv, self->kn_hms);
    } else if (minute) {
        PyObject *kv[] = {PyLong_FromLong(hour), PyLong_FromLong(minute)};
        obj = vrel(self, s, e, 2, kv, self->kn_hour_min);
    } else {
        PyObject *kv[] = {PyLong_FromLong(hour)};
        obj = vrel(self, s, e, 1, kv, self->kn_hour);
    }
    return emit(results, obj);
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 8: Tokenizer pattern matchers
 * (Pure C, unchanged from v2)
 * ══════════════════════════════════════════════════════════════ */

enum {
    TOK_SKIP = 0, TOK_PUNCT, TOK_HMS_MICRO, TOK_HMS, TOK_HM,
    TOK_HOUR_APM, TOK_ISO_DATE, TOK_ISO_COMPACT, TOK_DD_MM_YYYY,
    TOK_YEAR4, TOK_QUARTER_Q, TOK_ORDINAL_NUM, TOK_NUMBER, TOK_WORD,
};

static int match_apm(const char *t, int p, int n, int *pm) {
    if (p >= n) return p;
    char c = tolower((unsigned char)t[p]);
    if (c != 'a' && c != 'p') return p;
    *pm = (c == 'p');
    int q = p + 1;
    if (q >= n) return p;
    if (t[q] == '.') {
        q++;
        if (q < n && tolower((unsigned char)t[q]) == 'm') {
            q++; if (q < n && t[q] == '.') q++;
            return q;
        }
        return p;
    }
    if (tolower((unsigned char)t[q]) == 'm') {
        q++; if (q < n && t[q] == '.') q++;
        return q;
    }
    return p;
}

static int try_hms_micro(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int s=p;
    if (!ISDIG(t[p])) return 0;
    p++;
    if (p<n && ISDIG(t[p])) p++;
    if (p>=n||(t[p]!=':'&&tolower((unsigned char)t[p])!='h')) return 0;
    p++;
    if (p+1>=n||!ISDIG(t[p])||!ISDIG(t[p+1])) return 0;
    p+=2;
    if (p>=n||(t[p]!=':'&&tolower((unsigned char)t[p])!='m')) return 0;
    p++;
    if (p+1>=n||!ISDIG(t[p])||!ISDIG(t[p+1])) return 0;
    p+=2;
    if (p>=n||t[p]!='.') return 0;
    p++;
    if (p>=n||!ISDIG(t[p])) return 0;
    while (p<n && ISDIG(t[p])) p++;
    return wb_end(t,p,n) ? p-s : 0;
}

static int try_hms(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int s=p;
    if (!ISDIG(t[p])) return 0;
    p++;
    if (p<n && ISDIG(t[p])) p++;
    if (p>=n||(t[p]!=':'&&tolower((unsigned char)t[p])!='h')) return 0;
    p++;
    if (p+1>=n||!ISDIG(t[p])||!ISDIG(t[p+1])) return 0;
    p+=2;
    if (p>=n||(t[p]!=':'&&tolower((unsigned char)t[p])!='m')) return 0;
    p++;
    if (p+1>=n||!ISDIG(t[p])||!ISDIG(t[p+1])) return 0;
    p+=2;
    int sv=p;
    if (p<n&&t[p]==' ') p++;
    int pm; int ap=match_apm(t,p,n,&pm);
    if (ap>p) p=ap; else p=sv;
    return wb_end(t,p,n) ? p-s : 0;
}

static int try_hm(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int s=p;
    if (!ISDIG(t[p])) return 0;
    p++;
    if (p<n && ISDIG(t[p])) p++;
    if (p>=n||(t[p]!=':'&&tolower((unsigned char)t[p])!='h'&&t[p]!='\'')) return 0;
    p++;
    if (p+1>=n||!ISDIG(t[p])||!ISDIG(t[p+1])) return 0;
    p+=2;
    int sv=p;
    if (p<n&&t[p]==' ') p++;
    int pm; int ap=match_apm(t,p,n,&pm);
    if (ap>p) p=ap;
    else { p=sv; if (p<n&&(tolower((unsigned char)t[p])=='h'||tolower((unsigned char)t[p])=='m')) p++; }
    return wb_end(t,p,n) ? p-s : 0;
}

static int try_hour_apm(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int s=p;
    if (!ISDIG(t[p])) return 0;
    p++;
    if (p<n && ISDIG(t[p])) p++;
    int sv=p;
    if (p<n&&t[p]==' ') p++;
    int pm; int ap=match_apm(t,p,n,&pm);
    if (ap>p && wb_end(t,ap,n)) return ap-s;
    p=sv; if (p<n&&t[p]==' ') p++;
    if (ci_starts(t,p,n,"afternoon") && wb_end(t,p+9,n)) return p+9-s;
    p=sv; if (p<n&&t[p]==' ') p++;
    if (p<n && tolower((unsigned char)t[p])=='o') {
        int q=p+1; if (q<n&&t[q]=='\'') q++;
        if (ci_starts(t,q,n,"clock") && wb_end(t,q+5,n)) return q+5-s;
    }
    return 0;
}

static int try_iso_date(const char *t, int p, int n) {
    if (!wb_start(t,p)||p+7>=n) return 0;
    if (t[p]!='1'&&t[p]!='2') return 0;
    if (!ISDIG(t[p+1])||!ISDIG(t[p+2])||!ISDIG(t[p+3])) return 0;
    int q=p+4;
    if (t[q]!='-'&&t[q]!='/') return 0;
    q++;
    if (q+1>=n||!ISDIG(t[q])) return 0;
    q++; if (q<n&&ISDIG(t[q])) q++;
    if (q>=n||(t[q]!='-'&&t[q]!='/')) return 0;
    q++;
    if (q>=n||!ISDIG(t[q])) return 0;
    q++; if (q<n&&ISDIG(t[q])) q++;
    return wb_end(t,q,n) ? q-p : 0;
}

static int try_iso_compact(const char *t, int p, int n) {
    if (!wb_start(t,p)||p+8>n) return 0;
    for (int i=0;i<8;i++) if (!ISDIG(t[p+i])) return 0;
    if (p+8<n && ISDIG(t[p+8])) return 0;
    return wb_end(t,p+8,n) ? 8 : 0;
}

static int try_dd_mm_yyyy(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int q=p;
    if (q>=n||!ISDIG(t[q])) return 0;
    q++;
    if (q>=n||!ISDIG(t[q])) return 0;
    q++;
    if (q>=n||(t[q]!='/'&&t[q]!=' ')) return 0;
    q++;
    if (q>=n||!ISDIG(t[q])) return 0;
    q++;
    if (q>=n||!ISDIG(t[q])) return 0;
    q++;
    if (q>=n||(t[q]!='/'&&t[q]!=' ')) return 0;
    q++;
    if (q+3>=n) return 0;
    for (int i=0;i<4;i++) if (!ISDIG(t[q+i])) return 0;
    q+=4;
    return wb_end(t,q,n) ? q-p : 0;
}

static int try_year4(const char *t, int p, int n) {
    if (!wb_start(t,p)||p+4>n) return 0;
    if ((t[p]!='1'&&t[p]!='2')||!ISDIG(t[p+1])||!ISDIG(t[p+2])||!ISDIG(t[p+3])) return 0;
    return wb_end(t,p+4,n) ? 4 : 0;
}

static int try_ordinal_num(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int q=p;
    while (q<n && ISDIG(t[q])) q++;
    if (q==p) return 0;
    if (q+1<n) {
        char c0=tolower((unsigned char)t[q]),c1=tolower((unsigned char)t[q+1]);
        if ((c0=='s'&&c1=='t')||(c0=='n'&&c1=='d')||(c0=='r'&&c1=='d')||(c0=='t'&&c1=='h'))
        { q+=2; return wb_end(t,q,n)?q-p:0; }
    }
    return 0;
}

static int try_number(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int q=p;
    while (q<n && ISDIG(t[q])) q++;
    if (q<n && (t[q]==','||t[q]=='.') && q+1<n && ISDIG(t[q+1])) {
        q++; while (q<n && ISDIG(t[q])) q++;
    }
    return (q>p && wb_end(t,q,n)) ? q-p : 0;
}

static int try_word(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int q=p; while (q<n && ISALP(t[q])) q++;
    if (q<n && t[q]=='\'') { int q2=q+1; while (q2<n && ISALP(t[q2])) q2++; q=q2; }
    return q>p ? q-p : 0;
}

static int try_quarter_q(const char *t, int p, int n) {
    if (p+2>n) return 0;
    if (tolower((unsigned char)t[p])!='q') return 0;
    if (t[p+1]<'1'||t[p+1]>'4') return 0;
    return wb_end(t,p+2,n) ? 2 : 0;
}

static int tokenize_at(const char *t, int pos, int n, int *kind, int *end) {
    unsigned char c = (unsigned char)t[pos];
    int r;
    if (ISDIG(c)) {
        if (wb_start(t,pos)) {
            if ((r=try_hms_micro(t,pos,n))){ *kind=TOK_HMS_MICRO;*end=pos+r;return 1; }
            if ((r=try_hms(t,pos,n)))      { *kind=TOK_HMS;      *end=pos+r;return 1; }
            if ((r=try_hm(t,pos,n)))       { *kind=TOK_HM;       *end=pos+r;return 1; }
            if ((r=try_hour_apm(t,pos,n))) { *kind=TOK_HOUR_APM; *end=pos+r;return 1; }
            if ((r=try_iso_date(t,pos,n))) { *kind=TOK_ISO_DATE;  *end=pos+r;return 1; }
            if ((r=try_iso_compact(t,pos,n))){ *kind=TOK_ISO_COMPACT;*end=pos+r;return 1; }
            if ((r=try_dd_mm_yyyy(t,pos,n))){ *kind=TOK_DD_MM_YYYY;*end=pos+r; return 1; }
            if ((r=try_year4(t,pos,n)))     { *kind=TOK_YEAR4;     *end=pos+r; return 1; }
            if ((r=try_ordinal_num(t,pos,n))){ *kind=TOK_ORDINAL_NUM;*end=pos+r;return 1; }
        }
        if ((r=try_number(t,pos,n))) { *kind=TOK_NUMBER; *end=pos+r; return 1; }
        return 0;
    }
    if (ISALP(c)) {
        if (wb_start(t,pos) && (r=try_quarter_q(t,pos,n))) { *kind=TOK_QUARTER_Q; *end=pos+r; return 1; }
        if ((r=try_word(t,pos,n))) { *kind=TOK_WORD; *end=pos+r; return 1; }
        return 0;
    }
    if (c=='!'||c=='?') {
        int q=pos+1; while (q<n&&(t[q]=='!'||t[q]=='?')) q++;
        *kind=TOK_PUNCT; *end=q; return 1;
    }
    if (c=='\n') { *kind=TOK_PUNCT; *end=pos+1; return 1; }
    if (c=='.') {
        if (pos+1>=n||(!ISALP(t[pos+1])&&!ISDIG(t[pos+1])&&t[pos+1]!='.'))
        { *kind=TOK_PUNCT; *end=pos+1; return 1; }
    }
    if (isspace(c)) {
        int q=pos+1; while (q<n&&isspace((unsigned char)t[q])) q++;
        *kind=TOK_SKIP; *end=q; return 1;
    }
    if (c=='-'||c==',') { *kind=TOK_SKIP; *end=pos+1; return 1; }
    return 0;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 9: Multi-word matching (uses unified table)
 * ══════════════════════════════════════════════════════════════ */

static int do_try_multiword(ScannerObject *self, const char *tl, const char *mt,
                            int pos, int n, PyObject *results) {
    const char *chunk = tl + pos;
    int rem = n - pos;

    /* "and a half" */
    if (rem >= 10 && memcmp(chunk, "and a half", 10) == 0) {
        return emit(results, vord(self, 0.5, pos, pos+10)) < 0 ? 0 : 10;
    }

    /* "in the", "over the", etc — must be followed by space */
    for (int i = 0; i < self->n_in_the; i++) {
        int pl = self->ph_in_the[i].len;
        if (rem > pl && memcmp(chunk, self->ph_in_the[i].str, pl) == 0 && chunk[pl] == ' ') {
            if (emit(results, vrng(self, self->ph_in_the[i].str, pos, pos+pl)) < 0) return 0;
            return pl + 1;
        }
    }

    /* Multi-word today/tomorrow */
    for (int i = 0; i < self->n_today_multi; i++) {
        int pl = self->ph_today_multi[i].len;
        if (rem >= pl && memcmp(chunk, self->ph_today_multi[i].str, pl) == 0) {
            if (pos+pl >= n || !ISALP((unsigned char)tl[pos+pl])) {
                const UEntry *ue = ut_find(&self->words, self->ph_today_multi[i].str);
                if (!ue) continue;
                PyObject *kv[] = {PyLong_FromLong(ue->ival)};
                if (emit(results, vrel(self, pos, pos+pl, 1, kv, self->kn_days)) < 0) return 0;
                return pl;
            }
        }
    }

    /* Multi-word quarters */
    for (int i = 0; i < self->n_quarter_multi; i++) {
        int pl = self->ph_quarter_multi[i].len;
        if (rem >= pl && memcmp(chunk, self->ph_quarter_multi[i].str, pl) == 0) {
            if (pos+pl >= n || !ISALP((unsigned char)tl[pos+pl])) {
                const UEntry *ue = ut_find(&self->words, self->ph_quarter_multi[i].str);
                if (!ue) continue;
                PyObject *lv = levels1(self->u_QUARTER);
                PyObject *kv[] = {PyLong_FromLong(ue->ival), PyLong_FromLong(1), lv};
                if (emit(results, vrel(self, pos, pos+pl, 3, kv, self->kn_month_day_lv)) < 0) return 0;
                return pl;
            }
        }
    }

    /* Multi-word ordinals ("a couple") */
    for (int i = 0; i < self->n_ordinal_multi; i++) {
        int pl = self->ph_ordinal_multi[i].len;
        if (rem >= pl && memcmp(chunk, self->ph_ordinal_multi[i].str, pl) == 0) {
            if (pos+pl >= n || !ISALP((unsigned char)tl[pos+pl])) {
                const UEntry *ue = ut_find(&self->words, self->ph_ordinal_multi[i].str);
                if (!ue) continue;
                if (emit(results, vord(self, ue->dval, pos, pos+pl)) < 0) return 0;
                return pl;
            }
        }
    }

    /* "on the NNst/nd/rd/th" */
    if (rem >= 8 && memcmp(chunk, "on the ", 7) == 0) {
        int q = 7;
        if (q < rem && ISDIG(chunk[q])) {
            int day = chunk[q]-'0'; q++;
            if (q < rem && ISDIG(chunk[q])) { day = day*10+(chunk[q]-'0'); q++; }
            if (q+1 < rem) {
                char c0=chunk[q], c1=chunk[q+1];
                if ((c0=='s'&&c1=='t')||(c0=='n'&&c1=='d')||
                    (c0=='r'&&c1=='d')||(c0=='t'&&c1=='h')) {
                    q += 2;
                    if (wb_end(tl, pos+q, n) && VALID_DAY(day)) {
                        PyObject *kv[] = {PyLong_FromLong(day)};
                        if (emit(results, vrel(self, pos, pos+q, 1, kv, self->kn_day)) < 0) return 0;
                        return q;
                    }
                }
            }
        }
    }

    /* "at NN" */
    if (rem >= 4 && chunk[0]=='a' && chunk[1]=='t' && chunk[2]==' ') {
        int q = 3;
        if (q < rem && ISDIG(chunk[q])) {
            int hour = chunk[q]-'0'; q++;
            if (q < rem && ISDIG(chunk[q])) { hour = hour*10+(chunk[q]-'0'); q++; }
            if (q < rem && tolower((unsigned char)chunk[q]) == 'h') q++;
            if (wb_end(mt, pos+q, n)) {
                int bad = 0, ep = pos+q;
                if (ep < n && (mt[ep]==':'||mt[ep]=='\'')) bad = 1;
                if (ep+1 < n && mt[ep]=='.' && mt[ep+1]==' ') bad = 1;
                if (!bad && VALID_HOUR(hour)) {
                    PyObject *kv[] = {PyLong_FromLong(hour)};
                    if (emit(results, vrel(self, pos, pos+q, 1, kv, self->kn_hour)) < 0) return 0;
                    return q;
                }
            }
        }
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 10: Number+month / month+day (uses unified table)
 * ══════════════════════════════════════════════════════════════ */

static int do_try_number_month(ScannerObject *self, const char *text, const char *tl,
                               int ns, int ne, int n, PyObject *results, int *out) {
    int p = ne;
    while (p < n && (text[p]==' '||text[p]=='-'||text[p]=='\t')) p++;
    if (p+3 <= n && tl[p]=='o' && tl[p+1]=='f' && text[p+2]==' ') {
        p += 3; while (p < n && text[p]==' ') p++;
    }
    if (p >= n || !ISALP(text[p])) return 0;
    int ws = p; while (p < n && ISALP(text[p])) p++;
    int wlen = p - ws;
    char word[64]; if (wlen >= 64) return 0;
    for (int i = 0; i < wlen; i++) word[i] = tolower((unsigned char)text[ws+i]);
    word[wlen] = '\0';

    long month = 0;
    const UEntry *ue = ut_find(&self->words, word);
    if (ue && (ue->wtype == WT_MONTH || ue->wtype == WT_MONTH_SHORT)) {
        month = ue->ival;
    } else {
        /* Try 3-char short form */
        if (wlen < 3) return 0;
        char sw[4]; sw[0]=word[0]; sw[1]=word[1]; sw[2]=word[2]; sw[3]='\0';
        ue = ut_find(&self->words, sw);
        if (!ue || ue->wtype != WT_MONTH_SHORT) return 0;
        month = ue->ival;
    }

    int day = 0;
    for (int i = ns; i < ne; i++) if (ISDIG(text[i])) day = day*10+(text[i]-'0');
    if (!VALID_DAY(day)) return 0;

    PyObject *kv[] = {PyLong_FromLong(month), PyLong_FromLong(day)};
    if (emit(results, vrel(self, ns, p, 2, kv, self->kn_month_day)) < 0) return 0;
    *out = p;
    return 1;
}

/* Try to find day number after a known month word.
 * Returns: 1=month+day emitted, 0=no day, -1=error */
static int try_day_after_month(ScannerObject *self, const char *text, const char *tl,
                               int ws, int we, int n, long month,
                               PyObject *results, int *out) {
    int p = we;
    if (p < n && (text[p]=='.'||text[p]==' '||text[p]=='-')) p++;
    if (p >= n || !ISDIG(text[p])) return 0;
    int day = text[p]-'0'; p++;
    if (p < n && ISDIG(text[p])) { day = day*10+(text[p]-'0'); p++; }
    int sv = p;
    if (p < n && text[p]==' ') p++;
    if (p+1 < n) {
        char c0=tolower((unsigned char)text[p]), c1=tolower((unsigned char)text[p+1]);
        if ((c0=='s'&&c1=='t')||(c0=='n'&&c1=='d')||(c0=='r'&&c1=='d')||(c0=='t'&&c1=='h')) p+=2;
        else p=sv;
    } else p=sv;
    if (!wb_end(text,p,n) || !VALID_DAY(day)) return 0;

    PyObject *kv[] = {PyLong_FromLong(month), PyLong_FromLong(day)};
    if (emit(results, vrel(self, ws, p, 2, kv, self->kn_month_day)) < 0) return -1;
    *out = p;
    return 1;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 11: Word classification — single unified-table lookup
 * ══════════════════════════════════════════════════════════════ */

static int classify_word(ScannerObject *self, const char *text, const char *tl,
                         int ws, int we, int n, PyObject *results, int *out_end) {
    int wlen = we - ws;
    char low[128];
    if (wlen >= 128) wlen = 127;
    for (int i = 0; i < wlen; i++) low[i] = tolower((unsigned char)text[ws+i]);
    low[wlen] = '\0';

    const UEntry *ue = ut_find(&self->words, low);

    if (ue) switch (ue->wtype) {

    case WT_NOW:
        if (n > 5) return 0;
        { PyObject *kv[] = {PyLong_FromLong(0), PyLong_FromLong(0),
                            PyLong_FromLong(0), PyLong_FromLong(0)};
          return emit(results, vrel(self, ws, we, 4, kv, self->kn_now)) < 0 ? -1 : 1; }

    case WT_TODAY:
        { PyObject *kv[] = {PyLong_FromLong(ue->ival)};
          return emit(results, vrel(self, ws, we, 1, kv, self->kn_days)) < 0 ? -1 : 1; }

    case WT_SEASON:
        { PyObject *lv = levels1(self->u_SEASON);
          PyObject *kv[] = {PyLong_FromLong(ue->ival), PyLong_FromLong(21), lv};
          return emit(results, vrel(self, ws, we, 3, kv, self->kn_month_day_lv)) < 0 ? -1 : 1; }

    case WT_QUARTER:
        { PyObject *lv = levels1(self->u_QUARTER);
          PyObject *kv[] = {PyLong_FromLong(ue->ival), PyLong_FromLong(1), lv};
          return emit(results, vrel(self, ws, we, 3, kv, self->kn_month_day_lv)) < 0 ? -1 : 1; }

    case WT_NOON_SA:
        { PyObject *lv = levels1(self->u_HOUR);
          PyObject *kv[] = {PyLong_FromLong(ue->ival), lv};
          return emit(results, vrel(self, ws, we, 2, kv, self->kn_hour_lv)) < 0 ? -1 : 1; }

    case WT_MONTH:
    case WT_MONTH_SHORT:
        { int md = try_day_after_month(self, text, tl, ws, we, n, ue->ival, results, out_end);
          if (md == 1) return 1;
          if (md < 0) return -1;
          /* standalone month */
          PyObject *kv[] = {PyLong_FromLong(ue->ival)};
          return emit(results, vrel(self, ws, we, 1, kv, self->kn_month)) < 0 ? -1 : 1; }

    case WT_WEEKDAY:
    case WT_WEEKDAY_SHORT:
        { PyObject *kv[] = {PyLong_FromLong(ue->ival)};
          return emit(results, vrel(self, ws, we, 1, kv, self->kn_weekday)) < 0 ? -1 : 1; }

    case WT_MODIFIER:
        return emit(results, vmod(self, low, ue->ival, ws, we)) < 0 ? -1 : 1;

    case WT_UNIT:
        return emit(results, vunit(self, ue->unit_idx, ws, we)) < 0 ? -1 : 1;

    case WT_ORDINAL:
        { /* am/pm lookahead */
          int after = we;
          while (after < n && text[after]==' ') after++;
          int pm = 0;
          int apm_end = match_apm(text, after, n, &pm);
          if (apm_end <= after) {
              if (ci_starts(tl, after, n, "in the afternoon"))      { pm=1; apm_end=after+16; }
              else if (ci_starts(tl, after, n, "in the morning"))   { pm=0; apm_end=after+14; }
          }
          if (apm_end > after && wb_end(text, apm_end, n)) {
              int hv = (int)ue->dval;
              int hoff = pm ? 12 : 0;
              if (hv == 12 && hoff) hoff = 0;
              int hour = (hoff + hv) % 24;
              PyObject *lv = levels2(self->u_MINUTE, self->u_HOUR);
              PyObject *kv[] = {PyLong_FromLong(hour), PyLong_FromLong(0),
                                PyLong_FromLong(0), lv};
              *out_end = apm_end;
              return emit(results, vrel(self, ws, apm_end, 4, kv, self->kn_hms_lv)) < 0 ? -1 : 1;
          }
          return emit(results, vord(self, ue->dval, ws, we)) < 0 ? -1 : 1;
        }

    case WT_AND:
        return emit(results, vand(self, low, ws, we)) < 0 ? -1 : 1;

    case WT_WHITELIST:
        return 0;
    } /* end switch */

    /* Unknown word → string separator */
    return emit_str(results, text, ws, we) < 0 ? -1 : 1;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 12: Token classification dispatch
 * ══════════════════════════════════════════════════════════════ */

static int classify_token(ScannerObject *self, int kind, const char *text, const char *tl,
                          int start, int end, int n, PyObject *results, int *out_end) {
    *out_end = end;

    if (kind == TOK_SKIP) return 0;
    if (kind == TOK_PUNCT) return emit_sent(self, results);

    if (kind == TOK_HMS_MICRO || kind == TOK_HMS || kind == TOK_HM || kind == TOK_HOUR_APM)
        return classify_time(self, text+start, end-start, start, end, results);

    if (kind == TOK_QUARTER_Q) {
        char low[4]; low[0]=tolower((unsigned char)text[start]); low[1]=text[start+1]; low[2]='\0';
        const UEntry *ue = ut_find(&self->words, low);
        if (ue && ue->wtype == WT_QUARTER) {
            PyObject *lv = levels1(self->u_QUARTER);
            PyObject *kv[] = {PyLong_FromLong(ue->ival), PyLong_FromLong(1), lv};
            return emit(results, vrel(self, start, end, 3, kv, self->kn_month_day_lv));
        }
        return emit_str(results, text, start, end);
    }

    if (kind == TOK_ISO_DATE) {
        int nums[3]={0},nc=0,i=start;
        while (i<end&&nc<3) {
            if (ISDIG(text[i])) { int v=0; while (i<end&&ISDIG(text[i])) { v=v*10+(text[i]-'0'); i++; } nums[nc++]=v; } else i++;
        }
        int yr=nums[0],mo=nums[1],dy=nums[2];
        if (VALID_YEAR(yr)&&VALID_MONTH(mo)&&VALID_DAY(dy)) {
            PyObject *kv[] = {PyLong_FromLong(yr), PyLong_FromLong(mo), PyLong_FromLong(dy)};
            return emit(results, vrel(self, start, end, 3, kv, self->kn_ymd));
        }
        return emit_str(results, text, start, end);
    }

    if (kind == TOK_ISO_COMPACT) {
        int yr=(text[start]-'0')*1000+(text[start+1]-'0')*100+(text[start+2]-'0')*10+(text[start+3]-'0');
        int mo=(text[start+4]-'0')*10+(text[start+5]-'0');
        int dy=(text[start+6]-'0')*10+(text[start+7]-'0');
        if (VALID_YEAR(yr)&&VALID_MONTH(mo)&&VALID_DAY(dy)) {
            PyObject *kv[] = {PyLong_FromLong(yr), PyLong_FromLong(mo), PyLong_FromLong(dy)};
            return emit(results, vrel(self, start, end, 3, kv, self->kn_ymd));
        }
        return emit_str(results, text, start, end);
    }

    if (kind == TOK_DD_MM_YYYY) {
        int nums[3]={0},nc=0,i=start;
        while (i<end&&nc<3) {
            if (ISDIG(text[i])) { int v=0; while (i<end&&ISDIG(text[i])) { v=v*10+(text[i]-'0'); i++; } nums[nc++]=v; } else i++;
        }
        int dy=nums[0],mo=nums[1],yr=nums[2];
        if (!self->dd_left_first) { int t2=dy; dy=mo; mo=t2; }
        if (VALID_YEAR(yr)&&VALID_MONTH(mo)&&VALID_DAY(dy)) {
            PyObject *kv[] = {PyLong_FromLong(yr), PyLong_FromLong(mo), PyLong_FromLong(dy)};
            return emit(results, vrel(self, start, end, 3, kv, self->kn_ymd));
        }
        { int t2=dy; dy=mo; mo=t2; }
        if (VALID_YEAR(yr)&&VALID_MONTH(mo)&&VALID_DAY(dy)) {
            PyObject *kv[] = {PyLong_FromLong(yr), PyLong_FromLong(mo), PyLong_FromLong(dy)};
            return emit(results, vrel(self, start, end, 3, kv, self->kn_ymd));
        }
        return emit_str(results, text, start, end);
    }

    if (kind == TOK_YEAR4) {
        int yr=0; for (int i=start;i<end;i++) yr=yr*10+(text[i]-'0');
        if (VALID_YEAR(yr)) {
            PyObject *kv[] = {PyLong_FromLong(yr)};
            return emit(results, vrel(self, start, end, 1, kv, self->kn_year));
        }
        return emit_str(results, text, start, end);
    }

    if (kind == TOK_ORDINAL_NUM) {
        int num=0; for (int i=start;i<end;i++) if (ISDIG(text[i])) num=num*10+(text[i]-'0');
        return emit(results, vord(self, (double)num, start, end));
    }

    if (kind == TOK_NUMBER) {
        char buf[64]; int bl=end-start; if (bl>=64) bl=63;
        memcpy(buf, text+start, bl); buf[bl]='\0';
        for (int i=0;i<bl;i++) if (buf[i]==',') buf[i]='.';
        return emit(results, vord(self, strtod(buf,NULL), start, end));
    }

    if (kind == TOK_WORD)
        return classify_word(self, text, tl, start, end, n, results, out_end);

    return emit_str(results, text, start, end);
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 13: Main scan method
 * ══════════════════════════════════════════════════════════════ */

static PyObject *Scanner_scan(ScannerObject *self, PyObject *args) {
    const char *text;
    Py_ssize_t text_len;
    if (!PyArg_ParseTuple(args, "s#", &text, &text_len)) return NULL;

    int n = (int)text_len;
    /* Build modified text (en-dash → '-') and lowercase copy */
    char *mt = (char *)malloc(n + 1);
    char *tl = (char *)malloc(n + 1);
    if (!mt || !tl) { free(mt); free(tl); return PyErr_NoMemory(); }
    {
        int j = 0;
        for (int i = 0; i < n; ) {
            unsigned char c = (unsigned char)text[i];
            if (c==0xE2 && i+2<n && (unsigned char)text[i+1]==0x80 && (unsigned char)text[i+2]==0x93) {
                mt[j] = '-'; tl[j] = '-'; j++; i += 3;
            } else {
                mt[j] = text[i]; tl[j] = tolower(c); j++; i++;
            }
        }
        mt[j] = '\0'; tl[j] = '\0'; n = j;
    }

    PyObject *results = PyList_New(0);
    if (!results) { free(mt); free(tl); return NULL; }

    int pos = 0;
    while (pos < n) {
        int consumed = do_try_multiword(self, tl, mt, pos, n, results);
        if (consumed) { pos += consumed; continue; }

        int kind, tok_end;
        if (!tokenize_at(mt, pos, n, &kind, &tok_end)) {
            emit_str(results, mt, pos, pos+1);
            pos++; continue;
        }

        if ((kind==TOK_NUMBER || kind==TOK_ORDINAL_NUM) && tok_end < n) {
            int merged_end;
            if (do_try_number_month(self, mt, tl, pos, tok_end, n, results, &merged_end)) {
                pos = merged_end; continue;
            }
        }

        int out_end = tok_end;
        classify_token(self, kind, mt, tl, pos, tok_end, n, results, &out_end);
        pos = out_end;
    }

    free(mt); free(tl);
    return results;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 14: Build unified table from Python locale dicts
 * ══════════════════════════════════════════════════════════════ */

/* Map unit-name string to index (0..7) */
static int uname_to_idx(const char *s) {
    switch (s[0]) {
    case 'Y': return 0; /* YEAR */
    case 'M':
        if (s[1]=='O') return 1; /* MONTH */
        if (s[1]=='I') return (s[5]=='S') ? 7 : 5; /* MICROSECOND or MINUTE */
        break;
    case 'W': return 2; /* WEEK */
    case 'D': return 3; /* DAY */
    case 'H': return 4; /* HOUR */
    case 'S': return 6; /* SECOND */
    }
    return 0;
}

static void build_ut_int(UnifiedTable *ut, PyObject *d, int wtype) {
    PyObject *k, *v; Py_ssize_t p = 0;
    while (PyDict_Next(d, &p, &k, &v)) {
        const char *ks = PyUnicode_AsUTF8(k);
        UEntry *e = ut_slot(ut, ks);
        e->wtype = wtype;
        e->ival = PyLong_Check(v) ? PyLong_AsLong(v) : (long)PyFloat_AsDouble(v);
    }
}

static void build_ut_int2(UnifiedTable *ut, PyObject *d, int wtype) {
    PyObject *k, *v; Py_ssize_t p = 0;
    while (PyDict_Next(d, &p, &k, &v)) {
        const char *ks = PyUnicode_AsUTF8(k);
        UEntry *e = ut_slot(ut, ks);
        e->wtype = wtype;
        e->ival  = PyLong_AsLong(PyTuple_GET_ITEM(v, 0));
        e->ival2 = PyLong_AsLong(PyTuple_GET_ITEM(v, 1));
    }
}

static void build_ut_dbl(UnifiedTable *ut, PyObject *d, int wtype) {
    PyObject *k, *v; Py_ssize_t p = 0;
    while (PyDict_Next(d, &p, &k, &v)) {
        const char *ks = PyUnicode_AsUTF8(k);
        UEntry *e = ut_slot(ut, ks);
        e->wtype = wtype;
        e->dval = PyLong_Check(v) ? (double)PyLong_AsLong(v) : PyFloat_AsDouble(v);
    }
}

static void build_ut_str_units(UnifiedTable *ut, PyObject *d, int wtype) {
    PyObject *k, *v; Py_ssize_t p = 0;
    while (PyDict_Next(d, &p, &k, &v)) {
        const char *ks = PyUnicode_AsUTF8(k);
        const char *vs = PyUnicode_AsUTF8(v);
        UEntry *e = ut_slot(ut, ks);
        e->wtype = wtype;
        e->unit_idx = uname_to_idx(vs);
    }
}

static void build_ut_marker(UnifiedTable *ut, PyObject *lst, int wtype) {
    Py_ssize_t n = PyList_GET_SIZE(lst);
    for (Py_ssize_t i = 0; i < n; i++) {
        const char *s = PyUnicode_AsUTF8(PyList_GET_ITEM(lst, i));
        UEntry *e = ut_slot(ut, s);
        e->wtype = wtype;
    }
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 15: Init / dealloc
 * ══════════════════════════════════════════════════════════════ */

static PyObject *Scanner_new(PyTypeObject *type, PyObject *a, PyObject *k) {
    ScannerObject *self = (ScannerObject *)type->tp_alloc(type, 0);
    if (self) { memset(&self->words, 0, sizeof(ScannerObject) - offsetof(ScannerObject, words)); }
    return (PyObject *)self;
}

#define GET_DICT(name) PyDict_GetItemString(ld, name)

static int Scanner_init(ScannerObject *self, PyObject *args, PyObject *kwds) {
    PyObject *ld;
    if (!PyArg_ParseTuple(args, "O", &ld)) return -1;

    /* ── Build unified table ─────────────────────────────────
     * Insert lowest priority first; higher overwrites.         */
    ut_init(&self->words);
    build_ut_marker (&self->words, GET_DICT("whitelist"),      WT_WHITELIST);
    build_ut_marker (&self->words, GET_DICT("and"),            WT_AND);
    build_ut_dbl    (&self->words, GET_DICT("ordinal_numbers"),WT_ORDINAL);
    build_ut_str_units(&self->words, GET_DICT("units"),        WT_UNIT);
    build_ut_int    (&self->words, GET_DICT("modifiers"),      WT_MODIFIER);
    build_ut_int    (&self->words, GET_DICT("weekday_shorts"), WT_WEEKDAY_SHORT);
    build_ut_int    (&self->words, GET_DICT("weekday"),        WT_WEEKDAY);
    build_ut_int    (&self->words, GET_DICT("months_shorts"),  WT_MONTH_SHORT);
    build_ut_int    (&self->words, GET_DICT("months"),         WT_MONTH);
    build_ut_int2   (&self->words, GET_DICT("noon_standalone"),WT_NOON_SA);
    build_ut_int    (&self->words, GET_DICT("quarters"),       WT_QUARTER);
    build_ut_int    (&self->words, GET_DICT("seasons"),        WT_SEASON);
    build_ut_int    (&self->words, GET_DICT("today_tomorrow"), WT_TODAY);
    build_ut_marker (&self->words, GET_DICT("now"),            WT_NOW);

    PyObject *dd = GET_DICT("dd_left_first");
    self->dd_left_first = dd && PyObject_IsTrue(dd);

    /* Build phrase arrays */
    self->ph_in_the       = phrases_from_pylist(GET_DICT("in_the"),       &self->n_in_the);
    self->ph_today_multi  = phrases_from_pylist(GET_DICT("today_multi"),  &self->n_today_multi);
    self->ph_quarter_multi= phrases_from_pylist(GET_DICT("quarter_multi"),&self->n_quarter_multi);
    self->ph_ordinal_multi= phrases_from_pylist(GET_DICT("ordinal_multi"),&self->n_ordinal_multi);

    /* ── Import and cache Meta* classes + __init__ functions ── */
    PyObject *cls_mod = PyImport_ImportModule("metadate.classes");
    if (!cls_mod) return -1;
    self->cls_Rel  = PyObject_GetAttrString(cls_mod, "MetaRelative");
    self->cls_Ord  = PyObject_GetAttrString(cls_mod, "MetaOrdinal");
    self->cls_Unit = PyObject_GetAttrString(cls_mod, "MetaUnit");
    self->cls_Mod  = PyObject_GetAttrString(cls_mod, "MetaModifier");
    self->cls_Rng  = PyObject_GetAttrString(cls_mod, "MetaRange");
    self->cls_And  = PyObject_GetAttrString(cls_mod, "MetaAnd");
    self->cls_FromScanner = PyObject_GetAttrString(self->cls_Rel, "_from_scanner");
    if (!self->cls_FromScanner) { Py_DECREF(cls_mod); return -1; }
    Py_DECREF(cls_mod);

    /* ── Import and cache relativedelta class ─────────────── */
    PyObject *du_mod = PyImport_ImportModule("dateutil.relativedelta");
    if (!du_mod) return -1;
    self->cls_rd = PyObject_GetAttrString(du_mod, "relativedelta");
    Py_DECREF(du_mod);
    if (!self->cls_rd) return -1;

    /* ── Cache Units enum values ────────────────────────────── */
    PyObject *util_mod = PyImport_ImportModule("metadate.utils");
    if (!util_mod) return -1;
    PyObject *Units = PyObject_GetAttrString(util_mod, "Units");
    Py_DECREF(util_mod);
    self->u_YEAR    = PyObject_GetAttrString(Units, "YEAR");
    self->u_SEASON  = PyObject_GetAttrString(Units, "SEASON");
    self->u_QUARTER = PyObject_GetAttrString(Units, "QUARTER");
    self->u_MONTH   = PyObject_GetAttrString(Units, "MONTH");
    self->u_WEEK    = PyObject_GetAttrString(Units, "WEEK");
    self->u_DAY     = PyObject_GetAttrString(Units, "DAY");
    self->u_HOUR    = PyObject_GetAttrString(Units, "HOUR");
    self->u_MINUTE  = PyObject_GetAttrString(Units, "MINUTE");
    self->u_SECOND  = PyObject_GetAttrString(Units, "SECOND");
    self->u_MICRO   = PyObject_GetAttrString(Units, "MICROSECOND");
    Py_DECREF(Units);

    /* ── Cache unit-name strings for MetaUnit ─────────────── */
    self->uname[0] = PyUnicode_InternFromString("YEAR");
    self->uname[1] = PyUnicode_InternFromString("MONTH");
    self->uname[2] = PyUnicode_InternFromString("WEEK");
    self->uname[3] = PyUnicode_InternFromString("DAY");
    self->uname[4] = PyUnicode_InternFromString("HOUR");
    self->uname[5] = PyUnicode_InternFromString("MINUTE");
    self->uname[6] = PyUnicode_InternFromString("SECOND");
    self->uname[7] = PyUnicode_InternFromString("MICROSECOND");

    /* ── Build kwnames tuples for vectorcall patterns ──────── */
    PyObject *ky  = PyUnicode_InternFromString("year");
    PyObject *kmo = PyUnicode_InternFromString("month");
    PyObject *kd  = PyUnicode_InternFromString("day");
    PyObject *kh  = PyUnicode_InternFromString("hour");
    PyObject *kmi = PyUnicode_InternFromString("minute");
    PyObject *ks  = PyUnicode_InternFromString("second");
    PyObject *kus = PyUnicode_InternFromString("microsecond");
    PyObject *kwd = PyUnicode_InternFromString("weekday");
    PyObject *kds = PyUnicode_InternFromString("days");
    PyObject *khs = PyUnicode_InternFromString("hours");
    PyObject *kms = PyUnicode_InternFromString("minutes");
    PyObject *kss = PyUnicode_InternFromString("seconds");
    PyObject *klv = PyUnicode_InternFromString("levels");

    self->kn_days         = PyTuple_Pack(1, kds);
    self->kn_now          = PyTuple_Pack(4, kds, khs, kms, kss);
    self->kn_month        = PyTuple_Pack(1, kmo);
    self->kn_month_day    = PyTuple_Pack(2, kmo, kd);
    self->kn_month_day_lv = PyTuple_Pack(3, kmo, kd, klv);
    self->kn_year         = PyTuple_Pack(1, ky);
    self->kn_ymd          = PyTuple_Pack(3, ky, kmo, kd);
    self->kn_weekday      = PyTuple_Pack(1, kwd);
    self->kn_hour         = PyTuple_Pack(1, kh);
    self->kn_hour_lv      = PyTuple_Pack(2, kh, klv);
    self->kn_hour_min     = PyTuple_Pack(2, kh, kmi);
    self->kn_hms          = PyTuple_Pack(3, kh, kmi, ks);
    self->kn_hmsm         = PyTuple_Pack(4, kh, kmi, ks, kus);
    self->kn_hms_lv       = PyTuple_Pack(4, kh, kmi, ks, klv);
    self->kn_day          = PyTuple_Pack(1, kd);

    /* PyTuple_Pack INCREFs items; drop our local refs */
    Py_DECREF(ky);  Py_DECREF(kmo); Py_DECREF(kd);  Py_DECREF(kh);
    Py_DECREF(kmi); Py_DECREF(ks);  Py_DECREF(kus); Py_DECREF(kwd);
    Py_DECREF(kds); Py_DECREF(khs); Py_DECREF(kms); Py_DECREF(kss);
    Py_DECREF(klv);

    self->str_SENT = PyUnicode_InternFromString("SENT");

    return 0;
}

static void Scanner_dealloc(ScannerObject *self) {
    ut_free(&self->words);

    phrases_free(self->ph_in_the,       self->n_in_the);
    phrases_free(self->ph_today_multi,  self->n_today_multi);
    phrases_free(self->ph_quarter_multi,self->n_quarter_multi);
    phrases_free(self->ph_ordinal_multi,self->n_ordinal_multi);

    Py_XDECREF(self->cls_Rel);  Py_XDECREF(self->cls_Ord);
    Py_XDECREF(self->cls_Unit); Py_XDECREF(self->cls_Mod);
    Py_XDECREF(self->cls_Rng);  Py_XDECREF(self->cls_And);
    Py_XDECREF(self->cls_FromScanner); Py_XDECREF(self->cls_rd);

    Py_XDECREF(self->u_YEAR);  Py_XDECREF(self->u_SEASON);
    Py_XDECREF(self->u_QUARTER); Py_XDECREF(self->u_MONTH);
    Py_XDECREF(self->u_WEEK);  Py_XDECREF(self->u_DAY);
    Py_XDECREF(self->u_HOUR);  Py_XDECREF(self->u_MINUTE);
    Py_XDECREF(self->u_SECOND);Py_XDECREF(self->u_MICRO);

    for (int i = 0; i < 8; i++) Py_XDECREF(self->uname[i]);

    Py_XDECREF(self->kn_days);  Py_XDECREF(self->kn_now);
    Py_XDECREF(self->kn_month); Py_XDECREF(self->kn_month_day);
    Py_XDECREF(self->kn_month_day_lv); Py_XDECREF(self->kn_year);
    Py_XDECREF(self->kn_ymd);   Py_XDECREF(self->kn_weekday);
    Py_XDECREF(self->kn_hour);  Py_XDECREF(self->kn_hour_lv);
    Py_XDECREF(self->kn_hour_min); Py_XDECREF(self->kn_hms);
    Py_XDECREF(self->kn_hmsm);  Py_XDECREF(self->kn_hms_lv);
    Py_XDECREF(self->kn_day);

    Py_XDECREF(self->str_SENT);

    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 16: Type and module definition
 * ══════════════════════════════════════════════════════════════ */

static PyMethodDef Scanner_methods[] = {
    {"scan", (PyCFunction)Scanner_scan, METH_VARARGS, "Scan text → list of Meta* objects."},
    {NULL}
};

static PyTypeObject ScannerType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "_cscanner.Scanner",
    .tp_basicsize = sizeof(ScannerObject),
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_new       = Scanner_new,
    .tp_init      = (initproc)Scanner_init,
    .tp_dealloc   = (destructor)Scanner_dealloc,
    .tp_methods   = Scanner_methods,
    .tp_doc       = "C-accelerated date scanner (v3: unified table + vectorcall).",
};

static PyModuleDef cscanner_module = {
    PyModuleDef_HEAD_INIT, "_cscanner",
    "C-accelerated date scanning module.", -1, NULL,
};

PyMODINIT_FUNC PyInit__cscanner(void) {
    if (PyType_Ready(&ScannerType) < 0) return NULL;
    PyObject *m = PyModule_Create(&cscanner_module);
    if (!m) return NULL;
    Py_INCREF(&ScannerType);
    if (PyModule_AddObject(m, "Scanner", (PyObject *)&ScannerType) < 0) {
        Py_DECREF(&ScannerType); Py_DECREF(m); return NULL;
    }
    return m;
}
