/* SPDX-License-Identifier: GPL-3.0-only */
/* X0X_AMY on the FM-1: the C library AMY needs that JieLi's newlib (pi32v2/lib/r3) leaves out —
 * no malloc and no printf family there (JieLi's SDK brings its own). The host and web builds use
 * their own C library and do not compile this.
 *
 * malloc / free: first fit over one arena in the .pool RAM region, merging free neighbours as it
 *   walks. Only AMY allocates, and only from the audio ISR (at start-up and on a patch change), so
 *   there is one context and no lock.
 * printf family: integers, strings, characters, pointers, and floats as %f (%g and %e as %f with
 *   up to six decimals, trailing zeros dropped; no exponents). AMY needs the floats: a MIDI note or a
 *   mapped CC becomes a wire message with its value written "%.3f". A float argument arrives as a
 *   double, and the FPU is single precision (tools/build.py fails a build that links soft-double
 *   arithmetic), so the double's bits are turned into digits with integer arithmetic. fprintf goes
 *   nowhere.
 * abort / assert: a trap, so X0X's crash screen shows where and the FM-1 restarts. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

#ifndef X0X_AMY_HEAP
#define X0X_AMY_HEAP (256u * 1024u)   /* AMY: ~120 KB with 6 Juno voices and chorus, ~260 KB with the
                                       * reverb (measured on a 64-bit host; the pool keeps its 8 KB spare) */
#endif
#define HB 8u                          /* block header: size in bytes with the header, bit 0 = in use */
static uint8_t heap[X0X_AMY_HEAP] __attribute__((section(".pool"), aligned(8)));
uint32_t amy_heap_used, amy_heap_peak;  /* (a debugger reads them) */

static uint32_t *hdr(uint32_t off) { return (uint32_t *)(void *)&heap[off]; }

void *malloc(size_t n)
{
    uint32_t need = ((uint32_t)n + HB + 7u) & ~7u, off = 0;
    if (!*hdr(0))                      /* the pool is zero at boot: one free block */
        *hdr(0) = X0X_AMY_HEAP;
    while (off < X0X_AMY_HEAP) {
        uint32_t sz = *hdr(off) & ~1u;
        if (!(*hdr(off) & 1u)) {
            while (off + sz < X0X_AMY_HEAP && !(*hdr(off + sz) & 1u))   /* merge the free run */
                sz += *hdr(off + sz);
            if (sz >= need) {
                if (sz - need >= 2u * HB) {                              /* split off the rest */
                    *hdr(off + need) = sz - need;
                    sz = need;
                }
                *hdr(off) = sz | 1u;
                amy_heap_used += sz;
                if (amy_heap_used > amy_heap_peak)
                    amy_heap_peak = amy_heap_used;
                return &heap[off + HB];
            }
            *hdr(off) = sz;
        }
        off += sz;
    }
    return NULL;
}

void free(void *p)
{
    uint32_t off;
    if (!p)
        return;
    off = (uint32_t)((uint8_t *)p - heap) - HB;
    amy_heap_used -= *hdr(off) & ~1u;
    *hdr(off) &= ~1u;
}

/* ---- printf ---- */
typedef struct {
    char *p;
    size_t left;                       /* room for characters, the terminator kept aside */
    int n;                             /* what would have been written */
} out_t;

static void put(out_t *o, char c)
{
    if (o->left) {
        *o->p++ = c;
        o->left--;
    }
    o->n++;
}

static void put_field(out_t *o, const char *s, int len, int width, int left, char pad)
{
    int k;
    if (!left && pad == '0' && len && (*s == '-' || *s == '+')) {   /* the sign goes before the zeros */
        put(o, *s++);
        len--;
        width--;
    }
    for (k = len; !left && k < width; k++)
        put(o, pad);
    for (k = 0; k < len; k++)
        put(o, s[k]);
    for (k = len; left && k < width; k++)
        put(o, ' ');
}

/* a double (its IEEE-754 bits: no double arithmetic) as %f with prec (<= 9) decimals, rounded as
 * the C library does (to nearest, exact ties to even); trim: drop trailing zeros and a bare point
 * (%g). The fraction is kept exactly, in two 64-bit words: below 2^-33 a value shows as 0 at nine
 * decimals, so it needs at most 86 bits. Returns the length; |x| >= 2^63 prints "?" */
static int fmt_double(char *s, uint64_t u, int prec, int trim)
{
    uint64_t mant = u & ((1ull << 52) - 1u), ip = 0, hi = 0, lo = 0;
    int ex = (int)((u >> 52) & 0x7FFu), e2, sh = 0, n = 0, i, up;
    char d[12], ib[24];
    if (u >> 63)
        s[n++] = '-';
    if (ex == 0x7FF) {
        s[n++] = mant ? 'n' : 'i';
        s[n++] = mant ? 'a' : 'n';
        s[n++] = mant ? 'n' : 'f';
        return n;
    }
    if (ex)
        mant |= 1ull << 52;
    e2 = ex ? ex - 1075 : -1074;                      /* the value is mant * 2^e2 */
    if (e2 >= 11) {
        s[n++] = '?';
        return n;
    }
    if (e2 >= 0) {
        ip = mant << e2;
    } else if (e2 >= -86) {                           /* the fraction, rem = hi:lo, is below 2^sh */
        sh = -e2;
        ip = sh < 64 ? mant >> sh : 0;
        lo = sh < 64 ? mant & ((1ull << sh) - 1u) : mant;
    }
    for (i = 0; i <= prec; i++) {                     /* prec decimals, and one to round with */
        uint64_t l0 = (lo & 0xFFFFFFFFu) * 10u, l1 = (lo >> 32) * 10u + (l0 >> 32);
        lo = (l1 << 32) | (l0 & 0xFFFFFFFFu);         /* rem *= 10 */
        hi = hi * 10u + (l1 >> 32);
        if (!sh) {
            d[i] = 0;
        } else if (sh >= 64) {
            d[i] = (char)(hi >> (sh - 64));
            hi &= (1ull << (sh - 64)) - 1u;
        } else {
            d[i] = (char)((hi << (64 - sh)) | (lo >> sh));
            hi = 0;
            lo &= (1ull << sh) - 1u;
        }
    }
    up = d[prec] > 5 || (d[prec] == 5 && (hi || lo || (prec ? d[prec - 1] : (char)ip) & 1));
    if (up) {
        for (i = prec - 1; i >= 0 && ++d[i] == 10; i--)
            d[i] = 0;
        if (i < 0)
            ip++;
    }
    i = 0;
    do ib[i++] = (char)('0' + ip % 10u); while (ip /= 10u);
    while (i)
        s[n++] = ib[--i];
    if (trim)
        while (prec > 0 && d[prec - 1] == 0)
            prec--;
    if (prec > 0) {
        s[n++] = '.';
        for (i = 0; i < prec; i++)
            s[n++] = (char)('0' + d[i]);
    }
    return n;
}

int vsnprintf(char *buf, size_t size, const char *f, va_list ap)
{
    out_t o = {buf, size ? size - 1u : 0u, 0};
    for (; *f; f++) {
        char num[40], *e = num + sizeof num, *s;
        int left = 0, plus = 0, space = 0, width = 0, prec = -1, lng = 0, len;
        char pad = ' ';
        unsigned long long v;
        if (*f != '%') {
            put(&o, *f);
            continue;
        }
        for (f++;; f++) {
            if (*f == '-') left = 1;
            else if (*f == '+') plus = 1;
            else if (*f == ' ') space = 1;
            else if (*f == '0') pad = '0';
            else break;
        }
        if (*f == '*') {
            width = va_arg(ap, int);
            f++;
        }
        while (*f >= '0' && *f <= '9')
            width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            prec = 0;
            for (f++; *f >= '0' && *f <= '9'; f++)
                prec = prec * 10 + (*f - '0');
        }
        while (*f == 'l' || *f == 'h' || *f == 'z' || *f == 'j' || *f == 't')
            lng += (*f++ == 'l');
        switch (*f) {
        case 'd': case 'i': {
            long long x = lng >= 2 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
            v = x < 0 ? (unsigned long long)-x : (unsigned long long)x;
            s = e;
            do *--s = (char)('0' + v % 10u); while (v /= 10u);
            if (x < 0) *--s = '-';
            else if (plus) *--s = '+';
            else if (space) *--s = ' ';
            put_field(&o, s, (int)(e - s), width, left, pad);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': case 'p': {
            unsigned base = *f == 'u' ? 10u : *f == 'o' ? 8u : 16u;
            const char *dig = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            v = *f == 'p' ? (uintptr_t)va_arg(ap, void *) :
                lng >= 2 ? va_arg(ap, unsigned long long) : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            s = e;
            do *--s = dig[v % base]; while (v /= base);
            put_field(&o, s, (int)(e - s), width, left, pad);
            break;
        }
        case 'c':
            num[0] = (char)va_arg(ap, int);
            put_field(&o, num, 1, width, left, ' ');
            break;
        case 's':
            s = va_arg(ap, char *);
            if (!s) s = "(null)";
            for (len = 0; s[len] && (prec < 0 || len < prec); len++)
                ;
            put_field(&o, s, len, width, left, ' ');
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
            union { double d; uint64_t u; } x;
            int g = *f != 'f' && *f != 'F';
            x.d = va_arg(ap, double);
            len = fmt_double(num, x.u, prec < 0 ? 6 : prec > 9 ? 9 : prec, g);
            s = num;
            if (num[0] != '-' && (plus || space)) {   /* the sign the flags ask for */
                for (s = num + len; s > num; s--)
                    s[0] = s[-1];
                num[0] = plus ? '+' : ' ';
                len++;
                s = num;
            }
            put_field(&o, s, len, width, left, pad);
            break;
        }
        case '%':
            put(&o, '%');
            break;
        case 0:
            f--;
            break;
        default:
            put(&o, '%');
            put(&o, *f);
            break;
        }
    }
    if (size)
        *o.p = 0;
    return o.n;
}

int snprintf(char *buf, size_t size, const char *f, ...)
{
    va_list ap;
    int n;
    va_start(ap, f);
    n = vsnprintf(buf, size, f, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *f, ...)
{
    va_list ap;
    int n;
    va_start(ap, f);
    n = vsnprintf(buf, (size_t)-1 / 2u, f, ap);
    va_end(ap);
    return n;
}

int vfprintf(FILE *fp, const char *f, va_list ap) { (void)fp; (void)f; (void)ap; return 0; }
int fprintf(FILE *fp, const char *f, ...) { (void)fp; (void)f; return 0; }

int gettimeofday(struct timeval *tv, void *tz)   /* AMY times itself by samples rendered */
{
    (void)tz;
    if (tv)
        tv->tv_sec = tv->tv_usec = 0;
    return 0;
}

void abort(void)
{
    for (;;)
        __builtin_trap();
}

void __assert_func(const char *file, int line, const char *fn, const char *expr)
{
    (void)file;
    (void)line;
    (void)fn;
    (void)expr;
    abort();
}
