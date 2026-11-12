// pump.fun create_v2 (pump_ref.h).

#include <string.h>
#include "pump_ref.h"

const sol_pubkey_t PUMP_PROGRAM = {{1, 86, 224, 246, 147, 102, 90, 207, 68, 219, 21, 104, 191, 23, 91, 170, 81, 137, 203, 151, 245, 210, 255, 59, 101, 93, 43, 182, 253, 109, 24, 176}};  // 6EF8rrecthR5Dkzon8Nwu78hRvfCKubJ14M5uBEwF6P
const sol_pubkey_t PUMP_GLOBAL = {{58, 134, 94, 105, 238, 15, 84, 128, 202, 188, 246, 99, 87, 228, 220, 47, 24, 213, 141, 69, 193, 234, 116, 137, 251, 55, 35, 217, 121, 60, 114, 166}};  // 4wTV1YmiEkRvAtNtsSGPtUrqRYQMe5SKy2uB4Jjaxnjf
const sol_pubkey_t PUMP_EVENT_AUTHORITY = {{172, 241, 54, 235, 1, 252, 28, 78, 136, 61, 35, 200, 181, 132, 74, 181, 154, 55, 246, 106, 221, 87, 197, 233, 172, 59, 83, 224, 89, 211, 92, 100}};  // Ce6TQqeHC9p8KetsN6JsjHK7UTZk7nasjjnr7XxXp9F1
const sol_pubkey_t PUMP_MINT_AUTHORITY = {{6, 197, 193, 206, 99, 141, 37, 103, 210, 100, 104, 176, 94, 185, 81, 209, 162, 141, 204, 110, 18, 52, 130, 181, 198, 117, 20, 151, 112, 230, 43, 242}};  // TSLvdd1pWpHVjahSpsvCXUbgwsL3JAcvokwaKt1eokM
const sol_pubkey_t MAYHEM_PROGRAM = {{5, 42, 229, 215, 167, 218, 167, 36, 166, 234, 176, 167, 41, 84, 145, 133, 90, 212, 160, 103, 22, 96, 103, 76, 78, 3, 69, 89, 128, 61, 101, 163}};  // MAyhSmzXzV1pTf7LsNkrNwkWKTo4ougAJ1PPg47MD4e
const sol_pubkey_t MAYHEM_GLOBAL_PARAMS = {{0, 173, 174, 162, 125, 179, 198, 243, 208, 78, 92, 240, 162, 2, 159, 3, 227, 57, 143, 8, 83, 149, 239, 239, 239, 23, 31, 198, 74, 248, 23, 65}};  // 13ec7XdrjF3h3YcqBTFDSReRcUFwbCnJaAQspM4j6DDJ
const sol_pubkey_t MAYHEM_SOL_VAULT = {{162, 139, 95, 210, 106, 180, 121, 166, 169, 204, 108, 191, 107, 11, 35, 235, 97, 136, 90, 55, 30, 1, 32, 172, 169, 19, 190, 239, 61, 19, 138, 120}};  // BwWK17cbHxwWBKZkUYvzxLcNQ1YVyaFezduWbtm2de6s

static const uint8_t CREATE_V2[8] = {214, 144, 76, 236, 95, 139, 49, 180};

const char *pump_metadata_check(const char *name, const char *symbol, const char *uri)
{
    size_t n = strlen(name), s = strlen(symbol), u = strlen(uri);
    if (!n || n > PUMP_NAME_MAX) {
        return "name: 1 to 32 bytes";
    }
    if (!s || s > PUMP_SYMBOL_MAX) {
        return "symbol: 1 to 13 bytes";
    }
    if (!u || u > PUMP_URI_MAX) {
        return "uri: 1 to 200 bytes";
    }
    return NULL;
}

void pump_bonding_curve(const sol_pubkey_t *mint, sol_pubkey_t *out)
{
    static const char SEED[] = "bonding-curve";
    const uint8_t *seeds[] = {(const uint8_t *)SEED, mint->b};
    const size_t lens[] = {sizeof(SEED) - 1, 32};
    sol_pda(seeds, lens, 2, &PUMP_PROGRAM, out, NULL);
}

static void mayhem_state(const sol_pubkey_t *mint, sol_pubkey_t *out)
{
    static const char SEED[] = "mayhem-state";
    const uint8_t *seeds[] = {(const uint8_t *)SEED, mint->b};
    const size_t lens[] = {sizeof(SEED) - 1, 32};
    sol_pda(seeds, lens, 2, &MAYHEM_PROGRAM, out, NULL);
}

static bool put_str(sol_ix_t *ix, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    uint8_t len[4] = {n & 0xff, (n >> 8) & 0xff, (n >> 16) & 0xff, n >> 24};
    return sol_ix_bytes(ix, len, 4) && sol_ix_bytes(ix, s, n);
}

bool pump_create_v2(sol_tx_t *tx, const sol_pubkey_t *mint, const sol_pubkey_t *user, const sol_pubkey_t *creator,
                    const char *name, const char *symbol, const char *uri)
{
    if (pump_metadata_check(name, symbol, uri)) {
        return false;
    }
    sol_pubkey_t curve, curve_ata, state, vault_ata;
    pump_bonding_curve(mint, &curve);
    sol_ata(&curve, mint, &SOL_TOKEN_2022_PROGRAM, &curve_ata);
    mayhem_state(mint, &state);
    sol_ata(&MAYHEM_SOL_VAULT, mint, &SOL_TOKEN_2022_PROGRAM, &vault_ata);

    sol_ix_t *ix = sol_tx_add(tx, &PUMP_PROGRAM);
    if (!ix) {
        return false;
    }
    sol_ix_meta(ix, mint, true, true);
    sol_ix_meta(ix, &PUMP_MINT_AUTHORITY, false, false);
    sol_ix_meta(ix, &curve, false, true);
    sol_ix_meta(ix, &curve_ata, false, true);
    sol_ix_meta(ix, &PUMP_GLOBAL, false, false);
    sol_ix_meta(ix, user, true, true);
    sol_ix_meta(ix, &SOL_SYSTEM_PROGRAM, false, false);
    sol_ix_meta(ix, &SOL_TOKEN_2022_PROGRAM, false, false);
    sol_ix_meta(ix, &SOL_ATA_PROGRAM, false, false);
    sol_ix_meta(ix, &MAYHEM_PROGRAM, false, true);
    sol_ix_meta(ix, &MAYHEM_GLOBAL_PARAMS, false, false);
    sol_ix_meta(ix, &MAYHEM_SOL_VAULT, false, true);
    sol_ix_meta(ix, &state, false, true);
    sol_ix_meta(ix, &vault_ata, false, true);
    sol_ix_meta(ix, &PUMP_EVENT_AUTHORITY, false, false);
    sol_ix_meta(ix, &PUMP_PROGRAM, false, false);

    uint8_t flags[2] = {0, 0};           // is_mayhem_mode false, is_cashback_enabled Some(false)
    return sol_ix_bytes(ix, CREATE_V2, sizeof(CREATE_V2)) && put_str(ix, name) && put_str(ix, symbol) &&
           put_str(ix, uri) && sol_ix_bytes(ix, creator->b, 32) && sol_ix_bytes(ix, flags, 2);
}
