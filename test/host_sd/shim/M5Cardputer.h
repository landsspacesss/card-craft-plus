#pragma once
#include <stdint.h>

namespace m5 {
enum board_t { board_M5Cardputer, board_M5CardputerADV };
}

struct M5Class {
  m5::board_t board = m5::board_M5CardputerADV;
  m5::board_t getBoard() const { return board; }
};
extern M5Class M5;
