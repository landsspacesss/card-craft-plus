#pragma once
struct SPIClass {
  void begin(int, int, int, int) {}
  void begin() {}
};
extern SPIClass SPI;
