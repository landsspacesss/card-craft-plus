// =============================================================================
//  storage_sd.h — save slots on the SD card
//
//  Three numbered slots under /cardcraft, each one a world snapshot and a
//  simulation blob with a header and a checksum around them. The card is the
//  whole point: the board has one NVS namespace and no room in RAM to keep a
//  second run, and a run is ninety kilobytes.
//
//  This is the only file in src/ that talks to the SD card, and the only one
//  besides hal/ that knows the board's pinout. Everything below the header is
//  streamed straight from the world's own arrays -- see world::Sink, which is
//  why nothing here ever holds a copy of the map.
//
//  A slot file is:
//      0   magic "CCSL", header version, slot index
//      8   payload length, CRC32 of the payload
//      16  simulation version, night, hearts, score, seed
//      64  payload: world::snapshotSave() then game::stateSave()
//
//  The header is what the slot picker reads, and it is read on its own: listing
//  three slots is three 64-byte reads rather than three ninety-kilobyte ones.
// =============================================================================
#pragma once

#include <stdint.h>

#include "game.h"

namespace save {

constexpr int SLOT_N = 3;

// Everything that is in the header, and so everything a picker row can show
// without opening the payload.
struct Meta {
  bool     used  = false;
  uint16_t night = 0;
  uint16_t hp    = 0;
  uint32_t score = 0;
  uint32_t seed  = 0;
};

enum Status : uint8_t {
  ST_OK = 0,
  ST_NO_CARD,    // nothing in the slot, or the card would not mount
  ST_IO,         // card there, read or write failed
  ST_CORRUPT,    // magic or checksum did not survive
  ST_VERSION,    // written by a different build
};

// Mounts the card once and creates /cardcraft. Cheap and safe to call often:
// after the first answer it only reports. False when there is no usable card.
bool begin();

// Forgets the answer, so the next begin() probes again. Called when the picker
// opens: a card that was absent at boot is still absent three reads later, but
// one the player has just pushed in should be found without a reboot.
void reset();
bool ready();
Status status();

// A short phrase for the slot card, or "" once something has succeeded.
const char* statusText();

// Header only.
bool readMeta(int slot, Meta& m);

// Writes the world and the simulation into `slot`. False on any failure, and a
// failed write leaves a file whose magic no longer matches, so it reads as
// empty rather than as a torn save.
bool write(int slot, const game::State& s);

// Reads `slot` back. On false nothing is usable -- the world may hold a partial
// grid -- so the caller must not carry on with the run it was in.
bool read(int slot, game::State& s);

bool erase(int slot);

// Successful writes this boot. Only the dev overlay wants this.
int writes();

}  // namespace save
