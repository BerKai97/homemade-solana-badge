-- pump.fun for a SOL coin (Token-2022, not mayhem), from pump's public IDL.
-- create_v2: 16 accounts; data = discriminator, name, symbol, uri (u32-length
-- strings), creator (32 bytes), is_mayhem_mode (false). The optional trailing
-- arguments are left off: no cashback, the standard SOL fee schedule, creator
-- fees to the creator.
-- The creator's dev buy: its Token-2022 account (made if missing), then
-- buy_exact_quote_in_v3, which spends an exact amount of SOL, fees included.
-- lib/selftest.lua checks them against golden vectors.
local M = {}

M.PROGRAM        = "6EF8rrecthR5Dkzon8Nwu78hRvfCKubJ14M5uBEwF6P"
M.GLOBAL         = "4wTV1YmiEkRvAtNtsSGPtUrqRYQMe5SKy2uB4Jjaxnjf"
M.EVENT_AUTH     = "Ce6TQqeHC9p8KetsN6JsjHK7UTZk7nasjjnr7XxXp9F1"
M.MINT_AUTH      = "TSLvdd1pWpHVjahSpsvCXUbgwsL3JAcvokwaKt1eokM"
M.FEE_CONFIG     = "8Wf5TiAheLUqBrKXeYg2JtAFFMWtKdG2BSFgqUcPVwTt"
M.BUYBACK        = "5YxQFdt3Tr9zJLvkFccqXVUwhdTWJQc1fFg2YPbxvxeD"   -- the first of pump's 8 buyback fee recipients
M.MAYHEM         = "MAyhSmzXzV1pTf7LsNkrNwkWKTo4ougAJ1PPg47MD4e"
M.MAYHEM_PARAMS  = "13ec7XdrjF3h3YcqBTFDSReRcUFwbCnJaAQspM4j6DDJ"
M.MAYHEM_VAULT   = "BwWK17cbHxwWBKZkUYvzxLcNQ1YVyaFezduWbtm2de6s"
M.SYSTEM         = "11111111111111111111111111111111"
M.TOKEN          = "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA"
M.TOKEN_2022     = "TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb"
M.ATA            = "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL"
M.WSOL           = "So11111111111111111111111111111111111111112"

M.NAME_MAX, M.SYMBOL_MAX, M.URI_MAX = 32, 13, 200
M.SLIPPAGE_BPS = 150
M.BUY_MAX = 85                  -- SOL: about 86 buys the whole curve
local CREATE_V2 = string.char(214, 144, 76, 236, 95, 139, 49, 180)
local BUY_EXACT_QUOTE_IN_V3 = string.char(225, 247, 80, 30, 213, 179, 132, 136)

-- A new SOL curve: 30 SOL against 1,073,000,000 tokens, as pump's Global opens
-- every create_v2 SOL coin; a buy pays 0.95% protocol and 0.3% creator fee on it.
local CURVE_SOL, CURVE_TOKENS, FEE_BPS = 30, 1073000000, 125

-- nil when the metadata fits pump's limits (bytes, not characters), else why not
function M.check(name, symbol, uri)
  if #name < 1 or #name > M.NAME_MAX then return "name: 1 to 32 bytes" end
  if #symbol < 1 or #symbol > M.SYMBOL_MAX then return "symbol: 1 to 13 bytes" end
  if #uri < 1 or #uri > M.URI_MAX then return "uri: 1 to 200 bytes" end
  return nil
end

function M.curve(mint)
  return (sol.pda({"bonding-curve", sol.decode(mint)}, M.PROGRAM))
end

-- The create_v2 instruction: user pays and signs, creator earns the creator fees.
function M.create(mint, user, creator, name, symbol, uri)
  local curve = M.curve(mint)
  local state = sol.pda({"mayhem-state", sol.decode(mint)}, M.MAYHEM)
  return {
    program = M.PROGRAM,
    accounts = {
      {mint, true, true},
      {M.MINT_AUTH, false, false},
      {curve, false, true},
      {sol.ata(curve, mint, M.TOKEN_2022), false, true},
      {M.GLOBAL, false, false},
      {user, true, true},
      {M.SYSTEM, false, false},
      {M.TOKEN_2022, false, false},
      {M.ATA, false, false},
      {M.MAYHEM, false, true},
      {M.MAYHEM_PARAMS, false, false},
      {M.MAYHEM_VAULT, false, true},
      {state, false, true},
      {sol.ata(M.MAYHEM_VAULT, mint, M.TOKEN_2022), false, true},
      {M.EVENT_AUTH, false, false},
      {M.PROGRAM, false, false},
    },
    data = CREATE_V2 .. string.pack("<s4s4s4", name, symbol, uri) .. sol.decode(creator) .. "\0",   -- not mayhem
  }
end

-- The user's Token-2022 account for the coin, made if missing (CreateIdempotent).
function M.token_account(mint, user)
  return {
    program = M.ATA,
    accounts = {
      {user, true, true},
      {sol.ata(user, mint, M.TOKEN_2022), false, true},
      {user, false, false},
      {mint, false, false},
      {M.SYSTEM, false, false},
      {M.TOKEN_2022, false, false},
    },
    data = "\1",
  }
end

-- buy_exact_quote_in_v3: spend exactly `lamports`, fees included, for at least
-- `min_tokens` (integers or decimal strings). The SOL side's token accounts
-- are named but never opened.
function M.buy(mint, user, lamports, min_tokens)
  local curve = M.curve(mint)
  return {
    program = M.PROGRAM,
    accounts = {
      {M.GLOBAL, false, false},
      {mint, false, false},
      {M.WSOL, false, false},
      {M.TOKEN_2022, false, false},
      {M.TOKEN, false, false},
      {curve, false, true},
      {sol.ata(curve, mint, M.TOKEN_2022), false, true},
      {sol.ata(curve, M.WSOL, M.TOKEN), false, true},
      {user, true, true},
      {sol.ata(user, mint, M.TOKEN_2022), false, true},
      {sol.ata(user, M.WSOL, M.TOKEN), false, true},
      {sol.pda({"user_volume_accumulator", sol.decode(user)}, M.PROGRAM), false, true},
      {M.FEE_CONFIG, false, false},
      {M.BUYBACK, false, true},
      {M.SYSTEM, false, false},
      {M.EVENT_AUTH, false, false},
      {M.PROGRAM, false, false},
    },
    data = BUY_EXACT_QUOTE_IN_V3 .. sol.u64(lamports) .. sol.u64(min_tokens) .. "\0",   -- partial_fill: false
  }
end

-- min_tokens for spending `amount` SOL on a new curve: what it buys less the
-- slippage, in whole tokens (the badge's floats are 32-bit), as raw units.
function M.min_tokens(amount)
  local net = amount * 10000 / (10000 + FEE_BPS)
  local tokens = CURVE_TOKENS * net / (CURVE_SOL + net)
  return math.floor(tokens * (10000 - M.SLIPPAGE_BPS) / 10000) .. "000000"
end

return M
