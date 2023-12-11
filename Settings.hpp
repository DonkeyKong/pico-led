#pragma once

#include "hardware/flash.h"
#include <pico/stdlib.h>
#include <pico/unique_id.h>
#include <hardware/sync.h>
#include <vector>
#include <type_traits>
#include <cstring>
#include <stdio.h>
#include "Color.hpp"

#define MAX_BUFFER_LENGTH 10000

struct Settings
{
public:
  uint64_t crc;   // The crc of the settings object
  size_t size = sizeof(Settings);  // The size of the whole settings object
  pico_unique_board_id_t boardId;
  bool autosave;
  int scene;
  float brightness;
  float param;
  uint32_t chain0Count;
  uint32_t chain1Count;
  uint32_t chain2Count;
  uint32_t chain3Count;
  int chain0Offset;
  int chain1Offset;
  int chain2Offset;
  int chain3Offset;
  Vec3f chain0ColorBalance;
  Vec3f chain1ColorBalance;
  Vec3f chain2ColorBalance;
  Vec3f chain3ColorBalance;
  float chain0Gamma;
  float chain1Gamma;
  float chain2Gamma;
  float chain3Gamma;

  // Set all settings to their default values
  void setDefaults();

  // Returns true if all settings are ok, false if any had to be changed 
  bool validateAll(int numScenes);

  // Write the settings object to the last two flash sectors.
  // Updates the size, board ID, and crc as a side effect.
  // Writing it twice ensures there is always one valid copy
  // in the event of power failure during write.
  // Returns false if flash contents is already correct
  bool writeToFlash();

  // Read the settings from flash memory. Returns false
  // if none of the flash sectors with settings were valid
  bool readFromFlash();

  // Print all the settings to cout
  void print();
private:
  // Return true if the flash is actually written
  bool writeToFlashInternal(int sectorOffset);
  size_t structSize() const;
  uint64_t calculateCrc() const;
};

class SettingsManager
{
private:
  static const uint32_t Minimum_Write_Interval_Ms = 15000;
  absolute_time_t nextWriteTime;
  Settings settings;

public:
  SettingsManager(int numScenes);
  Settings& getSettings();
  bool autosave();
};