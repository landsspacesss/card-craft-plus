// A File that is a FILE*, rooted at $HOST_SD_ROOT. Enough of the SD library to
// be honest about the save layer's behaviour: short writes, short reads, and a
// seek back to rewrite the header.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SPIClass;

#define FILE_READ  "r"
#define FILE_WRITE "w"

// Set by the test to make the next write stop early, which is what a card
// pulled out mid-save looks like from up here.
extern long host_write_budget;

class File {
 public:
  File() {}
  explicit File(FILE* f) : f_(f) {}
  operator bool() const { return f_ != nullptr; }

  size_t write(const uint8_t* p, size_t n) {
    if (!f_) return 0;
    if (host_write_budget >= 0) {
      if ((long)n > host_write_budget) n = (size_t)host_write_budget;
      host_write_budget -= (long)n;
    }
    return fwrite(p, 1, n, f_);
  }
  size_t read(uint8_t* p, size_t n) { return f_ ? fread(p, 1, n, f_) : 0; }
  bool   seek(uint32_t pos) { return f_ && fseek(f_, (long)pos, SEEK_SET) == 0; }
  void   close() { if (f_) { fclose(f_); f_ = nullptr; } }

 private:
  FILE* f_ = nullptr;
};

class SDClass {
 public:
  // The card is always present on the host; begin() succeeding is the whole of
  // what the save layer asks of it.
  bool begin(uint8_t, SPIClass&, uint32_t) { return true; }
  void end() {}
  File open(const char* path, const char* mode);
  bool exists(const char* path);
  bool remove(const char* path);
  bool mkdir(const char* path);
};
extern SDClass SD;

// Builds the on-disk path for an absolute card path like "/cardcraft/slot1.dat".
// Exposed for the test so it can inspect and damage the files directly.
void host_path(const char* cardPath, char* out, int cap);
