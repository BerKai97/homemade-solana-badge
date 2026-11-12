// Host test of main/sol (sol.c) through a C reference of pump's create_v2 (pump_ref.c): tests/run.sh.
// Ed25519 against RFC 8032; create_v2 and a v1 message against golden vectors built from made-up keys
// and metadata.

#include <stdio.h>
#include <string.h>
#include "pump_ref.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void hex(const char *h, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) sscanf(h + 2 * i, "%2hhx", &out[i]);
}

int main(void)
{
    // RFC 8032 7.1 test 1
    uint8_t seed[32], pk[32], sig[64], want[64];
    hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed, 32);
    hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pk, 32);
    hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", want, 64);
    sol_keypair_t kp;
    sol_keypair_from_seed(seed, &kp);
    CHECK(!memcmp(kp.pub.b, pk, 32), "RFC 8032 public key");
    sol_sign(&kp, (const uint8_t *)"", 0, sig);
    CHECK(!memcmp(sig, want, 64), "RFC 8032 signature");

    // base58 round trip
    char s[SOL_B58_PUBKEY];
    sol_pubkey_str(&PUMP_PROGRAM, s);
    CHECK(!strcmp(s, "6EF8rrecthR5Dkzon8Nwu78hRvfCKubJ14M5uBEwF6P"), "base58 encode: %s", s);
    sol_pubkey_str(&SOL_SYSTEM_PROGRAM, s);
    CHECK(!strcmp(s, "11111111111111111111111111111111"), "base58 zeros: %s", s);

    // create_v2: accounts, flags and data
    static const struct { const char *key; int signer, writable; } accounts[] = {
        {"Mint111111111111111111111111111111111111111", 1, 1},
        {"TSLvdd1pWpHVjahSpsvCXUbgwsL3JAcvokwaKt1eokM", 0, 0},
        {"BxCmKo26PUdTt4BiRxBnmuF3cVM6MjQ5rjcGFMk7ugyi", 0, 1},
        {"5AyrdAGAtWzSir8KUE3cWCJgqgYjAwsC7eVphqiZyw5S", 0, 1},
        {"4wTV1YmiEkRvAtNtsSGPtUrqRYQMe5SKy2uB4Jjaxnjf", 0, 0},
        {"User111111111111111111111111111111111111111", 1, 1},
        {"11111111111111111111111111111111", 0, 0},
        {"TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb", 0, 0},
        {"ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL", 0, 0},
        {"MAyhSmzXzV1pTf7LsNkrNwkWKTo4ougAJ1PPg47MD4e", 0, 1},
        {"13ec7XdrjF3h3YcqBTFDSReRcUFwbCnJaAQspM4j6DDJ", 0, 0},
        {"BwWK17cbHxwWBKZkUYvzxLcNQ1YVyaFezduWbtm2de6s", 0, 1},
        {"5pm41Y8Wr1Dy1Rwavnt7zoif9s8s3tk5eiQj9swp5qBa", 0, 1},
        {"7aag3QgfoUqWqMoh2Ey1rcMUgQYpWMuZVpTxivSYa46f", 0, 1},
        {"Ce6TQqeHC9p8KetsN6JsjHK7UTZk7nasjjnr7XxXp9F1", 0, 0},
        {"6EF8rrecthR5Dkzon8Nwu78hRvfCKubJ14M5uBEwF6P", 0, 0},
    };
    static const char DATA[] =
        "d6904cec5f8b31b4090000005465737420436f696e04000000544553541d00000068747470733a2f2f6578616d706c652e"
        "636f6d2f636f696e2e6a736f6e030986ad25ee71a3ada66c6ed3548f4df20d11446905b9af1e93af10000000000000";
    uint8_t data[sizeof(DATA) / 2];
    hex(DATA, data, sizeof(data));
    sol_pubkey_t mint, user, creator;
    sol_pubkey_parse(accounts[0].key, &mint);
    sol_pubkey_parse(accounts[5].key, &user);
    sol_pubkey_parse("Creator111111111111111111111111111111111111", &creator);
    static sol_tx_t tx;
    tx.payer = user;
    CHECK(pump_create_v2(&tx, &mint, &user, &creator, "Test Coin", "TEST", "https://example.com/coin.json"),
          "create_v2 built");
    sol_ix_t *ix = &tx.ix[0];
    CHECK(ix->n_metas == 16, "16 accounts, got %d", ix->n_metas);
    for (int i = 0; i < 16 && i < ix->n_metas; i++) {
        sol_pubkey_str(&ix->metas[i].key, s);
        CHECK(!strcmp(s, accounts[i].key) && ix->metas[i].signer == accounts[i].signer &&
              ix->metas[i].writable == accounts[i].writable, "account %d: %s (want %s)", i, s, accounts[i].key);
    }
    CHECK(ix->n_data == sizeof(data) && !memcmp(ix->data, data, sizeof(data)), "data bytes (%zu vs %zu)",
          ix->n_data, sizeof(data));

    // a whole signed transaction fits and verifies its own layout
    sol_keypair_t payer, mkp;
    sol_keypair_random(&payer); sol_keypair_random(&mkp);
    static sol_tx_t t2; static uint8_t wire[SOL_TX_MAX]; uint8_t bh[32] = {1}, sig0[64];
    t2.payer = payer.pub;
    sol_tx_cu_limit(&t2, PUMP_CREATE_UNITS);
    sol_tx_cu_price(&t2, 100000);
    char maxname[33], maxuri[201];
    memset(maxname, 'N', 32); maxname[32] = 0; memset(maxuri, 'u', 200); maxuri[200] = 0;
    CHECK(pump_create_v2(&t2, &mkp.pub, &payer.pub, &payer.pub, maxname, "SYMBOLSYMBOL1", maxuri), "max create");
    const sol_keypair_t *kps[] = {&mkp, &payer};
    size_t n = sol_tx_sign(&t2, bh, kps, 2, wire, sizeof(wire), sig0);
    CHECK(n > 0, "longest create fits in a packet");
    printf("longest create: %zu bytes of %d, signers %d\n", n, SOL_TX_MAX, wire[0]);
    // 2 signatures, header {2 signers, 0 readonly signers, ...}, then the payer as the first key
    CHECK(wire[0] == 2 && wire[129] == 2 && wire[130] == 0, "header");
    CHECK(!memcmp(wire + 1 + 128 + 3 + 1, payer.pub.b, 32), "payer is the first key");

    // v1: that create_v2 with its whole budget
    static const char V1[] =
        "810200080f000000043f98278b4c15555e82c599b0d9a77db92f99a8e8bb482944d810000000000001100723f46e2a91b496"
        "eea74a09030fc6c7e1cf7f6dd31c44936ab6c78000000000054ede93b606d00687164f93c9834593e5a1386c778b2ec3f278"
        "de8000000000052ae5d7a7daa724a6eab0a7295491855ad4a0671660674c4e034559803d65a33dfd4304d540f73817cec6a7"
        "445af8e108064280ac649a464adacb48f74a02f947aadaf895e817584f2bb9b9a271d7e02d79585c38e8530442d1a60ddd51"
        "8ff561c05cd27bff270041a80ccf9f17d44aa71c13cd3b85c10e5f078722e3ea4a2ca28b5fd26ab479a6a9cc6cbf6b0b23eb"
        "61885a371e0120aca913beef3d138a78a2b91d979622dcea15093522b08ac6fb77d3a066f5ac7f050f10cb5a581a0ab50000"
        "00000000000000000000000000000000000000000000000000000000000000adaea27db3c6f3d04e5cf0a2029f03e3398f08"
        "5395efefef171fc64af817410156e0f693665acf44db1568bf175baa5189cb97f5d2ff3b655d2bb6fd6d18b006c5c1ce638d"
        "2567d26468b05eb951d1a28dcc6e123482b5c675149770e62bf206ddf6e1ee758fde18425dbce46ccddab61afc4d83b90d27"
        "febdf928d8a18bfc3a865e69ee0f5480cabcf66357e4dc2f18d58d45c1ea7489fb3723d9793c72a68c97258f4e2489f1bb3d"
        "1029148e0d830b5a1399daff1084048e7bd8dbe9f859acf136eb01fc1c4e883d23c8b5844ab59a37f66add57c5e9ac3b53e0"
        "59d35c6480841e000000000090d00300000000040a106000010b07030d00080c0e02090604050f0ad6904cec5f8b31b40900"
        "00005465737420436f696e04000000544553541d00000068747470733a2f2f6578616d706c652e636f6d2f636f696e2e6a73"
        "6f6e030986ad25ee71a3ada66c6ed3548f4df20d11446905b9af1e93af10000000000000";
    static uint8_t want_v1[sizeof(V1) / 2], built[SOL_TX_V1_MAX], signed_v1[SOL_TX_V1_MAX];
    hex(V1, want_v1, sizeof(want_v1));
    uint8_t hash[32];
    sol_b58_decode("Hash111111111111111111111111111111111111111", hash, 32);
    sol_budget_t budget = {.priority_fee = 2000000, .cu_limit = PUMP_CREATE_UNITS,
                           .loaded_data_limit = SOL_LOADED_DATA_MAX};
    size_t n_built = sol_tx_message_v1(&tx, hash, &budget, built, sizeof(built));
    CHECK(n_built == sizeof(want_v1) && !memcmp(built, want_v1, n_built), "v1 message bytes (%zu vs %zu)", n_built,
          sizeof(want_v1));
    sol_pubkey_t v1_signers[4];
    CHECK(sol_msg_signers(built, n_built, v1_signers, 4) == 2 && !memcmp(v1_signers[0].b, user.b, 32), "v1 signers");

    // a v1 message is signed after it: the message, then the signatures
    static sol_tx_t t4;
    t4.payer = payer.pub;
    CHECK(pump_create_v2(&t4, &mkp.pub, &payer.pub, &payer.pub, maxname, "SYMBOLSYMBOL1", maxuri), "v1 create");
    n_built = sol_tx_message_v1(&t4, bh, &budget, built, sizeof(built));
    n = sol_msg_sign(built, n_built, kps, 2, signed_v1, sizeof(signed_v1), sig0);
    uint8_t want_sig[64];
    sol_sign(&payer, built, n_built, want_sig);
    CHECK(n == n_built + 128 && !memcmp(signed_v1, built, n_built) && !memcmp(signed_v1 + n_built, want_sig, 64) &&
          !memcmp(sig0, want_sig, 64), "v1 wire: message then signatures, payer's first");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
