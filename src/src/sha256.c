/* SHA-256 — FIPS 180-4 */
#define _POSIX_C_SOURCE 200809L
#include "sha256.h"
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x) (ROTR(x,2)^ROTR(x,13)^ROTR(x,22))
#define EP1(x) (ROTR(x,6)^ROTR(x,11)^ROTR(x,25))
#define SG0(x) (ROTR(x,7)^ROTR(x,18)^((x)>>3))
#define SG1(x) (ROTR(x,17)^ROTR(x,19)^((x)>>10))

static void sha256_transform(SHA256 *ctx, const uint8_t data[64]) {
    uint32_t m[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++)
        m[i] = ((uint32_t)data[i*4+0] << 24) | ((uint32_t)data[i*4+1] << 16) |
               ((uint32_t)data[i*4+2] <<  8) | ((uint32_t)data[i*4+3]);
    for (; i < 64; i++)
        m[i] = SG1(m[i-2]) + m[i-7] + SG0(m[i-15]) + m[i-16];

    a = ctx->s[0]; b = ctx->s[1]; c = ctx->s[2]; d = ctx->s[3];
    e = ctx->s[4]; f = ctx->s[5]; g = ctx->s[6]; h = ctx->s[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e,f,g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->s[0] += a; ctx->s[1] += b; ctx->s[2] += c; ctx->s[3] += d;
    ctx->s[4] += e; ctx->s[5] += f; ctx->s[6] += g; ctx->s[7] += h;
}

void sha256_init(SHA256 *ctx) {
    ctx->s[0] = 0x6a09e667; ctx->s[1] = 0xbb67ae85;
    ctx->s[2] = 0x3c6ef372; ctx->s[3] = 0xa54ff53a;
    ctx->s[4] = 0x510e527f; ctx->s[5] = 0x9b05688c;
    ctx->s[6] = 0x1f83d9ab; ctx->s[7] = 0x5be0cd19;
    ctx->bl = 0;
    ctx->n  = 0;
}

void sha256_update(SHA256 *ctx, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0) {
        size_t room = 64 - ctx->bl;
        size_t take = len < room ? len : room;
        memcpy(ctx->b + ctx->bl, p, take);
        ctx->bl += (uint32_t)take;
        ctx->n  += take;
        p       += take;
        len     -= take;
        if (ctx->bl == 64) {
            sha256_transform(ctx, ctx->b);
            ctx->bl = 0;
        }
    }
}

void sha256_final(SHA256 *ctx, uint8_t out[32]) {
    uint64_t bitlen = ctx->n * 8;
    uint8_t pad[64];
    uint32_t padlen;
    int i;

    /* append 0x80 */
    pad[0] = 0x80;
    sha256_update(ctx, pad, 1);

    /* zero-pad to 56 bytes mod 64 */
    memset(pad, 0, sizeof(pad));
    padlen = (ctx->bl <= 56) ? (56 - ctx->bl) : (120 - ctx->bl);
    sha256_update(ctx, pad, padlen);

    /* append big-endian 64-bit bit length */
    pad[0] = (uint8_t)(bitlen >> 56); pad[1] = (uint8_t)(bitlen >> 48);
    pad[2] = (uint8_t)(bitlen >> 40); pad[3] = (uint8_t)(bitlen >> 32);
    pad[4] = (uint8_t)(bitlen >> 24); pad[5] = (uint8_t)(bitlen >> 16);
    pad[6] = (uint8_t)(bitlen >>  8); pad[7] = (uint8_t)(bitlen);
    sha256_update(ctx, pad, 8);

    /* produce output in big-endian order */
    for (i = 0; i < 8; i++) {
        out[i*4+0] = (uint8_t)(ctx->s[i] >> 24);
        out[i*4+1] = (uint8_t)(ctx->s[i] >> 16);
        out[i*4+2] = (uint8_t)(ctx->s[i] >>  8);
        out[i*4+3] = (uint8_t)(ctx->s[i]);
    }
}

void sha256_hex(const void *data, size_t len, char out[65]) {
    static const char hx[] = "0123456789abcdef";
    uint8_t digest[32];
    SHA256 ctx;
    int i;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
    for (i = 0; i < 32; i++) {
        out[i*2+0] = hx[digest[i] >> 4];
        out[i*2+1] = hx[digest[i] & 15];
    }
    out[64] = '\0';
}
