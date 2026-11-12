#pragma once

// pump.fun's create_v2 (a Token-2022 coin quoted in SOL, not mayhem), from
// pump's public IDL (pump-public-docs, idl/pump.json): its 16 accounts, data
// = discriminator, name, symbol, uri, creator, is_mayhem_mode (false),
// is_cashback_enabled (false). tests/pump_test.c checks it against golden vectors.

#include <stdbool.h>
#include "sol/sol.h"

#define PUMP_NAME_MAX    32             // bytes, pump's limits
#define PUMP_SYMBOL_MAX  13
#define PUMP_URI_MAX     200
#define PUMP_CREATE_UNITS 250000        // compute limit for a create (measured worst ~196k)

extern const sol_pubkey_t PUMP_PROGRAM, PUMP_GLOBAL, PUMP_EVENT_AUTHORITY, PUMP_MINT_AUTHORITY;
extern const sol_pubkey_t MAYHEM_PROGRAM, MAYHEM_GLOBAL_PARAMS, MAYHEM_SOL_VAULT;

// NULL when the metadata is fine, else why not.
const char *pump_metadata_check(const char *name, const char *symbol, const char *uri);

// Appends create_v2 to tx. user pays and signs; creator is who earns the
// creator fees (usually user). false: metadata out of limits or tx full.
bool pump_create_v2(sol_tx_t *tx, const sol_pubkey_t *mint, const sol_pubkey_t *user, const sol_pubkey_t *creator,
                    const char *name, const char *symbol, const char *uri);

void pump_bonding_curve(const sol_pubkey_t *mint, sol_pubkey_t *out);
