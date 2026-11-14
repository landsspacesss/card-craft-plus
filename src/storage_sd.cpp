// =============================================================================
//  storage_sd.cpp — see storage_sd.h
// =============================================================================
#include "storage_sd.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>

#include <string.h>

#include "game.h"
#include "world.h"

namespace save {
namespace {

// The card slot's bus, as on the original Cardputer and the ADV alike. The
// display is on a different SPI bus, so this shares it with nothing.
constexpr int SD_SCK  = 40;
constexpr int SD_MISO = 39;
constexpr int SD_MOSI = 14;
constexpr int SD_CS   = 12;

// Cardputer ADV: GPIO5 is a slot enable, and the card is invisible until it is
// driven high. On the original Cardputer the same pin is one of the seven
// keypad matrix inputs, so driving it would fight the 74HC138 multiplexer and
// break part of the keyboard -- which is why this is gated on the board rather
// than done unconditionally, the way Launcher does it (Launcher's keypad path
// differs, so it can).
constexpr int SD_ENABLE_PIN = 5;

constexpr const char* DIR = "/cardcraft";
constexpr int      HDR_BYTES  = 64;
constexpr uint32_t SLOT_MAGIC = 0x4C534343u;    // "CCSL", little-endian
constexpr uint16_t HDR_VERSION = 1;

// 0 unknown, 1 mounted, 2 no card. The mount is attempted on first use rather
// than in setup(): SPI and FAT both allocate, and on this board the two
// framebuffers take 129,600 of the ~136,000 bytes of internal RAM, so a buffer
// nothing has asked for yet is a boot that might not fit.
uint8_t s_mounted = 0;
Status  s_status  = ST_OK;
int     s_writes  = 0;

void setStatus(Status s) { s_status = s; }

// ---- checksum ---------------------------------------------------------------

constexpr uint32_t CRC_INIT = 0xFFFFFFFFu;

// Bitwise rather than table-driven: a 256-entry table is a kilobyte of flash
// this build does not need to spend for the one place a checksum appears, and
// ninety kilobytes is under a tenth of a second even at eight operations a
// byte.
uint32_t crcFeed(uint32_t c, const uint8_t* p, int n) {
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; ++i)
      c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(0u - (c & 1u)));
  }
  return c;
}
uint32_t crcDone(uint32_t c) { return c ^ 0xFFFFFFFFu; }

// ---- header fields ----------------------------------------------------------

void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void slotPath(int slot, char* out, int cap) {
  snprintf(out, cap, "%s/slot%d.dat", DIR, slot + 1);
}

bool slotInRange(int slot) { return slot >= 0 && slot < SLOT_N; }

// Remade before every write rather than only at mount. Mounting is memoised, so
// a directory removed from under a mounted card -- which a host test does
// deliberately, and a card reader on a desktop can do by accident -- would
// otherwise fail every save for the rest of the boot with nothing to say.
void ensureDir() {
  if (!SD.exists(DIR)) SD.mkdir(DIR);
}

// ---- streaming --------------------------------------------------------------
//
// The two ends of world::Sink / world::Source. Both halves of a save -- the
// world snapshot and the simulation blob -- go through the same instance, so
// the length and the checksum in the header cover the payload as one thing and
// a save that is half of one run and half of another cannot verify.

struct FileSink : world::Sink {
  File*    f   = nullptr;
  uint32_t crc = CRC_INIT;
  int      len = 0;
  int put(const void* p, int n) override {
    const uint8_t* b = (const uint8_t*)p;
    crc = crcFeed(crc, b, n);
    if (f->write(b, (size_t)n) != (size_t)n) return -1;
    len += n;
    return n;
  }
};

struct FileSource : world::Source {
  File*    f   = nullptr;
  uint32_t crc = CRC_INIT;
  int      len = 0;
  int get(void* p, int n) override {
    uint8_t* b = (uint8_t*)p;
    if ((int)f->read(b, (size_t)n) != n) return -1;
    crc = crcFeed(crc, b, n);
    len += n;
    return n;
  }
};

// Reads the payload once without interpreting it, to answer "is this file
// whole" before anything in memory is touched. storage_sd.h promises that a
// failed read leaves nothing usable behind, and a checksum verified after the
// grid had already been overwritten would not be able to keep that promise.
bool verifyPayload(File& f, uint32_t wantLen, uint32_t wantCrc) {
  if (!f.seek(HDR_BYTES)) return false;
  uint8_t buf[512];
  uint32_t crc = CRC_INIT;
  uint32_t n = 0;
  for (;;) {
    const int got = (int)f.read(buf, sizeof buf);
    if (got <= 0) break;
    crc = crcFeed(crc, buf, got);
    n += (uint32_t)got;
  }
  return n == wantLen && crcDone(crc) == wantCrc;
}

}  // namespace

// ---- mounting ---------------------------------------------------------------

bool begin() {
  if (s_mounted) return s_mounted == 1;

  if (M5.getBoard() == m5::board_t::board_M5CardputerADV) {
    pinMode(SD_ENABLE_PIN, OUTPUT);
    digitalWrite(SD_ENABLE_PIN, HIGH);
  }

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  // 25 MHz, then 4. A card that will not train at the fast clock is worth
  // having at the slow one: a save that takes a second is better than a save
  // that does not happen, and nothing here is latency-sensitive.
  bool ok = SD.begin(SD_CS, SPI, 25000000);
  if (!ok) ok = SD.begin(SD_CS, SPI, 4000000);

  if (ok && !SD.exists(DIR)) ok = SD.mkdir(DIR);

  s_mounted = ok ? 1 : 2;
  if (!ok) setStatus(ST_NO_CARD);
  return ok;
}

void reset() { s_mounted = 0; }

bool ready() { return s_mounted == 1; }
Status status() { return s_status; }
int writes() { return s_writes; }

const char* statusText() {
  switch (s_status) {
    case ST_OK:      return "";
    case ST_NO_CARD: return "NO SD CARD";
    case ST_IO:      return "CARD ERROR";
    case ST_CORRUPT: return "SAVE CORRUPT";
    case ST_VERSION: return "DIFFERENT BUILD";
  }
  return "";
}

// ---- slots ------------------------------------------------------------------

bool readMeta(int slot, Meta& m) {
  m = Meta{};
  if (!slotInRange(slot) || !begin()) return false;
  char path[48];
  slotPath(slot, path, sizeof path);
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  uint8_t hdr[HDR_BYTES];
  const bool got = (int)f.read(hdr, HDR_BYTES) == HDR_BYTES;
  f.close();
  // A file the header check refuses is an empty slot as far as the picker is
  // concerned. It is left on the card rather than deleted: the player asked to
  // load a run, not to have it tidied away because this build cannot read it.
  if (!got || get32(hdr + 0) != SLOT_MAGIC || get16(hdr + 4) != HDR_VERSION)
    return false;

  m.used  = true;
  m.night = get16(hdr + 18);
  m.hp    = get16(hdr + 20);
  m.score = get32(hdr + 24);
  m.seed  = get32(hdr + 28);
  return true;
}

bool write(int slot, const game::State& s) {
  if (!slotInRange(slot) || !begin()) return false;
  ensureDir();
  char path[48];
  slotPath(slot, path, sizeof path);

  File f = SD.open(path, FILE_WRITE);
  if (!f) { setStatus(ST_IO); return false; }

  // A blank header over whatever was there, before a byte of payload. If the
  // write is interrupted -- the player switches the board off, the card is
  // pulled -- the file no longer starts with a magic, so it reads as EMPTY
  // rather than as a save that verifies only halfway.
  uint8_t hdr[HDR_BYTES];
  memset(hdr, 0, sizeof hdr);
  put32(hdr + 0, SLOT_MAGIC);
  put16(hdr + 4, HDR_VERSION);
  put16(hdr + 6, (uint16_t)slot);
  if ((int)f.write(hdr, HDR_BYTES) != HDR_BYTES) { f.close(); setStatus(ST_IO); return false; }

  FileSink out;
  out.f = &f;
  const int wn = world::snapshotSave(out);
  const int sn = (wn < 0) ? -1 : game::stateSave(s, out);
  if (sn < 0) {
    f.close();
    SD.remove(path);          // no half-save left behind
    setStatus(ST_IO);
    return false;
  }

  put32(hdr + 8,  (uint32_t)out.len);
  put32(hdr + 12, crcDone(out.crc));
  put16(hdr + 16, game::STATE_BLOB_VERSION);
  put16(hdr + 18, s.night);
  put16(hdr + 20, (uint16_t)s.hp);
  put32(hdr + 24, game::score(s));
  put32(hdr + 28, world::currentSeed());

  bool ok = f.seek(0) && (int)f.write(hdr, HDR_BYTES) == HDR_BYTES;
  f.close();
  if (!ok) { SD.remove(path); setStatus(ST_IO); return false; }

  ++s_writes;
  setStatus(ST_OK);
  return true;
}

bool read(int slot, game::State& s) {
  if (!slotInRange(slot) || !begin()) return false;
  char path[48];
  slotPath(slot, path, sizeof path);

  File f = SD.open(path, FILE_READ);
  if (!f) { setStatus(ST_IO); return false; }

  uint8_t hdr[HDR_BYTES];
  if ((int)f.read(hdr, HDR_BYTES) != HDR_BYTES) { f.close(); setStatus(ST_CORRUPT); return false; }
  if (get32(hdr + 0) != SLOT_MAGIC || get16(hdr + 4) != HDR_VERSION) {
    f.close(); setStatus(ST_CORRUPT); return false;
  }
  if (get16(hdr + 16) != game::STATE_BLOB_VERSION) { f.close(); setStatus(ST_VERSION); return false; }

  const uint32_t wantLen = get32(hdr + 8);
  const uint32_t wantCrc = get32(hdr + 12);
  if (!verifyPayload(f, wantLen, wantCrc)) { f.close(); setStatus(ST_CORRUPT); return false; }
  if (!f.seek(HDR_BYTES)) { f.close(); setStatus(ST_IO); return false; }

  FileSource in;
  in.f = &f;
  const int wn = world::snapshotLoad(in);
  const int sn = (wn < 0) ? -1 : game::stateLoad(s, in);
  f.close();

  // Already verified above, so this is belt and braces: it catches a card that
  // returned different bytes the second time, which is a card that should not
  // be trusted with a run either way.
  if (sn < 0 || (uint32_t)in.len != wantLen || crcDone(in.crc) != wantCrc) {
    setStatus(ST_CORRUPT);
    return false;
  }
  setStatus(ST_OK);
  return true;
}

bool erase(int slot) {
  if (!slotInRange(slot) || !begin()) return false;
  char path[48];
  slotPath(slot, path, sizeof path);
  if (!SD.exists(path)) return true;
  const bool ok = SD.remove(path);
  if (!ok) setStatus(ST_IO);
  return ok;
}

}  // namespace save
