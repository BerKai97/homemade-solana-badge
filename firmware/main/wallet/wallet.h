#pragma once

// The badge's own Solana wallet (this fork only; Solana OS has none). One
// keypair's 32-byte seed in NVS (namespace "wallet"), never handed to Lua: apps
// get the address and ask wallet.sign, which the owner approves on screen.
// Also the Solana RPC URL that Lua's chain.rpc talks to directly, and the IPFS
// upload key (Filebase's IPFS RPC by default) that Lua's ipfs.add uses, and an
// optional imported mint keypair (a vanity address made on a PC) for the next
// launch, which Lua gets as wallet.mint().
//
// NVS is plain flash: anyone holding the badge with a USB cable can read it.
// Keep only what you'd be fine losing in this wallet.

#include <stdbool.h>
#include <stddef.h>
#include "sol/sol.h"

#define WALLET_RPC_MAX      200
#define WALLET_RPC_DEFAULT  "https://api.devnet.solana.com"
#define WALLET_IPFS_KEY_MAX 200
#define WALLET_IPFS_DEFAULT "https://rpc.filebase.io/api/v0/add"

void wallet_init(void);                         // console commands; before console_start()
bool wallet_address(sol_pubkey_t *out);         // false: no wallet yet
bool wallet_keypair(sol_keypair_t *out);        // false: no wallet. Wipe *out after use.
void wallet_rpc_url(char *out, size_t n);       // the RPC chain.rpc uses
const char *wallet_cluster(void);               // "devnet", "mainnet", "testnet" or "custom", from the URL
bool wallet_ipfs_key(char *out, size_t n);      // false: none set. Wipe out after use.
void wallet_ipfs_url(char *out, size_t n);      // where ipfs.add posts
bool wallet_mint_keypair(sol_keypair_t *out);   // the imported mint; false: none. Wipe *out after use.
bool wallet_mint_forget(void);                  // erase it (false: NVS failed)
