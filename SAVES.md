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
core-3 platform is a ~1.5 GB download. `platformio-adv.ini` therefore reuses the
toolchain that already builds and flashes for this board:

```
tools/link-local-libs.sh                      # once, points lib/ at the libraries
pio run -c platformio-adv.ini -e adv          # firmware
pio test -c platformio.ini -e native          # host tests (202 cases)
test/host_sd/run.sh                           # the save layer, on real files
```

Built image: **839,200 B**, flash 25.1%, static RAM 56.6%. The only warnings are
five that predate this work (three `-Wmisleading-indentation` in `raycast.cpp`,
one `-Wclass-memaccess` in `render.cpp`, one unused variable in M5GFX).

Three core-3-isms have to be satisfied for a core-2 build, and all three are
handled by `build_src_flags = -ffast-math -std=gnu++17` (project sources only,
so the core and M5GFX keep the standard they were written against):

1. `render::pack()` is a multi-statement `constexpr` (C++14).
2. `ui::MenuItem` is brace-initialised — an aggregate with default member
   initialisers is C++14 and later.
3. `sfx.cpp` `static_assert`s a loop-computed `constexpr`.

The four libraries are **symlinked into `lib/`**, not named in `lib_deps`:
LDF resolves a *declared* dependency from the registry even when an explicit
entry of the same name is listed, so `lib_deps` got as far as
`Installing M5GFX @ >=0.3.0` and stalled at 10% of it. Libraries found in
`lib/` satisfy those declarations first.

## Verification

**Host.** `pio test -c platformio.ini -e native` — 51 cases, including five new
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

**On the board — not done yet.** The image is verified app-only
(`0xABCD5432` at 0x20, chip id 9) so it installs into any 64 KB-aligned app slot,
but nothing below has been exercised on hardware — it is the SPI bus and GPIO5
that no host test can reach:

- [ ] first `SAVE GAME` writes `/cardcraft/slotN.dat`, size ~66 KB
- [ ] the row comes back as `NIGHT n  h HEARTS` without leaving the card
- [ ] power-cycle → `LOAD GAME` → the run resumes where it was, including the
      hole that was mined and the torch that was placed
- [ ] a card pulled mid-save reads `EMPTY`, not `SAVE CORRUPT` (host-verified
      with a write budget; the card-path version is the board's)
- [ ] emulator: SD image mounted via `storage.sd_image_artifact_id`, save,
      unmount, remount, load
