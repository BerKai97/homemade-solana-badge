-- A TFT-terminal look for the badge's screen: black, one mono font, boxes with
-- their title cut into the top border, and a row list drawn with plain labels
-- (ui.list has the OS's own look).

local T = {}

T.C = {
  bg = 0x000000, fg = 0xE4E4E4, dim = 0x6E6E6E, line = 0x3C3C3C,
  yel = 0xF5D90A, ora = 0xFF8A1F, red = 0xFF3B3B, grn = 0x2BE07A,
}
local C = T.C

function T.label(x, y, text, color, opts)
  opts = opts or {}
  opts.size, opts.color = 12, color or C.fg
  return ui.label(x, y, text, opts)
end

-- About how wide the mono font draws s (it's semi-mono: 5 to 8 px a character).
local function width(s) return #s * 7 end

-- s cut to n characters, ".." at the end when it was longer.
function T.fit(s, n)
  s = tostring(s or ""):gsub("%s+", " ")
  return #s > n and (s:sub(1, n - 2) .. "..") or s
end

-- A text cut into a border line at (x, y): a black gap, then the text on it.
local function tag(x, y, text, color)
  local gap = ui.rect(x - 2, y - 1, width(text) + 4, 3, {color = C.bg})
  local l = T.label(x, y - 7, text, color)
  return function(s)
    gap:size(width(s) + 4, 3)
    l:text(s)
  end
end

-- A 1 px frame with a title; returns a function to change the title.
function T.box(x, y, w, h, title)
  ui.rect(x, y, w, h, {color = C.bg, border = C.line})
  return tag(x + 8, y, title, C.ora)
end

-- Text on a box's top border, flush right.
function T.note(x, y, w, text)
  return tag(x + w - 8 - width(text), y, text, C.dim)
end

-- `rows` lines of `row_h` px at (x, y), w wide. Items: {k=, v=, c=colour of v, hot=}.
-- The selected one is a filled bar (orange when hot); the list scrolls to keep it on screen.
function T.list(x, y, w, rows, row_h, key_w)
  local L = {items = {}, sel = 1, top = 1}
  local bar = ui.rect(x, y, w, row_h, {color = C.yel})
  local ks, vs = {}, {}
  for i = 1, rows do
    local ry = y + (i - 1) * row_h
    ks[i] = T.label(x + 6, ry, "")
    vs[i] = T.label(x + 6 + key_w, ry, "")
  end
  local vmax = (w - 16 - key_w) // 8      -- wide letters are 8 px

  function L.draw()
    local n = #L.items
    L.sel = math.max(1, math.min(L.sel, n))
    if L.sel < L.top then L.top = L.sel end
    if L.sel >= L.top + rows then L.top = L.sel - rows + 1 end
    L.top = math.max(1, math.min(L.top, n - rows + 1))
    bar:show(n > 0 and L.shown ~= false)
    for i = 1, rows do
      local idx = L.top + i - 1
      local it, on = L.items[idx], idx == L.sel
      ks[i]:text(it and it.k or "")
      vs[i]:text(it and T.fit(it.v, vmax) or "")
      ks[i]:color(on and C.bg or (it and it.hot) and C.ora or C.dim)
      vs[i]:color(on and C.bg or (it and (it.c or (it.hot and C.ora))) or C.fg)
      if on then
        bar:pos(x, y + (i - 1) * row_h)
        bar:color(it.hot and C.ora or C.yel)
      end
    end
  end

  function L.set(items)
    L.items = items
    L.draw()
  end

  function L.move(d)
    L.sel = L.sel + d
    if L.sel < 1 then L.sel = #L.items elseif L.sel > #L.items then L.sel = 1 end
    L.draw()
  end

  function L.show(on)
    L.shown = on
    for i = 1, rows do ks[i]:show(on); vs[i]:show(on) end
    L.draw()
  end

  function L.item() return L.items[L.sel] end

  -- The index of the item drawn at (px, py), or nil.
  function L.hit(px, py)
    if L.shown == false or px < x or px >= x + w or py < y or py >= y + rows * row_h then return nil end
    local idx = L.top + (py - y) // row_h
    return idx <= #L.items and idx or nil
  end

  return L
end

return T
