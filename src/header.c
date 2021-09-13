/*!
 * header.c - header for libsatoshi
 * Copyright (c) 2021, Christopher Jeffrey (MIT License).
 * https://github.com/chjj/libsatoshi
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <satoshi/header.h>
#include <satoshi/util.h>
#include <satoshi/crypto/hash.h>
#include "impl.h"
#include "internal.h"

/*
 * Header
 */

DEFINE_SERIALIZABLE_OBJECT(btc_header, SCOPE_EXTERN)

void
btc_header_init(btc_header_t *z) {
  z->version = 0;
  memset(z->prev_block, 0, 32);
  memset(z->merkle_root, 0, 32);
  z->time = 0;
  z->bits = 0;
  z->nonce = 0;
}

void
btc_header_clear(btc_header_t *z) {
  (void)z;
}

void
btc_header_copy(btc_header_t *z, const btc_header_t *x) {
  z->version = x->version;
  memcpy(z->prev_block, x->prev_block, 32);
  memcpy(z->merkle_root, x->merkle_root, 32);
  z->time = x->time;
  z->bits = x->bits;
  z->nonce = x->nonce;
}

size_t
btc_header_size(const btc_header_t *x) {
  size_t size = 0;

  (void)x;

  size += 4;
  size += 32;
  size += 32;
  size += 4;
  size += 4;
  size += 4;

  return size;
}

uint8_t *
btc_header_write(uint8_t *zp, const btc_header_t *x) {
  zp = btc_uint32_write(zp, x->version);
  zp = btc_raw_write(zp, x->prev_block, 32);
  zp = btc_raw_write(zp, x->merkle_root, 32);
  zp = btc_uint32_write(zp, x->time);
  zp = btc_uint32_write(zp, x->bits);
  zp = btc_uint32_write(zp, x->nonce);
  return zp;
}

int
btc_header_read(btc_header_t *z, const uint8_t **xp, size_t *xn) {
  if (!btc_uint32_read(&z->version, xp, xn))
    return 0;

  if (!btc_raw_read(z->prev_block, 32, xp, xn))
    return 0;

  if (!btc_raw_read(z->merkle_root, 32, xp, xn))
    return 0;

  if (!btc_uint32_read(&z->time, xp, xn))
    return 0;

  if (!btc_uint32_read(&z->bits, xp, xn))
    return 0;

  if (!btc_uint32_read(&z->nonce, xp, xn))
    return 0;

  return 1;
}

void
btc_header_hash(uint8_t *hash, const btc_header_t *hdr) {
  btc_hash256_t ctx;

  btc_hash256_init(&ctx);

  btc_uint32_update(&ctx, hdr->version);
  btc_raw_update(&ctx, hdr->prev_block, 32);
  btc_raw_update(&ctx, hdr->merkle_root, 32);
  btc_uint32_update(&ctx, hdr->time);
  btc_uint32_update(&ctx, hdr->bits);
  btc_uint32_update(&ctx, hdr->nonce);

  btc_hash256_final(&ctx, hash);
}

int
btc_header_verify(const btc_header_t *hdr) {
  uint8_t target[32];
  uint8_t hash[32];

  if (!btc_compact_export(target, hdr->bits))
    return 0;

  btc_header_hash(hash, hdr);

  return btc_hash_compare(hash, target) <= 0;
}

int
btc_header_mine(btc_header_t *hdr,
                const uint8_t *target,
                uint64_t limit,
                uint32_t (*adjtime)(void *),
                void *arg) {
  uint64_t attempt = 0;
  btc_hash256_t pre, ctx;
  uint8_t hash[32];

  memset(&pre, 0, sizeof(pre));

  for (;;) {
    hdr->time = adjtime(arg);

    btc_hash256_init(&pre);

    btc_uint32_update(&pre, hdr->version);
    btc_raw_update(&pre, hdr->prev_block, 32);
    btc_raw_update(&pre, hdr->merkle_root, 32);
    btc_uint32_update(&pre, hdr->time);
    btc_uint32_update(&pre, hdr->bits);

    do {
      ctx = pre;

      btc_uint32_update(&ctx, hdr->nonce);
      btc_hash256_final(&ctx, hash);

      if (btc_hash_compare(hash, target) <= 0)
        return 1;

      hdr->nonce++;

      if (limit > 0 && ++attempt == limit)
        return 0;
    } while (hdr->nonce != 0);
  }
}
