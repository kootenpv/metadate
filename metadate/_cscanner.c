/*
 * _cscanner.c — C-accelerated date scanner for metadatez.
 *
 * v2: Creates Meta* objects directly (no intermediate tuples).
 *     Uses C hash tables instead of Python dicts for locale lookups.
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
 * SECTION 1: C Hash Table (FNV-1a, open addressing)
 * ══════════════════════════════════════════════════════════════ */

#define HT_CAP  128
#define HT_MASK (HT_CAP - 1)

typedef struct {
    char           *key;
    long            ival;
    long            ival2;       /* noon second value */
    double          dval;
    char            sval[16];    /* unit name strings */
    unsigned char   occupied;
} HTEntry;

typedef struct { HTEntry entries[HT_CAP]; } HashTable;

static unsigned int fnv1a(const char *s) {
    unsigned int h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

static void ht_init(HashTable *ht) { memset(ht, 0, sizeof(*ht)); }

static HTEntry *ht_slot(HashTable *ht, const char *key) {
    unsigned int idx = fnv1a(key) & HT_MASK;
    while (ht->entries[idx].occupied) {
        if (strcmp(ht->entries[idx].key, key) == 0) return &ht->entries[idx];
        idx = (idx + 1) & HT_MASK;
    }
    ht->entries[idx].key = strdup(key);
    ht->entries[idx].occupied = 1;
    return &ht->entries[idx];
}

static const HTEntry *ht_find(const HashTable *ht, const char *key) {
    unsigned int idx = fnv1a(key) & HT_MASK;
    int probe = 0;
    while (ht->entries[idx].occupied && probe < HT_CAP) {
        if (strcmp(ht->entries[idx].key, key) == 0) return &ht->entries[idx];
        idx = (idx + 1) & HT_MASK;
        probe++;
    }
    return NULL;
}

static void ht_set_int  (HashTable *h, const char *k, long v)    { ht_slot(h,k)->ival = v; }
static void ht_set_dbl  (HashTable *h, const char *k, double v)  { ht_slot(h,k)->dval = v; }
static void ht_set_int2 (HashTable *h, const char *k, long a, long b) {
    HTEntry *e = ht_slot(h,k); e->ival = a; e->ival2 = b;
}
static void ht_set_str  (HashTable *h, const char *k, const char *v) {
    HTEntry *e = ht_slot(h,k);
    strncpy(e->sval, v, 15); e->sval[15] = '\0';
}
static void ht_mark     (HashTable *h, const char *k) { ht_slot(h,k); }

static int ht_get_int(const HashTable *h, const char *k, long *out) {
    const HTEntry *e = ht_find(h,k); if (!e) return 0; *out = e->ival; return 1;
}
static int ht_get_dbl(const HashTable *h, const char *k, double *out) {
    const HTEntry *e = ht_find(h,k); if (!e) return 0; *out = e->dval; return 1;
}
static int ht_get_str(const HashTable *h, const char *k, const char **out) {
    const HTEntry *e = ht_find(h,k); if (!e) return 0; *out = e->sval; return 1;
}
static int ht_get_int2(const HashTable *h, const char *k, long *a, long *b) {
    const HTEntry *e = ht_find(h,k); if (!e) return 0; *a = e->ival; *b = e->ival2; return 1;
}
static int ht_has(const HashTable *h, const char *k) { return ht_find(h,k) != NULL; }

static void ht_free(HashTable *ht) {
    for (int i = 0; i < HT_CAP; i++)
        if (ht->entries[i].key) { free(ht->entries[i].key); ht->entries[i].key = NULL; }
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

    /* C hash tables */
    HashTable months, months_shorts, weekday, weekday_shorts;
    HashTable modifiers, units, ordinal_numbers;
    HashTable today_tomorrow, seasons, quarters;
    HashTable noon, noon_standalone, whitelist, now_words, and_words;
    int dd_left_first;

    /* Phrase arrays (sorted longest-first) */
    Phrase *ph_in_the;       int n_in_the;
    Phrase *ph_today_multi;  int n_today_multi;
    Phrase *ph_quarter_multi;int n_quarter_multi;
    Phrase *ph_ordinal_multi;int n_ordinal_multi;

    /* Cached Python class references (strong) */
    PyObject *cls_MetaRelative, *cls_MetaOrdinal, *cls_MetaUnit;
    PyObject *cls_MetaModifier, *cls_MetaRange, *cls_MetaAnd;
    PyObject *str_SENT;   /* cached "SENT" string */

    /* Cached Units enum values (strong) */
    PyObject *u_YEAR, *u_SEASON, *u_QUARTER, *u_MONTH, *u_WEEK;
    PyObject *u_DAY, *u_HOUR, *u_MINUTE, *u_SECOND, *u_MICRO;

    /* Cached keyword strings (interned, strong) */
    PyObject *kw_year, *kw_month, *kw_day, *kw_hour, *kw_minute, *kw_second;
    PyObject *kw_microsecond, *kw_weekday;
    PyObject *kw_days, *kw_hours, *kw_minutes, *kw_seconds, *kw_microseconds;
    PyObject *kw_levels, *kw_modifier;
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

/* Dict-set helpers using cached interned key objects */
static void kw_int(PyObject *d, PyObject *key, long val) {
    PyObject *v = PyLong_FromLong(val);
    PyDict_SetItem(d, key, v);
    Py_DECREF(v);
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 5: Meta* object factories
 *
 * make_relative steals refs to kwargs and levels.
 * All factories return a new reference (caller must DECREF or
 * hand off to PyList_Append + DECREF).
 * ══════════════════════════════════════════════════════════════ */

static PyObject *make_relative(ScannerObject *self, int start, int end,
                                PyObject *kwargs, PyObject *levels, int modifier) {
    PyObject *span = Py_BuildValue("(ii)", start, end);
    if (levels) {
        PyDict_SetItem(kwargs, self->kw_levels, levels);
        Py_DECREF(levels);
    }
    if (modifier) {
        PyDict_SetItem(kwargs, self->kw_modifier, Py_True);
    }
    PyObject *args = PyTuple_Pack(1, span);
    PyObject *obj = PyObject_Call(self->cls_MetaRelative, args, kwargs);
    Py_DECREF(args);
    Py_DECREF(span);
    Py_DECREF(kwargs);
    return obj;
}

static PyObject *make_ordinal(ScannerObject *self, double amount, int s, int e) {
    PyObject *a = PyFloat_FromDouble(amount);
    PyObject *sp = Py_BuildValue("(ii)", s, e);
    PyObject *obj = PyObject_CallFunctionObjArgs(self->cls_MetaOrdinal, a, sp, NULL);
    Py_DECREF(a); Py_DECREF(sp);
    return obj;
}

static PyObject *make_unit(ScannerObject *self, const char *unit, int s, int e) {
    PyObject *u = PyUnicode_FromString(unit);
    PyObject *sp = Py_BuildValue("(ii)", s, e);
    PyObject *obj = PyObject_CallFunctionObjArgs(self->cls_MetaUnit, u, sp, NULL);
    Py_DECREF(u); Py_DECREF(sp);
    return obj;
}

static PyObject *make_modifier_obj(ScannerObject *self, const char *word, long val, int s, int e) {
    PyObject *x = PyUnicode_FromString(word);
    PyObject *v = PyLong_FromLong(val);
    PyObject *sp = Py_BuildValue("(ii)", s, e);
    PyObject *obj = PyObject_CallFunctionObjArgs(self->cls_MetaModifier, x, v, sp, NULL);
    Py_DECREF(x); Py_DECREF(v); Py_DECREF(sp);
    return obj;
}

static PyObject *make_range(ScannerObject *self, const char *word, int s, int e) {
    PyObject *x = PyUnicode_FromString(word);
    PyObject *sp = Py_BuildValue("(ii)", s, e);
    PyObject *obj = PyObject_CallFunctionObjArgs(self->cls_MetaRange, x, sp, NULL);
    Py_DECREF(x); Py_DECREF(sp);
    return obj;
}

static PyObject *make_and(ScannerObject *self, const char *word, int s, int e) {
    PyObject *x = PyUnicode_FromString(word);
    PyObject *sp = Py_BuildValue("(ii)", s, e);
    PyObject *obj = PyObject_CallFunctionObjArgs(self->cls_MetaAnd, x, sp, NULL);
    Py_DECREF(x); Py_DECREF(sp);
    return obj;
}

/* Append a Meta* object to results list. Returns 0 on success, -1 on error. */
static int emit(PyObject *list, PyObject *obj) {
    if (!obj) return -1;
    int rc = PyList_Append(list, obj);
    Py_DECREF(obj);
    return rc;
}

/* Append a string to results list. */
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

/* Build a levels set with one element */
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
 * SECTION 6: Time parsing
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
    PyObject *kw = PyDict_New();
    kw_int(kw, self->kw_hour, hour);
    int has_dot = (memchr(raw, '.', rawlen) != NULL);
    if (has_dot && micro) {
        kw_int(kw, self->kw_minute, minute);
        kw_int(kw, self->kw_second, second);
        kw_int(kw, self->kw_microsecond, micro);
    } else if (second) {
        kw_int(kw, self->kw_minute, minute);
        kw_int(kw, self->kw_second, second);
    } else if (minute) {
        kw_int(kw, self->kw_minute, minute);
    }
    return emit(results, make_relative(self, s, e, kw, NULL, 0));
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 7: Tokenizer pattern matchers
 * (Pure C, unchanged from v1)
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
    if (!ISDIG(t[p])) return 0; p++;
    if (p<n && ISDIG(t[p])) p++;
    if (p>=n||(t[p]!=':'&&tolower((unsigned char)t[p])!='h')) return 0; p++;
    if (p+1>=n||!ISDIG(t[p])||!ISDIG(t[p+1])) return 0; p+=2;
    if (p>=n||(t[p]!=':'&&tolower((unsigned char)t[p])!='m')) return 0; p++;
    if (p+1>=n||!ISDIG(t[p])||!ISDIG(t[p+1])) return 0; p+=2;
    if (p>=n||t[p]!='.') return 0; p++;
    if (p>=n||!ISDIG(t[p])) return 0;
    while (p<n && ISDIG(t[p])) p++;
    return wb_end(t,p,n) ? p-s : 0;
}

static int try_hms(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int s=p;
    if (!ISDIG(t[p])) return 0; p++;
    if (p<n && ISDIG(t[p])) p++;
    if (p>=n||(t[p]!=':'&&tolower((unsigned char)t[p])!='h')) return 0; p++;
    if (p+1>=n||!ISDIG(t[p])||!ISDIG(t[p+1])) return 0; p+=2;
    if (p>=n||(t[p]!=':'&&tolower((unsigned char)t[p])!='m')) return 0; p++;
    if (p+1>=n||!ISDIG(t[p])||!ISDIG(t[p+1])) return 0; p+=2;
    int sv=p;
    if (p<n&&t[p]==' ') p++;
    int pm; int ap=match_apm(t,p,n,&pm);
    if (ap>p) p=ap; else p=sv;
    return wb_end(t,p,n) ? p-s : 0;
}

static int try_hm(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int s=p;
    if (!ISDIG(t[p])) return 0; p++;
    if (p<n && ISDIG(t[p])) p++;
    if (p>=n||(t[p]!=':'&&tolower((unsigned char)t[p])!='h'&&t[p]!='\'')) return 0; p++;
    if (p+1>=n||!ISDIG(t[p])||!ISDIG(t[p+1])) return 0; p+=2;
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
    if (!ISDIG(t[p])) return 0; p++;
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
    if (t[q]!='-'&&t[q]!='/') return 0; q++;
    int ms=q;
    if (q>=n||!ISDIG(t[q])) return 0;
    if (t[q]=='0') { q++; if (q>=n||!ISDIG(t[q])) return 0; q++; }
    else if (t[q]=='1') { q++; if (q<n&&ISDIG(t[q])&&t[q]<='2') q++; }
    else if (t[q]>='2'&&t[q]<='9') q++;
    else return 0;
    int month=0; for (int i=ms;i<q;i++) month=month*10+(t[i]-'0');
    if (!VALID_MONTH(month)) return 0;
    if (q>=n||(t[q]!='-'&&t[q]!='/')) return 0; q++;
    if (q>=n||!ISDIG(t[q])) return 0;
    int ds=q; q++; if (q<n&&ISDIG(t[q])) q++;
    int day=0; for (int i=ds;i<q;i++) day=day*10+(t[i]-'0');
    if (!VALID_DAY(day)) return 0;
    return wb_end(t,q,n) ? q-p : 0;
}

static int try_iso_compact(const char *t, int p, int n) {
    if (!wb_start(t,p)||p+8>n) return 0;
    if (t[p]!='1'&&t[p]!='2') return 0;
    for (int i=1;i<8;i++) if (!ISDIG(t[p+i])) return 0;
    int mo=(t[p+4]-'0')*10+(t[p+5]-'0'), dy=(t[p+6]-'0')*10+(t[p+7]-'0');
    if (!VALID_MONTH(mo)||!VALID_DAY(dy)) return 0;
    return wb_end(t,p+8,n) ? 8 : 0;
}

static int try_dd_mm_yyyy(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int q=p;
    if (q>=n||!ISDIG(t[q])) return 0; q++;
    if (q<n&&ISDIG(t[q])) q++;
    if (q>=n||(t[q]!='/'&&t[q]!=' ')) return 0; q++;
    if (q>=n||!ISDIG(t[q])) return 0; q++;
    if (q<n&&ISDIG(t[q])) q++;
    if (q>=n||(t[q]!='/'&&t[q]!=' ')) return 0; q++;
    if (q+3>=n||((t[q]!='1')&&(t[q]!='2'))) return 0;
    for (int i=1;i<4;i++) if (!ISDIG(t[q+i])) return 0;
    q+=4;
    if (!wb_end(t,q,n)) return 0;
    /* validate */
    int day=0,mo=0,yr=0; int i=p;
    while (ISDIG(t[i])) { day=day*10+(t[i]-'0'); i++; } i++;
    while (ISDIG(t[i])) { mo=mo*10+(t[i]-'0'); i++; } i++;
    while (i<q&&ISDIG(t[i])) { yr=yr*10+(t[i]-'0'); i++; }
    if (!VALID_DAY(day)||!VALID_MONTH(mo)||!VALID_YEAR(yr)) return 0;
    return q-p;
}

static int try_year4(const char *t, int p, int n) {
    if (!wb_start(t,p)||p+4>n) return 0;
    if (t[p]!='1'&&t[p]!='2') return 0;
    if (!ISDIG(t[p+1])||!ISDIG(t[p+2])||!ISDIG(t[p+3])) return 0;
    if (!wb_end(t,p+4,n)) return 0;
    int y=(t[p]-'0')*1000+(t[p+1]-'0')*100+(t[p+2]-'0')*10+(t[p+3]-'0');
    return VALID_YEAR(y) ? 4 : 0;
}

static int try_quarter_q(const char *t, int p, int n) {
    if (!wb_start(t,p)||p+2>n) return 0;
    if (tolower((unsigned char)t[p])!='q') return 0;
    if (t[p+1]<'1'||t[p+1]>'4') return 0;
    return wb_end(t,p+2,n) ? 2 : 0;
}

static int try_ordinal_num(const char *t, int p, int n) {
    if (!wb_start(t,p)) return 0;
    int q=p; if (!ISDIG(t[q])) return 0; q++;
    if (q<n&&ISDIG(t[q])) q++;
    if (q+1>=n) return 0;
    char c0=tolower((unsigned char)t[q]),c1=tolower((unsigned char)t[q+1]);
    if (!((c0=='s'&&c1=='t')||(c0=='n'&&c1=='d')||(c0=='r'&&c1=='d')||(c0=='t'&&c1=='h')))
        return 0;
    q+=2;
    return wb_end(t,q,n) ? q-p : 0;
}

static int try_number(const char *t, int p, int n) {
    if (!ISDIG(t[p])) return 0;
    int q=p; while (q<n&&ISDIG(t[q])) q++;
    if (q<n&&(t[q]==','||t[q]=='.')) { q++; while (q<n&&ISDIG(t[q])) q++; }
    return q-p;
}

static int try_word(const char *t, int p, int n) {
    if (!ISALP(t[p])) return 0;
    int q=p; while (q<n&&ISALP(t[q])) q++;
    return q-p;
}

static int tokenize_at(const char *t, int pos, int n, int *kind, int *end) {
    unsigned char c = (unsigned char)t[pos];
    int r;
    if (ISDIG(c)) {
        if (wb_start(t,pos)) {
            if ((r=try_hms_micro(t,pos,n))) { *kind=TOK_HMS_MICRO; *end=pos+r; return 1; }
            if ((r=try_hms(t,pos,n)))       { *kind=TOK_HMS;       *end=pos+r; return 1; }
            if ((r=try_hm(t,pos,n)))        { *kind=TOK_HM;        *end=pos+r; return 1; }
            if ((r=try_hour_apm(t,pos,n)))  { *kind=TOK_HOUR_APM;  *end=pos+r; return 1; }
            if ((r=try_iso_date(t,pos,n)))  { *kind=TOK_ISO_DATE;  *end=pos+r; return 1; }
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
 * SECTION 8: Multi-word matching
 * ══════════════════════════════════════════════════════════════ */

static int do_try_multiword(ScannerObject *self, const char *tl, const char *mt,
                            int pos, int n, PyObject *results) {
    const char *chunk = tl + pos;
    int rem = n - pos;

    /* "and a half" */
    if (rem >= 10 && memcmp(chunk, "and a half", 10) == 0) {
        return emit(results, make_ordinal(self, 0.5, pos, pos+10)) < 0 ? 0 : 10;
    }

    /* "in the", "over the", etc — must be followed by space */
    for (int i = 0; i < self->n_in_the; i++) {
        int pl = self->ph_in_the[i].len;
        if (rem > pl && memcmp(chunk, self->ph_in_the[i].str, pl) == 0 && chunk[pl] == ' ') {
            if (emit(results, make_range(self, self->ph_in_the[i].str, pos, pos+pl)) < 0) return 0;
            return pl + 1;
        }
    }

    /* Multi-word today/tomorrow */
    for (int i = 0; i < self->n_today_multi; i++) {
        int pl = self->ph_today_multi[i].len;
        if (rem >= pl && memcmp(chunk, self->ph_today_multi[i].str, pl) == 0) {
            if (pos+pl >= n || !ISALP((unsigned char)tl[pos+pl])) {
                long days; ht_get_int(&self->today_tomorrow, self->ph_today_multi[i].str, &days);
                PyObject *kw = PyDict_New();
                kw_int(kw, self->kw_days, days);
                if (emit(results, make_relative(self, pos, pos+pl, kw, NULL, 0)) < 0) return 0;
                return pl;
            }
        }
    }

    /* Multi-word quarters */
    for (int i = 0; i < self->n_quarter_multi; i++) {
        int pl = self->ph_quarter_multi[i].len;
        if (rem >= pl && memcmp(chunk, self->ph_quarter_multi[i].str, pl) == 0) {
            if (pos+pl >= n || !ISALP((unsigned char)tl[pos+pl])) {
                long mo; ht_get_int(&self->quarters, self->ph_quarter_multi[i].str, &mo);
                PyObject *kw = PyDict_New();
                kw_int(kw, self->kw_month, mo);
                kw_int(kw, self->kw_day, 1);
                if (emit(results, make_relative(self, pos, pos+pl, kw,
                                                levels1(self->u_QUARTER), 0)) < 0) return 0;
                return pl;
            }
        }
    }

    /* Multi-word ordinals ("a couple") */
    for (int i = 0; i < self->n_ordinal_multi; i++) {
        int pl = self->ph_ordinal_multi[i].len;
        if (rem >= pl && memcmp(chunk, self->ph_ordinal_multi[i].str, pl) == 0) {
            if (pos+pl >= n || !ISALP((unsigned char)tl[pos+pl])) {
                double val; ht_get_dbl(&self->ordinal_numbers, self->ph_ordinal_multi[i].str, &val);
                if (emit(results, make_ordinal(self, val, pos, pos+pl)) < 0) return 0;
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
                        PyObject *kw = PyDict_New();
                        kw_int(kw, self->kw_day, day);
                        if (emit(results, make_relative(self, pos, pos+q, kw, NULL, 0)) < 0) return 0;
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
                    PyObject *kw = PyDict_New();
                    kw_int(kw, self->kw_hour, hour);
                    if (emit(results, make_relative(self, pos, pos+q, kw, NULL, 0)) < 0) return 0;
                    return q;
                }
            }
        }
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 9: Number+month / month+day lookahead
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

    long month;
    if (!ht_get_int(&self->months, word, &month)) {
        char sw[4]; if (wlen<3) return 0;
        sw[0]=word[0]; sw[1]=word[1]; sw[2]=word[2]; sw[3]='\0';
        if (!ht_get_int(&self->months_shorts, sw, &month)) return 0;
    }
    int day = 0;
    for (int i = ns; i < ne; i++) if (ISDIG(text[i])) day = day*10+(text[i]-'0');
    if (!VALID_DAY(day)) return 0;

    PyObject *kw = PyDict_New();
    kw_int(kw, self->kw_month, month);
    kw_int(kw, self->kw_day, day);
    if (emit(results, make_relative(self, ns, p, kw, NULL, 0)) < 0) return 0;
    *out = p;
    return 1;
}

/* Returns: 1=month+day, 0=not month, -1=month but no day */
static int do_try_month_day(ScannerObject *self, const char *text, const char *tl,
                            int ws, int we, int n, const char *low,
                            PyObject *results, int *out) {
    long month;
    if (!ht_get_int(&self->months, low, &month))
        if (!ht_get_int(&self->months_shorts, low, &month)) return 0;

    int p = we;
    if (p < n && (text[p]=='.'||text[p]==' '||text[p]=='-')) p++;
    if (p >= n || !ISDIG(text[p])) return -1;
    int day = text[p]-'0'; p++;
    if (p < n && ISDIG(text[p])) { day = day*10+(text[p]-'0'); p++; }
    int sv = p;
    if (p < n && text[p]==' ') p++;
    if (p+1 < n) {
        char c0=tolower((unsigned char)text[p]), c1=tolower((unsigned char)text[p+1]);
        if ((c0=='s'&&c1=='t')||(c0=='n'&&c1=='d')||(c0=='r'&&c1=='d')||(c0=='t'&&c1=='h')) p+=2;
        else p=sv;
    } else p=sv;
    if (!wb_end(text,p,n)) return -1;
    if (!VALID_DAY(day)) return -1;

    PyObject *kw = PyDict_New();
    kw_int(kw, self->kw_month, month);
    kw_int(kw, self->kw_day, day);
    if (emit(results, make_relative(self, ws, p, kw, NULL, 0)) < 0) return -1;
    *out = p;
    return 1;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 10: Word classification
 * ══════════════════════════════════════════════════════════════ */

static int classify_word(ScannerObject *self, const char *text, const char *tl,
                         int ws, int we, int n, PyObject *results, int *out_end) {
    int wlen = we - ws;
    char low[128];
    if (wlen >= 128) wlen = 127;
    for (int i = 0; i < wlen; i++) low[i] = tolower((unsigned char)text[ws+i]);
    low[wlen] = '\0';

    /* NOW */
    if (ht_has(&self->now_words, low)) {
        if (n > 5) return 0;
        PyObject *kw = PyDict_New();
        kw_int(kw, self->kw_days, 0);
        kw_int(kw, self->kw_hours, 0);
        kw_int(kw, self->kw_minutes, 0);
        kw_int(kw, self->kw_seconds, 0);
        return emit(results, make_relative(self, ws, we, kw, NULL, 0)) < 0 ? -1 : 1;
    }

    /* today/tomorrow/yesterday */
    { long v; if (ht_get_int(&self->today_tomorrow, low, &v)) {
        PyObject *kw = PyDict_New();
        kw_int(kw, self->kw_days, v);
        return emit(results, make_relative(self, ws, we, kw, NULL, 0)) < 0 ? -1 : 1;
    }}

    /* seasons */
    { long v; if (ht_get_int(&self->seasons, low, &v)) {
        PyObject *kw = PyDict_New();
        kw_int(kw, self->kw_month, v);
        kw_int(kw, self->kw_day, 21);
        return emit(results, make_relative(self, ws, we, kw, levels1(self->u_SEASON), 0)) < 0 ? -1 : 1;
    }}

    /* single-word quarters */
    { long v; if (ht_get_int(&self->quarters, low, &v)) {
        PyObject *kw = PyDict_New();
        kw_int(kw, self->kw_month, v);
        kw_int(kw, self->kw_day, 1);
        return emit(results, make_relative(self, ws, we, kw, levels1(self->u_QUARTER), 0)) < 0 ? -1 : 1;
    }}

    /* noon/midnight (standalone) */
    { long h1, h2; if (ht_get_int2(&self->noon_standalone, low, &h1, &h2)) {
        PyObject *kw = PyDict_New();
        kw_int(kw, self->kw_hour, h1);
        return emit(results, make_relative(self, ws, we, kw, levels1(self->u_HOUR), 0)) < 0 ? -1 : 1;
    }}

    /* month+day lookahead */
    { int md = do_try_month_day(self, text, tl, ws, we, n, low, results, out_end);
      if (md == 1) return 1;
      if (md == -1) {
          /* standalone month */
          long mo;
          if (ht_get_int(&self->months, low, &mo) || ht_get_int(&self->months_shorts, low, &mo)) {
              PyObject *kw = PyDict_New();
              kw_int(kw, self->kw_month, mo);
              return emit(results, make_relative(self, ws, we, kw, NULL, 0)) < 0 ? -1 : 1;
          }
      }
    }

    /* standalone months */
    { long v; if (ht_get_int(&self->months, low, &v)) {
        PyObject *kw = PyDict_New(); kw_int(kw, self->kw_month, v);
        return emit(results, make_relative(self, ws, we, kw, NULL, 0)) < 0 ? -1 : 1;
    }}
    { long v; if (ht_get_int(&self->months_shorts, low, &v)) {
        PyObject *kw = PyDict_New(); kw_int(kw, self->kw_month, v);
        return emit(results, make_relative(self, ws, we, kw, NULL, 0)) < 0 ? -1 : 1;
    }}

    /* weekdays */
    { long v; if (ht_get_int(&self->weekday, low, &v)) {
        PyObject *kw = PyDict_New(); kw_int(kw, self->kw_weekday, v);
        return emit(results, make_relative(self, ws, we, kw, NULL, 0)) < 0 ? -1 : 1;
    }}
    { long v; if (ht_get_int(&self->weekday_shorts, low, &v)) {
        PyObject *kw = PyDict_New(); kw_int(kw, self->kw_weekday, v);
        return emit(results, make_relative(self, ws, we, kw, NULL, 0)) < 0 ? -1 : 1;
    }}

    /* modifiers */
    { long v; if (ht_get_int(&self->modifiers, low, &v)) {
        return emit(results, make_modifier_obj(self, low, v, ws, we)) < 0 ? -1 : 1;
    }}

    /* units */
    { const char *uname; if (ht_get_str(&self->units, low, &uname)) {
        return emit(results, make_unit(self, uname, ws, we)) < 0 ? -1 : 1;
    }}

    /* ordinal numbers — with am/pm lookahead */
    { double dv; if (ht_get_dbl(&self->ordinal_numbers, low, &dv)) {
        int after = we;
        while (after < n && text[after]==' ') after++;
        int pm = 0;
        int apm_end = match_apm(text, after, n, &pm);
        if (apm_end <= after) {
            if (ci_starts(tl, after, n, "in the afternoon"))      { pm=1; apm_end=after+16; }
            else if (ci_starts(tl, after, n, "in the morning"))   { pm=0; apm_end=after+14; }
        }
        if (apm_end > after && wb_end(text, apm_end, n)) {
            int hv = (int)dv;
            int hoff = pm ? 12 : 0;
            if (hv == 12 && hoff) hoff = 0;
            int hour = (hoff + hv) % 24;
            PyObject *kw = PyDict_New();
            kw_int(kw, self->kw_hour, hour);
            kw_int(kw, self->kw_minute, 0);
            kw_int(kw, self->kw_second, 0);
            int rc = emit(results, make_relative(self, ws, apm_end, kw,
                                                 levels2(self->u_MINUTE, self->u_HOUR), 0));
            *out_end = apm_end;
            return rc < 0 ? -1 : 1;
        }
        return emit(results, make_ordinal(self, dv, ws, we)) < 0 ? -1 : 1;
    }}

    /* AND words */
    if (ht_has(&self->and_words, low))
        return emit(results, make_and(self, low, ws, we)) < 0 ? -1 : 1;

    /* whitelist — skip */
    if (ht_has(&self->whitelist, low))
        return 0;

    /* unknown word → string separator */
    return emit_str(results, text, ws, we) < 0 ? -1 : 1;
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 11: Token classification dispatch
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
        long mo;
        if (ht_get_int(&self->quarters, low, &mo)) {
            PyObject *kw = PyDict_New();
            kw_int(kw, self->kw_month, mo);
            kw_int(kw, self->kw_day, 1);
            return emit(results, make_relative(self, start, end, kw, levels1(self->u_QUARTER), 0));
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
            PyObject *kw = PyDict_New();
            kw_int(kw, self->kw_year, yr);
            kw_int(kw, self->kw_month, mo);
            kw_int(kw, self->kw_day, dy);
            return emit(results, make_relative(self, start, end, kw, NULL, 0));
        }
        return emit_str(results, text, start, end);
    }

    if (kind == TOK_ISO_COMPACT) {
        int yr=(text[start]-'0')*1000+(text[start+1]-'0')*100+(text[start+2]-'0')*10+(text[start+3]-'0');
        int mo=(text[start+4]-'0')*10+(text[start+5]-'0');
        int dy=(text[start+6]-'0')*10+(text[start+7]-'0');
        if (VALID_YEAR(yr)&&VALID_MONTH(mo)&&VALID_DAY(dy)) {
            PyObject *kw = PyDict_New();
            kw_int(kw, self->kw_year, yr);
            kw_int(kw, self->kw_month, mo);
            kw_int(kw, self->kw_day, dy);
            return emit(results, make_relative(self, start, end, kw, NULL, 0));
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
            PyObject *kw = PyDict_New();
            kw_int(kw, self->kw_year, yr); kw_int(kw, self->kw_month, mo); kw_int(kw, self->kw_day, dy);
            return emit(results, make_relative(self, start, end, kw, NULL, 0));
        }
        { int t2=dy; dy=mo; mo=t2; }
        if (VALID_YEAR(yr)&&VALID_MONTH(mo)&&VALID_DAY(dy)) {
            PyObject *kw = PyDict_New();
            kw_int(kw, self->kw_year, yr); kw_int(kw, self->kw_month, mo); kw_int(kw, self->kw_day, dy);
            return emit(results, make_relative(self, start, end, kw, NULL, 0));
        }
        return emit_str(results, text, start, end);
    }

    if (kind == TOK_YEAR4) {
        int yr=0; for (int i=start;i<end;i++) yr=yr*10+(text[i]-'0');
        if (VALID_YEAR(yr)) {
            PyObject *kw = PyDict_New(); kw_int(kw, self->kw_year, yr);
            return emit(results, make_relative(self, start, end, kw, NULL, 0));
        }
        return emit_str(results, text, start, end);
    }

    if (kind == TOK_ORDINAL_NUM) {
        int num=0; for (int i=start;i<end;i++) if (ISDIG(text[i])) num=num*10+(text[i]-'0');
        return emit(results, make_ordinal(self, (double)num, start, end));
    }

    if (kind == TOK_NUMBER) {
        char buf[64]; int bl=end-start; if (bl>=64) bl=63;
        memcpy(buf, text+start, bl); buf[bl]='\0';
        for (int i=0;i<bl;i++) if (buf[i]==',') buf[i]='.';
        return emit(results, make_ordinal(self, strtod(buf,NULL), start, end));
    }

    if (kind == TOK_WORD)
        return classify_word(self, text, tl, start, end, n, results, out_end);

    return emit_str(results, text, start, end);
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 12: Main scan method
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
 * SECTION 13: Build C hash tables from Python dicts
 * ══════════════════════════════════════════════════════════════ */

static void build_ht_int(HashTable *ht, PyObject *d) {
    ht_init(ht);
    PyObject *k, *v; Py_ssize_t p = 0;
    while (PyDict_Next(d, &p, &k, &v)) {
        const char *ks = PyUnicode_AsUTF8(k);
        long val = PyLong_Check(v) ? PyLong_AsLong(v) : (long)PyFloat_AsDouble(v);
        ht_set_int(ht, ks, val);
    }
}

static void build_ht_dbl(HashTable *ht, PyObject *d) {
    ht_init(ht);
    PyObject *k, *v; Py_ssize_t p = 0;
    while (PyDict_Next(d, &p, &k, &v)) {
        const char *ks = PyUnicode_AsUTF8(k);
        double val = PyLong_Check(v) ? (double)PyLong_AsLong(v) : PyFloat_AsDouble(v);
        ht_set_dbl(ht, ks, val);
    }
}

static void build_ht_str(HashTable *ht, PyObject *d) {
    ht_init(ht);
    PyObject *k, *v; Py_ssize_t p = 0;
    while (PyDict_Next(d, &p, &k, &v)) {
        const char *ks = PyUnicode_AsUTF8(k);
        const char *vs = PyUnicode_AsUTF8(v);
        ht_set_str(ht, ks, vs);
    }
}

static void build_ht_int2(HashTable *ht, PyObject *d) {
    ht_init(ht);
    PyObject *k, *v; Py_ssize_t p = 0;
    while (PyDict_Next(d, &p, &k, &v)) {
        const char *ks = PyUnicode_AsUTF8(k);
        long a = PyLong_AsLong(PyTuple_GET_ITEM(v, 0));
        long b = PyLong_AsLong(PyTuple_GET_ITEM(v, 1));
        ht_set_int2(ht, ks, a, b);
    }
}

static void build_ht_set_from_list(HashTable *ht, PyObject *lst) {
    ht_init(ht);
    Py_ssize_t n = PyList_GET_SIZE(lst);
    for (Py_ssize_t i = 0; i < n; i++) {
        const char *s = PyUnicode_AsUTF8(PyList_GET_ITEM(lst, i));
        ht_mark(ht, s);
    }
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 14: Init / dealloc
 * ══════════════════════════════════════════════════════════════ */

static PyObject *Scanner_new(PyTypeObject *type, PyObject *a, PyObject *k) {
    ScannerObject *self = (ScannerObject *)type->tp_alloc(type, 0);
    if (self) { memset(&self->months, 0, sizeof(ScannerObject) - offsetof(ScannerObject, months)); }
    return (PyObject *)self;
}

#define GET_DICT(name) PyDict_GetItemString(ld, name)

static int Scanner_init(ScannerObject *self, PyObject *args, PyObject *kwds) {
    PyObject *ld;
    if (!PyArg_ParseTuple(args, "O", &ld)) return -1;

    /* Build C hash tables */
    build_ht_int(&self->months,        GET_DICT("months"));
    build_ht_int(&self->months_shorts, GET_DICT("months_shorts"));
    build_ht_int(&self->weekday,       GET_DICT("weekday"));
    build_ht_int(&self->weekday_shorts,GET_DICT("weekday_shorts"));
    build_ht_int(&self->modifiers,     GET_DICT("modifiers"));
    build_ht_str(&self->units,         GET_DICT("units"));
    build_ht_dbl(&self->ordinal_numbers,GET_DICT("ordinal_numbers"));
    build_ht_int(&self->today_tomorrow,GET_DICT("today_tomorrow"));
    build_ht_int(&self->seasons,       GET_DICT("seasons"));
    build_ht_int(&self->quarters,      GET_DICT("quarters"));
    build_ht_int2(&self->noon,         GET_DICT("noon"));
    build_ht_int2(&self->noon_standalone,GET_DICT("noon_standalone"));
    build_ht_set_from_list(&self->whitelist,  GET_DICT("whitelist"));
    build_ht_set_from_list(&self->now_words,  GET_DICT("now"));
    build_ht_set_from_list(&self->and_words,  GET_DICT("and"));

    PyObject *dd = GET_DICT("dd_left_first");
    self->dd_left_first = dd && PyObject_IsTrue(dd);

    /* Build phrase arrays */
    self->ph_in_the       = phrases_from_pylist(GET_DICT("in_the"),       &self->n_in_the);
    self->ph_today_multi  = phrases_from_pylist(GET_DICT("today_multi"),  &self->n_today_multi);
    self->ph_quarter_multi= phrases_from_pylist(GET_DICT("quarter_multi"),&self->n_quarter_multi);
    self->ph_ordinal_multi= phrases_from_pylist(GET_DICT("ordinal_multi"),&self->n_ordinal_multi);

    /* Import and cache Python classes */
    PyObject *cls_mod = PyImport_ImportModule("metadate.classes");
    if (!cls_mod) return -1;
    self->cls_MetaRelative = PyObject_GetAttrString(cls_mod, "MetaRelative");
    self->cls_MetaOrdinal  = PyObject_GetAttrString(cls_mod, "MetaOrdinal");
    self->cls_MetaUnit     = PyObject_GetAttrString(cls_mod, "MetaUnit");
    self->cls_MetaModifier = PyObject_GetAttrString(cls_mod, "MetaModifier");
    self->cls_MetaRange    = PyObject_GetAttrString(cls_mod, "MetaRange");
    self->cls_MetaAnd      = PyObject_GetAttrString(cls_mod, "MetaAnd");
    Py_DECREF(cls_mod);

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

    /* Cache keyword strings (interned) */
    self->kw_year        = PyUnicode_InternFromString("year");
    self->kw_month       = PyUnicode_InternFromString("month");
    self->kw_day         = PyUnicode_InternFromString("day");
    self->kw_hour        = PyUnicode_InternFromString("hour");
    self->kw_minute      = PyUnicode_InternFromString("minute");
    self->kw_second      = PyUnicode_InternFromString("second");
    self->kw_microsecond = PyUnicode_InternFromString("microsecond");
    self->kw_weekday     = PyUnicode_InternFromString("weekday");
    self->kw_days        = PyUnicode_InternFromString("days");
    self->kw_hours       = PyUnicode_InternFromString("hours");
    self->kw_minutes     = PyUnicode_InternFromString("minutes");
    self->kw_seconds     = PyUnicode_InternFromString("seconds");
    self->kw_microseconds= PyUnicode_InternFromString("microseconds");
    self->kw_levels      = PyUnicode_InternFromString("levels");
    self->kw_modifier    = PyUnicode_InternFromString("modifier");

    self->str_SENT = PyUnicode_InternFromString("SENT");

    return 0;
}

static void Scanner_dealloc(ScannerObject *self) {
    /* Free C hash tables */
    ht_free(&self->months);       ht_free(&self->months_shorts);
    ht_free(&self->weekday);      ht_free(&self->weekday_shorts);
    ht_free(&self->modifiers);    ht_free(&self->units);
    ht_free(&self->ordinal_numbers); ht_free(&self->today_tomorrow);
    ht_free(&self->seasons);      ht_free(&self->quarters);
    ht_free(&self->noon);         ht_free(&self->noon_standalone);
    ht_free(&self->whitelist);    ht_free(&self->now_words);
    ht_free(&self->and_words);

    /* Free phrase arrays */
    phrases_free(self->ph_in_the,       self->n_in_the);
    phrases_free(self->ph_today_multi,  self->n_today_multi);
    phrases_free(self->ph_quarter_multi,self->n_quarter_multi);
    phrases_free(self->ph_ordinal_multi,self->n_ordinal_multi);

    /* Release cached Python objects */
    Py_XDECREF(self->cls_MetaRelative); Py_XDECREF(self->cls_MetaOrdinal);
    Py_XDECREF(self->cls_MetaUnit);     Py_XDECREF(self->cls_MetaModifier);
    Py_XDECREF(self->cls_MetaRange);    Py_XDECREF(self->cls_MetaAnd);
    Py_XDECREF(self->str_SENT);

    Py_XDECREF(self->u_YEAR);  Py_XDECREF(self->u_SEASON);
    Py_XDECREF(self->u_QUARTER); Py_XDECREF(self->u_MONTH);
    Py_XDECREF(self->u_WEEK);  Py_XDECREF(self->u_DAY);
    Py_XDECREF(self->u_HOUR);  Py_XDECREF(self->u_MINUTE);
    Py_XDECREF(self->u_SECOND);Py_XDECREF(self->u_MICRO);

    Py_XDECREF(self->kw_year);  Py_XDECREF(self->kw_month);
    Py_XDECREF(self->kw_day);   Py_XDECREF(self->kw_hour);
    Py_XDECREF(self->kw_minute);Py_XDECREF(self->kw_second);
    Py_XDECREF(self->kw_microsecond); Py_XDECREF(self->kw_weekday);
    Py_XDECREF(self->kw_days);  Py_XDECREF(self->kw_hours);
    Py_XDECREF(self->kw_minutes); Py_XDECREF(self->kw_seconds);
    Py_XDECREF(self->kw_microseconds);
    Py_XDECREF(self->kw_levels); Py_XDECREF(self->kw_modifier);

    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ══════════════════════════════════════════════════════════════
 * SECTION 15: Type and module definition
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
    .tp_doc       = "C-accelerated date scanner (v2: direct Meta* creation + C hash tables).",
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
