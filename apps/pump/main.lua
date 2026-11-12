-- Pump Launch: create a pump.fun coin from the badge's own wallet.
-- Needs this firmware's wallet, chain, sol, ipfs and ui.image.
-- The wallet is set up on the console: `wallet new` or `wallet import <secret>`,
-- the RPC with `wallet rpc <url|devnet|mainnet>` (devnet by default), and the
-- IPFS upload key with `wallet ipfs key <Filebase IPFS RPC token>`.
-- The coin's mint is a new random address, or one imported with
-- `wallet mint import <secret>` (tools/badge_mint.py), e.g. one ending in "pump".
-- Pictures: PNG or JPEG files in the app's images/ (tools/badge_put.py --add).
-- On mainnet the launch goes to Jito's block engine as a bundle of one, and to
-- the RPC too when it isn't seen within about 4 s.

local pump = require("lib.pump")
local jito = require("lib.jito")

local TIP = 2000000                   -- 0.002 SOL: Jito's tip on mainnet, the priority fee elsewhere
local POLLS = 30                      -- signature status polls, 2 s apart
local BUNDLE_WAIT = 2                 -- polls before an unseen bundle goes out by RPC too
local MIN_LAMPORTS = 15000000         -- 0.015 SOL: rent for the new accounts (~0.0082) + fees
local BUY_RENT = 4000000              -- 0.004 SOL: the buyer's token account and pump volume account

local GATEWAY = "https://ipfs.filebase.io/ipfs/"   -- what the on-chain URI and image name
local IMAGES = "images"               -- the pictures' folder in the app's files
local LINK_MAX = 100                  -- X, Telegram and website URLs

local FIELDS = {"name", "symbol", "description", "image", "twitter", "telegram", "website", "uri", "buy"}
local defaults = fs.json("token.json") or {}
local token = storage.get("token") or {}
for _, k in ipairs(FIELDS) do token[k] = token[k] or defaults[k] or "" end
local last = storage.get("last")      -- {mint=, sig=, cluster=}
local address = wallet.address()
local imported = wallet.mint()        -- the console's `wallet mint import`, or nil
local mint_kp                         -- this run's random mint, kept until a coin lands
local balance                         -- lamports: a number, or a string past 2^31
local busy = false

-- "5000000000" or 1500000000 -> "5" or "1.5" (Lua's numbers are 32-bit here)
local function sol_str(lamports)
  local s = string.rep("0", 10) .. tostring(lamports)
  local whole = s:sub(1, -10):gsub("^0+", "")
  local frac = s:sub(-9):gsub("0+$", "")
  return (whole == "" and "0" or whole) .. (frac ~= "" and "." .. frac:sub(1, 4) or "")
end

-- "1.5" -> "1500000000" (at most 9 decimals)
local function lamports(sol)
  local whole, frac = sol:match("^(%d*)%.?(%d*)$")
  return ((whole .. frac .. ("0"):rep(9 - #frac)):match("^0*(%d+)$"))
end

-- The dev buy as typed: "" for none, nil when it isn't SOL up to pump.BUY_MAX
local function buy_text(text)
  local frac = text:match("^%d*%.?(%d*)$")
  local n = text == "" and 0 or tonumber(text)
  if not frac or #frac > 9 or not n or n > pump.BUY_MAX then return nil end
  return n > 0 and text or ""
end

-- Lamports a launch needs: the create, and the dev buy with its accounts' rent
local function needed()
  return MIN_LAMPORTS + TIP + (token.buy ~= "" and tonumber(token.buy) * 1e9 + BUY_RENT or 0)
end

local function enough(n)
  return (tonumber(n) or 0) >= needed()
end

local function short(a)
  return a and (a:sub(1, 4) .. "..." .. a:sub(-4)) or "none"
end

local function coin_link(mint, cluster)
  if cluster == "mainnet" then return "https://pump.fun/coin/" .. mint end
  return "https://solscan.io/token/" .. mint .. "?cluster=" .. cluster
end

-- ---- screen: a TFT terminal (lib/tui.lua) ----

local tui = require("lib.tui")
local C = tui.C
local COLOR = {orange = C.ora, green = C.grn, yellow = C.yel, red = C.red}

-- A picture as ui.image, or nil (and why on the status line) when it can't be shown.
local say
local function picture(x, y, size)
  if token.image == "" then return nil end
  local ok, img, why = pcall(ui.image, x, y, token.image, {size = size})
  if ok and img then return img end
  say(token.image .. ": " .. tostring(ok and why or img), "orange")
  return nil
end

ui.bg(C.bg)
local net_bg = ui.rect(240, 1, 76, 14, {color = C.yel})
local net = tui.label(240, 2, "", C.bg, {w = 76, align = "center"})

tui.box(2, 22, 210, 46, "COIN")
local coin_img                        -- ui.image, at most 2 at a time: this and the QR screen's
local c_name = tui.label(10, 29, "")
local c_sym = tui.label(10, 42, "", C.yel)
local c_mint = tui.label(10, 55, "", C.dim)

tui.box(218, 22, 100, 46, "WALLET")
local w_addr = tui.label(226, 29, "", C.dim)
local w_bal = tui.label(226, 42, "", C.yel)
local w_ok = tui.label(226, 55, "")

local pad_title = tui.box(2, 76, 316, 112, "LAUNCH PAD")
local MENU_KEYS = "OK open  BACK exit"
local keys = tui.note(2, 76, 316, MENU_KEYS)
local menu = tui.list(3, 82, 314, 8, 13, 70)
local status = tui.label(4, 192, "", C.dim, {w = 312})
local qr, qr_label, qr_img, picking

local function header()
  if coin_img then coin_img:remove(); coin_img = nil end
  coin_img = picture(10, 29, 36)
  local x = coin_img and 52 or 10
  c_name:pos(x, 29); c_sym:pos(x, 42); c_mint:pos(x, 55)
  c_name:text(tui.fit(token.name ~= "" and token.name or "no name yet", coin_img and 21 or 27))
  c_sym:text(token.symbol ~= "" and ("$" .. token.symbol) or "$???")
  c_mint:text("mint " .. (imported and short(imported) or "random"))
  local main = chain.cluster() == "mainnet"
  net_bg:color(main and C.red or C.yel)
  net:color(main and 0xFFFFFF or C.bg)
  net:text((main and "\u{2022} " or "") .. chain.cluster():upper())
end

local function wallet_box()
  w_addr:text(short(address))
  w_bal:text(balance and (sol_str(balance) .. " SOL") or "-- SOL")
  local ready = balance and enough(balance)
  w_ok:text(not address and "no wallet" or not balance and "..." or ready and "ready" or "fund it")
  w_ok:color(ready and C.grn or C.red)
end

local function set(v) return v ~= "" and v or nil end

local function text_row(id, k) return {id = id, k = k, v = set(token[id]) or "set", c = token[id] == "" and C.dim} end

local function rows()
  local missing = token.image ~= "" and not fs.exists(token.image)
  local r = {
    {id = "wallet", k = "WALLET", v = address or "console: wallet new", c = not address and C.ora},
    text_row("name", "NAME"),
    {id = "symbol", k = "SYMBOL", v = set(token.symbol) and "$" .. token.symbol or "set", c = token.symbol == "" and C.dim},
    {id = "image", k = "IMAGE", v = set(token.image) and (token.image:sub(#IMAGES + 2) .. (missing and "  missing" or ""))
       or "pick a picture", c = (token.image == "" and C.dim) or (missing and C.red)},
    {id = "mint", k = "MINT", v = imported or "new random address", c = imported and C.grn or C.dim},
    {id = "buy", k = "DEV BUY", v = set(token.buy) and token.buy .. " SOL" or "none", c = token.buy == "" and C.dim},
    text_row("description", "DESC"),
    text_row("twitter", "X"),
    text_row("telegram", "TELEGRAM"),
    text_row("website", "WEBSITE"),
    {id = "uri", k = "URI", v = set(token.uri) or "uploaded at launch (ipfs)", c = token.uri == "" and C.dim},
    {id = "launch", k = "LAUNCH", v = "fire on " .. chain.cluster(), hot = true},
  }
  if last then r[#r + 1] = {id = "last", k = "LAST", v = last.mint .. " " .. last.cluster, c = C.grn} end
  r[#r + 1] = {id = "selftest", k = "SELFTEST", v = "check the launch's transaction", c = C.dim}
  return r
end

function say(text, color)
  status:text("> " .. text)
  status:color(COLOR[color] or C.dim)
  print(text)
end

local function menu_title()
  pad_title(#menu.items > 8 and ("LAUNCH PAD " .. menu.sel .. "/" .. #menu.items) or "LAUNCH PAD")
end

local function refresh()
  wallet_box()
  if not picking and not qr_label then
    menu.set(rows())
    menu_title()
  end
end

header()
refresh()

-- A QR code in the pad, with the coin's picture beside it when it's about the coin.
local function show_qr(text, title, caption, with_coin)
  menu.show(false)
  pad_title(title)
  keys("BACK close")
  if qr then qr:text(text) else qr = ui.qr(10, 84, 98, text) end
  qr:show(true)
  if qr_label then qr_label:remove() end
  if with_coin and not qr_img then qr_img = picture(118, 84, 36) end
  qr_label = tui.label(118, qr_img and 126 or 84, caption, C.fg, {w = 194})
end

local function hide_qr()
  if qr then qr:show(false) end
  if qr_label then qr_label:remove(); qr_label = nil end
  if qr_img then qr_img:remove(); qr_img = nil end
  keys(MENU_KEYS)
  menu.show(true)
  refresh()
end

local function load_balance()
  if not address then return end
  chain.rpc("getBalance", {address, {commitment = "confirmed"}}, function(ok, r)
    if ok then balance = r.value; refresh() else say("Balance: " .. tostring(r), "orange") end
  end)
end

-- ---- launch ----

local function done(ok, text)
  busy = false
  led.off()
  say(text, ok and "green" or "orange")
  refresh()
end

-- resend (or nil): called once when the signature is still unseen after BUNDLE_WAIT polls.
local function watch(sig, mint, cluster, tries, resend)
  chain.rpc("getSignatureStatuses", {{sig}}, function(ok, r)
    local st = ok and r.value and r.value[1]
    if st and st.err then
      done(false, "Failed on chain: " .. json.encode(st.err))
    elseif st and (st.confirmationStatus == "confirmed" or st.confirmationStatus == "finalized") then
      last = {mint = mint, sig = sig, cluster = cluster}
      storage.set("last", last)
      -- A mint makes one coin. Devnet tests leave the imported one for mainnet.
      if mint == imported and cluster == "mainnet" then wallet.mint_used(mint); imported = nil end
      if mint == mint_kp then mint_kp = nil end
      done(true, "Launched! " .. mint)
      led.fill(0x14, 0xF1, 0x95)
      load_balance()
      show_qr(coin_link(mint, cluster), "$" .. token.symbol .. " IS LIVE", "on " .. cluster .. "\n\n" .. mint, true)
    elseif tries > 0 then
      if resend and not st and tries <= POLLS - BUNDLE_WAIT then resend(); resend = nil end
      timer.after(2000, function() watch(sig, mint, cluster, tries - 1, resend) end)
    else
      done(false, "Not confirmed after 60 s. Check " .. short(sig) .. " on an explorer")
    end
  end)
end

-- The pump.fun metadata JSON on IPFS (the image first), then then_(); the URI is
-- kept, so a failed launch doesn't upload again. Edit name, symbol, image or
-- description and it's cleared.
local function upload_metadata(then_)
  if not ipfs.ready() then return done(false, "No IPFS key: wallet ipfs key <token> on the console") end
  local function put_json(image_uri)
    local meta = {
      name = token.name, symbol = token.symbol, description = token.description,
      image = image_uri, showName = true, createdOn = "https://pump.fun",
      twitter = token.twitter, telegram = token.telegram, website = token.website,
    }
    say("Uploading metadata...")
    ipfs.add({name = "metadata.json", data = json.encode(meta)}, function(ok, cid)
      if not ok then return done(false, "Metadata upload: " .. tostring(cid)) end
      token.uri = GATEWAY .. cid
      storage.set("token", token)
      refresh()
      then_()
    end)
  end
  if not fs.exists(token.image) then return done(false, token.image .. " is gone") end
  say("Uploading " .. token.image .. "...")
  ipfs.add(token.image, function(ok, cid)
    if not ok then return done(false, "Image upload: " .. tostring(cid)) end
    put_json(GATEWAY .. cid)
  end)
end

local function send_rpc(tx, then_)
  chain.rpc("sendTransaction", {tx, {encoding = "base64", preflightCommitment = "confirmed", maxRetries = 5}}, then_)
end

-- The compute limit the runtime gives instructions that set none: 3,000 for a
-- system program call, 200,000 for any other.
local function units(ixs)
  local n = 0
  for _, ix in ipairs(ixs) do n = n + (ix.program == pump.SYSTEM and 3000 or 200000) end
  return n
end

local function short_of()
  return string.format("Need about %.4f SOL in the wallet (%s now)", needed() / 1e9, sol_str(balance))
end

local function launch()
  if busy then return end
  if not address then return say("No wallet: run 'wallet new' on the console", "orange") end
  if token.uri == "" then
    local why = pump.check(token.name, token.symbol, "x")
    if why then return say(why, "orange") end
    if token.image == "" then return say("Pick an image first", "orange") end
    if balance and not enough(balance) then return say(short_of(), "orange") end
    busy = true
    led.fill(0x99, 0x45, 0xFF)
    return upload_metadata(function() busy = false; launch() end)
  end
  local why = pump.check(token.name, token.symbol, token.uri)
  if why then return say(why, "orange") end
  if balance and not enough(balance) then return say(short_of(), "orange") end
  busy = true
  led.fill(0x99, 0x45, 0xFF)
  local cluster = chain.cluster()
  imported = wallet.mint()                -- imported or erased on the console meanwhile?
  local mint = imported or mint_kp or wallet.keypair()   -- the key stays in the firmware
  if mint ~= imported then mint_kp = mint end
  say("Checking mint " .. short(mint) .. "...")
  chain.rpc("getAccountInfo", {mint, {encoding = "base64", commitment = "confirmed"}}, function(ok0, a)
    if not ok0 then return done(false, "Mint check: " .. tostring(a)) end
    if a.value then
      if mint == imported then
        return done(false, "Mint " .. short(mint) .. " is already used on " .. cluster ..
          ". Console: wallet mint off, or import another")
      end
      mint_kp = nil                       -- a random one that landed unseen: next try, a new one
      return done(false, "Mint " .. short(mint) .. " exists: the last launch landed after all")
    end
    say("Getting a blockhash...")
    chain.rpc("getLatestBlockhash", {{commitment = "confirmed"}}, function(ok, r)
      if not ok then return done(false, "Blockhash: " .. tostring(r)) end
      -- On mainnet a bundle of one through Jito, its tip last; elsewhere the RPC, the tip as the priority fee.
      local bundle = cluster == "mainnet"
      local ixs = {pump.create(mint, address, address, token.name, token.symbol, token.uri)}
      local buy = token.buy ~= "" and ", dev buy " .. token.buy .. " SOL" or ""
      if buy ~= "" then
        ixs[#ixs + 1] = pump.token_account(mint, address)
        ixs[#ixs + 1] = pump.buy(mint, address, lamports(token.buy), pump.min_tokens(tonumber(token.buy)))
      end
      if bundle then ixs[#ixs + 1] = jito.tip(address, TIP) end
      local msg = sol.message{
        payer = address,
        blockhash = r.value.blockhash,
        instructions = ixs,
        budget = {cu_limit = units(ixs), priority_fee = bundle and 0 or TIP},
      }
      say("Approve on screen...")
      wallet.sign(msg, "Create $" .. token.symbol .. " (" .. token.name .. ")" .. buy .. " on pump.fun, mint " ..
        short(mint), function(ok2, s)
        if not ok2 then return done(false, "Not signed: " .. tostring(s)) end
        local sig = s.signature
        if not bundle then
          say("Sending " .. short(sig) .. "...")
          return send_rpc(s.tx, function(ok3, why)
            if not ok3 then return done(false, tostring(why)) end
            say("Sent, confirming " .. short(sig) .. "...")
            watch(sig, mint, cluster, POLLS)
          end)
        end
        local function fallback()           -- the same signed transaction: it can land once only
          say("Bundle not seen, sending via RPC...")
          send_rpc(s.tx, function(ok3, why)
            if not ok3 then say("RPC: " .. tostring(why), "orange") end
          end)
        end
        say("Sending bundle...")
        chain.rpc("sendBundle", {{s.tx}, {encoding = "base64"}}, {url = jito.BUNDLES}, function(ok3)
          if not ok3 then fallback() end
          watch(sig, mint, cluster, POLLS, ok3 and fallback or nil)
        end)
      end)
    end)
  end)
end

-- ---- image picker: the PNG and JPEG files in images/, in the pad ----

local function pictures()
  local out = {}
  for _, name in ipairs(fs.list(IMAGES)) do
    local n = name:lower()                -- FAT finds a name in any case
    if (n:match("%.png$") or n:match("%.jpe?g$")) and n:match("^[a-z0-9_%-%.]+$") then
      out[#out + 1] = {id = "pic", k = "IMG " .. (#out + 1), v = n}
    end
  end
  return out
end

local function close_picker()
  picking = nil
  menu.sel = 4
  keys(MENU_KEYS)
  refresh()
end

local function open_picker()
  local items = pictures()
  if #items == 0 then
    return say("No pictures: add PNG/JPEG files to images/ (lowercase names, no spaces)", "yellow")
  end
  picking = true
  pad_title("PICK IMAGE  " .. #items)
  keys("OK pick  BACK back")
  menu.sel = 1
  menu.set(items)
end

-- ---- keys ----

-- check(text), if given: the value to keep, or nil when the text isn't one.
local function edit(field, label, max, check)
  ui.input(label, max, function(text)
    if text and check then
      text = check(text)
      if not text then return say(label .. ": not a valid value", "orange") end
    end
    if text then
      token[field] = field == "symbol" and text:upper() or text
      if field ~= "uri" and field ~= "buy" then token.uri = "" end     -- upload the new metadata at launch
      storage.set("token", token)
      header()
      refresh()
    end
  end)
end

local function open(it)
  local id = it.id
  if id == "pic" then
    token.image = IMAGES .. "/" .. it.v
    token.uri = ""                        -- new metadata at launch
    storage.set("token", token)
    close_picker()
    header()
  elseif busy then return
  elseif id == "wallet" then
    if address then show_qr(address, "SEND SOL", "Send SOL to\n\n" .. address) end
    load_balance()
  elseif id == "name" then edit("name", "Coin name", pump.NAME_MAX)
  elseif id == "symbol" then edit("symbol", "Symbol", pump.SYMBOL_MAX)
  elseif id == "image" then open_picker()
  elseif id == "buy" then edit("buy", "Dev buy in SOL (0: none)", 12, buy_text)
  elseif id == "description" then edit("description", "Description", 200)
  elseif id == "twitter" then edit("twitter", "X link", LINK_MAX)
  elseif id == "telegram" then edit("telegram", "Telegram link", LINK_MAX)
  elseif id == "website" then edit("website", "Website", LINK_MAX)
  elseif id == "uri" then edit("uri", "Metadata URI (empty: upload)", pump.URI_MAX)
  elseif id == "launch" then launch()
  elseif id == "last" then
    show_qr(coin_link(last.mint, last.cluster), "LAST COIN", "$" .. token.symbol .. " on " .. last.cluster ..
      "\n\n" .. last.mint, true)
  elseif id == "selftest" then
    local why = require("lib.selftest")()
    say(why and ("Self-test failed: " .. why) or "Self-test passed", why and "orange" or "green")
  end
end

function on_key(k)
  if qr_label then
    if k == "back" then hide_qr() end
    return true
  end
  if k == "back" and picking then close_picker(); return true end
  if k == "up" or k == "down" then
    menu.move(k == "up" and -1 or 1)
    if not picking then menu_title() end
    return true
  end
  if k == "select" then
    local it = menu.item()
    if it then open(it) end
    return true
  end
  return k ~= "back"
end

-- Touch: tap a row to highlight it, tap the highlighted row to open it.
-- Taps anywhere else do nothing (true: no SELECT from the firmware).
function on_touch(x, y)
  local i = not qr_label and menu.hit(x, y)
  if i and i == menu.sel then
    on_key("select")
  elseif i then
    menu.sel = i - 1                    -- then "down" moves there and updates the title
    on_key("down")
  end
  return true
end

if not address then say("No wallet yet: run 'wallet new' or 'wallet import' on the console", "yellow")
else say("ready. set the coin, then LAUNCH") end
load_balance()
