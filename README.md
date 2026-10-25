# KRUCZEK SURVIVAL (working title)

A two-screen survival / base-builder for the **Wii U**.

* **TV** - the battlefield. Kruczek (a crow) auto-shoots feathers at waves of rats, snakes, cats and a fox boss.
* **GamePad** - the *same arena as a touch map*. Whatever you build on it appears in the same spot on the TV:
  Hole (trap + digs up loot), House (+25 max HP), Picnic (heals), Campfire (faster attacks, burns), Tower (shoots), Garden (grows loot).
* **The link between the screens** - loot you pick up on the TV *falls onto the GamePad*. Tap it before it lands
  (or while it sits there) to bank it. Miss it and you only keep half. Chain catches for a COMBO bonus.
* Clearing waves unlocks new blueprints. Die and you lose a quarter of your loot, the wave restarts, the base stays.

## Controls (Wii U GamePad)
| | |
|---|---|
| Left stick | move Kruczek |
| A / B | dash / caw (area knock-back) |
| Touch screen | build, catch loot, menu |
| Right stick + R/ZR | move a cursor on the pad + "tap" (no-touch alternative) |
| D-pad left/right | pick building, D-pad down or Y = demolish mode |
| + | pause menu |

## Build for Wii U
```
dkp-pacman -S wiiu-dev           # once, devkitPro pacman
export DEVKITPRO=/opt/devkitpro
make                             # -> kruczek.wuhb  (copy to SD:/wiiu/apps/ , run with Aroma)
```
No local toolchain? Push this folder to GitHub: `.github/workflows/build.yml` builds it with the `devkitpro/devkitppc`
image and attaches `kruczek.wuhb` as a build artifact. Or: `docker run --rm -v "$PWD":/p -w /p devkitpro/devkitppc bash -c "dkp-pacman -S --noconfirm wiiu-dev && make"`.

Saves go to `SD:/wiiu/apps/kruczek/kruczek_save.dat`.

## Desktop simulator (for development)
`make -f Makefile.desktop && ./kruczek_sim` opens two windows (TV + GamePad; mouse on the pad window = touch).
Keys: WASD move, J dash, K caw, Left/Right pick building, R demolish, Esc pause. `--bot --fast --frames N` runs a bot headless.

## Layout
* `src/game.c` - all game logic and drawing (platform independent)
* `src/gfx.c` - small software renderer (anti-aliased shapes, ink outlines, text) writing 0xRRGGBBAA frame buffers
* `src/audio.c` - synthesised sound effects + layered soundtrack (calm while building, drums + lead in battle)
* `src/platform_wiiu.c` / `src/platform_sdl.c` - Wii U (OSScreen, VPAD, SDL audio) and desktop layers

## Honest status
The game logic, both screens, audio and the Wii U source were written and **tested on desktop only**. The Wii U layer
has not been compiled against the real wut headers or run on hardware. The least certain parts are marked in
`platform_wiiu.c`: the OSScreen buffer probing (falls back to a slow path), the touch coordinate range (`TOUCH_SRC_W/H`)
and the save path. Frame rate on the Wii U CPU is unknown - the renderer is plain C with no SIMD.

Fonts: Inter (SIL OFL) and TeX Gyre Bonum (GUST Font License), subsetted. `stb_truetype.h` is public domain.

## Installing as a WUP (WUP Installer GX2)
You do **not** need this to play: copy `kruczek.wuhb` to `SD:/wiiu/apps/` and start it from Aroma. A WUP is only useful if you want a
real Wii U Menu channel. Caveats: an installed title is sandboxed, so it **cannot write to the SD card** (the save file will not work,
the game still runs), and WUP Installer GX2 needs a console with signature checks patched.

1. `make` (produces `kruczek.rpx`).
2. Get the `code/`, `meta/` folders of any existing installable homebrew channel (supplies `app.xml`, `cos.xml`, `meta.xml` for your firmware).
3. `tools/make_wup.sh path/to/that/template 0005000012340001` - swaps in our `.rpx`, TGA images (`tools/make_tga.py`), names and title id -> `build/wup_input/`.
4. `java -jar NUSPacker.jar -in build/wup_input -out build/wup_out -encryptKeyWith <Wii U common key>` (you must supply the key yourself;
   without it NUSPacker writes a placeholder key and the package installs but will not boot).
5. Copy the output title folder to `SD:/install/`, run WUP Installer GX2, install to **USB**, not NAND.
The XML templates and the install flow are untested here.
