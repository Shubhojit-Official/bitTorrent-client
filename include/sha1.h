#ifndef SHA1_H
#define SHA1_H

#include <stddef.h>
#include <stdint.h>

#define SHA1_BLOCK_SIZE 20 // 160 bits

typedef struct
{
    uint32_t state[5];
    uint64_t bitlen;
    uint8_t buffer[64];
    size_t buffer_len;
} SHA1_CTX;

void sha1_init(SHA1_CTX *ctx);
void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len);
void sha1_final(SHA1_CTX *ctx, uint8_t hash[SHA1_BLOCK_SIZE]);

// Convenience one-shot function
void sha1(const uint8_t *data, size_t len, uint8_t hash[SHA1_BLOCK_SIZE]);

#endif
