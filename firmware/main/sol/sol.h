#pragma once

// Just enough Solana for the Pump app: keys, base58, PDAs, Ed25519 signing and
// legacy and v1 transaction messages. Portable C (badge and emulator).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t b[32];
} sol_pubkey_t;

typedef struct {
    uint8_t secret[64];                 // seed (32) || public key (32), as Solana's CLI and wallets store it
    sol_pubkey_t pub;
} sol_keypair_t;

#define SOL_B58_PUBKEY  45              // 44 characters + NUL
#define SOL_B58_SIG     89              // 88 characters + NUL
#define SOL_TX_MAX      1232            // a legacy or v0 transaction's packet limit
#define SOL_TX_V1_MAX   4096            // a v1 transaction's (SIMD-0385)
#define SOL_V1_PREFIX   0x81            // a v1 message's first byte
#define SOL_V1_KEYS     42              // where its account keys start
#define SOL_LOADED_DATA_MAX (64u << 20) // the runtime's loaded accounts data limit, its default

// base58 (Bitcoin alphabet). encode: NUL-terminated, false if out is too small.
// decode: exactly n bytes expected, false otherwise.
bool sol_b58_encode(const uint8_t *in, size_t n, char *out, size_t cap);
bool sol_b58_decode(const char *in, uint8_t *out, size_t n);
bool sol_pubkey_parse(const char *b58, sol_pubkey_t *out);
void sol_pubkey_str(const sol_pubkey_t *k, char out[SOL_B58_PUBKEY]);

void sol_sha256(const void *a, size_t na, const void *b, size_t nb, uint8_t out[32]);

void sol_keypair_from_seed(const uint8_t seed[32], sol_keypair_t *kp);
void sol_keypair_random(sol_keypair_t *kp);
void sol_sign(const sol_keypair_t *kp, const uint8_t *msg, size_t n, uint8_t sig[64]);

// find_program_address: false when no bump in 255..0 gives an off-curve key.
bool sol_pda(const uint8_t *const *seeds, const size_t *lens, int count, const sol_pubkey_t *program,
             sol_pubkey_t *out, uint8_t *bump);
// The associated token account of owner for mint under token_program.
void sol_ata(const sol_pubkey_t *owner, const sol_pubkey_t *mint, const sol_pubkey_t *token_program,
             sol_pubkey_t *out);

// ---- Transaction messages ----

#define SOL_MAX_KEYS    32
#define SOL_MAX_IX      6
#define SOL_MAX_METAS   24
#define SOL_IX_DATA_MAX 400

typedef struct {
    sol_pubkey_t key;
    bool signer, writable;
} sol_meta_t;

typedef struct {
    sol_pubkey_t program;
    sol_meta_t metas[SOL_MAX_METAS];
    int n_metas;
    uint8_t data[SOL_IX_DATA_MAX];
    size_t n_data;
} sol_ix_t;

typedef struct {
    sol_pubkey_t payer;
    sol_ix_t ix[SOL_MAX_IX];
    int n_ix;
} sol_tx_t;

sol_ix_t *sol_tx_add(sol_tx_t *tx, const sol_pubkey_t *program);
void sol_ix_meta(sol_ix_t *ix, const sol_pubkey_t *key, bool signer, bool writable);
bool sol_ix_bytes(sol_ix_t *ix, const void *data, size_t n);

// Compile a legacy message: keys ordered as Solana's compiler orders them, the
// payer first, then signer+writable, signer, writable, readonly, each class by
// its bytes. Returns the message length (0 = too big / too many keys) and the
// signers in the order their signatures go, as pubkeys.
size_t sol_tx_message(const sol_tx_t *tx, const uint8_t blockhash[32], uint8_t *out, size_t cap,
                      sol_pubkey_t *signers, int *n_signers);

// A v1 message's compute budget, which it carries in its config: v1 ignores
// ComputeBudget instructions and reads an unset compute limit as 0.
typedef struct {
    uint64_t priority_fee;              // lamports, the whole fee (not per unit)
    uint32_t cu_limit;
    uint32_t loaded_data_limit;
} sol_budget_t;

// Compile a v1 message (SIMD-0385), keys as sol_tx_message orders them, the
// whole budget in its config. Returns the message length, 0 if it doesn't fit.
size_t sol_tx_message_v1(const sol_tx_t *tx, const uint8_t blockhash[32], const sol_budget_t *budget,
                         uint8_t *out, size_t cap);

// Sign and serialize: message from sol_tx_message, one keypair per signer (any
// order: matched by public key). Returns the wire length, 0 if a signer has no
// keypair or it doesn't fit. sig0 (optional) gets the first signature: the
// transaction's id.
size_t sol_tx_sign(const sol_tx_t *tx, const uint8_t blockhash[32], const sol_keypair_t *const *kps, int n_kps,
                   uint8_t *out, size_t cap, uint8_t sig0[64]);

// The signers a serialized message (legacy, v0 or v1) names, in signature
// order: its first header[0] account keys. Returns the count, -1 if malformed.
int sol_msg_signers(const uint8_t *msg, size_t n, sol_pubkey_t *out, int max);

// Sign a serialized message with the keypairs whose keys it names as signers:
// writes the wire transaction (legacy and v0: the signatures, then the message;
// v1: the message, then the signatures). Returns its length, 0 if a signer has
// no keypair or it doesn't fit. sig0 (optional) gets the first signature.
size_t sol_msg_sign(const uint8_t *msg, size_t n, const sol_keypair_t *const *kps, int n_kps,
                    uint8_t *out, size_t cap, uint8_t sig0[64]);

// Compute budget instructions.
void sol_tx_cu_limit(sol_tx_t *tx, uint32_t units);
void sol_tx_cu_price(sol_tx_t *tx, uint64_t micro_lamports);

extern const sol_pubkey_t SOL_SYSTEM_PROGRAM, SOL_TOKEN_2022_PROGRAM, SOL_ATA_PROGRAM, SOL_COMPUTE_BUDGET;
