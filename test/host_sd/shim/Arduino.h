// Just enough of the Arduino API for src/storage_sd.cpp to compile on the host.
// The point of this directory is to exercise the save format and its failure
// paths against real files, not to test the SD driver -- the pin wiring is the
// one thing this cannot cover, and SAVES.md says so.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define OUTPUT 1
#define HIGH 1
#define LOW 0

inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
