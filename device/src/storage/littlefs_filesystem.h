#pragma once

#include <LittleFS.h>

#include "arduino_filesystem.h"

class LittleFSFileSystem : public ArduinoFileSystem {
 public:
  LittleFSFileSystem() : ArduinoFileSystem(LittleFS) {}

  bool mount() override { return true; }
  void unmount() override {}
  bool format() override {
    LittleFS.format();
    return LittleFS.begin(false);
  }
};
