/*
 * _cscanner.c — C-accelerated date scanner for metadatez.
 *
 * Implements the same tokenization + classification logic as NewScanner
 * but in C for speed.  Returns lightweight descriptor tuples that the
 * Python wrapper (c_scanner.py) converts to Meta* objects.
 *
 * Result tuple formats:
 *   ("R", start, end, kwargs_dict)   — MetaRelative
 *   ("O", start, end, amount_float)  — MetaOrdinal
 *   ("U", start, end, unit_str)      — MetaUnit
 *   ("M", start, end, word, value)   — MetaModifier
 *   ("G", start, end, word)          — MetaRange
 *   ("A", start, end, word)          — MetaAnd
 *   ("S", raw_text)                  — string separator
 *   ("X",)                           — sentence break (SENT)
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* ── Macros ─────────────────────────────────────────────────── */

#define ISDIG(c)  ((c) >= '0' && (c) <= '9')
#define ISALP(c)  (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define ISWAN(c)  (ISDIG(c) || ISALP(c) || (c) == '_')
#define TOLC(c)   (((c) >= 'A' && (c) <= 'Z') ? (c) + 32 : (c))

#define VALID_DAY(d)    ((d) >= 1 && (d) <= 31)
#define VALID_MONTH(m)  ((m) >= 1 && (m) <= 12)
#define VALID_YEAR(y)   ((y) >= 1900 && (y) <= 2100)
#define VALID_HOUR(h)   ((h) >= 0 && (h) <= 23)
#define VALID_MIN(m)    ((m) >= 0 && (m) <= 59)
#define VALID_SEC(s)    ((s) >= 0 && (s) <= 59)

#define U_YEAR   9
#define U_SEASON 8
#define U_QUARTER 7
#define U_MONTH  6
#define U_WEEK   5
#define U_DAY    4
#define U_HOUR   3
#define U_MINUTE 2
#define U_SECOND 1
#define U_MICRO  0

/* ── Token kinds ────────────────────────────────────────────── */
enum {
    TOK_SKIP = 0, TOK_PUNCT, TOK_HMS_MICRO, TOK_HMS, TOK_HM,
    TOK_HOUR_APM, TOK_ISO_DATE, TOK_ISO_COMPACT, TOK_DD_MM_YYYY,
    TOK_YEAR4, TOK_QUARTER_Q, TOK_ORDINAL_NUM, TOK_NUMBER,
    TOK_AND_HALF, TOK_WORD,
};

/* ── Scanner object ─────────────────────────────────────────── */

typedef struct {
    PyObject_HEAD
    /* Python dicts (strong refs) — all keys are lowercase strings */
    PyObject *months;           /* str -> int */
    PyObject *months_shorts;    /* str -> int */
    PyObject *weekday;          /* str -> int */
    PyObject *weekday_shorts;   /* str -> int */
    PyObject *modifiers;        /* str -> int */
    PyObject *units;            /* str -> str */
    PyObject *ordinal_numbers;  /* str -> number */
    PyObject *today_tomorrow;   /* str -> int */
    PyObject *seasons;          /* str -> int */
    PyObject *quarters;         /* str -> int */
    PyObject *noon;             /* str -> (int,int) */
    PyObject *noon_standalone;  /* str -> (int,int) */
    PyObject *whitelist;        /* list of str */
    PyObject *now_list;         /* list of str */
    PyObject *and_list;         /* list of str */
    PyObject *in_the_list;      /* list, sorted longest first */
    PyObject *today_multi;      /* list */
    PyObject *quarter_multi;    /* list */
    PyObject *ordinal_multi;    /* list */
    int dd_left_first;
} ScannerObject;

/* ── Forward declarations ───────────────────────────────────── */

static int wb_start(const char *t, int p);
static int wb_end(const char *t, int p, int n);

/* ── Dict / list helpers ────────────────────────────────────── */

static int dict_set_int(PyObject *d, const char *key, long val) {
    PyObject *v = PyLong_FromLong(val);
    if (!v) return -1;
    int rc = PyDict_SetItemString(d, key, v);
    Py_DECREF(v);
    return rc;
}

static int dict_set_float(PyObject *d, const char *key, double val) {
    PyObject *v = PyFloat_FromDouble(val);
    if (!v) return -1;
    int rc = PyDict_SetItemString(d, key, v);
    Py_DECREF(v);
    return rc;
}

/* Set "_levels" key to a tuple of ints */
static int kw_set_levels(PyObject *kw, const int *arr, int count) {
    PyObject *tup = PyTuple_New(count);
    if (!tup) return -1;
    for (int i = 0; i < count; i++)
        PyTuple_SET_ITEM(tup, i, PyLong_FromLong(arr[i]));
    int rc = PyDict_SetItemString(kw, "_levels", tup);
    Py_DECREF(tup);
    return rc;
}

/* Check if C string is in a Python list of strings */
static int list_has(PyObject *lst, const char *key) {
    Py_ssize_t n = PyList_GET_SIZE(lst);
    for (Py_ssize_t i = 0; i < n; i++) {
        const char *s = PyUnicode_AsUTF8(PyList_GET_ITEM(lst, i));
        if (s && strcmp(s, key) == 0) return 1;
    }
    return 0;
}

/* Get int value from Python dict by C string key. Returns dflt if missing. */
static long dict_get_long(PyObject *d, const char *key, long dflt) {
    PyObject *v = PyDict_GetItemString(d, key);  /* borrowed */
    if (!v) return dflt;
    if (PyLong_Check(v)) return PyLong_AsLong(v);
    if (PyFloat_Check(v)) return (long)PyFloat_AsDouble(v);
    return dflt;
}

static double dict_get_double(PyObject *d, const char *key, double dflt) {
    PyObject *v = PyDict_GetItemString(d, key);
    if (!v) return dflt;
    if (PyLong_Check(v)) return (double)PyLong_AsLong(v);
    if (PyFloat_Check(v)) return PyFloat_AsDouble(v);
    return dflt;
}

/* ── Result-building helpers ────────────────────────────────── */

static int add_relative(PyObject *list, int s, int e, PyObject *kw) {
    PyObject *item = Py_BuildValue("(siiN)", "R", s, e, kw);
    if (!item) return -1;
    int rc = PyList_Append(list, item);
    Py_DECREF(item);
    return rc;
}

static int add_ordinal(PyObject *list, int s, int e, double amount) {
    PyObject *item = Py_BuildValue("(siid)", "O", s, e, amount);
    if (!item) return -1;
    int rc = PyList_Append(list, item);
    Py_DECREF(item);
    return rc;
}

static int add_unit(PyObject *list, int s, int e, const char *unit) {
    PyObject *item = Py_BuildValue("(siis)", "U", s, e, unit);
    if (!item) return -1;
    int rc = PyList_Append(list, item);
    Py_DECREF(item);
    return rc;
}

static int add_modifier(PyObject *list, int s, int e, const char *word, long val) {
    PyObject *item = Py_BuildValue("(siisi)", "M", s, e, word, (int)val);
    if (!item) return -1;
    int rc = PyList_Append(list, item);
    Py_DECREF(item);
    return rc;
}

static int add_range(PyObject *list, int s, int e, const char *word) {
    PyObject *item = Py_BuildValue("(siis)", "G", s, e, word);
    if (!item) return -1;
    int rc = PyList_Append(list, item);
    Py_DECREF(item);
    return rc;
}

static int add_and(PyObject *list, int s, int e, const char *word) {
    PyObject *item = Py_BuildValue("(siis)", "A", s, e, word);
    if (!item) return -1;
    int rc = PyList_Append(list, item);
    Py_DECREF(item);
    return rc;
}

static int add_string(PyObject *list, const char *text, int s, int e) {
    PyObject *str = PyUnicode_FromStringAndSize(text + s, e - s);
    if (!str) return -1;
    PyObject *item = Py_BuildValue("(sN)", "S", str);
    if (!item) return -1;
    int rc = PyList_Append(list, item);
    Py_DECREF(item);
    return rc;
}

static int add_sent(PyObject *list) {
    PyObject *item = Py_BuildValue("(s)", "X");
    if (!item) return -1;
    int rc = PyList_Append(list, item);
    Py_DECREF(item);
    return rc;
}

/* ── Word boundary ──────────────────────────────────────────── */

static int wb_start(const char *t, int p) {
    if (p == 0) return 1;
    return !ISWAN((unsigned char)t[p - 1]);
}

static int wb_end(const char *t, int p, int n) {
    if (p >= n) return 1;
    return !ISWAN((unsigned char)t[p]);
}

/* Case-insensitive prefix match */
static int ci_starts(const char *t, int p, int n, const char *target) {
    for (int i = 0; target[i]; i++) {
        if (p + i >= n) return 0;
        if (tolower((unsigned char)t[p + i]) != (unsigned char)target[i]) return 0;
    }
    return 1;
}

/* ── Time parsing ───────────────────────────────────────────── */

static int str_has_ci(const char *s, int len, const char *needle) {
    int nlen = (int)strlen(needle);
    for (int i = 0; i + nlen <= len; i++) {
        int ok = 1;
        for (int j = 0; j < nlen; j++) {
            if (tolower((unsigned char)s[i + j]) != (unsigned char)needle[j]) { ok = 0; break; }
        }
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

    int nums[4] = {0, 0, 0, 0};
    int nc = 0;
    int i = 0;
    while (i < len && nc < 4) {
        if (ISDIG(raw[i])) {
            int v = 0;
            while (i < len && ISDIG(raw[i])) { v = v * 10 + (raw[i] - '0'); i++; }
            nums[nc++] = v;
        } else {
            i++;
        }
    }
    *hour = nums[0];
    if (nc >= 2) *minute = nums[1];
    if (nc >= 3) *second = nums[2];
    if (nc >= 4) *micro = nums[3];
    if (*hour == 12 && hoffset) hoffset = 0;
    *hour = (hoffset + *hour) % 24;
}

static int classify_time(const char *raw, int raw_len, int s, int e, PyObject *results) {
    int hour, minute, second, micro;
    parse_time_str(raw, raw_len, &hour, &minute, &second, &micro);
    PyObject *kw = PyDict_New();
    if (!kw) return -1;
    dict_set_int(kw, "hour", hour);
    int has_dot = (memchr(raw, '.', raw_len) != NULL);
    if (has_dot && micro) {
        dict_set_int(kw, "minute", minute);
        dict_set_int(kw, "second", second);
        dict_set_int(kw, "microsecond", micro);
    } else if (second) {
        dict_set_int(kw, "minute", minute);
        dict_set_int(kw, "second", second);
    } else if (minute) {
        dict_set_int(kw, "minute", minute);
    }
    return add_relative(results, s, e, kw);
}

/* ── Tokenizer pattern matchers ─────────────────────────────── */
/* Each returns chars consumed (0 = no match). */

/* Match optional am/pm at pos (no leading space). Returns end pos or start. */
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
            q++;
            if (q < n && t[q] == '.') q++;
            return q;
        }
        return p;
    }
    if (tolower((unsigned char)t[q]) == 'm') {
        q++;
        if (q < n && t[q] == '.') q++;
        return q;
    }
    return p;
}

/* \b\d{1,2}[:h]\d{2}[:m]\d{2}\.\d+\b */
static int try_hms_micro(const char *t, int p, int n) {
    if (!wb_start(t, p)) return 0;
    int s = p;
    if (!ISDIG(t[p])) return 0; p++;
    if (p < n && ISDIG(t[p])) p++;
    if (p >= n || (t[p] != ':' && tolower((unsigned char)t[p]) != 'h')) return 0; p++;
    if (p + 1 >= n || !ISDIG(t[p]) || !ISDIG(t[p + 1])) return 0; p += 2;
    if (p >= n || (t[p] != ':' && tolower((unsigned char)t[p]) != 'm')) return 0; p++;
    if (p + 1 >= n || !ISDIG(t[p]) || !ISDIG(t[p + 1])) return 0; p += 2;
    if (p >= n || t[p] != '.') return 0; p++;
    if (p >= n || !ISDIG(t[p])) return 0;
    while (p < n && ISDIG(t[p])) p++;
    if (!wb_end(t, p, n)) return 0;
    return p - s;
}

/* \b\d{1,2}[:h]\d{2}[:m]\d{2}(\s?[ap]\.?m\.?)?\b */
static int try_hms(const char *t, int p, int n) {
    if (!wb_start(t, p)) return 0;
    int s = p;
    if (!ISDIG(t[p])) return 0; p++;
    if (p < n && ISDIG(t[p])) p++;
    if (p >= n || (t[p] != ':' && tolower((unsigned char)t[p]) != 'h')) return 0; p++;
    if (p + 1 >= n || !ISDIG(t[p]) || !ISDIG(t[p + 1])) return 0; p += 2;
    if (p >= n || (t[p] != ':' && tolower((unsigned char)t[p]) != 'm')) return 0; p++;
    if (p + 1 >= n || !ISDIG(t[p]) || !ISDIG(t[p + 1])) return 0; p += 2;
    /* optional am/pm with optional leading space */
    int save = p;
    if (p < n && t[p] == ' ') p++;
    int pm;
    int ap = match_apm(t, p, n, &pm);
    if (ap > p) { p = ap; } else { p = save; }
    if (!wb_end(t, p, n)) return 0;
    return p - s;
}

/* \b\d{1,2}[:h']\d{2}(\s?[ap]\.?m\.?|[hm])?\b */
static int try_hm(const char *t, int p, int n) {
    if (!wb_start(t, p)) return 0;
    int s = p;
    if (!ISDIG(t[p])) return 0; p++;
    if (p < n && ISDIG(t[p])) p++;
    if (p >= n || (t[p] != ':' && tolower((unsigned char)t[p]) != 'h' && t[p] != '\'')) return 0;
    p++;
    if (p + 1 >= n || !ISDIG(t[p]) || !ISDIG(t[p + 1])) return 0; p += 2;
    /* optional suffix */
    int save = p;
    if (p < n && t[p] == ' ') p++;
    int pm;
    int ap = match_apm(t, p, n, &pm);
    if (ap > p) { p = ap; }
    else {
        p = save;
        if (p < n && (tolower((unsigned char)t[p]) == 'h' || tolower((unsigned char)t[p]) == 'm'))
            p++;
    }
    if (!wb_end(t, p, n)) return 0;
    return p - s;
}

/* \b\d{1,2}\s?(?:a\.?m\.?|p\.?m\.?|afternoon|o'?clock)\b */
static int try_hour_apm(const char *t, int p, int n) {
    if (!wb_start(t, p)) return 0;
    int s = p;
    if (!ISDIG(t[p])) return 0; p++;
    if (p < n && ISDIG(t[p])) p++;
    int save = p;
    if (p < n && t[p] == ' ') p++;
    /* am/pm */
    int pm;
    int ap = match_apm(t, p, n, &pm);
    if (ap > p && wb_end(t, ap, n)) return ap - s;
    /* afternoon */
    p = save;
    if (p < n && t[p] == ' ') p++;
    if (ci_starts(t, p, n, "afternoon") && wb_end(t, p + 9, n)) return (p + 9) - s;
    /* o'clock / oclock */
    p = save;
    if (p < n && t[p] == ' ') p++;
    if (p < n && tolower((unsigned char)t[p]) == 'o') {
        int q = p + 1;
        if (q < n && t[q] == '\'') q++;
        if (ci_starts(t, q, n, "clock") && wb_end(t, q + 5, n)) return (q + 5) - s;
    }
    return 0;
}

/* \b[12]\d{3}[-/]month[-/]day\b */
static int try_iso_date(const char *t, int p, int n) {
    if (!wb_start(t, p)) return 0;
    if (p + 9 >= n + 1) return 0; /* minimum YYYY-M-D = 8 chars */
    if (t[p] != '1' && t[p] != '2') return 0;
    if (!ISDIG(t[p+1]) || !ISDIG(t[p+2]) || !ISDIG(t[p+3])) return 0;
    int q = p + 4;
    if (q >= n || (t[q] != '-' && t[q] != '/')) return 0; q++;
    /* month: 0?[1-9]|1[0-2] */
    if (q >= n || !ISDIG(t[q])) return 0;
    int ms = q;
    if (t[q] == '0') { q++; if (q >= n || !ISDIG(t[q])) return 0; q++; }
    else if (t[q] == '1') { q++; if (q < n && ISDIG(t[q]) && t[q] <= '2') q++; }
    else if (t[q] >= '2' && t[q] <= '9') { q++; }
    else return 0;
    int month = 0;
    for (int i = ms; i < q; i++) month = month * 10 + (t[i] - '0');
    if (!VALID_MONTH(month)) return 0;
    if (q >= n || (t[q] != '-' && t[q] != '/')) return 0; q++;
    /* day */
    if (q >= n || !ISDIG(t[q])) return 0;
    int ds = q;
    q++;
    if (q < n && ISDIG(t[q])) q++;
    int day = 0;
    for (int i = ds; i < q; i++) day = day * 10 + (t[i] - '0');
    if (!VALID_DAY(day)) return 0;
    if (!wb_end(t, q, n)) return 0;
    return q - p;
}

/* \b[12]\d{3}(?:0[1-9]|1[0-2])(?:0[1-9]|[12]\d|3[01])\b */
static int try_iso_compact(const char *t, int p, int n) {
    if (!wb_start(t, p)) return 0;
    if (p + 8 > n) return 0;
    if (t[p] != '1' && t[p] != '2') return 0;
    for (int i = 1; i < 8; i++) if (!ISDIG(t[p + i])) return 0;
    int month = (t[p+4]-'0')*10 + (t[p+5]-'0');
    int day   = (t[p+6]-'0')*10 + (t[p+7]-'0');
    if (!VALID_MONTH(month) || !VALID_DAY(day)) return 0;
    if (!wb_end(t, p + 8, n)) return 0;
    return 8;
}

/* \b(?:0?[1-9]|[12]\d|3[01])[/ ](?:0?[1-9]|1[0-2])[/ ][12]\d{3}\b */
static int try_dd_mm_yyyy(const char *t, int p, int n) {
    if (!wb_start(t, p)) return 0;
    int q = p;
    /* day part */
    if (q >= n || !ISDIG(t[q])) return 0;
    int ds = q; q++;
    if (q < n && ISDIG(t[q])) q++;
    if (q >= n || (t[q] != '/' && t[q] != ' ')) return 0; q++;
    /* month part */
    if (q >= n || !ISDIG(t[q])) return 0;
    int ms = q; q++;
    if (q < n && ISDIG(t[q])) q++;
    if (q >= n || (t[q] != '/' && t[q] != ' ')) return 0; q++;
    /* year part */
    if (q + 3 >= n) return 0;
    if (t[q] != '1' && t[q] != '2') return 0;
    for (int i = 1; i < 4; i++) if (!ISDIG(t[q + i])) return 0;
    q += 4;
    if (!wb_end(t, q, n)) return 0;
    /* validate */
    int day = 0, month = 0, year = 0;
    for (int i = ds; i < ds + (t[ds+1] == '/' || t[ds+1] == ' ' ? 1 : 2); i++)
        day = day * 10 + (t[i] - '0');
    /* Actually, re-parse properly */
    day = 0; month = 0; year = 0;
    {
        int i = p;
        while (ISDIG(t[i])) { day = day*10 + (t[i]-'0'); i++; }
        i++; /* skip separator */
        while (ISDIG(t[i])) { month = month*10 + (t[i]-'0'); i++; }
        i++; /* skip separator */
        while (ISDIG(t[i]) && i < q) { year = year*10 + (t[i]-'0'); i++; }
    }
    if (!VALID_DAY(day) || !VALID_MONTH(month) || !VALID_YEAR(year)) return 0;
    return q - p;
}

/* \b[12]\d{3}\b */
static int try_year4(const char *t, int p, int n) {
    if (!wb_start(t, p)) return 0;
    if (p + 4 > n) return 0;
    if (t[p] != '1' && t[p] != '2') return 0;
    if (!ISDIG(t[p+1]) || !ISDIG(t[p+2]) || !ISDIG(t[p+3])) return 0;
    if (!wb_end(t, p + 4, n)) return 0;
    int y = (t[p]-'0')*1000 + (t[p+1]-'0')*100 + (t[p+2]-'0')*10 + (t[p+3]-'0');
    if (!VALID_YEAR(y)) return 0;
    return 4;
}

/* \b[Qq][1-4]\b */
static int try_quarter_q(const char *t, int p, int n) {
    if (!wb_start(t, p)) return 0;
    if (p + 2 > n) return 0;
    if (tolower((unsigned char)t[p]) != 'q') return 0;
    if (t[p+1] < '1' || t[p+1] > '4') return 0;
    if (!wb_end(t, p + 2, n)) return 0;
    return 2;
}

/* \b\d{1,2}(?:st|nd|rd|th)\b */
static int try_ordinal_num(const char *t, int p, int n) {
    if (!wb_start(t, p)) return 0;
    int q = p;
    if (!ISDIG(t[q])) return 0; q++;
    if (q < n && ISDIG(t[q])) q++;
    if (q + 1 >= n) return 0;
    char c0 = tolower((unsigned char)t[q]);
    char c1 = tolower((unsigned char)t[q + 1]);
    if (!((c0=='s'&&c1=='t') || (c0=='n'&&c1=='d') || (c0=='r'&&c1=='d') || (c0=='t'&&c1=='h')))
        return 0;
    q += 2;
    if (!wb_end(t, q, n)) return 0;
    return q - p;
}

/* \d+[.,]?\d* */
static int try_number(const char *t, int p, int n) {
    if (!ISDIG(t[p])) return 0;
    int q = p;
    while (q < n && ISDIG(t[q])) q++;
    if (q < n && (t[q] == ',' || t[q] == '.')) {
        q++;
        while (q < n && ISDIG(t[q])) q++;
    }
    return q - p;
}

/* [a-zA-Z]+ */
static int try_word(const char *t, int p, int n) {
    if (!ISALP(t[p])) return 0;
    int q = p;
    while (q < n && ISALP(t[q])) q++;
    return q - p;
}

/* Main tokenizer: try all patterns at pos, return kind + end */
static int tokenize_at(const char *t, int pos, int n, int *kind, int *end) {
    unsigned char c = (unsigned char)t[pos];
    int r;

    if (ISDIG(c)) {
        if (wb_start(t, pos)) {
            r = try_hms_micro(t, pos, n);
            if (r) { *kind = TOK_HMS_MICRO; *end = pos + r; return 1; }
            r = try_hms(t, pos, n);
            if (r) { *kind = TOK_HMS; *end = pos + r; return 1; }
            r = try_hm(t, pos, n);
            if (r) { *kind = TOK_HM; *end = pos + r; return 1; }
            r = try_hour_apm(t, pos, n);
            if (r) { *kind = TOK_HOUR_APM; *end = pos + r; return 1; }
            r = try_iso_date(t, pos, n);
            if (r) { *kind = TOK_ISO_DATE; *end = pos + r; return 1; }
            r = try_iso_compact(t, pos, n);
            if (r) { *kind = TOK_ISO_COMPACT; *end = pos + r; return 1; }
            r = try_dd_mm_yyyy(t, pos, n);
            if (r) { *kind = TOK_DD_MM_YYYY; *end = pos + r; return 1; }
            r = try_year4(t, pos, n);
            if (r) { *kind = TOK_YEAR4; *end = pos + r; return 1; }
            r = try_ordinal_num(t, pos, n);
            if (r) { *kind = TOK_ORDINAL_NUM; *end = pos + r; return 1; }
        }
        r = try_number(t, pos, n);
        if (r) { *kind = TOK_NUMBER; *end = pos + r; return 1; }
        return 0;
    }

    if (ISALP(c)) {
        if (wb_start(t, pos)) {
            r = try_quarter_q(t, pos, n);
            if (r) { *kind = TOK_QUARTER_Q; *end = pos + r; return 1; }
        }
        r = try_word(t, pos, n);
        if (r) { *kind = TOK_WORD; *end = pos + r; return 1; }
        return 0;
    }

    if (c == '!' || c == '?') {
        int q = pos + 1;
        while (q < n && (t[q] == '!' || t[q] == '?')) q++;
        *kind = TOK_PUNCT; *end = q; return 1;
    }
    if (c == '\n') { *kind = TOK_PUNCT; *end = pos + 1; return 1; }
    if (c == '.') {
        if (pos + 1 >= n || (!ISALP(t[pos+1]) && !ISDIG(t[pos+1]) && t[pos+1] != '.')) {
            *kind = TOK_PUNCT; *end = pos + 1; return 1;
        }
    }

    if (isspace(c)) {
        int q = pos + 1;
        while (q < n && isspace((unsigned char)t[q])) q++;
        *kind = TOK_SKIP; *end = q; return 1;
    }
    if (c == '-' || c == ',') { *kind = TOK_SKIP; *end = pos + 1; return 1; }

    return 0;
}

/* ── Multi-word matching ────────────────────────────────────── */

static int do_try_multiword(ScannerObject *self, const char *tl, const char *text,
                            int pos, int n, PyObject *results) {
    const char *chunk = tl + pos;
    int remaining = n - pos;

    /* "and a half" */
    if (remaining >= 10 && memcmp(chunk, "and a half", 10) == 0) {
        add_ordinal(results, pos, pos + 10, 0.5);
        return 10;
    }

    /* "in the" / "over the" / etc — must be followed by space */
    {
        Py_ssize_t cnt = PyList_GET_SIZE(self->in_the_list);
        for (Py_ssize_t i = 0; i < cnt; i++) {
            PyObject *pobj = PyList_GET_ITEM(self->in_the_list, i);
            const char *phrase = PyUnicode_AsUTF8(pobj);
            int plen = (int)strlen(phrase);
            if (remaining > plen && memcmp(chunk, phrase, plen) == 0 && chunk[plen] == ' ') {
                add_range(results, pos, pos + plen, phrase);
                return plen + 1; /* consume trailing space */
            }
        }
    }

    /* Multi-word today/tomorrow ("day after tomorrow", etc.) */
    {
        Py_ssize_t cnt = PyList_GET_SIZE(self->today_multi);
        for (Py_ssize_t i = 0; i < cnt; i++) {
            PyObject *pobj = PyList_GET_ITEM(self->today_multi, i);
            const char *phrase = PyUnicode_AsUTF8(pobj);
            int plen = (int)strlen(phrase);
            if (remaining >= plen && memcmp(chunk, phrase, plen) == 0) {
                if (pos + plen >= n || !ISALP((unsigned char)tl[pos + plen])) {
                    long days = dict_get_long(self->today_tomorrow, phrase, 0);
                    PyObject *kw = PyDict_New();
                    dict_set_int(kw, "days", days);
                    add_relative(results, pos, pos + plen, kw);
                    return plen;
                }
            }
        }
    }

    /* Multi-word quarters ("first quarter", etc.) */
    {
        Py_ssize_t cnt = PyList_GET_SIZE(self->quarter_multi);
        for (Py_ssize_t i = 0; i < cnt; i++) {
            PyObject *pobj = PyList_GET_ITEM(self->quarter_multi, i);
            const char *phrase = PyUnicode_AsUTF8(pobj);
            int plen = (int)strlen(phrase);
            if (remaining >= plen && memcmp(chunk, phrase, plen) == 0) {
                if (pos + plen >= n || !ISALP((unsigned char)tl[pos + plen])) {
                    long month = dict_get_long(self->quarters, phrase, 1);
                    PyObject *kw = PyDict_New();
                    dict_set_int(kw, "month", month);
                    dict_set_int(kw, "day", 1);
                    int levels[] = {U_QUARTER};
                    kw_set_levels(kw, levels, 1);
                    add_relative(results, pos, pos + plen, kw);
                    return plen;
                }
            }
        }
    }

    /* Multi-word ordinals ("a couple", etc.) */
    {
        Py_ssize_t cnt = PyList_GET_SIZE(self->ordinal_multi);
        for (Py_ssize_t i = 0; i < cnt; i++) {
            PyObject *pobj = PyList_GET_ITEM(self->ordinal_multi, i);
            const char *phrase = PyUnicode_AsUTF8(pobj);
            int plen = (int)strlen(phrase);
            if (remaining >= plen && memcmp(chunk, phrase, plen) == 0) {
                if (pos + plen >= n || !ISALP((unsigned char)tl[pos + plen])) {
                    double val = dict_get_double(self->ordinal_numbers, phrase, 0);
                    add_ordinal(results, pos, pos + plen, val);
                    return plen;
                }
            }
        }
    }

    /* "on the NNst/nd/rd/th" */
    if (remaining >= 8 && memcmp(chunk, "on the ", 7) == 0) {
        int q = 7;
        if (q < remaining && ISDIG(chunk[q])) {
            int day = chunk[q] - '0'; q++;
            if (q < remaining && ISDIG(chunk[q])) { day = day*10 + (chunk[q]-'0'); q++; }
            if (q + 1 < remaining) {
                char c0 = chunk[q], c1 = chunk[q+1];
                if ((c0=='s'&&c1=='t') || (c0=='n'&&c1=='d') ||
                    (c0=='r'&&c1=='d') || (c0=='t'&&c1=='h')) {
                    q += 2;
                    if (wb_end(tl, pos + q, n) && VALID_DAY(day)) {
                        PyObject *kw = PyDict_New();
                        dict_set_int(kw, "day", day);
                        add_relative(results, pos, pos + q, kw);
                        return q;
                    }
                }
            }
        }
    }

    /* "at NN" pattern */
    if (remaining >= 4 && chunk[0] == 'a' && chunk[1] == 't' && chunk[2] == ' ') {
        int q = 3;
        if (q < remaining && ISDIG(chunk[q])) {
            int hour = chunk[q] - '0'; q++;
            if (q < remaining && ISDIG(chunk[q])) { hour = hour*10 + (chunk[q]-'0'); q++; }
            /* optional 'h' */
            if (q < remaining && tolower((unsigned char)chunk[q]) == 'h') q++;
            if (wb_end(text, pos + q, n)) {
                /* negative lookahead: not [:'] and not ". " */
                int bad = 0;
                int ep = pos + q;
                if (ep < n && (text[ep] == ':' || text[ep] == '\'')) bad = 1;
                if (ep + 1 < n && text[ep] == '.' && text[ep+1] == ' ') bad = 1;
                if (!bad && VALID_HOUR(hour)) {
                    PyObject *kw = PyDict_New();
                    dict_set_int(kw, "hour", hour);
                    add_relative(results, pos, pos + q, kw);
                    return q;
                }
            }
        }
    }

    return 0;
}

/* ── Number + month lookahead ───────────────────────────────── */

static int do_try_number_month(ScannerObject *self, const char *text, const char *tl,
                               int num_start, int num_end, int n,
                               PyObject *results, int *out_end) {
    int p = num_end;
    /* skip whitespace and hyphens */
    while (p < n && (text[p] == ' ' || text[p] == '-' || text[p] == '\t')) p++;
    /* optional "of " */
    if (p + 3 <= n && tl[p] == 'o' && tl[p+1] == 'f' && text[p+2] == ' ') {
        p += 3;
        while (p < n && text[p] == ' ') p++;
    }
    /* word */
    if (p >= n || !ISALP(text[p])) return 0;
    int ws = p;
    while (p < n && ISALP(text[p])) p++;
    int wlen = p - ws;
    char word[64];
    if (wlen >= 64) return 0;
    for (int i = 0; i < wlen; i++) word[i] = tolower((unsigned char)text[ws + i]);
    word[wlen] = '\0';

    /* lookup month */
    PyObject *mval = PyDict_GetItemString(self->months, word);
    if (!mval && wlen >= 3) {
        char sw[4] = {word[0], word[1], word[2], '\0'};
        mval = PyDict_GetItemString(self->months_shorts, sw);
    }
    if (!mval) return 0;

    /* extract day from num token */
    int day = 0;
    for (int i = num_start; i < num_end; i++)
        if (ISDIG(text[i])) day = day * 10 + (text[i] - '0');
    if (!VALID_DAY(day)) return 0;

    int month = (int)PyLong_AsLong(mval);
    PyObject *kw = PyDict_New();
    dict_set_int(kw, "month", month);
    dict_set_int(kw, "day", day);
    add_relative(results, num_start, p, kw);
    *out_end = p;
    return 1;
}

/* ── Month + day lookahead ──────────────────────────────────── */

/* Returns: 1 = month+day added, 0 = not a month, -1 = month but no day */
static int do_try_month_day(ScannerObject *self, const char *text, const char *tl,
                            int ws, int we, int n, const char *low,
                            PyObject *results, int *out_end) {
    PyObject *mval = PyDict_GetItemString(self->months, low);
    if (!mval) mval = PyDict_GetItemString(self->months_shorts, low);
    if (!mval) return 0; /* not a month */

    int p = we;
    /* optional [.\s-] */
    if (p < n && (text[p] == '.' || text[p] == ' ' || text[p] == '-'))
        p++;
    /* \d{1,2} */
    if (p >= n || !ISDIG(text[p])) return -1;
    int day = text[p] - '0'; p++;
    if (p < n && ISDIG(text[p])) { day = day * 10 + (text[p] - '0'); p++; }
    /* optional ordinal suffix */
    int save = p;
    if (p < n && text[p] == ' ') p++;
    if (p + 1 < n) {
        char c0 = tolower((unsigned char)text[p]), c1 = tolower((unsigned char)text[p+1]);
        if ((c0=='s'&&c1=='t') || (c0=='n'&&c1=='d') || (c0=='r'&&c1=='d') || (c0=='t'&&c1=='h'))
            p += 2;
        else p = save;
    } else p = save;
    /* word boundary */
    if (!wb_end(text, p, n)) return -1;
    if (!VALID_DAY(day)) return -1;

    int month = (int)PyLong_AsLong(mval);
    PyObject *kw = PyDict_New();
    dict_set_int(kw, "month", month);
    dict_set_int(kw, "day", day);
    add_relative(results, ws, p, kw);
    *out_end = p;
    return 1;
}

/* ── Token classification ───────────────────────────────────── */

static int classify_word(ScannerObject *self, const char *text, const char *tl,
                         int ws, int we, int n, PyObject *results, int *out_end) {
    int wlen = we - ws;
    char low[128];
    if (wlen >= 128) wlen = 127;
    for (int i = 0; i < wlen; i++) low[i] = tolower((unsigned char)text[ws + i]);
    low[wlen] = '\0';

    /* NOW */
    if (list_has(self->now_list, low)) {
        if (n > 5) return 0; /* skip "now" in longer text */
        PyObject *kw = PyDict_New();
        dict_set_int(kw, "days", 0);
        dict_set_int(kw, "hours", 0);
        dict_set_int(kw, "minutes", 0);
        dict_set_int(kw, "seconds", 0);
        add_relative(results, ws, we, kw);
        return 1;
    }

    /* today/tomorrow/yesterday (single-word) */
    {
        PyObject *v = PyDict_GetItemString(self->today_tomorrow, low);
        if (v) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "days", PyLong_AsLong(v));
            add_relative(results, ws, we, kw);
            return 1;
        }
    }

    /* seasons */
    {
        PyObject *v = PyDict_GetItemString(self->seasons, low);
        if (v) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "month", PyLong_AsLong(v));
            dict_set_int(kw, "day", 21);
            int levels[] = {U_SEASON};
            kw_set_levels(kw, levels, 1);
            add_relative(results, ws, we, kw);
            return 1;
        }
    }

    /* single-word quarters */
    {
        PyObject *v = PyDict_GetItemString(self->quarters, low);
        if (v) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "month", PyLong_AsLong(v));
            dict_set_int(kw, "day", 1);
            int levels[] = {U_QUARTER};
            kw_set_levels(kw, levels, 1);
            add_relative(results, ws, we, kw);
            return 1;
        }
    }

    /* noon/midnight (standalone) */
    {
        PyObject *v = PyDict_GetItemString(self->noon_standalone, low);
        if (v) {
            int hour = (int)PyLong_AsLong(PyTuple_GET_ITEM(v, 0));
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "hour", hour);
            int levels[] = {U_HOUR};
            kw_set_levels(kw, levels, 1);
            add_relative(results, ws, we, kw);
            return 1;
        }
    }

    /* month + day lookahead */
    {
        int md = do_try_month_day(self, text, tl, ws, we, n, low, results, out_end);
        if (md == 1) return 1; /* month+day added */
        if (md == -1) {
            /* is a month, but no day follows — add standalone month */
            PyObject *mval = PyDict_GetItemString(self->months, low);
            if (!mval) mval = PyDict_GetItemString(self->months_shorts, low);
            if (mval) {
                PyObject *kw = PyDict_New();
                dict_set_int(kw, "month", PyLong_AsLong(mval));
                add_relative(results, ws, we, kw);
                return 1;
            }
        }
    }

    /* standalone months */
    {
        PyObject *v = PyDict_GetItemString(self->months, low);
        if (v) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "month", PyLong_AsLong(v));
            add_relative(results, ws, we, kw);
            return 1;
        }
    }
    {
        PyObject *v = PyDict_GetItemString(self->months_shorts, low);
        if (v) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "month", PyLong_AsLong(v));
            add_relative(results, ws, we, kw);
            return 1;
        }
    }

    /* weekdays */
    {
        PyObject *v = PyDict_GetItemString(self->weekday, low);
        if (v) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "weekday", PyLong_AsLong(v));
            add_relative(results, ws, we, kw);
            return 1;
        }
    }
    {
        PyObject *v = PyDict_GetItemString(self->weekday_shorts, low);
        if (v) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "weekday", PyLong_AsLong(v));
            add_relative(results, ws, we, kw);
            return 1;
        }
    }

    /* modifiers */
    {
        PyObject *v = PyDict_GetItemString(self->modifiers, low);
        if (v) {
            add_modifier(results, ws, we, low, PyLong_AsLong(v));
            return 1;
        }
    }

    /* units */
    {
        PyObject *v = PyDict_GetItemString(self->units, low);
        if (v) {
            const char *unit_name = PyUnicode_AsUTF8(v);
            add_unit(results, ws, we, unit_name);
            return 1;
        }
    }

    /* ordinal numbers — with am/pm lookahead */
    {
        PyObject *v = PyDict_GetItemString(self->ordinal_numbers, low);
        if (v) {
            /* Peek ahead for am/pm */
            int after = we;
            while (after < n && text[after] == ' ') after++;
            int pm = 0;
            int apm_end = match_apm(text, after, n, &pm);
            if (apm_end <= after) {
                /* try "in the afternoon" / "in the morning" */
                if (ci_starts(tl, after, n, "in the afternoon")) {
                    pm = 1; apm_end = after + 16;
                } else if (ci_starts(tl, after, n, "in the morning")) {
                    pm = 0; apm_end = after + 14;
                }
            }
            if (apm_end > after && wb_end(text, apm_end, n)) {
                double hour_val;
                if (PyLong_Check(v)) hour_val = (double)PyLong_AsLong(v);
                else hour_val = PyFloat_AsDouble(v);
                int hoffset = pm ? 12 : 0;
                if ((int)hour_val == 12 && hoffset) hoffset = 0;
                int hour = (hoffset + (int)hour_val) % 24;
                PyObject *kw = PyDict_New();
                dict_set_int(kw, "hour", hour);
                dict_set_int(kw, "minute", 0);
                dict_set_int(kw, "second", 0);
                int levels[] = {U_MINUTE, U_HOUR};
                kw_set_levels(kw, levels, 2);
                add_relative(results, ws, apm_end, kw);
                *out_end = apm_end;
                return 1;
            }
            /* regular ordinal */
            double amount;
            if (PyLong_Check(v)) amount = (double)PyLong_AsLong(v);
            else amount = PyFloat_AsDouble(v);
            add_ordinal(results, ws, we, amount);
            return 1;
        }
    }

    /* AND words */
    if (list_has(self->and_list, low)) {
        add_and(results, ws, we, low);
        return 1;
    }

    /* whitelist — consume silently */
    if (list_has(self->whitelist, low)) {
        return 0; /* skip */
    }

    /* unknown word → string separator */
    add_string(results, text, ws, we);
    return 1;
}

static int classify_token(ScannerObject *self, int kind, const char *text, const char *tl,
                          int start, int end, int n, PyObject *results, int *out_end) {
    *out_end = end;

    if (kind == TOK_SKIP) return 0;

    if (kind == TOK_PUNCT) {
        add_sent(results);
        return 1;
    }

    if (kind == TOK_HMS_MICRO || kind == TOK_HMS || kind == TOK_HM || kind == TOK_HOUR_APM) {
        return classify_time(text + start, end - start, start, end, results);
    }

    if (kind == TOK_QUARTER_Q) {
        char low[4];
        low[0] = tolower((unsigned char)text[start]);
        low[1] = text[start + 1];
        low[2] = '\0';
        PyObject *v = PyDict_GetItemString(self->quarters, low);
        if (v) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "month", PyLong_AsLong(v));
            dict_set_int(kw, "day", 1);
            int levels[] = {U_QUARTER};
            kw_set_levels(kw, levels, 1);
            add_relative(results, start, end, kw);
            return 1;
        }
        add_string(results, text, start, end);
        return 1;
    }

    if (kind == TOK_ISO_DATE) {
        /* Parse YYYY-MM-DD or YYYY/MM/DD */
        int nums[3] = {0, 0, 0};
        int nc = 0, i = start;
        while (i < end && nc < 3) {
            if (ISDIG(text[i])) {
                int v = 0;
                while (i < end && ISDIG(text[i])) { v = v*10 + (text[i]-'0'); i++; }
                nums[nc++] = v;
            } else i++;
        }
        int year = nums[0], month = nums[1], day = nums[2];
        if (VALID_YEAR(year) && VALID_MONTH(month) && VALID_DAY(day)) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "year", year);
            dict_set_int(kw, "month", month);
            dict_set_int(kw, "day", day);
            add_relative(results, start, end, kw);
            return 1;
        }
        add_string(results, text, start, end);
        return 1;
    }

    if (kind == TOK_ISO_COMPACT) {
        int year = (text[start]-'0')*1000 + (text[start+1]-'0')*100 +
                   (text[start+2]-'0')*10 + (text[start+3]-'0');
        int month = (text[start+4]-'0')*10 + (text[start+5]-'0');
        int day   = (text[start+6]-'0')*10 + (text[start+7]-'0');
        if (VALID_YEAR(year) && VALID_MONTH(month) && VALID_DAY(day)) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "year", year);
            dict_set_int(kw, "month", month);
            dict_set_int(kw, "day", day);
            add_relative(results, start, end, kw);
            return 1;
        }
        add_string(results, text, start, end);
        return 1;
    }

    if (kind == TOK_DD_MM_YYYY) {
        int nums[3] = {0, 0, 0};
        int nc = 0, i = start;
        while (i < end && nc < 3) {
            if (ISDIG(text[i])) {
                int v = 0;
                while (i < end && ISDIG(text[i])) { v = v*10 + (text[i]-'0'); i++; }
                nums[nc++] = v;
            } else i++;
        }
        int day = nums[0], month = nums[1], year = nums[2];
        if (!self->dd_left_first) { int tmp = day; day = month; month = tmp; }
        if (VALID_YEAR(year) && VALID_MONTH(month) && VALID_DAY(day)) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "year", year);
            dict_set_int(kw, "month", month);
            dict_set_int(kw, "day", day);
            add_relative(results, start, end, kw);
            return 1;
        }
        /* try swapped */
        { int tmp = day; day = month; month = tmp; }
        if (VALID_YEAR(year) && VALID_MONTH(month) && VALID_DAY(day)) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "year", year);
            dict_set_int(kw, "month", month);
            dict_set_int(kw, "day", day);
            add_relative(results, start, end, kw);
            return 1;
        }
        add_string(results, text, start, end);
        return 1;
    }

    if (kind == TOK_YEAR4) {
        int year = 0;
        for (int i = start; i < end; i++) year = year*10 + (text[i]-'0');
        if (VALID_YEAR(year)) {
            PyObject *kw = PyDict_New();
            dict_set_int(kw, "year", year);
            add_relative(results, start, end, kw);
            return 1;
        }
        add_string(results, text, start, end);
        return 1;
    }

    if (kind == TOK_ORDINAL_NUM) {
        int num = 0;
        for (int i = start; i < end; i++)
            if (ISDIG(text[i])) num = num*10 + (text[i]-'0');
        add_ordinal(results, start, end, (double)num);
        return 1;
    }

    if (kind == TOK_NUMBER) {
        /* Replace comma with dot, convert to float */
        char buf[64];
        int blen = end - start;
        if (blen >= 64) blen = 63;
        memcpy(buf, text + start, blen);
        buf[blen] = '\0';
        for (int i = 0; i < blen; i++) if (buf[i] == ',') buf[i] = '.';
        double amount = strtod(buf, NULL);
        add_ordinal(results, start, end, amount);
        return 1;
    }

    if (kind == TOK_WORD) {
        return classify_word(self, text, tl, start, end, n, results, out_end);
    }

    add_string(results, text, start, end);
    return 1;
}

/* ── Main scan method ───────────────────────────────────────── */

static PyObject *Scanner_scan(ScannerObject *self, PyObject *args) {
    const char *text;
    Py_ssize_t text_len;
    if (!PyArg_ParseTuple(args, "s#", &text, &text_len))
        return NULL;

    int n = (int)text_len;

    /* Build lowercase copy, replacing en-dash (UTF-8: E2 80 93) with '-' */
    char *tl = (char *)malloc(n + 1);
    if (!tl) return PyErr_NoMemory();
    {
        int j = 0;
        for (int i = 0; i < n; ) {
            unsigned char c = (unsigned char)text[i];
            if (c == 0xE2 && i + 2 < n &&
                (unsigned char)text[i+1] == 0x80 && (unsigned char)text[i+2] == 0x93) {
                tl[j++] = '-';
                i += 3;
            } else {
                tl[j++] = tolower(c);
                i++;
            }
        }
        tl[j] = '\0';
        n = j; /* may have shrunk due to en-dash replacement */
    }

    /* We also need a "clean" text copy with en-dash replaced for span accuracy.
       Since en-dash is 3 bytes → 1 byte, positions shift. For simplicity,
       replace en-dash in-place before processing: use tl for lowercase,
       but we need original-case text for raw extraction.
       Actually, to keep spans correct relative to the ORIGINAL text, let's
       work on the original text but with en-dash handled differently.

       The original NewScanner does: text = text.replace("\u2013", "-")
       which changes the string and all spans are relative to the new string.
       We do the same: build a modified text buffer. */

    /* Re-do: build both modified text and lowercase */
    char *mt = (char *)malloc(text_len + 1); /* modified text */
    if (!mt) { free(tl); return PyErr_NoMemory(); }
    {
        int j = 0;
        for (int i = 0; i < (int)text_len; ) {
            unsigned char c = (unsigned char)text[i];
            if (c == 0xE2 && i + 2 < (int)text_len &&
                (unsigned char)text[i+1] == 0x80 && (unsigned char)text[i+2] == 0x93) {
                mt[j] = '-';
                tl[j] = '-';
                j++;
                i += 3;
            } else {
                mt[j] = text[i];
                tl[j] = tolower(c);
                j++;
                i++;
            }
        }
        mt[j] = '\0';
        tl[j] = '\0';
        n = j;
    }

    PyObject *results = PyList_New(0);
    if (!results) { free(tl); free(mt); return NULL; }

    int pos = 0;
    while (pos < n) {
        /* Multi-word phrases */
        int consumed = do_try_multiword(self, tl, mt, pos, n, results);
        if (consumed) { pos += consumed; continue; }

        /* Single token */
        int kind, tok_end;
        if (!tokenize_at(mt, pos, n, &kind, &tok_end)) {
            add_string(results, mt, pos, pos + 1);
            pos++;
            continue;
        }

        /* Number/ordinal + month lookahead */
        if ((kind == TOK_NUMBER || kind == TOK_ORDINAL_NUM) && tok_end < n) {
            int merged_end;
            if (do_try_number_month(self, mt, tl, pos, tok_end, n, results, &merged_end)) {
                pos = merged_end;
                continue;
            }
        }

        int out_end = tok_end;
        classify_token(self, kind, mt, tl, pos, tok_end, n, results, &out_end);
        pos = out_end;
    }

    free(tl);
    free(mt);
    return results;
}

/* ── Scanner init / dealloc ─────────────────────────────────── */

static PyObject *Scanner_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    ScannerObject *self = (ScannerObject *)type->tp_alloc(type, 0);
    return (PyObject *)self;
}

#define EXTRACT_DICT(attr, key) do { \
    self->attr = PyDict_GetItemString(locale_data, key); \
    if (!self->attr) { PyErr_SetString(PyExc_KeyError, key); return -1; } \
    Py_INCREF(self->attr); \
} while(0)

#define EXTRACT_LIST(attr, key) EXTRACT_DICT(attr, key)

static int Scanner_init(ScannerObject *self, PyObject *args, PyObject *kwds) {
    PyObject *locale_data;
    if (!PyArg_ParseTuple(args, "O", &locale_data))
        return -1;

    EXTRACT_DICT(months, "months");
    EXTRACT_DICT(months_shorts, "months_shorts");
    EXTRACT_DICT(weekday, "weekday");
    EXTRACT_DICT(weekday_shorts, "weekday_shorts");
    EXTRACT_DICT(modifiers, "modifiers");
    EXTRACT_DICT(units, "units");
    EXTRACT_DICT(ordinal_numbers, "ordinal_numbers");
    EXTRACT_DICT(today_tomorrow, "today_tomorrow");
    EXTRACT_DICT(seasons, "seasons");
    EXTRACT_DICT(quarters, "quarters");
    EXTRACT_DICT(noon, "noon");
    EXTRACT_DICT(noon_standalone, "noon_standalone");
    EXTRACT_LIST(whitelist, "whitelist");
    EXTRACT_LIST(now_list, "now");
    EXTRACT_LIST(and_list, "and");
    EXTRACT_LIST(in_the_list, "in_the");
    EXTRACT_LIST(today_multi, "today_multi");
    EXTRACT_LIST(quarter_multi, "quarter_multi");
    EXTRACT_LIST(ordinal_multi, "ordinal_multi");

    PyObject *dd = PyDict_GetItemString(locale_data, "dd_left_first");
    self->dd_left_first = dd && PyObject_IsTrue(dd);

    return 0;
}

static void Scanner_dealloc(ScannerObject *self) {
    Py_XDECREF(self->months);
    Py_XDECREF(self->months_shorts);
    Py_XDECREF(self->weekday);
    Py_XDECREF(self->weekday_shorts);
    Py_XDECREF(self->modifiers);
    Py_XDECREF(self->units);
    Py_XDECREF(self->ordinal_numbers);
    Py_XDECREF(self->today_tomorrow);
    Py_XDECREF(self->seasons);
    Py_XDECREF(self->quarters);
    Py_XDECREF(self->noon);
    Py_XDECREF(self->noon_standalone);
    Py_XDECREF(self->whitelist);
    Py_XDECREF(self->now_list);
    Py_XDECREF(self->and_list);
    Py_XDECREF(self->in_the_list);
    Py_XDECREF(self->today_multi);
    Py_XDECREF(self->quarter_multi);
    Py_XDECREF(self->ordinal_multi);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ── Type and module definition ─────────────────────────────── */

static PyMethodDef Scanner_methods[] = {
    {"scan", (PyCFunction)Scanner_scan, METH_VARARGS, "Scan text and return descriptor list."},
    {NULL}
};

static PyTypeObject ScannerType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_cscanner.Scanner",
    .tp_basicsize = sizeof(ScannerObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = Scanner_new,
    .tp_init = (initproc)Scanner_init,
    .tp_dealloc = (destructor)Scanner_dealloc,
    .tp_methods = Scanner_methods,
    .tp_doc = "C-accelerated date scanner.",
};

static PyModuleDef cscanner_module = {
    PyModuleDef_HEAD_INIT,
    "_cscanner",
    "C-accelerated date scanning module.",
    -1,
    NULL,
};

PyMODINIT_FUNC PyInit__cscanner(void) {
    if (PyType_Ready(&ScannerType) < 0)
        return NULL;
    PyObject *m = PyModule_Create(&cscanner_module);
    if (!m) return NULL;
    Py_INCREF(&ScannerType);
    if (PyModule_AddObject(m, "Scanner", (PyObject *)&ScannerType) < 0) {
        Py_DECREF(&ScannerType);
        Py_DECREF(m);
        return NULL;
    }
    return m;
}
