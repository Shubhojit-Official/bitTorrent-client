#include "../include/sha1.h"
#include <string.h>

#define ROTLEFT(a, b) (((a) << (b)) | ((a) >> (32 - (b))))

static void sha1_transform(SHA1_CTX *ctx, const uint8_t data[64])
{
  uint32_t a, b, c, d, e, f, k, temp, m[80];

  for (int i = 0; i < 16; ++i) {
    m[i] = (data[i * 4] << 24);
    m[i] |= (data[i * 4 + 1] << 16);
    m[i] |= (data[i * 4 + 2] << 8);
    m[i] |= (data[i * 4 + 3]);
  }
  for (int i = 16; i < 80; ++i)
    m[i] = ROTLEFT(m[i - 3] ^ m[i - 8] ^ m[i - 14] ^ m[i - 16], 1);

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];

  for (int i = 0; i < 80; ++i) {
    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5A827999;
    }
    else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    }
    else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    }
    else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }
    temp = ROTLEFT(a, 5) + f + e + k + m[i];
    e = d;
    d = c;
    c = ROTLEFT(b, 30);
    b = a;
    a = temp;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
}

void sha1_init(SHA1_CTX *ctx)
{
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xEFCDAB89;
  ctx->state[2] = 0x98BADCFE;
  ctx->state[3] = 0x10325476;
  ctx->state[4] = 0xC3D2E1F0;
  ctx->bitlen = 0;
  ctx->buffer_len = 0;
}

void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len)
{
  for (size_t i = 0; i < len; ++i) {
    ctx->buffer[ctx->buffer_len++] = data[i];
    if (ctx->buffer_len == 64) {
      sha1_transform(ctx, ctx->buffer);
      ctx->bitlen += 512;
      ctx->buffer_len = 0;
    }
  }
}

void sha1_final(SHA1_CTX *ctx, uint8_t hash[SHA1_BLOCK_SIZE])
{
  size_t i = ctx->buffer_len;

  // Pad
  ctx->buffer[i++] = 0x80;
  if (i > 56) {
    while (i < 64)
      ctx->buffer[i++] = 0x00;
    sha1_transform(ctx, ctx->buffer);
    i = 0;
  }
  while (i < 56)
    ctx->buffer[i++] = 0x00;

  // Append length in bits
  ctx->bitlen += ctx->buffer_len * 8;
  ctx->buffer[63] = ctx->bitlen;
  ctx->buffer[62] = ctx->bitlen >> 8;
  ctx->buffer[61] = ctx->bitlen >> 16;
  ctx->buffer[60] = ctx->bitlen >> 24;
  ctx->buffer[59] = ctx->bitlen >> 32;
  ctx->buffer[58] = ctx->bitlen >> 40;
  ctx->buffer[57] = ctx->bitlen >> 48;
  ctx->buffer[56] = ctx->bitlen >> 56;
  sha1_transform(ctx, ctx->buffer);

  // Output
  for (i = 0; i < 5; ++i) {
    hash[i * 4] = (ctx->state[i] >> 24) & 0xff;
    hash[i * 4 + 1] = (ctx->state[i] >> 16) & 0xff;
    hash[i * 4 + 2] = (ctx->state[i] >> 8) & 0xff;
    hash[i * 4 + 3] = (ctx->state[i]) & 0xff;
  }
}

void sha1(const uint8_t *data, size_t len, uint8_t hash[SHA1_BLOCK_SIZE])
{
  SHA1_CTX ctx;
  sha1_init(&ctx);
  sha1_update(&ctx, data, len);
  sha1_final(&ctx, hash);
}
