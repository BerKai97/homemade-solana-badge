// The badge's Solana wallet and RPC setting (wallet.h), and their console:
//   wallet                      address, RPC
//   wallet new [force]          make a keypair here (force: replace the one there)
//   wallet import <secret>      base58 of a 64-byte secret key (Phantom/Solflare export) or a 32-byte seed
//   wallet export yes           print the secret key (base58, 64 bytes) to back it up
//   wallet forget yes           erase it
//   wallet rpc [url|devnet|mainnet]
//   wallet ipfs [key <token> | url <url> | off]   Filebase: Buckets > IPFS RPC API key
//   wallet mint [import <secret> | off]           a mint keypair for the next launch
//                               (tools/badge_mint.py sends a solana-keygen grind file)

#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "esp_log.h"
#include "core/os_cmd.h"
#include "wallet/wallet.h"

static const char *TAG = "wallet";
static const char *NS = "wallet";

// A 32-byte seed under key: the wallet's ("seed") or the imported mint's ("mint").
static bool load_blob(const char *key, uint8_t seed[32])
{
    nvs_handle_t h;
    size_t len = 32;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_get_blob(h, key, seed, &len) == ESP_OK && len == 32;
    nvs_close(h);
    return ok;
}

// NULL erases (erasing what isn't there is fine).
static bool save_blob(const char *key, const uint8_t *seed)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t e = seed ? nvs_set_blob(h, key, seed, 32) : nvs_erase_key(h, key);
    bool ok = (e == ESP_OK || (!seed && e == ESP_ERR_NVS_NOT_FOUND)) && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

static bool load_seed(uint8_t seed[32]) { return load_blob("seed", seed); }
static bool save_seed(const uint8_t *seed) { return save_blob("seed", seed); }

bool wallet_keypair(sol_keypair_t *out)
{
    uint8_t seed[32];
    if (!load_seed(seed)) {
        return false;
    }
    sol_keypair_from_seed(seed, out);
    memset(seed, 0, sizeof(seed));
    return true;
}

bool wallet_mint_keypair(sol_keypair_t *out)
{
    uint8_t seed[32];
    if (!load_blob("mint", seed)) {
        return false;
    }
    sol_keypair_from_seed(seed, out);
    memset(seed, 0, sizeof(seed));
    return true;
}

bool wallet_mint_forget(void)
{
    return save_blob("mint", NULL);
}

// The address, derived once and kept (deriving costs a scalar multiplication);
// a new or erased key drops it.
static sol_pubkey_t s_pub;
static bool s_known;

static void forget_cache(void)
{
    s_known = false;
}

bool wallet_address(sol_pubkey_t *out)
{
    sol_keypair_t kp;
    if (!s_known) {
        if (!wallet_keypair(&kp)) {
            return false;
        }
        s_pub = kp.pub;
        s_known = true;
        memset(&kp, 0, sizeof(kp));
    }
    *out = s_pub;
    return true;
}

void wallet_rpc_url(char *out, size_t n)
{
    nvs_handle_t h;
    size_t len = n;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        bool ok = nvs_get_str(h, "rpc", out, &len) == ESP_OK && out[0];
        nvs_close(h);
        if (ok) {
            return;
        }
    }
    strlcpy(out, WALLET_RPC_DEFAULT, n);
}

static bool get_str(const char *key, char *out, size_t n)
{
    nvs_handle_t h;
    size_t len = n;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_get_str(h, key, out, &len) == ESP_OK && out[0];
    nvs_close(h);
    return ok;
}

static bool set_str(const char *key, const char *v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = (v ? nvs_set_str(h, key, v) : nvs_erase_key(h, key)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

bool wallet_ipfs_key(char *out, size_t n)
{
    if (!get_str("ipfs_key", out, n)) {
        out[0] = 0;
        return false;
    }
    return true;
}

void wallet_ipfs_url(char *out, size_t n)
{
    if (!get_str("ipfs_url", out, n)) {
        strlcpy(out, WALLET_IPFS_DEFAULT, n);
    }
}

const char *wallet_cluster(void)
{
    char url[WALLET_RPC_MAX + 1];
    wallet_rpc_url(url, sizeof(url));
    return strstr(url, "devnet") ? "devnet" : strstr(url, "testnet") ? "testnet"
         : strstr(url, "mainnet") || strstr(url, "helius-rpc.com") ? "mainnet" : "custom";
}

// ---- console ----

// base58 of a 64-byte secret key (seed || public key, checked) or a 32-byte seed.
static bool parse_secret(const char *b58, uint8_t seed[32])
{
    uint8_t secret[64];
    bool ok = false;
    if (sol_b58_decode(b58, secret, 64)) {
        sol_keypair_t kp;
        sol_keypair_from_seed(secret, &kp);
        ok = !memcmp(kp.pub.b, secret + 32, 32);
        if (!ok) {
            printf("that secret key's second half isn't its public key: not a Solana keypair\n");
        }
        memset(&kp, 0, sizeof(kp));
    } else if (sol_b58_decode(b58, secret, 32)) {
        ok = true;
    } else {
        printf("expected base58 of a 64-byte secret key or a 32-byte seed\n");
    }
    if (ok) {
        memcpy(seed, secret, 32);
    }
    memset(secret, 0, sizeof(secret));
    return ok;
}

static void print_mint(void)
{
    sol_keypair_t kp;
    char a[SOL_B58_PUBKEY];
    if (wallet_mint_keypair(&kp)) {
        sol_pubkey_str(&kp.pub, a);
        printf("mint: %s (imported, for the next launch)\n", a);
    } else {
        printf("mint: a new random one each launch (wallet mint import <secret> for your own)\n");
    }
    memset(&kp, 0, sizeof(kp));
}

static bool set_seed(const uint8_t seed[32])
{
    if (!save_seed(seed)) {
        printf("could not write NVS\n");
        return false;
    }
    forget_cache();
    sol_pubkey_t pub;
    char a[SOL_B58_PUBKEY];
    wallet_address(&pub);
    sol_pubkey_str(&pub, a);
    printf("wallet: %s\n", a);
    return true;
}

static int cmd_wallet(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    sol_pubkey_t pub;
    char a[SOL_B58_PUBKEY], url[WALLET_RPC_MAX + 1];
    uint8_t seed[32];

    if (!sub[0]) {
        wallet_rpc_url(url, sizeof(url));
        if (wallet_address(&pub)) {
            sol_pubkey_str(&pub, a);
            printf("wallet: %s\n", a);
        } else {
            printf("wallet: none (wallet new, or wallet import <secret>)\n");
        }
        printf("rpc:    %s (%s)\n", url, wallet_cluster());
        print_mint();
        return 0;
    }
    if (!strcmp(sub, "new")) {
        if (load_seed(seed) && (argc < 3 || strcmp(argv[2], "force"))) {
            memset(seed, 0, sizeof(seed));
            printf("a wallet is already here; 'wallet new force' replaces it (export it first!)\n");
            return 1;
        }
        sol_keypair_t kp;
        sol_keypair_random(&kp);
        bool ok = set_seed(kp.secret);
        memset(&kp, 0, sizeof(kp));
        return ok ? 0 : 1;
    }
    if (!strcmp(sub, "import") && argc == 3) {
        bool ok = parse_secret(argv[2], seed) && set_seed(seed);
        memset(seed, 0, sizeof(seed));
        return ok ? 0 : 1;
    }
    if (!strcmp(sub, "mint")) {
        const char *what = argc > 2 ? argv[2] : "";
        bool ok = true;
        if (!strcmp(what, "import") && argc == 4) {
            if (!parse_secret(argv[3], seed)) {
                return 1;
            }
            ok = save_blob("mint", seed);
            memset(seed, 0, sizeof(seed));
        } else if (!strcmp(what, "off")) {
            ok = wallet_mint_forget();
        } else if (what[0]) {
            printf("wallet mint [import <secret> | off]\n");
            return 1;
        }
        if (!ok) {
            printf("could not write NVS\n");
            return 1;
        }
        print_mint();
        return 0;
    }
    if (!strcmp(sub, "export") && argc == 3 && !strcmp(argv[2], "yes")) {
        sol_keypair_t kp;
        char b58[96];
        if (!wallet_keypair(&kp)) {
            printf("no wallet\n");
            return 1;
        }
        sol_b58_encode(kp.secret, 64, b58, sizeof(b58));
        printf("secret key (anyone with it owns the wallet):\n%s\n", b58);
        memset(&kp, 0, sizeof(kp));
        memset(b58, 0, sizeof(b58));
        return 0;
    }
    if (!strcmp(sub, "forget") && argc == 3 && !strcmp(argv[2], "yes")) {
        bool ok = save_seed(NULL);
        forget_cache();
        printf(ok ? "wallet erased\n" : "could not write NVS\n");
        return ok ? 0 : 1;
    }
    if (!strcmp(sub, "rpc")) {
        if (argc == 3) {
            const char *u = !strcmp(argv[2], "devnet") ? "https://api.devnet.solana.com"
                          : !strcmp(argv[2], "mainnet") ? "https://api.mainnet-beta.solana.com" : argv[2];
            if (strncmp(u, "https://", 8) || strlen(u) > WALLET_RPC_MAX) {
                printf("an https:// URL up to %d characters, or devnet / mainnet\n", WALLET_RPC_MAX);
                return 1;
            }
            nvs_handle_t h;
            if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK || nvs_set_str(h, "rpc", u) != ESP_OK ||
                    nvs_commit(h) != ESP_OK) {
                printf("could not write NVS\n");
                return 1;
            }
            nvs_close(h);
        }
        wallet_rpc_url(url, sizeof(url));
        printf("rpc: %s (%s)\n", url, wallet_cluster());
        return 0;
    }
    if (!strcmp(sub, "ipfs")) {
        const char *what = argc > 2 ? argv[2] : "";
        bool ok = true;
        if (!strcmp(what, "key") && argc == 4 && strlen(argv[3]) <= WALLET_IPFS_KEY_MAX) {
            ok = set_str("ipfs_key", argv[3]);
        } else if (!strcmp(what, "url") && argc == 4) {
            if (strncmp(argv[3], "https://", 8) || strlen(argv[3]) > WALLET_RPC_MAX) {
                printf("an https:// URL up to %d characters\n", WALLET_RPC_MAX);
                return 1;
            }
            ok = set_str("ipfs_url", argv[3]);
        } else if (!strcmp(what, "off")) {
            ok = set_str("ipfs_key", NULL);
            set_str("ipfs_url", NULL);
        } else if (what[0]) {
            printf("wallet ipfs [key <token> | url <url> | off]\n");
            return 1;
        }
        if (!ok) {
            printf("could not write NVS\n");
            return 1;
        }
        char key[WALLET_IPFS_KEY_MAX + 1];
        bool has = wallet_ipfs_key(key, sizeof(key));
        memset(key, 0, sizeof(key));
        wallet_ipfs_url(url, sizeof(url));
        printf("ipfs: %s, key %s\n", url, has ? "set" : "none (wallet ipfs key <token>)");
        return 0;
    }
    printf("wallet [new [force] | import <secret> | export yes | forget yes | rpc [url|devnet|mainnet] | "
           "ipfs [key <token>|url <url>|off] | mint [import <secret>|off]]\n");
    return 1;
}

void wallet_init(void)
{
    os_cmd_register("wallet", "wallet [new [force]|import <secret>|export yes|forget yes|rpc [url|devnet|mainnet]|ipfs ...|mint ...]",
                    cmd_wallet);
    ESP_LOGI(TAG, "ready");
}
