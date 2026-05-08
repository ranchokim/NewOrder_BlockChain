/* SHA-256 — FIPS 180-4 */
#include "sha256.h"
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define RR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(e,f,g) (((e)&(f))^((~(e))&(g)))
#define MA(a,b,c) (((a)&(b))^((a)&(c))^((b)&(c)))
#define S0(a) (RR(a,2)^RR(a,13)^RR(a,22))
#define S1(e) (RR(e,6)^RR(e,11)^RR(e,25))
#define G0(x) (RR(x,7)^RR(x,18)^((x)>>3))
#define G1(x) (RR(x,17)^RR(x,19)^((x)>>10))

static void sha256_block(SHA256 *c, const uint8_t *p) {
    uint32_t w[64], a, b, cc, d, e, f, g, h, t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|
               ((uint32_t)p[i*4+2]<<8)|(uint32_t)p[i*4+3];
    for (; i < 64; i++)
        w[i] = G1(w[i-2]) + w[i-7] + G0(w[i-15]) + w[i-16];
    a=c->s[0]; b=c->s[1]; cc=c->s[2]; d=c->s[3];
    e=c->s[4]; f=c->s[5]; g=c->s[6]; h=c->s[7];
    for (i = 0; i < 64; i++) {
        t1 = h + S1(e) + CH(e,f,g) + K[i] + w[i];
        t2 = S0(a) + MA(a,b,cc);
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->s[0]+=a; c->s[1]+=b; c->s[2]+=cc; c->s[3]+=d;
    c->s[4]+=e; c->s[5]+=f; c->s[6]+=g;  c->s[7]+=h;
}

void sha256_init(SHA256 *c) {
    c->s[0]=0x6a09e667; c->s[1]=0xbb67ae85; c->s[2]=0x3c6ef372; c->s[3]=0xa54ff53a;
    c->s[4]=0x510e527f; c->s[5]=0x9b05688c; c->s[6]=0x1f83d9ab; c->s[7]=0x5be0cd19;
    c->bl = 0; c->n = 0;
}

void sha256_update(SHA256 *c, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    while (len) {
        size_t sp = 64 - c->bl, tk = len < sp ? len : sp;
        memcpy(c->b + c->bl, p, tk);
        c->bl += (uint32_t)tk; c->n += tk; p += tk; len -= tk;
        if (c->bl == 64) { sha256_block(c, c->b); c->bl = 0; }
    }
}

void sha256_final(SHA256 *c, uint8_t out[32]) {
    uint8_t pad[64];
    uint64_t bits = c->n * 8;
    uint32_t pl = (c->bl < 56) ? 56 - c->bl : 120 - c->bl;
    int i;
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    sha256_update(c, pad, pl);
    pad[0]=(uint8_t)(bits>>56); pad[1]=(uint8_t)(bits>>48);
    pad[2]=(uint8_t)(bits>>40); pad[3]=(uint8_t)(bits>>32);
    pad[4]=(uint8_t)(bits>>24); pad[5]=(uint8_t)(bits>>16);
    pad[6]=(uint8_t)(bits>>8);  pad[7]=(uint8_t)bits;
    sha256_update(c, pad, 8);
    for (i = 0; i < 8; i++) {
        out[i*4]  =(uint8_t)(c->s[i]>>24); out[i*4+1]=(uint8_t)(c->s[i]>>16);
        out[i*4+2]=(uint8_t)(c->s[i]>>8);  out[i*4+3]=(uint8_t)c->s[i];
    }
}

void sha256_hex(const void *data, size_t len, char out[65]) {
    static const char hx[] = "0123456789abcdef";
    uint8_t d[32]; SHA256 c; int i;
    sha256_init(&c); sha256_update(&c, data, len); sha256_final(&c, d);
    for (i = 0; i < 32; i++) { out[i*2]=hx[d[i]>>4]; out[i*2+1]=hx[d[i]&15]; }
    out[64] = '\0';
}
