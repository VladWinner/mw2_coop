// The updater's core (updcore.h): no game, no network. The JSON reader takes only what the updater needs from GitHub's release answer; the inflate is the
// canonical-Huffman decoder of RFC 1951 (the shape of Mark Adler's puff.c); the zip reader takes the archives tools/make_release.sh makes (Python's zipfile,
// deflate or stored, no ZIP64).
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "updcore.h"

// ---------------------------------------------------------------------------------------------- versions

int upd_parse_version(const char *s, int v[3])
{
	if (!s)
		return 0;
	if (*s == 'v' || *s == 'V')
		s++;
	for (int i = 0; i < 3; i++) {
		if (*s < '0' || *s > '9')
			return 0;
		long n = 0;
		while (*s >= '0' && *s <= '9' && n < 100000)
			n = n * 10 + (*s++ - '0');
		v[i] = (int)n;
		if (i < 2) {
			if (*s != '.')
				return 0;
			s++;
		}
	}
	return *s == 0 || *s == '-' || *s == '+';
}

int upd_compare_versions(const int a[3], const int b[3])
{
	for (int i = 0; i < 3; i++)
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	return 0;
}

static long eight_digits(const char *s)
{
	long n = 0;
	for (int i = 0; i < 8; i++) {
		if (s[i] < '0' || s[i] > '9')
			return 0;
		n = n * 10 + (s[i] - '0');
	}
	return n;
}

long upd_nightly_date(const char *s)
{
	if (!s || strncmp(s, "nightly-", 8) != 0)
		return 0;
	return eight_digits(s + 8);
}

long upd_iso_date(const char *s)
{
	if (!s || strlen(s) < 10 || s[4] != '-' || s[7] != '-')
		return 0;
	char d[9] = {s[0], s[1], s[2], s[3], s[5], s[6], s[8], s[9], 0};
	return eight_digits(d);
}

// ---------------------------------------------------------------------------------------------- JSON

typedef struct {
	const char *p, *end;
	int depth, bad;
} js_t;

static void js_ws(js_t *j)
{
	while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n'))
		j->p++;
}

static int js_peek(js_t *j)
{
	js_ws(j);
	return j->p < j->end ? (unsigned char)*j->p : -1;
}

static int js_hex4(const char *p)
{
	int v = 0;
	for (int i = 0; i < 4; i++) {
		char c = p[i];
		v <<= 4;
		if (c >= '0' && c <= '9')
			v |= c - '0';
		else if (c >= 'a' && c <= 'f')
			v |= c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			v |= c - 'A' + 10;
		else
			return -1;
	}
	return v;
}

// a string into out (UTF-8, cut to size - 1); out may be NULL to skip it
static int js_string(js_t *j, char *out, size_t size)
{
	size_t n = 0;
	if (js_peek(j) != '"') {
		j->bad = 1;
		return 0;
	}
	j->p++;
	while (j->p < j->end && *j->p != '"') {
		unsigned c = (unsigned char)*j->p++;
		if (c == '\\') {
			if (j->p >= j->end)
				break;
			char e = *j->p++;
			switch (e) {
			case 'n': c = '\n'; break;
			case 't': c = '\t'; break;
			case 'r': c = '\r'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'u': {
				if (j->end - j->p < 4 || (c = (unsigned)js_hex4(j->p)) == (unsigned)-1) {
					j->bad = 1;
					return 0;
				}
				j->p += 4;
				if (c >= 0xd800 && c < 0xdc00 && j->end - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') { // a surrogate pair
					int lo = js_hex4(j->p + 2);
					if (lo >= 0xdc00 && lo < 0xe000) {
						c = 0x10000 + ((c - 0xd800) << 10) + (unsigned)(lo - 0xdc00);
						j->p += 6;
					}
				}
				char u[4];
				int k;
				if (c < 0x80)
					u[0] = (char)c, k = 1;
				else if (c < 0x800)
					u[0] = (char)(0xc0 | c >> 6), u[1] = (char)(0x80 | (c & 0x3f)), k = 2;
				else if (c < 0x10000)
					u[0] = (char)(0xe0 | c >> 12), u[1] = (char)(0x80 | ((c >> 6) & 0x3f)), u[2] = (char)(0x80 | (c & 0x3f)), k = 3;
				else
					u[0] = (char)(0xf0 | c >> 18), u[1] = (char)(0x80 | ((c >> 12) & 0x3f)), u[2] = (char)(0x80 | ((c >> 6) & 0x3f)), u[3] = (char)(0x80 | (c & 0x3f)), k = 4;
				for (int i = 0; i < k; i++)
					if (out && n + 1 < size)
						out[n++] = u[i];
				continue;
			}
			default: c = (unsigned char)e; break; // \" \\ \/
			}
		}
		if (out && n + 1 < size)
			out[n++] = (char)c;
	}
	if (j->p >= j->end) {
		j->bad = 1;
		return 0;
	}
	j->p++; // the closing quote
	if (out && size)
		out[n] = 0;
	return 1;
}

static void js_skip(js_t *j);

static int js_expect(js_t *j, char c)
{
	if (js_peek(j) != c) {
		j->bad = 1;
		return 0;
	}
	j->p++;
	return 1;
}

// a literal or a number as text (true, false, null, 123)
static int js_scalar(js_t *j, char *out, size_t size)
{
	js_ws(j);
	size_t n = 0, seen = 0;
	while (j->p < j->end && *j->p && !strchr(",}] \t\r\n:{[\"", *j->p)) {
		if (out && n + 1 < size)
			out[n++] = *j->p;
		j->p++;
		seen++;
	}
	if (out && size)
		out[n] = 0;
	if (!seen)
		j->bad = 1;
	return seen > 0;
}

static void js_skip(js_t *j)
{
	int c = js_peek(j);
	if (c == '"') {
		js_string(j, NULL, 0);
	} else if (c == '{' || c == '[') {
		if (++j->depth > 64) {
			j->bad = 1;
			return;
		}
		char close = c == '{' ? '}' : ']';
		j->p++;
		if (js_peek(j) == close) {
			j->p++;
			j->depth--;
			return;
		}
		while (!j->bad) {
			if (c == '{') {
				js_string(j, NULL, 0);
				js_expect(j, ':');
			}
			js_skip(j);
			int d = js_peek(j);
			if (d == ',') {
				j->p++;
				continue;
			}
			if (d == close) {
				j->p++;
				break;
			}
			j->bad = 1;
		}
		j->depth--;
	} else if (c >= 0) {
		js_scalar(j, NULL, 0);
	} else {
		j->bad = 1;
	}
}

static void js_asset(js_t *j, upd_asset *a)
{
	memset(a, 0, sizeof(*a));
	a->size = -1;
	if (!js_expect(j, '{'))
		return;
	if (js_peek(j) == '}') {
		j->p++;
		return;
	}
	while (!j->bad) {
		char key[48];
		js_string(j, key, sizeof(key));
		js_expect(j, ':');
		if (!strcmp(key, "name") && js_peek(j) == '"')
			js_string(j, a->name, sizeof(a->name));
		else if (!strcmp(key, "browser_download_url") && js_peek(j) == '"')
			js_string(j, a->url, sizeof(a->url));
		else if (!strcmp(key, "digest") && js_peek(j) == '"') {
			char d[96];
			js_string(j, d, sizeof(d));
			if (!strncmp(d, "sha256:", 7) && strlen(d + 7) == 64) {
				for (int i = 0; i < 64; i++)
					a->sha256[i] = (char)(d[7 + i] >= 'A' && d[7 + i] <= 'F' ? d[7 + i] + 32 : d[7 + i]);
				a->sha256[64] = 0;
			}
		} else if (!strcmp(key, "size") && js_peek(j) != '"' && js_peek(j) != '{' && js_peek(j) != '[') {
			char num[24];
			if (js_scalar(j, num, sizeof(num)))
				a->size = _atoi64(num);
		} else
			js_skip(j);
		int d = js_peek(j);
		if (d == ',') {
			j->p++;
			continue;
		}
		if (d == '}') {
			j->p++;
			break;
		}
		j->bad = 1;
	}
}

int upd_parse_release(const char *json, size_t len, upd_release *r)
{
	js_t j = {json, json + len, 0, 0};
	memset(r, 0, sizeof(*r));
	if (!js_expect(&j, '{'))
		return 0;
	while (!j.bad && js_peek(&j) != '}') {
		char key[48];
		js_string(&j, key, sizeof(key));
		js_expect(&j, ':');
		int c = js_peek(&j);
		if (!strcmp(key, "tag_name") && c == '"')
			js_string(&j, r->tag, sizeof(r->tag));
		else if (!strcmp(key, "published_at") && c == '"')
			js_string(&j, r->published, sizeof(r->published));
		else if (!strcmp(key, "html_url") && c == '"')
			js_string(&j, r->html_url, sizeof(r->html_url));
		else if ((!strcmp(key, "draft") || !strcmp(key, "prerelease")) && c != '"' && c != '{' && c != '[') {
			char v[8];
			js_scalar(&j, v, sizeof(v));
			*(key[0] == 'd' ? &r->draft : &r->prerelease) = !strcmp(v, "true");
		} else if (!strcmp(key, "assets") && c == '[') {
			j.p++;
			if (js_peek(&j) == ']')
				j.p++;
			else
				while (!j.bad) {
					if (r->asset_count < UPD_MAX_ASSETS)
						js_asset(&j, &r->assets[r->asset_count++]);
					else
						js_skip(&j);
					int d = js_peek(&j);
					if (d == ',') {
						j.p++;
						continue;
					}
					if (d == ']') {
						j.p++;
						break;
					}
					j.bad = 1;
				}
		} else
			js_skip(&j);
		if (js_peek(&j) == ',') {
			j.p++;
			if (js_peek(&j) != '"')
				j.bad = 1; // a key must follow
		} else if (js_peek(&j) != '}')
			j.bad = 1;
	}
	return !j.bad && r->tag[0] != 0;
}

const upd_asset *upd_find_asset(const upd_release *r, const char *name)
{
	for (int i = 0; i < r->asset_count; i++)
		if (!strcmp(r->assets[i].name, name))
			return &r->assets[i];
	return NULL;
}

// ---------------------------------------------------------------------------------------------- CRC-32, inflate

uint32_t upd_crc32(uint32_t crc, const void *data, size_t len)
{
	static uint32_t table[256];
	static volatile LONG ready;
	if (!ready) {
		for (uint32_t n = 0; n < 256; n++) {
			uint32_t c = n;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		InterlockedExchange(&ready, 1);
	}
	const uint8_t *p = data;
	crc = ~crc;
	while (len--)
		crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
	return ~crc;
}

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
	const uint8_t *in;
	size_t inlen, incnt;
	uint32_t bitbuf;
	int bitcnt, err;
	uint8_t *out;
	size_t outlen, outcnt;
} inf_t;

typedef struct {
	short count[MAXBITS + 1];
	short symbol[FIXLCODES];
} huff_t;

static int inf_bits(inf_t *s, int need)
{
	uint32_t val = s->bitbuf;
	while (s->bitcnt < need) {
		if (s->incnt >= s->inlen) {
			s->err = 1; // out of input
			return 0;
		}
		val |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
		s->bitcnt += 8;
	}
	s->bitbuf = need >= 32 ? 0 : val >> need;
	s->bitcnt -= need;
	return (int)(val & ((1u << need) - 1));
}

static int inf_stored(inf_t *s)
{
	s->bitbuf = 0;
	s->bitcnt = 0;
	if (s->incnt + 4 > s->inlen)
		return -1;
	unsigned len = s->in[s->incnt] | (unsigned)s->in[s->incnt + 1] << 8;
	if (s->in[s->incnt + 2] != (~len & 0xff) || s->in[s->incnt + 3] != ((~len >> 8) & 0xff))
		return -2;
	s->incnt += 4;
	if (s->incnt + len > s->inlen || s->outcnt + len > s->outlen)
		return -3;
	memcpy(s->out + s->outcnt, s->in + s->incnt, len);
	s->incnt += len;
	s->outcnt += len;
	return 0;
}

static int inf_decode(inf_t *s, const huff_t *h)
{
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= MAXBITS; len++) {
		code |= inf_bits(s, 1);
		if (s->err)
			return -10;
		int count = h->count[len];
		if (code - count < first)
			return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -10;
}

static int inf_construct(huff_t *h, const short *length, int n)
{
	short offs[MAXBITS + 1];
	for (int len = 0; len <= MAXBITS; len++)
		h->count[len] = 0;
	for (int symbol = 0; symbol < n; symbol++)
		h->count[length[symbol]]++;
	if (h->count[0] == n)
		return 0;
	int left = 1;
	for (int len = 1; len <= MAXBITS; len++) {
		left <<= 1;
		left -= h->count[len];
		if (left < 0)
			return left;
	}
	offs[1] = 0;
	for (int len = 1; len < MAXBITS; len++)
		offs[len + 1] = (short)(offs[len] + h->count[len]);
	for (int symbol = 0; symbol < n; symbol++)
		if (length[symbol] != 0)
			h->symbol[offs[length[symbol]]++] = (short)symbol;
	return left;
}

static int inf_codes(inf_t *s, const huff_t *lencode, const huff_t *distcode)
{
	static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
	static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
	static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
	static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
	int symbol;
	do {
		symbol = inf_decode(s, lencode);
		if (symbol < 0)
			return symbol;
		if (symbol < 256) {
			if (s->outcnt >= s->outlen)
				return -20;
			s->out[s->outcnt++] = (uint8_t)symbol;
		} else if (symbol > 256) {
			symbol -= 257;
			if (symbol >= 29)
				return -10;
			size_t len = (size_t)(lbase[symbol] + inf_bits(s, lext[symbol]));
			int d = inf_decode(s, distcode);
			if (d < 0)
				return d;
			if (d >= 30)
				return -10;
			size_t dist = (size_t)(dbase[d] + inf_bits(s, dext[d]));
			if (s->err)
				return -10;
			if (dist > s->outcnt)
				return -11; // before the start of the output
			if (s->outcnt + len > s->outlen)
				return -20;
			for (size_t i = 0; i < len; i++, s->outcnt++)
				s->out[s->outcnt] = s->out[s->outcnt - dist];
		}
	} while (symbol != 256);
	return 0;
}

static int inf_fixed(inf_t *s)
{
	static huff_t lencode, distcode;
	static volatile LONG built;
	if (!built) {
		short lengths[FIXLCODES];
		int symbol = 0;
		for (; symbol < 144; symbol++)
			lengths[symbol] = 8;
		for (; symbol < 256; symbol++)
			lengths[symbol] = 9;
		for (; symbol < 280; symbol++)
			lengths[symbol] = 7;
		for (; symbol < FIXLCODES; symbol++)
			lengths[symbol] = 8;
		inf_construct(&lencode, lengths, FIXLCODES);
		for (symbol = 0; symbol < MAXDCODES; symbol++)
			lengths[symbol] = 5;
		inf_construct(&distcode, lengths, MAXDCODES);
		InterlockedExchange(&built, 1);
	}
	return inf_codes(s, &lencode, &distcode);
}

static int inf_dynamic(inf_t *s)
{
	static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	short lengths[MAXLCODES + MAXDCODES];
	huff_t lencode, distcode;
	int nlen = inf_bits(s, 5) + 257, ndist = inf_bits(s, 5) + 1, ncode = inf_bits(s, 4) + 4;
	if (s->err || nlen > MAXLCODES || ndist > MAXDCODES)
		return -3;
	int index = 0;
	for (; index < ncode; index++)
		lengths[order[index]] = (short)inf_bits(s, 3);
	for (; index < 19; index++)
		lengths[order[index]] = 0;
	if (s->err || inf_construct(&lencode, lengths, 19) != 0)
		return -4;
	index = 0;
	while (index < nlen + ndist) {
		int symbol = inf_decode(s, &lencode), len = 0;
		if (symbol < 0)
			return symbol;
		if (symbol < 16) {
			lengths[index++] = (short)symbol;
			continue;
		}
		if (symbol == 16) {
			if (index == 0)
				return -5;
			len = lengths[index - 1];
			symbol = 3 + inf_bits(s, 2);
		} else if (symbol == 17)
			symbol = 3 + inf_bits(s, 3);
		else
			symbol = 11 + inf_bits(s, 7);
		if (s->err || index + symbol > nlen + ndist)
			return -6;
		while (symbol--)
			lengths[index++] = (short)len;
	}
	if (lengths[256] == 0)
		return -9;
	int err = inf_construct(&lencode, lengths, nlen);
	if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1]))
		return -7;
	err = inf_construct(&distcode, lengths + nlen, ndist);
	if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1]))
		return -8;
	return inf_codes(s, &lencode, &distcode);
}

long upd_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen)
{
	inf_t s = {in, inlen, 0, 0, 0, 0, out, outlen, 0};
	int last, err;
	do {
		last = inf_bits(&s, 1);
		int type = inf_bits(&s, 2);
		if (s.err)
			return -1;
		err = type == 0 ? inf_stored(&s) : type == 1 ? inf_fixed(&s) : type == 2 ? inf_dynamic(&s) : -1;
		if (err || s.err)
			return -1;
	} while (!last);
	return (long)s.outcnt;
}

// ---------------------------------------------------------------------------------------------- zip

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int upd_zip_list(const uint8_t *zip, size_t len, upd_zip_entry *out, int max)
{
	if (len < 22)
		return -1;
	size_t eocd = (size_t)-1;
	size_t lowest = len > 22 + 65535 ? len - 22 - 65535 : 0;
	for (size_t at = len - 22 + 1; at-- > lowest;)
		if (rd32(zip + at) == 0x06054b50) {
			eocd = at;
			break;
		}
	if (eocd == (size_t)-1)
		return -1;
	unsigned entries = rd16(zip + eocd + 10);
	uint32_t cd_size = rd32(zip + eocd + 12), cd_off = rd32(zip + eocd + 16);
	if (entries == 0xffff || cd_off == 0xffffffffu || (size_t)cd_off + cd_size > eocd)
		return -1; // ZIP64 or damaged
	size_t at = cd_off;
	int n = 0;
	for (unsigned i = 0; i < entries; i++) {
		if (at + 46 > eocd || rd32(zip + at) != 0x02014b50)
			return -1;
		const uint8_t *h = zip + at;
		uint16_t flags = rd16(h + 8), method = rd16(h + 10), name_len = rd16(h + 28), extra_len = rd16(h + 30), comment_len = rd16(h + 32);
		if (at + 46 + name_len + extra_len + comment_len > eocd || name_len == 0 || name_len >= sizeof(out[0].path))
			return -1;
		if (flags & 1)
			return -1; // encrypted
		upd_zip_entry e;
		memset(&e, 0, sizeof(e));
		memcpy(e.path, h + 46, name_len);
		e.flags = flags;
		e.method = method;
		e.crc = rd32(h + 16);
		e.csize = rd32(h + 20);
		e.usize = rd32(h + 24);
		e.local_offset = rd32(h + 42);
		if (e.csize == 0xffffffffu || e.usize == 0xffffffffu || e.local_offset == 0xffffffffu)
			return -1;
		at += 46u + name_len + extra_len + comment_len;
		if (e.path[name_len - 1] == '/' || e.path[name_len - 1] == '\\')
			continue; // a directory
		if (n >= max)
			return -1;
		out[n++] = e;
	}
	return n;
}

uint8_t *upd_zip_extract(const uint8_t *zip, size_t len, const upd_zip_entry *e)
{
	size_t at = e->local_offset;
	if (at + 30 > len || rd32(zip + at) != 0x04034b50)
		return NULL;
	size_t data = at + 30u + rd16(zip + at + 26) + rd16(zip + at + 28);
	if (data > len || e->csize > len - data || e->usize > 256u * 1024 * 1024)
		return NULL;
	uint8_t *out = HeapAlloc(GetProcessHeap(), 0, e->usize ? e->usize : 1);
	if (!out)
		return NULL;
	int ok = 0;
	if (e->method == 0)
		ok = e->csize == e->usize && (memcpy(out, zip + data, e->usize), 1);
	else if (e->method == 8)
		ok = upd_inflate(zip + data, e->csize, out, e->usize) == (long)e->usize;
	if (ok && upd_crc32(0, out, e->usize) == e->crc)
		return out;
	HeapFree(GetProcessHeap(), 0, out);
	return NULL;
}

int upd_safe_path(const char *zip_path, char *out, size_t size)
{
	static const char *const root_files[] = {"winmm.dll", "INSTALL.txt", "LICENSE.txt", "INSTALL-DLSS.txt", "NOTICE-DLSS.txt"};
	size_t n = strlen(zip_path);
	if (!n || n + 1 > size || zip_path[0] == '/' || zip_path[0] == '\\' || strchr(zip_path, ':'))
		return 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)zip_path[i];
		if (c < 0x20 || c == 0x7f || strchr("<>\"|?*", c))
			return 0;
		out[i] = c == '/' ? '\\' : (char)c;
	}
	out[n] = 0;
	// every part: not empty, not "." or "..", no trailing dot or space (Windows drops them)
	for (const char *part = out; *part;) {
		const char *end = strchr(part, '\\');
		size_t len = end ? (size_t)(end - part) : strlen(part);
		if (len == 0 || part[len - 1] == '.' || part[len - 1] == ' ')
			return 0;
		if (!end)
			break;
		part = end + 1;
		if (!*part)
			return 0;
	}
	if (!strchr(out, '\\')) {
		for (size_t i = 0; i < sizeof(root_files) / sizeof(root_files[0]); i++)
			if (!lstrcmpiA(out, root_files[i]))
				return 1;
		return 0;
	}
	if (_strnicmp(out, "mw2coop\\", 8) != 0 || !_strnicmp(out, "mw2coop\\update\\", 15) || !lstrcmpiA(out, "mw2coop\\settings.ini"))
		return 0;
	return 1;
}

// ---------------------------------------------------------------------------------------------- SHA-256

int upd_sha256(const void *data, size_t len, char hex[65])
{
	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE hash = NULL;
	UCHAR digest[32];
	int ok = 0;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 && BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0) {
		ok = 1;
		const UCHAR *p = data;
		while (ok && len) {
			ULONG part = len > 0x40000000u ? 0x40000000u : (ULONG)len;
			ok = BCryptHashData(hash, (PUCHAR)p, part, 0) == 0;
			p += part;
			len -= part;
		}
		ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
	}
	if (hash)
		BCryptDestroyHash(hash);
	if (alg)
		BCryptCloseAlgorithmProvider(alg, 0);
	if (ok)
		for (int i = 0; i < 32; i++)
			sprintf(hex + 2 * i, "%02x", digest[i]);
	return ok;
}
