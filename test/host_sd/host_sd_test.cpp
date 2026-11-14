// =============================================================================
//  host_sd_test.cpp — the save layer, against real files
//
//  The world snapshot itself is covered by test/test_world (in-memory Sink and
//  Source). What this covers is the layer above it: the header, the checksum,
//  the two-pass read, and what a card that stops accepting bytes in the middle
//  of a save actually leaves behind.
//
//  What it does NOT cover is the board: SPI, and GPIO5 on the ADV. Nothing on a
//  host can, and SAVES.md's checklist says so.
//
//  Build and run: test/host_sd/run.sh
// =============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "game.h"
#include "storage_sd.h"
#include "world.h"

#include "SD.h"          // host_path(), host_write_budget

namespace {

int g_fail = 0;
int g_pass = 0;

void ok(bool cond, const char* what, int line) {
  if (cond) { ++g_pass; return; }
  ++g_fail;
  printf("  FAIL line %d: %s\n", line, what);
}
#define OK(cond) ok((cond), #cond, __LINE__)

// A signature of the world that a wrong load cannot accidentally match.
uint32_t worldSignature() {
  uint32_t h = 2166136261u;
  for (int y = 0; y < world::H; ++y)
    for (int x = 0; x < world::W; ++x) {
      h = (h ^ world::height(x, y)) * 16777619u;
      h = (h ^ world::topMat(x, y)) * 16777619u;
      h = (h ^ world::light(x, y)) * 16777619u;
      for (int z = 0; z < world::MAX_H; ++z)
        h = (h ^ world::blockAt(x, y, z)) * 16777619u;
    }
  return h;
}

long fileSize(const char* cardPath) {
  char p[512];
  host_path(cardPath, p, sizeof p);
  FILE* f = fopen(p, "rb");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fclose(f);
  return n;
}

// Patches one little-endian field of the header, the way a file written by a
// different build or a damaged card would differ.
bool patchField(const char* cardPath, long off, const void* src, size_t n) {
  char p[512];
  host_path(cardPath, p, sizeof p);
  FILE* f = fopen(p, "r+b");
  if (!f) return false;
  fseek(f, off, SEEK_SET);
  const bool wrote = fwrite(src, 1, n, f) == n;
  fclose(f);
  return wrote;
}

void resetCard() {
  char p[512];
  host_path("/cardcraft", p, sizeof p);
  char cmd[600];
  snprintf(cmd, sizeof cmd, "rm -rf %s", p);
  if (system(cmd) != 0) { /* first run has nothing to remove */ }
  // The mount is memoised, so the layer has to be told the card changed.
  save::reset();
}

// -----------------------------------------------------------------------------

void test_save_then_meta_and_load(void) {
  resetCard();
  world::generate(4242);
  game::State s;
  game::begin(s, 4242);
  s.night = 7;
  s.hp = 4;

  // Something a reload has to notice: a block taken out of a column.
  const int cx = world::W / 2, cy = world::H / 2;
  for (int z = world::GROUND; z > 0; --z) {
    if (world::solidAt(cx, cy, z)) {
      uint8_t m, n;
      world::mine(cx, cy, z, 100000, m, n);
      break;
    }
  }
  world::rebuildLight();
  const uint32_t wantSig = worldSignature();
  const uint32_t wantSeed = world::currentSeed();

  OK(save::write(0, s));
  OK(save::status() == save::ST_OK);

  const long sz = fileSize("/cardcraft/slot1.dat");
  // 64,528 bytes of grid + 2 a marker + the state blob + 64 of header. A wide
  // band rather than an exact figure: sizeof(State) is the compiler's, and the
  // marker count is the world's.
  OK(sz > 64500 && sz < 75000);
  printf("  slot1.dat = %ld bytes\n", sz);

  save::Meta m;
  OK(save::readMeta(0, m));
  OK(m.used);
  OK(m.night == 7);
  OK(m.hp == 4);
  OK(m.seed == wantSeed);
  OK(m.score == game::score(s));

  // A different world, so a load that silently did nothing cannot match.
  world::generate(999);
  game::State back;
  game::begin(back, 999);
  OK(worldSignature() != wantSig);

  OK(save::read(0, back));
  OK(save::status() == save::ST_OK);
  OK(world::currentSeed() == wantSeed);
  OK(worldSignature() == wantSig);
  OK(back.night == 7);
  OK(back.hp == 4);
}

void test_missing_slot_is_not_used(void) {
  resetCard();
  save::reset();
  save::Meta m;
  OK(!save::readMeta(1, m));
  OK(!m.used);
  game::State s;
  game::begin(s, 3);
  OK(!save::read(1, s));
}

void test_foreign_header_is_refused(void) {
  resetCard();
  world::generate(11);
  game::State s;
  game::begin(s, 11);
  OK(save::write(0, s));

  // The magic.
  uint32_t bad = 0x11223344u;
  OK(patchField("/cardcraft/slot1.dat", 0, &bad, 4));
  save::Meta m;
  OK(!save::readMeta(0, m));         // reads as EMPTY, not as a run
  world::generate(12);
  game::State back;
  game::begin(back, 12);
  const uint32_t before = worldSignature();
  OK(!save::read(0, back));
  OK(worldSignature() == before);    // and the world was not touched
}

void test_other_build_is_refused(void) {
  resetCard();
  world::generate(21);
  game::State s;
  game::begin(s, 21);
  OK(save::write(0, s));

  const uint16_t other = (uint16_t)(game::STATE_BLOB_VERSION + 1);
  OK(patchField("/cardcraft/slot1.dat", 16, &other, 2));

  world::generate(22);
  game::State back;
  game::begin(back, 22);
  const uint32_t before = worldSignature();
  OK(!save::read(0, back));
  OK(save::status() == save::ST_VERSION);
  OK(worldSignature() == before);
}

void test_flipped_payload_byte_is_refused(void) {
  resetCard();
  world::generate(31);
  game::State s;
  game::begin(s, 31);
  OK(save::write(0, s));

  // One byte in the middle of the occupancy grid, which is the part a load
  // would otherwise apply before noticing.
  uint8_t b = 0;
  const long at = 64 + 16 + world::W * world::H + 500;
  {
    char p[512];
    host_path("/cardcraft/slot1.dat", p, sizeof p);
    FILE* f = fopen(p, "rb");
    OK(f != nullptr);
    if (!f) return;
    if (fseek(f, at, SEEK_SET) != 0 || fread(&b, 1, 1, f) != 1) { OK(false); fclose(f); return; }
    fclose(f);
  }
  b ^= 0xFF;
  OK(patchField("/cardcraft/slot1.dat", at, &b, 1));

  world::generate(32);
  game::State back;
  game::begin(back, 32);
  const uint32_t before = worldSignature();
  OK(!save::read(0, back));
  OK(save::status() == save::ST_CORRUPT);
  OK(worldSignature() == before);    // verified before it was applied
}

void test_truncated_file_is_refused(void) {
  resetCard();
  world::generate(41);
  game::State s;
  game::begin(s, 41);
  OK(save::write(0, s));

  char p[512];
  host_path("/cardcraft/slot1.dat", p, sizeof p);
  FILE* f = fopen(p, "rb");
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fclose(f);
  if (truncate(p, n - 100) != 0) { OK(false); return; }

  world::generate(42);
  game::State back;
  game::begin(back, 42);
  const uint32_t before = worldSignature();
  OK(!save::read(0, back));
  OK(worldSignature() == before);
}

// A card that stops accepting bytes part-way. What must not be left behind is
// a file that still starts with a magic: that would read as a run and fail its
// checksum every time, with nothing on screen to explain why.
void test_interrupted_write_leaves_no_save(void) {
  resetCard();
  world::generate(51);
  game::State s;
  game::begin(s, 51);

  host_write_budget = 30000;         // less than the payload
  OK(!save::write(0, s));
  host_write_budget = -1;

  save::Meta m;
  OK(!save::readMeta(0, m));         // empty, not corrupt
  OK(fileSize("/cardcraft/slot1.dat") < 0);   // and actually gone
}

void test_overwrite_then_erase(void) {
  resetCard();
  world::generate(61);
  game::State s;
  game::begin(s, 61);
  s.night = 2;
  OK(save::write(0, s));
  s.night = 9;
  OK(save::write(0, s));

  save::Meta m;
  OK(save::readMeta(0, m));
  OK(m.night == 9);                  // the second write, not the first

  OK(save::erase(0));
  OK(!save::readMeta(0, m));
  OK(save::erase(0));                // erasing an empty slot is not an error
}

void test_slot_bounds(void) {
  resetCard();
  world::generate(71);
  game::State s;
  game::begin(s, 71);
  OK(!save::write(-1, s));
  OK(!save::write(save::SLOT_N, s));
  save::Meta m;
  OK(!save::readMeta(-1, m));
  game::State b;
  OK(!save::read(save::SLOT_N, b));
}

}  // namespace

int main() {
  printf("host_sd: the save layer against real files\n");
  test_save_then_meta_and_load();
  test_missing_slot_is_not_used();
  test_foreign_header_is_refused();
  test_other_build_is_refused();
  test_flipped_payload_byte_is_refused();
  test_truncated_file_is_refused();
  test_interrupted_write_leaves_no_save();
  test_overwrite_then_erase();
  test_slot_bounds();
  printf("host_sd: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
