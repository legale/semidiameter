#include "md5.h"
#include <string.h>

/* MD5 implementation from BusyBox (Ulrich Drepper) adapted for standalone use. */

typedef uint32_t md5_uint32;

#define CYCLIC(w, s) (w = (w << s) | (w >> (32 - s)))

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SWAP(n) (n)
#else
#define SWAP(n) ((n << 24) | ((n&65280)<<8) | ((n&16711680)>>8) | (n>>24))
#endif

#define FF(b, c, d) (d ^ (b & (c ^ d)))
#define FG(b, c, d) (c ^ (d & (b ^ c)))
#define FH(b, c, d) (b ^ c ^ d)
#define FI(b, c, d) (c ^ (b | ~d))

#define OP(f, a, b, c, d, k, s, T)      \
do {                                    \
  a += f(b, c, d) + x[k] + T;           \
  CYCLIC(a, s);                         \
  a += b;                               \
} while (0)

static void md5_process_block(const void *buffer, size_t len, MD5_CTX *ctx)
{
    md5_uint32 a = ctx->state[0];
    md5_uint32 b = ctx->state[1];
    md5_uint32 c = ctx->state[2];
    md5_uint32 d = ctx->state[3];
    const md5_uint32 *words = buffer;
    size_t nwords = len / 4;
    const md5_uint32 *endp = words + nwords;

    while (words < endp) {
        md5_uint32 x[16];
        md5_uint32 a_save = a;
        md5_uint32 b_save = b;
        md5_uint32 c_save = c;
        md5_uint32 d_save = d;

        for (int i = 0; i < 16; i++) x[i] = SWAP(words[i]);
        words += 16;

        /* Round 1 */
        OP(FF, a, b, c, d, 0, 7, 0xd76aa478);
        OP(FF, d, a, b, c, 1, 12, 0xe8c7b756);
        OP(FF, c, d, a, b, 2, 17, 0x242070db);
        OP(FF, b, c, d, a, 3, 22, 0xc1bdceee);
        OP(FF, a, b, c, d, 4, 7, 0xf57c0faf);
        OP(FF, d, a, b, c, 5, 12, 0x4787c62a);
        OP(FF, c, d, a, b, 6, 17, 0xa8304613);
        OP(FF, b, c, d, a, 7, 22, 0xfd469501);
        OP(FF, a, b, c, d, 8, 7, 0x698098d8);
        OP(FF, d, a, b, c, 9, 12, 0x8b44f7af);
        OP(FF, c, d, a, b, 10, 17, 0xffff5bb1);
        OP(FF, b, c, d, a, 11, 22, 0x895cd7be);
        OP(FF, a, b, c, d, 12, 7, 0x6b901122);
        OP(FF, d, a, b, c, 13, 12, 0xfd987193);
        OP(FF, c, d, a, b, 14, 17, 0xa679438e);
        OP(FF, b, c, d, a, 15, 22, 0x49b40821);

        /* Round 2 */
        OP(FG, a, b, c, d, 1, 5, 0xf61e2562);
        OP(FG, d, a, b, c, 6, 9, 0xc040b340);
        OP(FG, c, d, a, b, 11, 14, 0x265e5a51);
        OP(FG, b, c, d, a, 0, 20, 0xe9b6c7aa);
        OP(FG, a, b, c, d, 5, 5, 0xd62f105d);
        OP(FG, d, a, b, c, 10, 9, 0x02441453);
        OP(FG, c, d, a, b, 15, 14, 0xd8a1e681);
        OP(FG, b, c, d, a, 4, 20, 0xe7d3fbc8);
        OP(FG, a, b, c, d, 9, 5, 0x21e1cde6);
        OP(FG, d, a, b, c, 14, 9, 0xc33707d6);
        OP(FG, c, d, a, b, 3, 14, 0xf4d50d87);
        OP(FG, b, c, d, a, 8, 20, 0x455a14ed);
        OP(FG, a, b, c, d, 13, 5, 0xa9e3e905);
        OP(FG, d, a, b, c, 2, 9, 0xfcefa3f8);
        OP(FG, c, d, a, b, 7, 14, 0x676f02d9);
        OP(FG, b, c, d, a, 12, 20, 0x8d2a4c8a);

        /* Round 3 */
        OP(FH, a, b, c, d, 5, 4, 0xfffa3942);
        OP(FH, d, a, b, c, 8, 11, 0x8771f681);
        OP(FH, c, d, a, b, 11, 16, 0x6d9d6122);
        OP(FH, b, c, d, a, 14, 23, 0xfde5380c);
        OP(FH, a, b, c, d, 1, 4, 0xa4beea44);
        OP(FH, d, a, b, c, 4, 11, 0x4bdecfa9);
        OP(FH, c, d, a, b, 7, 16, 0xf6bb4b60);
        OP(FH, b, c, d, a, 10, 23, 0xbebfbc70);
        OP(FH, a, b, c, d, 13, 4, 0x289b7ec6);
        OP(FH, d, a, b, c, 0, 11, 0xeaa127fa);
        OP(FH, c, d, a, b, 3, 16, 0xd4ef3085);
        OP(FH, b, c, d, a, 6, 23, 0x04881d05);
        OP(FH, a, b, c, d, 9, 4, 0xd9d4d039);
        OP(FH, d, a, b, c, 12, 11, 0xe6db99e5);
        OP(FH, c, d, a, b, 15, 16, 0x1fa27cf8);
        OP(FH, b, c, d, a, 2, 23, 0xc4ac5665);

        /* Round 4 */
        OP(FI, a, b, c, d, 0, 6, 0xf4292244);
        OP(FI, d, a, b, c, 7, 10, 0x432aff97);
        OP(FI, c, d, a, b, 14, 15, 0xab9423a7);
        OP(FI, b, c, d, a, 5, 21, 0xfc93a039);
        OP(FI, a, b, c, d, 12, 6, 0x655b59c3);
        OP(FI, d, a, b, c, 3, 10, 0x8f0ccc92);
        OP(FI, c, d, a, b, 10, 15, 0xffeff47d);
        OP(FI, b, c, d, a, 1, 21, 0x85845dd1);
        OP(FI, a, b, c, d, 8, 6, 0x6fa87e4f);
        OP(FI, d, a, b, c, 15, 10, 0xfe2ce6e0);
        OP(FI, c, d, a, b, 6, 15, 0xa3014314);
        OP(FI, b, c, d, a, 13, 21, 0x4e0811a1);
        OP(FI, a, b, c, d, 4, 6, 0xf7537e82);
        OP(FI, d, a, b, c, 11, 10, 0xbd3af235);
        OP(FI, c, d, a, b, 2, 15, 0x2ad7d2bb);
        OP(FI, b, c, d, a, 9, 21, 0xeb86d391);

        a += a_save;
        b += b_save;
        c += c_save;
        d += d_save;
    }

    ctx->state[0] = a;
    ctx->state[1] = b;
    ctx->state[2] = c;
    ctx->state[3] = d;
}

void MD5_Init(MD5_CTX *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count[0] = ctx->count[1] = 0;
}

void MD5_Update(MD5_CTX *ctx, const unsigned char *input, unsigned int len)
{
    md5_uint32 index = (ctx->count[0] >> 3) & 0x3f;
    if ((ctx->count[0] += (md5_uint32)len << 3) < (md5_uint32)len << 3)
        ctx->count[1]++;
    ctx->count[1] += (md5_uint32)len >> 29;

    unsigned int partlen = 64 - index;
    unsigned int i;

    if (len >= partlen) {
        memcpy(&ctx->buffer[index], input, partlen);
        md5_process_block(ctx->buffer, 64, ctx);
        for (i = partlen; i + 63 < len; i += 64)
            md5_process_block(&input[i], 64, ctx);
        index = 0;
    } else i = 0;

    memcpy(&ctx->buffer[index], &input[i], len - i);
}

void MD5_Final(unsigned char digest[16], MD5_CTX *ctx)
{
    static const unsigned char fillbuf[64] = { 0x80, 0 };
    unsigned char bits[8];
    md5_uint32 index = (ctx->count[0] >> 3) & 0x3f;
    unsigned int padlen = index < 56 ? 56 - index : 120 - index;

    md5_uint32 total0 = ctx->count[0];
    md5_uint32 total1 = ctx->count[1];

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(bits, &total0, 4);
    memcpy(bits + 4, &total1, 4);
#else
    bits[0] = total0; bits[1] = total0 >> 8; bits[2] = total0 >> 16; bits[3] = total0 >> 24;
    bits[4] = total1; bits[5] = total1 >> 8; bits[6] = total1 >> 16; bits[7] = total1 >> 24;
#endif

    MD5_Update(ctx, fillbuf, padlen);
    MD5_Update(ctx, bits, 8);

    for (int i = 0; i < 4; i++) {
        md5_uint32 s = SWAP(ctx->state[i]);
        memcpy(digest + i*4, &s, 4);
    }

    memset(ctx, 0, sizeof(*ctx));
}
void HMAC_MD5(const unsigned char *text, int text_len, const unsigned char *key, int key_len, unsigned char *digest)
{
    MD5_CTX ctx;
    unsigned char k_pad[64];
    unsigned char tk[16];
    int i;

    if (key_len > 64) {
        MD5_Init(&ctx);
        MD5_Update(&ctx, key, (unsigned int)key_len);
        MD5_Final(tk, &ctx);
        key = tk;
        key_len = 16;
    }

    memset(k_pad, 0, sizeof(k_pad));
    memcpy(k_pad, key, (size_t)key_len);

    for (i = 0; i < 64; i++) k_pad[i] ^= 0x36;
    MD5_Init(&ctx);
    MD5_Update(&ctx, k_pad, 64);
    MD5_Update(&ctx, text, (unsigned int)text_len);
    MD5_Final(digest, &ctx);

    memset(k_pad, 0, sizeof(k_pad));
    memcpy(k_pad, key, (size_t)key_len);

    for (i = 0; i < 64; i++) k_pad[i] ^= 0x5c;
    MD5_Init(&ctx);
    MD5_Update(&ctx, k_pad, 64);
    MD5_Update(&ctx, digest, 16);
    MD5_Final(digest, &ctx);
}
