#pragma once

#include <SD.h>
#include <SPI.h>

#include "arduino_filesystem.h"

class SDCardFileSystem : public ArduinoFileSystem {
 public:
  SDCardFileSystem() : ArduinoFileSystem(SD) {}

  bool mount() override { return SD.begin(CS_PIN, SPI, SPI_FREQ); }
  void unmount() override { SD.end(); }
  bool format() override { return false; }

 private:
  static constexpr uint8_t CS_PIN = 4;
  static constexpr uint32_t SPI_FREQ = 25000000;
};
