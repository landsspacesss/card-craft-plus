#include "SD.h"
#include "SPI.h"
#include "M5Cardputer.h"

#include <sys/stat.h>
#include <sys/types.h>

long host_write_budget = -1;

SPIClass SPI;
M5Class  M5;
SDClass  SD;

static const char* root() {
  const char* r = getenv("HOST_SD_ROOT");
  return r ? r : "/tmp/host-sd";
}

void host_path(const char* cardPath, char* out, int cap) {
  snprintf(out, (size_t)cap, "%s%s", root(), cardPath);
}

File SDClass::open(const char* path, const char* mode) {
  char p[512];
  host_path(path, p, sizeof p);
  return File(fopen(p, mode));
}

bool SDClass::exists(const char* path) {
  char p[512];
  host_path(path, p, sizeof p);
  struct stat st;
  return stat(p, &st) == 0;
}

bool SDClass::remove(const char* path) {
  char p[512];
  host_path(path, p, sizeof p);
  return ::remove(p) == 0;
}

bool SDClass::mkdir(const char* path) {
  char p[512];
  host_path(path, p, sizeof p);
  return ::mkdir(p, 0755) == 0 || exists(path);
}
