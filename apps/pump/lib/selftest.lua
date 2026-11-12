-- The launch's instructions and v1 message against golden vectors built from
-- made-up keys and metadata. Returns nil when all match, else what differs.
-- Checks sol.pda, sol.ata and sol.message in the firmware too.
local pump = require("lib.pump")
local jito = require("lib.jito")

local MINT = "Mint111111111111111111111111111111111111111"
local USER = "User111111111111111111111111111111111111111"
local HASH = "Hash111111111111111111111111111111111111111"
local TIP_ACCOUNT = "96gYZGLnJYVFmbjzopPSU6QiEV5fGqZNyN9nmNhvrZU5"
local CURVE, CURVE_ATA = "BxCmKo26PUdTt4BiRxBnmuF3cVM6MjQ5rjcGFMk7ugyi", "5AyrdAGAtWzSir8KUE3cWCJgqgYjAwsC7eVphqiZyw5S"
local USER_ATA = "Dxhzmzsp4UBM21iG2b2BMyb9FhxaRnMFxgDB6wLjjYbC"

-- flags, one per account: s signer and writable, w writable, r read-only
local CREATE = {
  MINT, "TSLvdd1pWpHVjahSpsvCXUbgwsL3JAcvokwaKt1eokM", CURVE, CURVE_ATA,
  "4wTV1YmiEkRvAtNtsSGPtUrqRYQMe5SKy2uB4Jjaxnjf", USER, "11111111111111111111111111111111",
  "TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb", "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL",
  "MAyhSmzXzV1pTf7LsNkrNwkWKTo4ougAJ1PPg47MD4e", "13ec7XdrjF3h3YcqBTFDSReRcUFwbCnJaAQspM4j6DDJ",
  "BwWK17cbHxwWBKZkUYvzxLcNQ1YVyaFezduWbtm2de6s", "5pm41Y8Wr1Dy1Rwavnt7zoif9s8s3tk5eiQj9swp5qBa",
  "7aag3QgfoUqWqMoh2Ey1rcMUgQYpWMuZVpTxivSYa46f", "Ce6TQqeHC9p8KetsN6JsjHK7UTZk7nasjjnr7XxXp9F1",
  "6EF8rrecthR5Dkzon8Nwu78hRvfCKubJ14M5uBEwF6P", flags = "srwwrsrrrwrwwwrr",
}
local CREATE_DATA = "d6904cec5f8b31b4090000005465737420436f696e04000000544553541d00000068747470733a2f2f6578616d"
  .. "706c652e636f6d2f636f696e2e6a736f6e0723f46e2a91b496eea74a09030fc6c7e1cf7f6dd31c44936ab6c7800000000000"

local TOKEN_ACCOUNT = {
  USER, USER_ATA, USER, MINT, "11111111111111111111111111111111", "TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb",
  flags = "swrrrr",
}

local BUY = {
  "4wTV1YmiEkRvAtNtsSGPtUrqRYQMe5SKy2uB4Jjaxnjf", MINT, "So11111111111111111111111111111111111111112",
  "TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb", "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA",
  CURVE, CURVE_ATA, "GM9FHkcSQ8hjutBk2yF6TiY6ZHwZgXNYMdXAuxcymM4j", USER, USER_ATA,
  "E864NX5rY5onKkt47R5syAZ9qjgLtRA2nWEwhZzgxHkg", "256Uh3SKAxb3F57qWbNRZBDzb5iNUtZzAqF52LzLsfEZ",
  "8Wf5TiAheLUqBrKXeYg2JtAFFMWtKdG2BSFgqUcPVwTt", "5YxQFdt3Tr9zJLvkFccqXVUwhdTWJQc1fFg2YPbxvxeD",
  "11111111111111111111111111111111", "Ce6TQqeHC9p8KetsN6JsjHK7UTZk7nasjjnr7XxXp9F1",
  "6EF8rrecthR5Dkzon8Nwu78hRvfCKubJ14M5uBEwF6P", flags = "rrrrrwwwswwwrwrrr",
}
local BUY_DATA = "e1f7501ed5b384880094357700000000010000000000000000"
local FILL = 66285714223523               -- raw tokens 2 SOL buys on a new curve, fees taken

local TIP = {USER, TIP_ACCOUNT, jito.DONT_FRONT, flags = "swr"}

-- token account and tip, cu_limit 203000, priority_fee 1000
local V1 = "810100050f000000043f98278b4c15555e82c599b0d9a77db92f99a8e8bb482944d810000000000002080723f46e2a91b4"
  .. "96eea74a09030fc6c7e1cf7f6dd31c44936ab6c7800000000078521cb179cebb8589b556a2d5ec94d2498682fdf9bb2af5ad64e4"
  .. "91cc4153dac0918ac69977f7734f9b861cd140e259558eef061f8e5fff02f8bbb08eaaa52b00000000000000000000000000000000"
  .. "00000000000000000000000000000000054ede93b606d00687164f93c9834593e5a1386c778b2ec3f278de800000000006ddf6e1"
  .. "ee758fde18425dbce46ccddab61afc4d83b90d27febdf928d8a18bfc0af1c3432188ca3a635135a13a18951acebd29e6ac2dae67"
  .. "ffdb06d7400000008c97258f4e2489f1bb3d1029148e0d830b5a1399daff1084048e7bd8dbe9f859e803000000000000f8180300"
  .. "000000040706010003030c000002000403050100010602000000e803000000000000"

local function hex(s)
  return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

-- nil when ix names `want` in order, with its flags, and its data is `data` (hex), else what differs
local function differs(what, ix, want, data)
  if #ix.accounts ~= #want then return what .. ": " .. #ix.accounts .. " accounts" end
  for i, a in ipairs(want) do
    local got = ix.accounts[i]
    local flag = got[2] and "s" or got[3] and "w" or "r"
    if got[1] ~= a or flag ~= want.flags:sub(i, i) then
      return what .. " account " .. i .. " is " .. got[1] .. " (" .. flag .. ")"
    end
  end
  if hex(ix.data) ~= data then return what .. " data differs" end
  return nil
end

return function()
  local token_account, tip = pump.token_account(MINT, USER), jito.tip(USER, 1000, TIP_ACCOUNT)
  local create = pump.create(MINT, USER, USER, "Test Coin", "TEST", "https://example.com/coin.json")
  local why = differs("create", create, CREATE, CREATE_DATA)
    or differs("token account", token_account, TOKEN_ACCOUNT, "01")
    or differs("buy", pump.buy(MINT, USER, 2000000000, 1), BUY, BUY_DATA)
    or differs("tip", tip, TIP, "02000000e803000000000000")
  if why then return why end

  local share = tonumber(pump.min_tokens(2)) / FILL
  if share < 0.9849 or share > 0.9851 then return "min_tokens is " .. share .. " of the fill" end

  local msg = sol.message{
    payer = USER,
    blockhash = HASH,
    instructions = {token_account, tip},
    budget = {cu_limit = 203000, priority_fee = 1000},
  }
  if hex(msg) ~= V1 then return "v1 message differs (" .. #msg .. " bytes)" end
  return nil
end
