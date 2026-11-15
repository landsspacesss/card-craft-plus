# Multi-save on the SD card

Upstream card-craft keeps **nothing but the best score** across a reboot. The
in-game line *"SAVED TO NVS. SURVIVES A REBOOT."* is about that score, not about
a run: there is no world save in `therezor/card-craft` at all. `saveOffsetToNVS`
and `loadOffsetFromNVS` in the shipped binary are **M5Unified's IMU calibration**
(`utility/IMU_Class.cpp`), not cardcraft code — they sit next to each other in
the string table and look like a save system if you only grep a binary.

So this is not an upgrade of an existing save system. It is a new one.

## What a player sees

* **Title** — `START` / `LOAD GAME` / `CONTROLS`. `LOAD GAME` opens the picker.
* **Pause** (`ESC`) — `RESUME` / `CONTROLS` / `SOUND` / `FPS` / **`SAVE GAME`** /
  `NEW WORLD` / `QUIT`.
* **The picker** — three rows, `SLOT 1..3`. A used row reads
  `NIGHT n  h HEARTS`; an empty one reads `EMPTY`, or `NO SD CARD` if there is
  no card. Arrows move, `ENTER` does it, `ESC` goes back — to the title after a
  load attempt, to the pause card after a save attempt.
* Saving an occupied slot overwrites it. Empty rows are unselectable when
  loading and selectable when saving.

## Why it is a dump and not a diff

The world is 96x96 columns of a 32-bit occupancy mask plus a material-marker
arena — about 90 KB of live state. A diff against `generate(seed)` would be a
fraction of that, and it would also make a save correct only if this build's
generator produced exactly what the writing build's did. A save file outlives
the build that wrote it; sixty kilobytes of SD is not worth that coupling.

Instead the four arrays are streamed straight out of the world, 128–256 bytes at
a time, through `world::Sink` / `world::Source`. **Nothing is ever copied into
one buffer**, which is the whole reason those two interfaces exist: the board has
~136 KB of internal RAM and the two framebuffers take 129,600 of it.

What is *not* stored, and why:

| Left out | Rebuilt by |
|---|---|
| the light nibble (`g_cell` high nibble) | `world::rebuildLight()` on load — it is a function of what is solid and what emits, so a glow saved mid-recompute can never arrive stale |
| the marker **arena** (pool order, free list) | rebuilt from per-cell lists; pool indices mean nothing outside the run that allocated them |
| slabs | not storage at all — `slabBase/slabTop/slabMat` read the lowest floating run out of `g_solid` |

## File layout

`/cardcraft/slot1.dat` … `/cardcraft/slot3.dat`

```
0   u32  magic "CCSL" (0x4C534343, little-endian)
4   u16  header version = 1
6   u16  slot index, 0-based
8   u32  payload length
12  u32  CRC32 of the payload (reflected, poly 0xEDB88320)
16  u16  game::STATE_BLOB_VERSION
18  u16  night
20  u16  hp
22  u16  -
24  u32  score
28  u32  world seed
32  ..  reserved to 64
64  payload
```

Payload = `world::snapshotSave()` then `game::stateSave()`, hashed as **one**
stream so a save that is half one run and half another cannot verify.

World snapshot:

```
u32 magic "CCWS" (0x53574343)
u16 version = 1
u16 W, u16 H, u16 MAX_H
u32 seed
u8  surf[W*H]              9216 B  generator terrain anchors
u32 solid[W*H]            36864 B  occupancy, one bit a block
u8  smat[W*H]              9216 B  surface material, 4 bits used
u8  markerCount[W*H]       9216 B
    (u8 base, u8 mat) x markerCount[i]   ascending, 2 B each
```

Simulation blob: `u32 "CGTS"`, `u16 version`, `u32 sizeof(State)`, then
`sizeof(State)` bytes. `game::stateSave` `static_assert`s that `State` is
trivially copyable, so a member with a constructor or a vtable stops the build
instead of quietly writing a save that restores the wrong bytes into the wrong
field. `STATE_BLOB_VERSION` is the version of the *shape*; bump it when it
changes and old saves read as `DIFFERENT BUILD` rather than as garbage.

**Measured on a real save: 71,946 bytes.** 64,528 B of grid, two bytes a marker
(a generated world uses about 1,700 of them), 16 B of simulation header and
`sizeof(State)` — a little under 3 KB. Three slots is under 220 KB.

## Failure behaviour

* A write starts by putting a **zeroed header** over the file. An interrupted
  write therefore reads as `EMPTY`, not as a save that verifies only halfway.
* A failed payload write **deletes** the file rather than leaving a partial one.
* A read **verifies the checksum before touching memory** (one pass over the
  payload, then a second to apply it). `save::read()`'s contract is that a
  `false` return leaves nothing usable behind, and a checksum checked after the
  grid had already been overwritten could not keep that.
* A file whose magic, header version or `STATE_BLOB_VERSION` does not match is
  shown as `EMPTY` and **left on the card** — the player asked to load a run,
  not to have it tidied away because this build cannot read it.

## SD card

Pins are the card slot's own SPI bus: SCK 40, MISO 39, MOSI 14, CS 12. On the
**ADV**, GPIO5 is a slot enable and the card is invisible until it is driven
high; on the original Cardputer that pin is one of the seven keypad matrix
inputs, so it is gated on `M5.getBoard() == board_M5CardputerADV` rather than
done unconditionally.

The card is mounted **on first use**, not in `setup()`: SPI and FAT both
allocate, and `render::reserve()` has to win the two contiguous 64.8 KB DMA
blocks before anything fragments the heap. `save::reset()` re-probes when the
picker opens, so a card pushed in after boot is found without a reboot.

## Building

Upstream pins pioarduino / Arduino core 3.x. M5Cardputer 1.1.1 builds against
Arduino core **2.0.16** — the EMBER build on this machine proves it — and the
core-3 platform is a ~1.5 GB download. The `[env:adv]` / `[env:adv-selftest]`
sections at the bottom of `platformio.ini` therefore reuse the toolchain that
already builds and flashes for this board:

```
tools/link-local-libs.sh     # once: points lib/ at the pinned library versions
pio run  -e adv              # the firmware
pio run  -e adv-selftest     # the same, plus the boot-time SD self-test
pio test -e native           # host tests (202 cases)
test/host_sd/run.sh          # the save layer, on real files (52 assertions)
```

Built image: **810,736 B**, flash 24.3%, static RAM 56.6%, md5
`4a1606e997fc5653fbab3c4052090fce`. **Zero warnings from this work**; five remain
that predate it (three `-Wmisleading-indentation` in `raycast.cpp`, one
`-Wclass-memaccess` in `render.cpp`, one unused variable in M5GFX). A clean
rebuild reproduces the same md5.

One trap worth writing down: do not reintroduce a second `platformio-*.ini`.
PlatformIO keys `.pio/build` on a checksum of the project configuration, so
alternating between two config files wipes the build directory -- including the
environment you were not building.


## Verification

**Host.** `pio test -e native` — 51 cases, including five new
ones against an in-memory `Sink`/`Source`:

| test | what it pins down |
|---|---|
| `test_snapshot_round_trip_is_exact` | a hole mined out of the middle of a column, a block built in mid-air and a lit torch all come back cell for cell — `height`, `topMat`, `light` and `blockAt` for every z |
| `test_snapshot_rebuilds_light` | the light nibble is not in the file, and the rebuild lands on the same answer |
| `test_snapshot_refuses_foreign_bytes` | a flipped magic is refused, not interpreted |
| `test_snapshot_refuses_a_truncated_stream` | a short stream fails the load; a sink that stops accepting bytes fails the save instead of reporting a length it did not write |
| `test_snapshot_restores_the_marker_pool` | the pool comes back to the same size — a load that leaked a marker a column would pass the cell comparisons and still run the board out of pool after a few saves |

**The save layer, against real files.** `test/host_sd/run.sh` compiles
`src/storage_sd.cpp` against shim `Arduino.h` / `SD.h` / `SPI.h` / `M5Cardputer.h`
headers (`test/host_sd/shim/`) that turn `File` into a `FILE*` rooted at
`$HOST_SD_ROOT`, then drives the whole layer. 52 assertions, all passing:

| test | what it pins down |
|---|---|
| save → meta → load | header fields and the score agree with what was written; the reloaded grid hashes identically and the seed is the one saved |
| missing slot | an absent file is not a used slot and does not load |
| foreign magic | patched to `0x11223344` → reads as EMPTY, and `read()` leaves the world **untouched** |
| other build | `STATE_BLOB_VERSION + 1` → `ST_VERSION`, world untouched |
| flipped payload byte | one byte in the occupancy grid → `ST_CORRUPT`, world untouched — the checksum is verified *before* anything is applied |
| truncated file | 100 bytes short → refused, world untouched |
| interrupted write | a write budget of 30,000 B → `write()` returns false, **the file is gone**, and the slot reads EMPTY rather than corrupt |
| overwrite / erase | the second write is the one that loads; erasing twice is not an error |
| slot bounds | -1 and `SLOT_N` are refused by every entry point |

**In the emulator (m5stack-esp-emulator, `-M m5cardputeradv`).** The image is
verified app-only (`0xABCD5432` at 0x20, chip id 9) so it installs into any
64 KB-aligned app slot. What the emulator established:

| check | result |
|---|---|
| boots to the title card | **pass** — `docs/emu/adv-title.png`; the badge reads `CARDPUTER ADV`, so M5GFX's variant detection ran and the panel came up |
| SD card mounts (SPI + the ADV's GPIO5 slot enable) | **pass** on one boot: `[selftest] mount=1 status=''` |
| a slot file reaches a real FAT card | **pass**, same boot: `write=1 status=''` |
| the header reads back | **pass**, same boot: `used=1 night=6 hp=7 score=666 seed=4242` — the values the self-test wrote |
| load restores the world | **not reached** — that boot died on a stack-canary trip that turned out to be the *self-test's* two `game::State` locals against the 8 KB Arduino task stack; after making them static, every later mount failed with `esp_vfs_fat_register failed 0x(101)` (`ESP_ERR_NO_MEM`) before any card I/O |

Two things in that table are worth more than the passes:

* **A real bug, found and fixed.** The first build of this tree linked M5GFX
  **0.3.0** and M5Unified **0.3.0** — the unversioned directories in
  `~/.pio/libdeps` — and it booted to a **black screen**, with
  `[error] Keyboard: Unsupported board type: 136` on the console. M5Cardputer
  1.1.1 is written against M5Unified 0.2.x. Pinning M5GFX **0.2.36** and
  M5Unified **0.2.29**, the pair EMBER's own link map shows, fixes it.
  `tools/link-local-libs.sh` now pins them explicitly. Without the emulator run
  this would have gone to the board as a black screen.
* **`sizeof(game::State)` is 2,604 bytes** and the Arduino loop task has 8,192
  bytes of stack; after `setup()` there are **5,724 bytes** left. That is the
  number to check before putting a `State` on the stack.

The emulator's Cardputer-ADV **keyboard does not deliver character keys to the
guest** — `dot`, `grave_accent`, `tab`, `esc`, `3`, `a`, `spc` and a held `w`
all do nothing, and neither does writing `/machine/tca8418`'s `key-event`
property directly — so the save card cannot be driven from the UI there. The SD
path is therefore exercised by a boot-time self-test (`pio run -e adv-selftest`)
that calls the same `save::` entry points the pause menu calls. It is not a
shipping configuration and is never flashed.

**On the board — not done yet.** What is left, on hardware:

- [ ] first `SAVE GAME` writes `/cardcraft/slot1.dat`, **71,946 bytes**
- [ ] the row comes back as `NIGHT n  h HEARTS` without leaving the card
- [ ] power-cycle → `LOAD GAME` → the run resumes where it was, including the
      hole that was mined and the torch that was placed
- [ ] a card pulled mid-save reads `EMPTY`, not `SAVE CORRUPT` (host-verified
      with a write budget; the card-path version is the board's)
- [ ] that the SD stack can mount at all once the two framebuffers are
      allocated. It did, once, in the emulator, and then reported `NO_MEM` on
      every later attempt — so this is the riskiest line in the whole feature
      and the first thing to try on the board

## The autosave, and dying

There are now **four** slots. The first three are the player's; the fourth is
`AUTO`, written by the game and never a manual target:

- every **60 s** of play (not every frame: the blob is 70 KB and a card write is
  the one thing in a frame that can take a tenth of a second)
- when a run **ends** — a death is exactly when the world is worth keeping

`AUTO` appears in the LOAD picker like any other row and is greyed out in the
SAVE picker, because a manual save into it would be silently overwritten by the
next automatic one.

Death is no longer the end of the world. The death card offers **ENTER
RESPAWN** or **ESC TITLE**: respawning keeps the world, the inventory and the
night, refills the hearts, grants a second of invulnerability, lifts the player
onto the surface of the column under them, and clears mobs within eight cells —
respawning inside the three that just killed you is not a second chance. The
score is still recorded, so a death costs the run's score, not its build.
