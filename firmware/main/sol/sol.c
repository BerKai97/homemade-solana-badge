// Solana primitives for the Pump app (sol.h). Ed25519 is TweetNaCl
// (vendor/, public domain); SHA-256 is PSA Crypto (ESP-IDF's mbedTLS, or the
// emulator's shim).

#include <stdlib.h>
#include <string.h>
#include "esp_random.h"
#include "psa/crypto.h"
#include "vendor/tweetnacl.h"
#include "sol/sol.h"

const sol_pubkey_t SOL_SYSTEM_PROGRAM = {{0}};  // 11111111111111111111111111111111
const sol_pubkey_t SOL_TOKEN_2022_PROGRAM = {{6, 221, 246, 225, 238, 117, 143, 222, 24, 66, 93, 188, 228, 108, 205, 218, 182, 26, 252, 77, 131, 185, 13, 39, 254, 189, 249, 40, 216, 161, 139, 252}};  // TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb
const sol_pubkey_t SOL_ATA_PROGRAM = {{140, 151, 37, 143, 78, 36, 137, 241, 187, 61, 16, 41, 20, 142, 13, 131, 11, 90, 19, 153, 218, 255, 16, 132, 4, 142, 123, 216, 219, 233, 248, 89}};  // ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL
const sol_pubkey_t SOL_COMPUTE_BUDGET = {{3, 6, 70, 111, 229, 33, 23, 50, 255, 236, 173, 186, 114, 195, 155, 231, 188, 140, 229, 187, 197, 247, 18, 107, 44, 67, 155, 58, 64, 0, 0, 0}};  // ComputeBudget111111111111111111111111111111

// TweetNaCl's only source of randomness (crypto_box_keypair etc.; unused by
// the seed-based keypairs below, but it must link).
void randombytes(unsigned char *buf, unsigned long long n)
{
    esp_fill_random(buf, (size_t)n);
}

// ---- base58 ----

static const char B58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

bool sol_b58_encode(const uint8_t *in, size_t n, char *out, size_t cap)
{
    uint8_t digits[96] = {0};               // base58 digits, little end first
    size_t nd = 0, zeros = 0;
    if (n > 64) {
        return false;
    }
    while (zeros < n && !in[zeros]) {
        zeros++;
    }
    for (size_t i = zeros; i < n; i++) {
        unsigned carry = in[i];
        for (size_t j = 0; j < nd; j++) {
            carry += (unsigned)digits[j] << 8;
            digits[j] = carry % 58;
            carry /= 58;
        }
        while (carry) {
            digits[nd++] = carry % 58;
            carry /= 58;
        }
    }
    if (zeros + nd + 1 > cap) {
        return false;
    }
    size_t o = 0;
    for (size_t i = 0; i < zeros; i++) {
        out[o++] = '1';
    }
    for (size_t i = nd; i > 0; i--) {
        out[o++] = B58[digits[i - 1]];
    }
    out[o] = 0;
    return true;
}

bool sol_b58_decode(const char *in, uint8_t *out, size_t n)
{
    uint8_t bytes[96] = {0};                // little end first
    size_t nb = 0, zeros = 0, len = strlen(in);
    if (n > 64 || len > 90) {
        return false;
    }
    while (zeros < len && in[zeros] == '1') {
        zeros++;
    }
    for (size_t i = zeros; i < len; i++) {
        const char *p = strchr(B58, in[i]);
        if (!p || !in[i]) {
            return false;
        }
        unsigned carry = (unsigned)(p - B58);
        for (size_t j = 0; j < nb; j++) {
            carry += (unsigned)bytes[j] * 58;
            bytes[j] = carry & 0xff;
            carry >>= 8;
        }
        while (carry) {
            if (nb >= sizeof(bytes)) {
                return false;
            }
            bytes[nb++] = carry & 0xff;
            carry >>= 8;
        }
    }
    if (zeros + nb != n) {
        return false;
    }
    memset(out, 0, zeros);
    for (size_t i = 0; i < nb; i++) {
        out[zeros + i] = bytes[nb - 1 - i];
    }
    return true;
}

bool sol_pubkey_parse(const char *b58, sol_pubkey_t *out)
{
    return sol_b58_decode(b58, out->b, 32);
}

void sol_pubkey_str(const sol_pubkey_t *k, char out[SOL_B58_PUBKEY])
{
    sol_b58_encode(k->b, 32, out, SOL_B58_PUBKEY);
}

// ---- hashing, keys, signatures ----

void sol_sha256(const void *a, size_t na, const void *b, size_t nb, uint8_t out[32])
{
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    size_t len = 0;
    psa_hash_setup(&op, PSA_ALG_SHA_256);
    psa_hash_update(&op, a, na);
    if (b && nb) {
        psa_hash_update(&op, b, nb);
    }
    psa_hash_finish(&op, out, 32, &len);
    psa_hash_abort(&op);
}

void sol_keypair_from_seed(const uint8_t seed[32], sol_keypair_t *kp)
{
    crypto_sign_seed_keypair(kp->pub.b, kp->secret, seed);
}

void sol_keypair_random(sol_keypair_t *kp)
{
    uint8_t seed[32];
    esp_fill_random(seed, sizeof(seed));
    sol_keypair_from_seed(seed, kp);
    memset(seed, 0, sizeof(seed));
}

void sol_sign(const sol_keypair_t *kp, const uint8_t *msg, size_t n, uint8_t sig[64])
{
    // TweetNaCl signs into sig || msg: one scratch buffer the size of both.
    static uint8_t sm[SOL_TX_V1_MAX + 64];
    unsigned long long smlen = 0;
    if (n > SOL_TX_V1_MAX) {
        memset(sig, 0, 64);
        return;
    }
    crypto_sign(sm, &smlen, msg, n, kp->secret);
    memcpy(sig, sm, 64);
    memset(sm, 0, sizeof(sm));
}

// ---- program addresses ----

bool sol_pda(const uint8_t *const *seeds, const size_t *lens, int count, const sol_pubkey_t *program,
             sol_pubkey_t *out, uint8_t *bump)
{
    static const char MARK[] = "ProgramDerivedAddress";
    for (int b = 255; b >= 0; b--) {
        psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
        uint8_t nonce = (uint8_t)b, hash[32];
        size_t len = 0;
        psa_hash_setup(&op, PSA_ALG_SHA_256);
        for (int i = 0; i < count; i++) {
            psa_hash_update(&op, seeds[i], lens[i]);
        }
        psa_hash_update(&op, &nonce, 1);
        psa_hash_update(&op, program->b, 32);
        psa_hash_update(&op, (const uint8_t *)MARK, sizeof(MARK) - 1);
        psa_hash_finish(&op, hash, 32, &len);
        psa_hash_abort(&op);
        if (!crypto_sign_is_on_curve(hash)) {
            memcpy(out->b, hash, 32);
            if (bump) {
                *bump = nonce;
            }
            return true;
        }
    }
    return false;
}

void sol_ata(const sol_pubkey_t *owner, const sol_pubkey_t *mint, const sol_pubkey_t *token_program,
             sol_pubkey_t *out)
{
    const uint8_t *seeds[] = {owner->b, token_program->b, mint->b};
    const size_t lens[] = {32, 32, 32};
    sol_pda(seeds, lens, 3, &SOL_ATA_PROGRAM, out, NULL);
}

// ---- transactions ----

sol_ix_t *sol_tx_add(sol_tx_t *tx, const sol_pubkey_t *program)
{
    if (tx->n_ix >= SOL_MAX_IX) {
        return NULL;
    }
    sol_ix_t *ix = &tx->ix[tx->n_ix++];
    memset(ix, 0, sizeof(*ix));
    ix->program = *program;
    return ix;
}

void sol_ix_meta(sol_ix_t *ix, const sol_pubkey_t *key, bool signer, bool writable)
{
    if (ix && ix->n_metas < SOL_MAX_METAS) {
        ix->metas[ix->n_metas++] = (sol_meta_t){.key = *key, .signer = signer, .writable = writable};
    }
}

bool sol_ix_bytes(sol_ix_t *ix, const void *data, size_t n)
{
    if (!ix || ix->n_data + n > SOL_IX_DATA_MAX) {
        return false;
    }
    memcpy(ix->data + ix->n_data, data, n);
    ix->n_data += n;
    return true;
}

typedef struct {
    sol_pubkey_t key;
    bool signer, writable;
} key_t_;

// A message's keys in Solana's order: the payer, then signer+writable,
// signer, writable and readonly keys, each class sorted by its bytes.
typedef struct {
    key_t_ k[SOL_MAX_KEYS];
    int n, n_sig, n_ro_sig, n_ro;
} keys_t;

static int find_key(const keys_t *ks, const sol_pubkey_t *k)
{
    for (int i = 0; i < ks->n; i++) {
        if (!memcmp(ks->k[i].key.b, k->b, 32)) {
            return i;
        }
    }
    return -1;
}

static bool add_key(keys_t *ks, const sol_pubkey_t *k, bool signer, bool writable)
{
    int i = find_key(ks, k);
    if (i < 0) {
        if (ks->n >= SOL_MAX_KEYS) {
            return false;
        }
        i = ks->n++;
        ks->k[i] = (key_t_){.key = *k};
    }
    ks->k[i].signer |= signer;
    ks->k[i].writable |= writable;
    return true;
}

static int by_key(const void *a, const void *b)
{
    return memcmp(((const key_t_ *)a)->key.b, ((const key_t_ *)b)->key.b, 32);
}

static bool compile(const sol_tx_t *tx, keys_t *out)
{
    keys_t all = {0};
    add_key(&all, &tx->payer, true, true);
    for (int i = 0; i < tx->n_ix; i++) {
        for (int m = 0; m < tx->ix[i].n_metas; m++) {
            const sol_meta_t *mt = &tx->ix[i].metas[m];
            if (!add_key(&all, &mt->key, mt->signer, mt->writable)) {
                return false;
            }
        }
        if (!add_key(&all, &tx->ix[i].program, false, false)) {
            return false;
        }
    }
    *out = (keys_t){.k = {all.k[0]}, .n = 1};
    for (int pass = 0; pass < 4; pass++) {
        int start = out->n;
        for (int i = 1; i < all.n; i++) {
            bool s = all.k[i].signer, w = all.k[i].writable;
            if (pass == (s ? 0 : 2) + !w) {
                out->k[out->n++] = all.k[i];
            }
        }
        qsort(out->k + start, (size_t)(out->n - start), sizeof(key_t_), by_key);
    }
    for (int i = 0; i < out->n; i++) {
        bool s = out->k[i].signer, w = out->k[i].writable;
        out->n_sig += s;
        out->n_ro_sig += s && !w;
        out->n_ro += !s && !w;
    }
    return true;
}

// compact-u16
static size_t put_len(uint8_t *out, unsigned v)
{
    size_t n = 0;
    do {
        uint8_t b = v & 0x7f;
        v >>= 7;
        out[n++] = b | (v ? 0x80 : 0);
    } while (v);
    return n;
}

static uint8_t *put_le(uint8_t *p, uint64_t v, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        *p++ = (uint8_t)(v >> (8 * i));
    }
    return p;
}

#define NEED(k) do { if ((size_t)(end - p) < (size_t)(k)) return 0; } while (0)

size_t sol_tx_message(const sol_tx_t *tx, const uint8_t blockhash[32], uint8_t *out, size_t cap,
                      sol_pubkey_t *signers, int *n_signers)
{
    keys_t ks;
    if (!compile(tx, &ks)) {
        return 0;
    }
    uint8_t *p = out, *end = out + cap;
    NEED(3 + 3 + 32 * ks.n + 32 + 3);
    *p++ = (uint8_t)ks.n_sig;
    *p++ = (uint8_t)ks.n_ro_sig;
    *p++ = (uint8_t)ks.n_ro;
    p += put_len(p, ks.n);
    for (int i = 0; i < ks.n; i++) {
        memcpy(p, ks.k[i].key.b, 32);
        p += 32;
    }
    memcpy(p, blockhash, 32);
    p += 32;
    p += put_len(p, tx->n_ix);
    for (int i = 0; i < tx->n_ix; i++) {
        const sol_ix_t *ix = &tx->ix[i];
        NEED(1 + 3 + ix->n_metas + 3 + ix->n_data);
        *p++ = (uint8_t)find_key(&ks, &ix->program);
        p += put_len(p, ix->n_metas);
        for (int m = 0; m < ix->n_metas; m++) {
            *p++ = (uint8_t)find_key(&ks, &ix->metas[m].key);
        }
        p += put_len(p, (unsigned)ix->n_data);
        memcpy(p, ix->data, ix->n_data);
        p += ix->n_data;
    }
    if (signers && n_signers) {
        for (int i = 0; i < ks.n_sig; i++) {
            signers[i] = ks.k[i].key;
        }
        *n_signers = ks.n_sig;
    }
    return (size_t)(p - out);
}

size_t sol_tx_message_v1(const sol_tx_t *tx, const uint8_t blockhash[32], const sol_budget_t *budget,
                         uint8_t *out, size_t cap)
{
    keys_t ks;
    if (!compile(tx, &ks)) {
        return 0;
    }
    uint8_t *p = out, *end = out + cap;
    NEED(SOL_V1_KEYS + 32 * ks.n + 16 + 4 * tx->n_ix);
    *p++ = SOL_V1_PREFIX;
    *p++ = (uint8_t)ks.n_sig;
    *p++ = (uint8_t)ks.n_ro_sig;
    *p++ = (uint8_t)ks.n_ro;
    p = put_le(p, 0x0f, 4);             // config mask: priority fee, compute limit, loaded data limit
    memcpy(p, blockhash, 32);
    p += 32;
    *p++ = (uint8_t)tx->n_ix;
    *p++ = (uint8_t)ks.n;
    for (int i = 0; i < ks.n; i++) {
        memcpy(p, ks.k[i].key.b, 32);
        p += 32;
    }
    p = put_le(p, budget->priority_fee, 8);
    p = put_le(p, budget->cu_limit, 4);
    p = put_le(p, budget->loaded_data_limit, 4);
    for (int i = 0; i < tx->n_ix; i++) {
        const sol_ix_t *ix = &tx->ix[i];
        *p++ = (uint8_t)find_key(&ks, &ix->program);
        *p++ = (uint8_t)ix->n_metas;
        p = put_le(p, ix->n_data, 2);
    }
    for (int i = 0; i < tx->n_ix; i++) {
        const sol_ix_t *ix = &tx->ix[i];
        NEED(ix->n_metas + ix->n_data);
        for (int m = 0; m < ix->n_metas; m++) {
            *p++ = (uint8_t)find_key(&ks, &ix->metas[m].key);
        }
        memcpy(p, ix->data, ix->n_data);
        p += ix->n_data;
    }
    return (size_t)(p - out);
}

#undef NEED

size_t sol_tx_sign(const sol_tx_t *tx, const uint8_t blockhash[32], const sol_keypair_t *const *kps, int n_kps,
                   uint8_t *out, size_t cap, uint8_t sig0[64])
{
    static uint8_t msg[SOL_TX_MAX];
    size_t n_msg = sol_tx_message(tx, blockhash, msg, sizeof(msg), NULL, NULL);
    return n_msg ? sol_msg_sign(msg, n_msg, kps, n_kps, out, cap, sig0) : 0;
}

int sol_msg_signers(const uint8_t *msg, size_t n, sol_pubkey_t *out, int max)
{
    size_t o = 0;
    unsigned keys = 0;
    int n_sig;
    if (n > SOL_V1_KEYS && msg[0] == SOL_V1_PREFIX) {
        n_sig = msg[1];
        keys = msg[SOL_V1_KEYS - 1];
        o = SOL_V1_KEYS;
    } else {
        if (n < 4) {
            return -1;
        }
        if (msg[0] & 0x80) {
            o = 1;                              // v0: a version prefix byte
        }
        n_sig = msg[o];
        o += 3;
        for (unsigned shift = 0;; shift += 7) { // compact-u16
            if (o >= n || shift > 14) {
                return -1;
            }
            uint8_t b = msg[o++];
            keys |= (unsigned)(b & 0x7f) << shift;
            if (!(b & 0x80)) {
                break;
            }
        }
    }
    if (n_sig < 1 || n_sig > max || (unsigned)n_sig > keys || o + 32u * keys > n) {
        return -1;
    }
    for (int i = 0; i < n_sig; i++) {
        memcpy(out[i].b, msg + o + 32 * i, 32);
    }
    return n_sig;
}

size_t sol_msg_sign(const uint8_t *msg, size_t n, const sol_keypair_t *const *kps, int n_kps,
                    uint8_t *out, size_t cap, uint8_t sig0[64])
{
    sol_pubkey_t signers[SOL_MAX_KEYS];
    int n_signers = sol_msg_signers(msg, n, signers, SOL_MAX_KEYS);
    if (n_signers < 1 || n_signers > 127) {
        return 0;
    }
    bool v1 = msg[0] == SOL_V1_PREFIX;  // v1: the message, then the signatures; else their count first
    size_t total = !v1 + 64 * (size_t)n_signers + n;
    if (total > cap || total > (v1 ? SOL_TX_V1_MAX : SOL_TX_MAX)) {
        return 0;
    }
    uint8_t *sigs = v1 ? out + n : out + 1;
    memcpy(v1 ? out : sigs + 64 * n_signers, msg, n);
    if (!v1) {
        out[0] = (uint8_t)n_signers;
    }
    for (int s = 0; s < n_signers; s++) {
        const sol_keypair_t *kp = NULL;
        for (int k = 0; k < n_kps && !kp; k++) {
            if (kps[k] && !memcmp(kps[k]->pub.b, signers[s].b, 32)) {
                kp = kps[k];
            }
        }
        if (!kp) {
            return 0;
        }
        sol_sign(kp, msg, n, sigs + 64 * s);
    }
    if (sig0) {
        memcpy(sig0, sigs, 64);
    }
    return total;
}

void sol_tx_cu_limit(sol_tx_t *tx, uint32_t units)
{
    sol_ix_t *ix = sol_tx_add(tx, &SOL_COMPUTE_BUDGET);
    uint8_t d[5] = {2, units & 0xff, (units >> 8) & 0xff, (units >> 16) & 0xff, units >> 24};
    sol_ix_bytes(ix, d, sizeof(d));
}

void sol_tx_cu_price(sol_tx_t *tx, uint64_t micro_lamports)
{
    sol_ix_t *ix = sol_tx_add(tx, &SOL_COMPUTE_BUDGET);
    uint8_t d[9] = {3};
    for (int i = 0; i < 8; i++) {
        d[1 + i] = (uint8_t)(micro_lamports >> (8 * i));
    }
    sol_ix_bytes(ix, d, sizeof(d));
}
