-- Jito's public block engine: a transaction sent as a bundle of one pays its
-- tip in its last instruction, to one of Jito's 8 tip accounts at random. The
-- read-only jitodontfront account on it asks the block engine to let no bundle
-- run ahead of it.
local M = {}

M.BUNDLES = "https://frankfurt.mainnet.block-engine.jito.wtf/api/v1/bundles"
M.DONT_FRONT = "jitodontfront111111111111111111111111111111"
local SYSTEM = "11111111111111111111111111111111"
local TIP_ACCOUNTS = {
  "DfXygSm4jCyNCybVYYK6DwvWqjKee8pbDmJGcLWNDXjh", "96gYZGLnJYVFmbjzopPSU6QiEV5fGqZNyN9nmNhvrZU5",
  "DttWaMuVvTiduZRnguLF7jNxTgiMBZ1hyAumKUiL2KRL", "HFqU5x63VTqvQss8hp11i4wVV8bD44PvwucfZ2bU7gRe",
  "Cw8CFyM9FkoMi7K7Crf6HNQqf4uEMzpKw6QNghXLvLkY", "ADaUMid9yfUytqMBgopwjb2DTLSokTSzL1zt6iGPaS49",
  "ADuUkR4vqLUMWXxW9gh6D6L8pMSawimctcNZ5pGwDcEt", "3AVi9Tg9Uo68tJfuvoKvqKNWKkC5wPdSSdeBnizKZ6jT",
}

-- A system transfer of `lamports` from payer to a tip account, marked.
function M.tip(payer, lamports, account)
  return {
    program = SYSTEM,
    accounts = {
      {payer, true, true},
      {account or TIP_ACCOUNTS[math.random(#TIP_ACCOUNTS)], false, true},
      {M.DONT_FRONT, false, false},
    },
    data = string.pack("<I4", 2) .. sol.u64(lamports),
  }
end

return M
