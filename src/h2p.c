/*
 * Pure (no IO, no heap) parsers for third-party banks: DEFLATE + ZIP, u-he .h2p text, Prophet-5 .syx, and the mappings from
 * Repro-5 / Prophet-5 values to TinyPro5 parameters. See bank.h.
 */
#include <math.h>
#include <string.h>

#include "bank.h"

/* ====================================================================================================================
 * inflate (RFC 1951), canonical-Huffman decoder after Mark Adler's public-domain "puff" structure
 * ==================================================================================================================== */
#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const uint8_t *in; size_t inlen, incnt;
    uint32_t bitbuf; int bitcnt;
    uint8_t *out; size_t outcap, outcnt;
} infl;

typedef struct { short count[MAXBITS + 1]; short symbol[MAXLCODES]; } huff;

static int get_bits(infl *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) return -1;
        val |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1u));
}

static int decode_sym(infl *s, const huff *h)
{
    int len, code = 0, first = 0, count, index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        const int b = get_bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -2;
}

static int construct(huff *h, const short *length, int n)
{
    int symbol, len, left;
    short offs[MAXBITS + 1];
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}

static int inflate_codes(infl *s, const huff *lc, const huff *dc)
{
    static const short lens[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dists[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    int symbol;
    do {
        symbol = decode_sym(s, lc);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            if (s->outcnt >= s->outcap) return -3;
            s->out[s->outcnt++] = (uint8_t)symbol;
        } else if (symbol > 256) {
            int len, dist, e;
            symbol -= 257;
            if (symbol >= 29) return -1;
            e = get_bits(s, lext[symbol]);
            if (e < 0) return -1;
            len = lens[symbol] + e;
            symbol = decode_sym(s, dc);
            if (symbol < 0 || symbol >= 30) return -1;
            e = get_bits(s, dext[symbol]);
            if (e < 0) return -1;
            dist = dists[symbol] + e;
            if ((size_t)dist > s->outcnt) return -1;
            if (s->outcnt + (size_t)len > s->outcap) return -3;
            while (len--) { s->out[s->outcnt] = s->out[s->outcnt - (size_t)dist]; s->outcnt++; }
        }
    } while (symbol != 256);
    return 0;
}

static int inflate_stored(infl *s)
{
    unsigned len, nlen;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return -1;
    len = s->in[s->incnt] | (unsigned)s->in[s->incnt + 1] << 8;
    nlen = s->in[s->incnt + 2] | (unsigned)s->in[s->incnt + 3] << 8;
    s->incnt += 4;
    if (len != (~nlen & 0xFFFFu)) return -1;
    if (s->incnt + len > s->inlen) return -1;
    if (s->outcnt + len > s->outcap) return -3;
    memcpy(s->out + s->outcnt, s->in + s->incnt, len);
    s->incnt += len;
    s->outcnt += len;
    return 0;
}

static int inflate_fixed(infl *s)
{
    static huff lc, dc;
    static int built;
    if (!built) {
        short lengths[FIXLCODES];
        int sym;
        for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
        construct(&lc, lengths, FIXLCODES);
        for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
        construct(&dc, lengths, MAXDCODES);
        built = 1;
    }
    return inflate_codes(s, &lc, &dc);
}

static int inflate_dynamic(infl *s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[MAXLCODES + MAXDCODES];
    huff lc, dc;
    int nlen, ndist, ncode, index, err, v;
    v = get_bits(s, 5); if (v < 0) return -1; nlen = v + 257;
    v = get_bits(s, 5); if (v < 0) return -1; ndist = v + 1;
    v = get_bits(s, 4); if (v < 0) return -1; ncode = v + 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES) return -1;
    for (index = 0; index < ncode; index++) {
        v = get_bits(s, 3);
        if (v < 0) return -1;
        lengths[order[index]] = (short)v;
    }
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (construct(&lc, lengths, 19) != 0) return -1;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode_sym(s, &lc), len, rep;
        if (symbol < 0) return -1;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            len = 0;
            if (symbol == 16) {
                if (index == 0) return -1;
                len = lengths[index - 1];
                v = get_bits(s, 2); if (v < 0) return -1; rep = 3 + v;
            } else if (symbol == 17) {
                v = get_bits(s, 3); if (v < 0) return -1; rep = 3 + v;
            } else {
                v = get_bits(s, 7); if (v < 0) return -1; rep = 11 + v;
            }
            if (index + rep > nlen + ndist) return -1;
            while (rep--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -1;
    err = construct(&lc, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lc.count[0] != 1)) return -1;
    err = construct(&dc, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dc.count[0] != 1)) return -1;
    return inflate_codes(s, &lc, &dc);
}

int tp5_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap, size_t *outlen)
{
    infl s;
    int last, type, err;
    memset(&s, 0, sizeof s);
    s.in = in; s.inlen = inlen; s.out = out; s.outcap = outcap;
    do {
        last = get_bits(&s, 1);
        type = get_bits(&s, 2);
        if (last < 0 || type < 0) return -1;
        err = type == 0 ? inflate_stored(&s) : type == 1 ? inflate_fixed(&s) : type == 2 ? inflate_dynamic(&s) : -1;
        if (err) return err;
    } while (!last);
    if (outlen) *outlen = s.outcnt;
    return 0;
}

/* ====================================================================================================================
 * zip
 * ==================================================================================================================== */
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

static int ends_with_ci(const char *s, const char *suffix)
{
    const size_t a = strlen(s), b = strlen(suffix);
    size_t i;
    if (b > a) return 0;
    for (i = 0; i < b; i++) {
        char x = s[a - b + i], y = suffix[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

int tp5_zip_walk(const uint8_t *zip, size_t zlen, const char *suffix, uint8_t *scratch, size_t scratch_cap,
                 tp5_zip_cb cb, void *user)
{
    size_t eocd, cd, i, delivered = 0;
    unsigned entries, e;
    if (!zip || zlen < 22) return -1;
    for (eocd = zlen - 22;; eocd--) {                       /* the end record is within the last 64 KB + 22 bytes */
        if (rd32(zip + eocd) == 0x06054b50u) break;
        if (eocd == 0 || zlen - eocd > 65557u) return -1;
    }
    entries = rd16(zip + eocd + 10);
    cd = rd32(zip + eocd + 16);
    if (entries == 0xFFFFu || cd == 0xFFFFFFFFu) return -3;     /* zip64 */
    i = cd;
    for (e = 0; e < entries; e++) {
        unsigned flags, method, nlen, xlen, clen;
        size_t csize, usize, lho, data;
        char name[260];
        if (i + 46 > zlen || rd32(zip + i) != 0x02014b50u) return -2;
        flags = rd16(zip + i + 8);
        method = rd16(zip + i + 10);
        csize = rd32(zip + i + 20);
        usize = rd32(zip + i + 24);
        nlen = rd16(zip + i + 28);
        xlen = rd16(zip + i + 30);
        clen = rd16(zip + i + 32);
        lho = rd32(zip + i + 42);
        if (i + 46 + nlen + xlen + clen > zlen) return -2;
        if (nlen >= sizeof name) nlen = sizeof name - 1;
        memcpy(name, zip + i + 46, nlen);
        name[nlen] = 0;
        i += 46 + rd16(zip + i + 28) + xlen + clen;
        if (name[0] && name[strlen(name) - 1] == '/') continue;             /* directory */
        if (!ends_with_ci(name, suffix)) continue;
        if (flags & 1u) return -3;                                           /* encrypted */
        if (lho + 30 > zlen || rd32(zip + lho) != 0x04034b50u) return -2;
        data = lho + 30 + rd16(zip + lho + 26) + rd16(zip + lho + 28);
        if (data + csize > zlen || usize > scratch_cap) continue;            /* too big for the scratch buffer: skip */
        if (method == 0) {
            if (csize != usize) continue;
            memcpy(scratch, zip + data, usize);
        } else if (method == 8) {
            size_t got = 0;
            if (tp5_inflate(zip + data, csize, scratch, scratch_cap, &got) != 0 || got != usize) continue;   /* corrupt entry */
        } else {
            continue;
        }
        delivered++;
        if (cb(name, scratch, usize, user) != 0) break;
    }
    return (int)delivered;
}

/* ====================================================================================================================
 * u-he .h2p
 * ==================================================================================================================== */
static const struct { const char *sec, *key; int idx; } k_h2p_map[] = {
    { "FEnv", "Atk", H_FENV_ATK }, { "FEnv", "Dec", H_FENV_DEC }, { "FEnv", "Sus", H_FENV_SUS }, { "FEnv", "Rel", H_FENV_REL },
    { "AEnv", "Atk", H_AENV_ATK }, { "AEnv", "Dec", H_AENV_DEC }, { "AEnv", "Sus", H_AENV_SUS }, { "AEnv", "Rel", H_AENV_REL },
    { "OscA", "Oct", H_A_OCT }, { "OscA", "Freq", H_A_FREQ }, { "OscA", "Fine", H_A_FINE }, { "OscA", "SawOn", H_A_SAW },
    { "OscA", "RectOn", H_A_RECT }, { "OscA", "PWidth", H_A_PW }, { "OscA", "Sync", H_A_SYNC }, { "OscA", "VModFM", H_A_VFM },
    { "OscA", "VModPWM", H_A_VPWM },
    { "OscB", "Oct", H_B_OCT }, { "OscB", "Freq", H_B_FREQ }, { "OscB", "Fine", H_B_FINE }, { "OscB", "SawOn", H_B_SAW },
    { "OscB", "TriOn", H_B_TRI }, { "OscB", "RectOn", H_B_RECT }, { "OscB", "PWidth", H_B_PW }, { "OscB", "LowFrq", H_B_LOW },
    { "OscB", "KPitch", H_B_KPITCH },
    { "VMod", "OscBAmt", H_VMOD_B }, { "VMod", "EnvAmt", H_VMOD_ENV },
    { "MIX", "OscA", H_MIX_A }, { "MIX", "OscB", H_MIX_B }, { "MIX", "Noise", H_MIX_NOISE },
    { "Filter", "Cutoff", H_FLT_CUT }, { "Filter", "Reso", H_FLT_RES }, { "Filter", "KeyAmt", H_FLT_KEY },
    { "Filter", "EnvAmt", H_FLT_ENV }, { "Filter", "VMod", H_FLT_VMOD }, { "Filter", "EnvPol", H_FLT_POL },
    { "Glide", "Amt", H_GLIDE },
};

/* Locale-independent number reader: [+-]digits[.digits][e[+-]digits]; 0 when the text is not a number */
static int parse_num(const char *s, const char *end, float *out)
{
    double v = 0.0, frac = 0.1;
    int neg = 0, digits = 0;
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    if (s < end && (*s == '-' || *s == '+')) { neg = *s == '-'; s++; }
    while (s < end && *s >= '0' && *s <= '9') { v = v * 10.0 + (*s - '0'); s++; digits++; }
    if (s < end && *s == '.') {
        s++;
        while (s < end && *s >= '0' && *s <= '9') { v += (*s - '0') * frac; frac *= 0.1; s++; digits++; }
    }
    if (!digits) return 0;
    if (s < end && (*s == 'e' || *s == 'E')) {
        int eneg = 0, ev = 0;
        s++;
        if (s < end && (*s == '-' || *s == '+')) { eneg = *s == '-'; s++; }
        while (s < end && *s >= '0' && *s <= '9' && ev < 400) { ev = ev * 10 + (*s - '0'); s++; }
        while (ev-- > 0) v = eneg ? v * 0.1 : v * 10.0;
    }
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\r')) s++;
    if (s != end) return 0;                       /* trailing text: a name or a path, not a number */
    *out = (float)(neg ? -v : v);
    return 1;
}

int tp5_h2p_parse(const char *text, size_t len, tp5_h2p *out)
{
    char sec[24] = "";
    int foreign = 0;
    size_t i = 0;
    memset(out, 0, sizeof *out);
    if (!text) return 0;
    while (i < len) {
        const char *ls = text + i, *le;
        size_t n = 0;
        while (i + n < len && text[i + n] != '\n') n++;
        le = ls + n;
        i += n + 1;
        if (n > 0 && le[-1] == '\r') le--;
        if (le - ls >= 4 && memcmp(ls, "$$$$", 4) == 0) break;                    /* the binary tail starts here */
        if (le - ls >= 10 && memcmp(ls, "// Section", 10) == 0) break;
        if (le <= ls) continue;
        if (*ls == '#') {
            if (le - ls > 4 && memcmp(ls, "#cm=", 4) == 0) {
                size_t m = (size_t)(le - ls - 4);
                if (m >= sizeof sec) m = sizeof sec - 1;
                memcpy(sec, ls + 4, m);
                sec[m] = 0;
            } else if (le - ls > 4 && memcmp(ls, "#AM=", 4) == 0) {
                foreign = !((le - ls - 4) == 7 && memcmp(ls + 4, "Repro-5", 7) == 0);
            }
            continue;
        }
        {
            const char *eq = ls;
            while (eq < le && *eq != '=') eq++;
            if (eq < le && eq > ls && sec[0]) {
                const size_t kl = (size_t)(eq - ls);
                size_t m;
                float v;
                for (m = 0; m < sizeof k_h2p_map / sizeof *k_h2p_map; m++) {
                    if (strcmp(sec, k_h2p_map[m].sec) == 0 && strlen(k_h2p_map[m].key) == kl && memcmp(ls, k_h2p_map[m].key, kl) == 0) {
                        if (parse_num(eq + 1, le, &v) && v == v && v > -1e6f && v < 1e6f) {
                            out->v[k_h2p_map[m].idx] = v;
                            out->has[k_h2p_map[m].idx] = 1;
                            out->n_keys++;
                        }
                        break;
                    }
                }
            }
        }
    }
    return !foreign && out->n_keys > 0;
}

/* ---- emit helpers ---------------------------------------------------------------------------------------------- */
static float clampv(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

static void emit(tp5_kv *out, int *n, int cap, int id, float v)
{
    v = clampv(v, tp5_params[id].min, tp5_params[id].max);
    if (tp5_params[id].is_int) v = floorf(v + 0.5f);
    if (fabsf(v - tp5_params[id].def) < 1e-4f) return;             /* defaults are implied */
    if (*n < cap) { out[*n].id = (uint8_t)id; out[*n].v = v; (*n)++; }
}

#define HV(i, d) (h->has[i] ? h->v[i] : (d))

/* Repro-5 plays  note + 12 (oct - 1) + semitone + fine / 100 - 1.994 semitones. A fixed-pitch Osc B
 * (keyboard off) sits at MIDI 72.04 for octave 2, semitone 0. */
#define REPRO_PITCH_OFFSET (-1.994f)

int tp5_h2p_to_kv(const tp5_h2p *h, tp5_kv *out, int cap)
{
    int n = 0;
    float oct, fr, fine;

    if (h->has[H_FLT_CUT]) emit(out, &n, cap, P_CUTOFF, h->v[H_FLT_CUT] / 100.f);
    if (h->has[H_FLT_RES]) emit(out, &n, cap, P_RESO, h->v[H_FLT_RES] / 100.f);
    if (h->has[H_FLT_KEY]) emit(out, &n, cap, P_KBD, h->v[H_FLT_KEY] / 100.f);
    if (h->has[H_FLT_ENV]) emit(out, &n, cap, P_ENVAMT, (h->v[H_FLT_ENV] / 100.f) * (HV(H_FLT_POL, 0.f) > 0.5f ? -1.f : 1.f));
    if (h->has[H_FENV_ATK]) emit(out, &n, cap, P_F_ATK, h->v[H_FENV_ATK] / 100.f);
    if (h->has[H_FENV_DEC]) emit(out, &n, cap, P_F_DEC, h->v[H_FENV_DEC] / 100.f);
    if (h->has[H_FENV_SUS]) emit(out, &n, cap, P_F_SUS, h->v[H_FENV_SUS] / 100.f);
    if (h->has[H_FENV_REL]) emit(out, &n, cap, P_F_REL, h->v[H_FENV_REL] / 100.f);
    if (h->has[H_AENV_ATK]) emit(out, &n, cap, P_A_ATK, h->v[H_AENV_ATK] / 100.f);
    if (h->has[H_AENV_DEC]) emit(out, &n, cap, P_A_DEC, h->v[H_AENV_DEC] / 100.f);
    if (h->has[H_AENV_SUS]) emit(out, &n, cap, P_A_SUS, h->v[H_AENV_SUS] / 100.f);
    if (h->has[H_AENV_REL]) emit(out, &n, cap, P_A_REL, h->v[H_AENV_REL] / 100.f);
    if (h->has[H_GLIDE]) emit(out, &n, cap, P_GLIDE, h->v[H_GLIDE] / 100.f);

    /* Osc A */
    if (h->has[H_A_OCT] || h->has[H_A_FREQ] || h->has[H_A_FINE]) {
        oct = HV(H_A_OCT, 1.f); fr = HV(H_A_FREQ, 0.f); fine = HV(H_A_FINE, 0.f);
        emit(out, &n, cap, P_A_SEMI, 12.f * (oct - 1.f) + fr + fine / 100.f + REPRO_PITCH_OFFSET);
    }
    if (h->has[H_A_SAW] || h->has[H_A_RECT]) {
        const int saw = HV(H_A_SAW, 0.f) > 0.5f, rect = HV(H_A_RECT, 0.f) > 0.5f;
        emit(out, &n, cap, P_A_WAVE, saw && !rect ? 0.f : (rect && !saw ? 1.f : 0.5f));
        emit(out, &n, cap, P_A_VOL, (saw || rect) ? HV(H_MIX_A, 0.f) / 100.f : 0.f);
    } else if (h->has[H_MIX_A]) {
        emit(out, &n, cap, P_A_VOL, h->v[H_MIX_A] / 100.f);
    }
    if (h->has[H_A_PW]) emit(out, &n, cap, P_A_PW, h->v[H_A_PW] / 100.f);
    if (h->has[H_A_SYNC]) emit(out, &n, cap, P_SYNC, h->v[H_A_SYNC] > 0.5f ? 1.f : 0.f);

    /* Osc B */
    {
        const int kb = HV(H_B_KPITCH, 1.f) > 0.5f, low = HV(H_B_LOW, 0.f) > 0.5f;
        if (h->has[H_B_OCT] || h->has[H_B_FREQ] || h->has[H_B_FINE]) {
            float semi;
            oct = HV(H_B_OCT, 2.f); fr = HV(H_B_FREQ, 0.f); fine = HV(H_B_FINE, 0.f);
            semi = 12.f * (oct - 1.f) + fr + fine / 100.f + (kb ? REPRO_PITCH_OFFSET : 0.04f);
            if (!low) emit(out, &n, cap, P_B_SEMI, semi);
        }
        if (h->has[H_B_SAW] || h->has[H_B_TRI] || h->has[H_B_RECT]) {
            const int sb = HV(H_B_SAW, 0.f) > 0.5f, tb = HV(H_B_TRI, 0.f) > 0.5f, pb = HV(H_B_RECT, 0.f) > 0.5f;
            float bw = 0.f;
            if (!sb && tb && !pb) bw = 0.5f;
            else if (!sb && !tb && pb) bw = 1.f;
            else if (sb && tb && !pb) bw = 0.25f;
            else if (!sb && tb && pb) bw = 0.75f;
            else if (sb && !tb && pb) bw = 0.85f;
            else if (sb && tb && pb) bw = 0.6f;
            emit(out, &n, cap, P_B_WAVE, bw);
            emit(out, &n, cap, P_B_VOL, (!low) ? HV(H_MIX_B, 0.f) / 100.f : 0.f);
        } else if (h->has[H_MIX_B]) {
            emit(out, &n, cap, P_B_VOL, low ? 0.f : h->v[H_MIX_B] / 100.f);
        }
        if (h->has[H_B_PW]) emit(out, &n, cap, P_B_PW, h->v[H_B_PW] / 100.f);
        if (h->has[H_B_LOW]) emit(out, &n, cap, P_B_LFO, low ? 1.f : 0.f);
        if (h->has[H_B_KPITCH]) emit(out, &n, cap, P_B_KBD, kb ? 1.f : 0.f);
    }
    if (h->has[H_MIX_NOISE]) emit(out, &n, cap, P_NOISE, h->v[H_MIX_NOISE] / 100.f);

    /* Poly-Mod */
    if (h->has[H_VMOD_B]) emit(out, &n, cap, P_PM_OSCB, h->v[H_VMOD_B] / 100.f);
    if (h->has[H_VMOD_ENV]) emit(out, &n, cap, P_PM_FENV, h->v[H_VMOD_ENV] / 100.f);
    if (h->has[H_A_VFM] || h->has[H_A_VPWM] || h->has[H_FLT_VMOD])
        emit(out, &n, cap, P_PM_DEST, (HV(H_A_VFM, 0.f) > 0.5f ? 1.f : 0.f) + (HV(H_A_VPWM, 0.f) > 0.5f ? 2.f : 0.f) +
                                       (HV(H_FLT_VMOD, 0.f) > 0.5f ? 4.f : 0.f));
    return n;
}

/* ====================================================================================================================
 * Prophet-5 .syx
 * ==================================================================================================================== */
enum {   /* byte index of each field in the 133-byte native patch */
    X_FREQ_A = 0, X_FREQ_B, X_FINE_B, X_SAW_A, X_PULSE_A, X_SAW_B, X_TRI_B, X_PULSE_B, X_PW_A, X_PW_B, X_SYNC, X_LOW_B, X_KEY_B,
    X_GLIDE, X_LEVEL_A, X_LEVEL_B, X_NOISE, X_CUTOFF, X_RES, X_KEY_FILTER, X_FILTER_REV, X_LFO_RATE, X_LFO_INITIAL, X_LFO_SAW,
    X_LFO_TRI, X_LFO_PULSE, X_WHEEL_MIX, X_W_FREQ_A, X_W_FREQ_B, X_W_PW_A, X_W_PW_B, X_W_FILTER, X_POLY_ENV, X_POLY_B,
    X_POLY_FREQ, X_POLY_PW, X_POLY_FILTER, X_VINTAGE,
    X_ENV_FILTER = 40, X_ATK_F = 43, X_ATK_A, X_DEC_F, X_DEC_A, X_SUS_F, X_SUS_A, X_REL_F, X_REL_A, X_RELEASE_ON, X_UNISON,
    X_UNISON_DETUNE = 54
};

/* in-place replace of every `from` by `to` in a NUL-terminated buffer of `cap` bytes (stops when it would not fit) */
static void name_replace(char *s, size_t cap, const char *from, const char *to)
{
    const size_t fl = strlen(from), tl = strlen(to);
    char *p = s;
    while ((p = strstr(p, from)) != NULL) {
        const size_t len = strlen(s);
        if (len - fl + tl + 1 > cap) return;
        memmove(p + tl, p + fl, strlen(p + fl) + 1);
        memcpy(p, to, tl);
        p += tl;
    }
}

/* the words "Ii" and "Iii" (bounded by non-alphanumerics) -> "II" / "III" */
static void name_roman(char *s)
{
    char *p;
    for (p = s; *p; p++) {
        const int start = (p == s) || !((p[-1] >= 'A' && p[-1] <= 'Z') || (p[-1] >= 'a' && p[-1] <= 'z') || (p[-1] >= '0' && p[-1] <= '9'));
        size_t n = 0;
        if (!start || p[0] != 'I') continue;
        while (p[n] == 'i' || (n == 0 && p[n] == 'I')) n++;
        if ((n == 2 || n == 3) && !((p[n] >= 'A' && p[n] <= 'Z') || (p[n] >= 'a' && p[n] <= 'z') || (p[n] >= '0' && p[n] <= '9'))) {
            size_t q;
            for (q = 1; q < n; q++) p[q] = 'I';
        }
    }
}

int tp5_syx_next(const uint8_t *d, size_t len, size_t *pos, uint8_t raw[133], char name[24])
{
    size_t i = *pos;
    while (i < len) {
        size_t s, e;
        while (i < len && d[i] != 0xF0) i++;
        if (i >= len) break;
        s = i;
        e = s + 1;
        while (e < len && d[e] != 0xF7 && d[e] < 0x80 && e - s < 256) e++;
        if (e >= len || d[e] != 0xF7) { i = s + 1; continue; }                 /* unterminated / contains a status byte */
        i = e + 1;
        {
            const size_t flen = e - s + 1;
            const uint8_t *f = d + s;
            const uint8_t *pk;
            size_t groups, k, j, size;
            if (flen < 8 || f[1] != 0x01 || (f[2] != 0x31 && f[2] != 0x32) || (f[3] != 2 && f[3] != 3)) continue;
            if (f[3] == 2) { pk = f + 6; size = flen - 7; if (size != 152) continue; }
            else { pk = f + 4; size = flen - 5; if (size != 147) continue; }
            memset(raw, 0, 133);
            groups = (size + 7) / 8;
            for (k = 0; k < groups; k++)
                for (j = 0; j < 7; j++) {
                    const size_t src = k * 8 + 1 + j, dst = k * 7 + j;
                    if (src >= size || dst >= 133) continue;
                    raw[dst] = (uint8_t)(pk[src] | (((pk[k * 8] >> j) & 1u) << 7));
                }
            {   /* name: raw[65..84], printable ASCII, spaces collapsed; ALL CAPS becomes Title Case:
                 * capital after every non-letter, "'S" -> "'s", " W " -> " w/ ",
                 * Pwm -> PWM, "E Piano" -> "E.Piano", the words Ii / Iii -> II / III */
                char tmp[24];
                size_t m = 0, o = 0;
                int has_lower = 0, prev_space = 1, prev_alpha = 0;
                for (k = 65; k < 85; k++) {
                    uint8_t c = raw[k];
                    if (c < 32 || c > 126) c = ' ';
                    if (c == ' ') { if (!prev_space && m < sizeof tmp - 1) tmp[m++] = ' '; prev_space = 1; continue; }
                    prev_space = 0;
                    if (c >= 'a' && c <= 'z') has_lower = 1;
                    if (m < sizeof tmp - 1) tmp[m++] = (char)c;
                }
                while (m > 0 && tmp[m - 1] == ' ') m--;
                for (k = 0; k < m; k++) {
                    char c = tmp[k];
                    if (!has_lower) {
                        const int alpha = (c >= 'A' && c <= 'Z');
                        if (alpha && prev_alpha) c = (char)(c + 32);
                        if (alpha && o > 0 && name[o - 1] == '\'') c = (char)(c + 32);          /* "'S" -> "'s" */
                        prev_alpha = alpha;
                    }
                    name[o++] = c;
                }
                name[o] = 0;
                if (!has_lower) {
                    name_replace(name, 24, " W ", " w/ ");
                    name_replace(name, 24, "Pwm", "PWM");
                    name_replace(name, 24, "E Piano", "E.Piano");
                    name_roman(name);
                }
                if (o == 0) { name[0] = '?'; name[1] = 0; }
            }
            *pos = i;
            return 1;
        }
    }
    *pos = len;
    return 0;
}

int tp5_p5_to_kv(const uint8_t *r, tp5_kv *out, int cap)
{
#define G(i) ((float)(r[i] > 127 ? 127 : r[i]))
    static const uint8_t lfo_dest_enum[8] = { 7, 0, 1, 5, 2, 3, 4, 6 };       /* wheel bit mask -> LFODest enum */
    const int curtis = r[X_FILTER_REV] == 1;
    int n = 0;
    float v;

    {   /* oscillators */
        const int sawa = r[X_SAW_A] != 0, pulsea = r[X_PULSE_A] != 0;
        const int sb = r[X_SAW_B] != 0, tb = r[X_TRI_B] != 0, pb = r[X_PULSE_B] != 0, low = r[X_LOW_B] != 0, keyb = r[X_KEY_B] != 0;
        float bw = 0.f, b_semi;
        emit(out, &n, cap, P_A_WAVE, (sawa && !pulsea) ? 0.f : ((pulsea && !sawa) ? 1.f : 0.5f));
        emit(out, &n, cap, P_A_VOL, (sawa || pulsea) ? G(X_LEVEL_A) / 127.f : 0.f);
        emit(out, &n, cap, P_A_SEMI, (float)((int)(G(X_FREQ_A) > 96.f ? 96.f : G(X_FREQ_A)) / 2 - 12));
        emit(out, &n, cap, P_A_PW, G(X_PW_A) / 127.f);
        if (!sb && tb && !pb) bw = 0.5f;
        else if (!sb && !tb && pb) bw = 1.f;
        else if (sb && tb && !pb) bw = 0.25f;
        else if (!sb && tb && pb) bw = 0.75f;
        else if (sb && !tb && pb) bw = 0.85f;
        else if (sb && tb && pb) bw = 0.6f;
        emit(out, &n, cap, P_B_WAVE, bw);
        emit(out, &n, cap, P_B_VOL, ((sb || tb || pb) && !low) ? G(X_LEVEL_B) / 127.f : 0.f);
        emit(out, &n, cap, P_B_PW, G(X_PW_B) / 127.f);
        if (keyb) b_semi = (float)((int)(G(X_FREQ_B) > 96.f ? 96.f : G(X_FREQ_B)) / 2 - 12);
        else b_semi = 12.f + G(X_FREQ_B) * 108.f / 127.f - 60.f;
        if (low) {
            const float note = keyb ? 60.f + b_semi : 12.f + G(X_FREQ_B) * 108.f / 127.f;
            const float hz = 440.f * exp2f((note - 69.f) / 12.f) / 128.f;
            b_semi = 6.f * log2f(hz / 4.f);
        }
        emit(out, &n, cap, P_B_SEMI, b_semi + G(X_FINE_B) * 100.f / 128.f / 100.f);
        emit(out, &n, cap, P_B_LFO, low ? 1.f : 0.f);
        emit(out, &n, cap, P_B_KBD, keyb ? 1.f : 0.f);
        emit(out, &n, cap, P_SYNC, r[X_SYNC] ? 1.f : 0.f);
        emit(out, &n, cap, P_NOISE, G(X_NOISE) / 127.f);
        emit(out, &n, cap, P_GLIDE, G(X_GLIDE) / 127.f);
    }
    {   /* filter */
        const float c0 = G(X_CUTOFF);
        const float ssi = curtis ? 0.f : (127.f - c0 < 66.f ? 127.f - c0 : 66.f) * 38.f / 256.f * 0.0713f / 10.f;
        static const float track[3] = { 0.f, 0.5f, 1.f };
        emit(out, &n, cap, P_KBD, track[r[X_KEY_FILTER] > 2 ? 2 : r[X_KEY_FILTER]]);
        emit(out, &n, cap, P_CUTOFF, clampv(c0 / 127.f + ssi, 0.f, 1.f));
        emit(out, &n, cap, P_RESO, G(X_RES) / 127.f);
        emit(out, &n, cap, P_ENVAMT, G(X_ENV_FILTER) / 127.f);
    }
    {   /* envelopes: dial positions */
        const int rel_on = r[X_RELEASE_ON] != 0;
        emit(out, &n, cap, P_A_ATK, G(X_ATK_A) / 127.f);
        emit(out, &n, cap, P_F_ATK, G(X_ATK_F) / 127.f);
        emit(out, &n, cap, P_A_DEC, G(X_DEC_A) / 127.f);
        emit(out, &n, cap, P_A_SUS, G(X_SUS_A) / 127.f);
        emit(out, &n, cap, P_A_REL, (rel_on ? G(X_REL_A) : 16.f) / 127.f);
        emit(out, &n, cap, P_F_SUS, G(X_SUS_F) / 127.f);
        emit(out, &n, cap, P_F_DEC, G(X_DEC_F) / 127.f);
        emit(out, &n, cap, P_F_REL, (rel_on ? G(X_REL_F) : 16.f) / 127.f);
    }
    {   /* poly-mod */
        const int dest = (r[X_POLY_FREQ] ? 1 : 0) | (r[X_POLY_PW] ? 2 : 0) | (r[X_POLY_FILTER] ? 4 : 0);
        emit(out, &n, cap, P_PM_DEST, (float)dest);
        emit(out, &n, cap, P_PM_FENV, G(X_POLY_ENV) / 127.f);
        emit(out, &n, cap, P_PM_OSCB, G(X_POLY_B) / 127.f);
    }
    {   /* LFO / wheel */
        const int wb = ((r[X_W_FREQ_A] || r[X_W_FREQ_B]) ? 1 : 0) | ((r[X_W_PW_A] || r[X_W_PW_B]) ? 2 : 0) | (r[X_W_FILTER] ? 4 : 0);
        const float rate_raw = G(X_LFO_RATE);
        float x = -3.1005f + 0.05387f * rate_raw, frac, amt;
        if (rate_raw > 100.f) x += (logf(500.f) - (-3.1005f + 0.05387f * 127.f)) * powf((rate_raw - 100.f) / 27.f, 2.f);
        if (rate_raw < 20.f) x -= (-3.1005f - logf(0.022f)) * powf((20.f - rate_raw) / 20.f, 2.f);
        emit(out, &n, cap, P_LFO_RATE, clampv(logf(expf(x) / 0.1f) / logf(300.f), 0.f, 1.f));
        emit(out, &n, cap, P_LFO_WAVE, r[X_LFO_TRI] ? 1.f : (r[X_LFO_SAW] ? 0.f : 2.f));
        emit(out, &n, cap, P_LFO_DEST, (float)lfo_dest_enum[wb]);
        frac = powf(G(X_LFO_INITIAL) / 127.f, 1.737f);
        if (wb & 1) amt = sqrtf(235.f * frac / 1200.f);
        else if (wb & 4) amt = 4.f * frac / 3.f;
        else if (wb & 2) amt = frac;
        else amt = 0.f;
        emit(out, &n, cap, P_LFO_AMT, clampv(amt, 0.f, 1.f));
    }
    v = 0.08f + 0.72f * G(X_VINTAGE) / 127.f;
    emit(out, &n, cap, P_DRIFT, v);
    emit(out, &n, cap, P_UNISON, r[X_UNISON] ? 1.f : 0.f);
    emit(out, &n, cap, P_DETUNE, (float)(r[X_UNISON_DETUNE] > 7 ? 7 : r[X_UNISON_DETUNE]) / 7.f);
    return n;
#undef G
}
