#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t s[8];
    uint8_t  b[64];
    uint32_t bl;
    uint64_t n;
} SHA256;

void sha256_init(SHA256 *c);
void sha256_update(SHA256 *c, const void *data, size_t len);
void sha256_final(SHA256 *c, uint8_t out[32]);
/* sha256_hex: compute SHA-256 and write 64-char lowercase hex + NUL into out[65] */
void sha256_hex(const void *data, size_t len, char out[65]);

#endif
