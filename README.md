# homemade-solana-badge

Launch a pump.fun coin from a homemade Solana Breakpoint 2026 badge (ESP32-C5, Solana OS 1.0.0).

- `apps/pump/`: the Pump Launch app (Lua). Pick a picture, set the name, ticker and description,
  launch: the picture and metadata go to IPFS (Filebase), then a standard `create_v2` (SOL pair,
  creator fees to you), with an optional dev buy, is signed on the badge after you approve it on
  screen. On mainnet it goes to Jito's block engine as a bundle of one.
- `firmware/`: what the app needs from Solana OS, in C:
  - `main/wallet`, `main/sol`: a wallet kept in the badge's NVS (Ed25519 via TweetNaCl), Solana
    transactions (legacy and v1) and PDAs. Lua gets `wallet.address()`, `wallet.keypair()` and
    `wallet.sign()`, never the key; every signature waits for the OS's own approval dialog.
  - `chain.rpc()`: Solana JSON-RPC straight from the badge. `ipfs.add()`: uploads with your key.
  - `ui.image(x, y, file, {size = n})`: any PNG or JPEG as an n x n thumbnail made on the badge
    (`main/img`), and picture icons in the Apps list (`icon_image`).
  - `kb`: a console command that types into the on-screen keyboard from a PC.
  - `solana-os.patch`: the changes to Solana OS's own files: the breadboard build (touch, apps in
    the internal flash), compiled apps sent over USB, and `docs/LUA_API.md` for all of the above.
- `tools/`: `badge_put.py` copies an app onto the badge over USB (compiled on the PC, see below),
  `badge_remote.py` drives it from the PC's keyboard, `badge_type.py` types into a text field,
  `badge_mint.py` gives it a mint keypair made on the PC.

## Build

```sh
./setup.sh                      # clones solana-os next to this repo, patches it, copies firmware/ in
cd ../solana-os && idf.py build flash monitor        # ESP-IDF 6.0.1
```

`./save.sh` copies firmware work done in that checkout back here.

An app's Lua compiled on the badge keeps ~70 bytes of heap per line, more than Pump Launch can spare.
So `badge_put.py` compiles it on the PC the way the App Store does (`code.bin`, run in place from
flash), and the patch lets the console write `code.bin`; the badge runs it only when `app.json`
names its SHA-256. Pump Launch then idles at ~38 KB and peaks at ~50 KB while launching, of the 96 KB
Solana OS gives an app. It needs node and the firmware's compiler, built once:

```sh
cd ../solana-os && podman run --rm -v "$PWD":/src:Z -w /src docker.io/emscripten/emsdk:4.0.10 \
    tools/luac/build.sh tools/luac/out/luac.mjs
```

Without it `badge_put.py` warns and sends the source only, and launching runs out of memory.

### Breadboard build

The homemade badge: an ESP32-C5 dev board and a 2.8" ILI9341 display with XPT2046 touch, no SD
card, buttons or battery.

```sh
idf.py -B build-diy -D SDKCONFIG=build-diy/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.diy" build flash monitor
```

| Display pin | ESP32-C5 |
|---|---|
| VCC, GND | 5V, GND, with 100 µF across them |
| CS, RESET, DC, LED | GPIO1, GPIO0, GPIO7, GPIO3 |
| SCK, SDI (MOSI), SDO (MISO) | GPIO9, GPIO10, GPIO8 |
| T_CLK, T_DIN, T_DO | GPIO9, GPIO10, GPIO8 (shared with the display) |
| T_CS | GPIO6 |
| T_IRQ | not connected (polled) |

Apps and pictures live on a FAT volume in the internal flash, mounted at `/sdcard` in place of the
card. Tap the screen (the first boot calibrates it; `touch cal` again), the board's BOOT button is
Home, and `tools/badge_remote.py` drives it all from a PC. Console commands work on either USB
port. It has no badge server unless `server <url>` sets one, and never offers the badge's software
updates.

## Use

On the badge's console (USB serial, or the emulator page's console box):

```
wallet new                      # or: wallet import <base58 secret key>
wallet rpc mainnet              # devnet by default; or any https RPC URL
wallet ipfs key <token>         # Filebase: the bucket's IPFS RPC API key
```

The coin's mint address is a new random one each launch, made on the badge. For one of your own,
like pump.fun's addresses ending in `pump`, make it on a PC and send it over (the console line
holds the secret key, so this is safer than typing it):

```sh
solana-keygen grind --ends-with pump:1                   # a few minutes; writes <address>.json
tools/badge_mint.py <address>.json                       # or --print: the line for the emulator
```

The badge keeps it until a mainnet launch uses it; devnet launches leave it, so you can test first.
`wallet mint` shows it, `wallet mint off` drops it. Keep the .json: it's the only other copy.

Install the app, with pictures to pick from:

```sh
tools/badge_put.py apps/pump --add images/coin.png=~/Pictures/coin.png
```

or copy PNG/JPEG files (lowercase names, no spaces) to `hh/apps/pump/files/images/` on the SD card.
The tools find the badge by its USB ID; give the port when there are several.

Typing: open a text field on the badge, then type or paste in `tools/badge_remote.py`, or send a
line with `tools/badge_type.py "My Coin"`. In the emulator page, type or press Ctrl+V while a text
field is open.

## Emulator

In the checkout, after a firmware build (it reads `sdkconfig` and `managed_components/`):

```sh
podman run --rm -v "$PWD":/src:Z -w /src docker.io/emscripten/emsdk:4.0.10 sh -c 'emcmake cmake \
    -S emulator -B emulator/build -DCMAKE_BUILD_TYPE=MinSizeRel -DEMU_OUT_DIR=/src/emulator/out &&
    cmake --build emulator/build -j"$(nproc)"'
```

Then `tools/badge_put.py apps/pump --out ../solana-os/emulator/harness/apps/pump.txt`, serve
`../solana-os/emulator` (`python3 -m http.server -d ../solana-os/emulator 8765`) and open
`http://localhost:8765/harness/?install=pump`.

## Tests

```sh
../solana-os/main/sol/tests/run.sh      # Ed25519 (RFC 8032), create_v2 and v1 messages against golden vectors
../solana-os/main/img/tests/run.sh      # thumbnails against Pillow
```

The app's SELFTEST row checks its instructions and v1 message against golden vectors on the badge.
