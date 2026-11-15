# Memory: what this build actually costs

Measured on `pio run -e adv` (the shipping build) and from the instrumented
`adv-dev` running in the emulator. Regenerate the runtime numbers with:

```sh
pio run -e adv-dev -t upload      # or run the merged image in the emulator
```

The dev build prints one line every 30 frames: frame time, per-stage
microseconds, heap, the low-water mark, an allocator integrity walk, both task
stack margins, and the placement-marker pool.

## Flash — plenty of room

The app partition `default_8MB.csv` gives it is 3,342,336 B (3.3 MB):

| | bytes | |
|---|---:|---|
| app image | 816,896 | 24.4% of the slot |
| `.flash.text` (code) | 410,255 | |
| `.flash.rodata` (sprites, textures, sfx banks, strings) | 328,956 | |
| `.iram0.text` + vectors | 62,622 | runs from IRAM |
| **free in the slot** | **≈ 2,525,000** | room for a lot more content |

## Static RAM — the tight one, and it has not moved

| | bytes | of 327,680 |
|---|---:|---|
| `.dram0.bss` | 170,744 | |
| `.dram0.data` | 14,704 | |
| **static total** | **185,448** | **56.6%** |

The largest static arrays, in order:

| symbol | bytes | what |
|---|---:|---|
| `world::g_solid` | 36,864 | 96×96 columns × 4 B of block bits |
| `game::g_flow` | 18,432 | water spread queue |
| `world::g_mhead` | 18,432 | per-column marker chain head |
| `render::s_shade` | 16,384 | material × face × shade × texel colour tables |
| `world::g_mark` | 16,384 | placement-marker pool (4096 × 4 B) |
| `world::g_cell`, `g_surf` | 9,216 each | per-cell material/light, surface material |
| `render::s_part` | 5,760 | particles |
| `render::s_tex`, `game::g_queue` | 4,096 each | texture copy, tick event queue |
| `render::s_limit/slabHi/slabLo` | 2,880 each | per-column occlusion tables |
| `game::State` | 2,604 | the whole simulation state, save-blob-shaped |

## Heap at runtime — about 50 KB spare, and it stays there

Measured in the play screen, after the two 64.8 KB DMA framebuffers have been
taken (129,600 B of the heap is gone before the first frame):

| | bytes |
|---|---:|
| free | **50,760** |
| largest single free block | **29,684** |
| low-water mark since boot | **50,468** |
| allocator integrity (`heapok`) | 1 (clean) |

The low-water mark is the interesting one: it is 292 B below the standing free
figure, so nothing in the play loop is quietly eating and returning memory. A new
static array comes out of this number; a new runtime allocation has to fit
29,684 B in one piece.

## Task stacks

| | free | of |
|---|---:|---:|
| Arduino loop task | 5,804 | 8,192 |
| render worker (the other core's column loop) | 5,424 | 8,192 |

Neither has a guard page — an overflow corrupts the heap block underneath rather
than faulting — which is why both margins are printed every half second.

## Reading the numbers

- **Flash is not a constraint.** 2.5 MB of the slot is unused; sprites, sounds
  and textures are cheap.
- **Static DRAM is the constraint.** The world grid alone is 36 KB, the shading
  tables 16 KB, and the two framebuffers 126.5 KB. Anything new that wants static
  RAM is competing with those.
- **Heap is comfortable but shallow.** 50 KB free, but the largest single block
  is 29.7 KB: a 40 KB allocation would fail even though 50 KB is "free".

The frame-rate figures in the same line are meaningless under QEMU (the emulator
does not model instruction timing); only the memory and stack figures are read
from this run.
