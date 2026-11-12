// sol, wallet and chain for Lua apps.
//
//   sol: pure helpers, no permission. Addresses are base58 strings; seeds,
//   instruction data and messages are binary strings (string.pack builds them).
//   wallet ("wallet" permission): the badge's address, throwaway keypairs for
//   this run (a new mint), the mint imported on the console (wallet.mint), and
//   wallet.sign (lua_net.c), which the owner approves in the OS's own dialog.
//   No secret key ever reaches Lua.
//   chain ("chain" permission): chain.rpc (lua_net.c) straight to the RPC set
//   with the console's "wallet rpc", and chain.cluster().

#include <stdlib.h>
#include <string.h>
#include "sol/sol.h"
#include "wallet/wallet.h"
#include "lua_priv.h"

#define EPH_MAX 4

// Throwaway keypairs of the running app (one app runs at a time). Cleared when
// it stops (lh_sol_stop) and before every launch (lh_open_sol).
static sol_keypair_t s_eph[EPH_MAX];
static int s_neph;

int lh_sol_keypairs(const sol_keypair_t **out, int max)
{
    int n = 0;
    for (int i = 0; i < s_neph && n < max; i++) {
        out[n++] = &s_eph[i];
    }
    return n;
}

void lh_sol_stop(void)
{
    memset(s_eph, 0, sizeof(s_eph));
    s_neph = 0;
}

static void check_key(lua_State *L, int idx, sol_pubkey_t *out)
{
    const char *s = luaL_checkstring(L, idx);
    if (!sol_pubkey_parse(s, out)) {
        luaL_error(L, "not a base58 address: %s", s);
    }
}

static void push_key(lua_State *L, const sol_pubkey_t *k)
{
    char a[SOL_B58_PUBKEY];
    sol_pubkey_str(k, a);
    lua_pushstring(L, a);
}

// sol.encode(bytes) -> base58
static int l_encode(lua_State *L)
{
    size_t n;
    const char *b = luaL_checklstring(L, 1, &n);
    char out[96];
    luaL_argcheck(L, n <= 64, 1, "at most 64 bytes");
    sol_b58_encode((const uint8_t *)b, n, out, sizeof(out));
    lua_pushstring(L, out);
    return 1;
}

// sol.decode(base58 [, bytes = 32]) -> binary string, or nil
static int l_decode(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    lua_Integer n = luaL_optinteger(L, 2, 32);
    uint8_t out[64];
    luaL_argcheck(L, n > 0 && n <= 64, 2, "1 to 64 bytes");
    if (!sol_b58_decode(s, out, (size_t)n)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, (const char *)out, (size_t)n);
    return 1;
}

// sol.pda({seed, ...}, program) -> address, bump. Seeds are binary strings (an
// address as a seed: sol.decode(address)), each up to 32 bytes, at most 16.
static int l_pda(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    sol_pubkey_t program, out;
    check_key(L, 2, &program);
    const uint8_t *seeds[16];
    size_t lens[16];
    int n = (int)luaL_len(L, 1);
    luaL_argcheck(L, n >= 0 && n <= 16, 1, "at most 16 seeds");
    for (int i = 0; i < n; i++) {
        lua_rawgeti(L, 1, i + 1);
        seeds[i] = (const uint8_t *)luaL_checklstring(L, -1, &lens[i]);  // kept alive by the table
        luaL_argcheck(L, lens[i] <= 32, 1, "a seed is at most 32 bytes");
        lua_pop(L, 1);
    }
    uint8_t bump = 0;
    if (!sol_pda(seeds, lens, n, &program, &out, &bump)) {
        return luaL_error(L, "no program address for these seeds");
    }
    push_key(L, &out);
    lua_pushinteger(L, bump);
    return 2;
}

// sol.ata(owner, mint, token_program) -> address
static int l_ata(lua_State *L)
{
    sol_pubkey_t owner, mint, prog, out;
    check_key(L, 1, &owner);
    check_key(L, 2, &mint);
    check_key(L, 3, &prog);
    sol_ata(&owner, &mint, &prog, &out);
    push_key(L, &out);
    return 1;
}

// A u64 argument: an integer or a decimal string (Lua's integers are 32-bit
// here, so 2^31 and up must be strings).
static uint64_t check_u64(lua_State *L, int idx)
{
    uint64_t v = 0;
    if (lua_type(L, idx) == LUA_TNUMBER && lua_isinteger(L, idx)) {
        lua_Integer i = lua_tointeger(L, idx);
        luaL_argcheck(L, i >= 0, idx, "not negative");
        return (uint64_t)i;
    }
    const char *s = luaL_checkstring(L, idx);
    luaL_argcheck(L, *s, idx, "a decimal string");
    for (; *s; s++) {
        luaL_argcheck(L, *s >= '0' && *s <= '9' && v <= (UINT64_MAX - 9) / 10, idx, "a decimal u64");
        v = v * 10 + (uint64_t)(*s - '0');
    }
    return v;
}

// sol.u64(n) -> 8 bytes little-endian. n: an integer or a decimal string.
static int l_u64(lua_State *L)
{
    uint64_t v = check_u64(L, 1);
    uint8_t b[8];
    for (int i = 0; i < 8; i++) {
        b[i] = (uint8_t)(v >> (8 * i));
    }
    lua_pushlstring(L, (const char *)b, 8);
    return 1;
}

// sol.message{payer=, blockhash=, instructions={{program=, accounts={{address,
// signer, writable}, ...}, data=binary}, ...}} -> the legacy message, binary.
// With budget={cu_limit=, priority_fee= (lamports, the whole fee), loaded_data=
// (bytes, default the runtime's limit)}: a v1 message (SIMD-0385) carrying it.
static int l_message(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    sol_tx_t *tx = lua_newuserdatauv(L, sizeof(sol_tx_t), 0);    // collected with the call
    memset(tx, 0, sizeof(*tx));
    uint8_t blockhash[32];

    lua_getfield(L, 1, "payer");
    check_key(L, -1, &tx->payer);
    lua_pop(L, 1);
    lua_getfield(L, 1, "blockhash");
    if (!sol_b58_decode(luaL_checkstring(L, -1), blockhash, 32)) {
        return luaL_error(L, "blockhash: base58 of 32 bytes");
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "instructions");
    luaL_checktype(L, -1, LUA_TTABLE);
    int nix = (int)luaL_len(L, -1);
    if (nix < 1 || nix > SOL_MAX_IX) {
        return luaL_error(L, "1 to %d instructions", SOL_MAX_IX);
    }
    for (int i = 1; i <= nix; i++) {
        lua_rawgeti(L, -1, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        sol_pubkey_t prog;
        lua_getfield(L, -1, "program");
        check_key(L, -1, &prog);
        lua_pop(L, 1);
        sol_ix_t *ix = sol_tx_add(tx, &prog);

        lua_getfield(L, -1, "accounts");
        luaL_checktype(L, -1, LUA_TTABLE);
        int na = (int)luaL_len(L, -1);
        if (na > SOL_MAX_METAS) {
            return luaL_error(L, "instruction %d: at most %d accounts", i, SOL_MAX_METAS);
        }
        for (int a = 1; a <= na; a++) {
            lua_rawgeti(L, -1, a);
            luaL_checktype(L, -1, LUA_TTABLE);
            sol_pubkey_t k;
            lua_rawgeti(L, -1, 1);
            check_key(L, -1, &k);
            lua_rawgeti(L, -2, 2);
            lua_rawgeti(L, -3, 3);
            sol_ix_meta(ix, &k, lua_toboolean(L, -2), lua_toboolean(L, -1));
            lua_pop(L, 4);
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "data");
        size_t nd = 0;
        const char *d = luaL_optlstring(L, -1, "", &nd);
        if (!sol_ix_bytes(ix, d, nd)) {
            return luaL_error(L, "instruction %d: data over %d bytes", i, SOL_IX_DATA_MAX);
        }
        lua_pop(L, 2);
    }
    lua_pop(L, 1);

    uint8_t *msg = lua_newuserdatauv(L, SOL_TX_V1_MAX, 0);
    size_t n;
    if (lua_getfield(L, 1, "budget") == LUA_TNIL) {
        n = sol_tx_message(tx, blockhash, msg, SOL_TX_MAX, NULL, NULL);
    } else {
        luaL_checktype(L, -1, LUA_TTABLE);
        int b = lua_gettop(L);
        lua_getfield(L, b, "cu_limit");
        lua_getfield(L, b, "priority_fee");
        lua_getfield(L, b, "loaded_data");
        lua_Integer cu = luaL_checkinteger(L, b + 1);
        lua_Integer data = luaL_optinteger(L, b + 3, SOL_LOADED_DATA_MAX);
        luaL_argcheck(L, cu > 0 && data > 0, 1, "budget: a compute limit and loaded data limit above 0");
        sol_budget_t budget = {
            .priority_fee = lua_isnil(L, b + 2) ? 0 : check_u64(L, b + 2),
            .cu_limit = (uint32_t)cu,
            .loaded_data_limit = (uint32_t)data,
        };
        n = sol_tx_message_v1(tx, blockhash, &budget, msg, SOL_TX_V1_MAX);
    }
    if (!n) {
        return luaL_error(L, "the message is too big or names too many accounts");
    }
    lua_pushlstring(L, (const char *)msg, n);
    return 1;
}

// wallet.address() -> base58, or nil when the badge has no wallet
static int l_address(lua_State *L)
{
    lh_need_perm(L, LUA_PERM_WALLET, "wallet");
    sol_pubkey_t k;
    if (wallet_address(&k)) {
        push_key(L, &k);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

// wallet.keypair() -> the address of a new throwaway keypair (a mint, say),
// which wallet.sign signs with when a message names it. Gone when the app stops.
static int l_keypair(lua_State *L)
{
    lh_need_perm(L, LUA_PERM_WALLET, "wallet");
    if (s_neph >= EPH_MAX) {
        return luaL_error(L, "at most %d keypairs per run", EPH_MAX);
    }
    sol_keypair_random(&s_eph[s_neph]);
    push_key(L, &s_eph[s_neph].pub);
    s_neph++;
    return 1;
}

// wallet.mint() -> the address of the mint imported with the console's
// "wallet mint import", or nil. Like wallet.keypair, wallet.sign then signs
// with it for the rest of this run.
static int l_mint(lua_State *L)
{
    lh_need_perm(L, LUA_PERM_WALLET, "wallet");
    sol_keypair_t kp;
    if (!wallet_mint_keypair(&kp)) {
        lua_pushnil(L);
        return 1;
    }
    int i = 0;
    while (i < s_neph && memcmp(s_eph[i].pub.b, kp.pub.b, 32)) {
        i++;
    }
    if (i == s_neph) {
        if (s_neph >= EPH_MAX) {
            memset(&kp, 0, sizeof(kp));
            return luaL_error(L, "at most %d keypairs per run", EPH_MAX);
        }
        s_eph[s_neph++] = kp;
    }
    memset(&kp, 0, sizeof(kp));
    push_key(L, &s_eph[i].pub);
    return 1;
}

// wallet.mint_used(address) -> true if that was the imported mint, now erased
// (a mint can make one coin). This run can still sign with it.
static int l_mint_used(lua_State *L)
{
    lh_need_perm(L, LUA_PERM_WALLET, "wallet");
    sol_pubkey_t want;
    sol_keypair_t kp;
    check_key(L, 1, &want);
    bool same = wallet_mint_keypair(&kp) && !memcmp(kp.pub.b, want.b, 32);
    memset(&kp, 0, sizeof(kp));
    lua_pushboolean(L, same && wallet_mint_forget());
    return 1;
}

// chain.cluster() -> "devnet" | "mainnet" | "testnet" | "custom"
static int l_cluster(lua_State *L)
{
    lh_need_perm(L, LUA_PERM_CHAIN, "chain");
    lua_pushstring(L, wallet_cluster());
    return 1;
}

void lh_open_sol(lua_State *L, const luaL_Reg *wallet_extra, const luaL_Reg *chain_extra)
{
    lh_sol_stop();
    static const luaL_Reg sol[] = {
        {"encode", l_encode}, {"decode", l_decode}, {"pda", l_pda}, {"ata", l_ata},
        {"u64", l_u64}, {"message", l_message}, {NULL, NULL},
    };
    static const luaL_Reg wallet[] = {{"address", l_address}, {"keypair", l_keypair},
                                         {"mint", l_mint}, {"mint_used", l_mint_used}, {NULL, NULL}};
    static const luaL_Reg chain[] = {{"cluster", l_cluster}, {NULL, NULL}};
    luaL_newlib(L, sol);
    lua_setglobal(L, "sol");
    luaL_newlib(L, wallet);
    luaL_setfuncs(L, wallet_extra, 0);
    lua_setglobal(L, "wallet");
    luaL_newlib(L, chain);
    luaL_setfuncs(L, chain_extra, 0);
    lua_setglobal(L, "chain");
}
