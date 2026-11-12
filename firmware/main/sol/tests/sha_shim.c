// Host-only SHA-256 for the emulator's mbedtls shim API (tests/run.sh), and esp_fill_random.
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include "mbedtls/sha256.h"
static SHA256_CTX c_;
void mbedtls_sha256_init(mbedtls_sha256_context *ctx) { (void)ctx; }
void mbedtls_sha256_free(mbedtls_sha256_context *ctx) { (void)ctx; }
int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224) { (void)ctx; (void)is224; SHA256_Init(&c_); return 0; }
int mbedtls_sha256_update(mbedtls_sha256_context *ctx, const unsigned char *in, size_t n) { (void)ctx; SHA256_Update(&c_, in, n); return 0; }
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx, unsigned char out[32]) { (void)ctx; SHA256_Final(out, &c_); return 0; }
void esp_fill_random(void *buf, size_t len) { FILE *f = fopen("/dev/urandom", "rb"); fread(buf, 1, len, f); fclose(f); }
