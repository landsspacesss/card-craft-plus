# Slimming the world — what is worth doing, measured

The world is 90,112 B of the 185,448 B static RAM (48%). Three of the four
obvious levers turned out to be dead ends; this records why, so nobody spends a
session on them again, and then lays out the one that works.

Run the census yourself:

```sh
g++ -std=gnu++17 -I src tools/world-census.cpp src/world.cpp -o /tmp/census && /tmp/census
```

## The census that decides the design

After `world::generate()`, how many columns are **not** simply "solid from the
base up to a height"?

| seed | columns | simple (solid prefix) | with gaps |
|---|---:|---:|---:|
| 4242 | 9,216 | 8,522 | **694** |
| 7 | 9,216 | 8,624 | **592** |
| 99 | 9,216 | 8,557 | **659** |

About **7%**. That is what makes a hybrid representation pay: the common column
needs one height byte, and only one column in fourteen needs a full 32-bit mask.
(Digging adds gap columns; a session that tunnels through 2,000 columns is a very
long session, and the override table is sized for that.)

## Dead ends (measured, do not retry)

| Lever | Why not |
|---|---|
| **Marker pool 4,096 × 4 B** | A fresh world already uses **2,370 of them** (1,726 free). Halving the pool would leave nothing for the player and `place()` would start refusing. It is not slack, it is headroom. |
| **`render::s_tex` (4,416 B)** | Upstream measured the flash-resident version at **+4.9 ms/frame**. 4 KB is not worth a third of the frame budget. |
| **`game::g_flow` (18,432 B)** | It is a per-cell distance+surafce field (`flowPack(dist, z)`), not a queue: 13 bits per column, so it cannot become 1 B. |

## The design that works: height + sparse overrides

```
g_top[9216]                1 byte per column
   6 bits: highest solid z + 1  (0..32 fits)
   1 bit : "this column has an override mask in the hash"
   1 bit : spare (use it for a scrub counter before it becomes a bug)

override table, open addressing, column index as the key
   2,048 slots × (uint16 column + uint32 mask) = 12,288 B
   -> about 2,000 columns can be gapped at once (22% of the map)

total 9,216 + 12,288 = 21,504 B     vs 36,864 B today     = -15,360 B
```

`world::Cell` already hands the mask out by value, and the raycaster never
touches storage (`g_solid` is file-static to `world.cpp`): it calls
`cellAt`/`matAt`. So `cellAt` can reconstruct the mask and **nothing outside
`world.cpp` changes**:

```cpp
uint32_t mask = hasOverride(i) ? overrideMask(i)
                               : (top >= 32 ? ~0u : ((1u << top) - 1u));
```

### What it costs

- **25 places in `world.cpp` touch `g_solid` directly**, and several are
  read-modify-write in hot paths (`mineTop`, `place`, `explode`, `devSlab`,
  worldgen, snapshot load, `runsAt`, `cellAt`, the light pass). Each becomes a
  read-modify-write through a helper that promotes the column to an override
  entry the first time a write makes it non-prefix. This is the whole job.
- **The save blob changes.** The world snapshot currently writes the raw masks;
  the new format writes `g_top` + the override table. `STATE_BLOB_VERSION` /
  the snapshot format needs its own version, and old saves need either a
  converter or a refusal.
- **A hash probe on gapped columns.** 7% at spawn, rising as the player digs.
  The non-gapped path gets *faster* (one byte load instead of a 4-byte mask),
  so this may net out even; it has to be measured on hardware with `adv-dev`.

### Verification the change must pass

1. `pio test -e world` and `pio test -e raycast` green — these already cover
   runs, overhangs, mining, placement and the light pass, which is most of the
   risk surface.
2. A new test: for a set of seeds, the reconstructed mask (`cellAt().solid`)
   equals the old raw mask for every column — a one-off diff against the old
   representation, kept as a property test.
3. Save round-trip: snapshot → load → snapshot is byte-identical.
4. Census after heavy building: place/mine a thousand blocks, then assert the
   override table still has room and the masks match a reference.

## Order of work

1. Add the accessors and the census test **without** changing storage (still
   `g_solid`). Green.
2. Switch storage to `g_top` + overrides, keeping `cellAt` as the seam. Green.
3. Bump the snapshot format. Green.
4. Measure on the device with `adv-dev`: frame time and free heap.

Step 2 is the one that must not be interrupted: it touches every mutator, and a
half-converted tree has two sources of truth for geometry. That is why it is its
own session and not an afternoon's patch.
