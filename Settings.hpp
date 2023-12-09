#pragma once

#include "hardware/flash.h"
#include <pico/stdlib.h>
#include <pico/unique_id.h>
#include <hardware/sync.h>
#include <vector>
#include <cstring>
#include <stdio.h>
#include "Color.hpp"

#define CURRENT_SETTINGS_VERSION 4
#define MAX_BUFFER_LENGTH 10000

bool operator!=(const pico_unique_board_id_t& id1, const pico_unique_board_id_t& id2)
{
  for (int i=0; i < sizeof(pico_unique_board_id_t::id); ++i)
  {
    if (id1.id[i] != id2.id[i]) return true;
  }
  return false;
}

struct Settings
{
  int version;
  pico_unique_board_id_t boardId;
  uint32_t chain0Count;
  bool autosave;
  int scene;
  float brightness;
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
  float param;
};

class SettingsManager
{
private:
  static const uint32_t Minimum_Write_Interval_Ms = 15000;
  static const uint32_t SettingsSize = FLASH_SECTOR_SIZE;
  static const uint32_t SettingsOffsetBytes = 2048 * 1024 - SettingsSize;
  
  absolute_time_t lastWriteTime;
  bool dirty;
  bool writtenOnce;
  Settings current;

public:
  SettingsManager()
  {
    // Read the current settings
    current = *((Settings*)(XIP_BASE+SettingsOffsetBytes));
    dirty = false;
    writtenOnce = false;
    lastWriteTime = get_absolute_time();

    // Detect if this is the first time we are launching or
    // if settings are corrupt and initialize the settings to
    // something good.
    pico_unique_board_id_t boardId;
    pico_get_unique_board_id(&boardId);
    
    bool boardIdInvalid = set(&Settings::boardId, boardId);
    int version = get(&Settings::version);
    setDefaults(boardIdInvalid, version);

    // Validate some settings to stop the system from crashing by trying to allocate too much memory
    bool failedValidation = false;
    failedValidation |= validate(&Settings::brightness, 0.0f, 1.0f, 1.0f);
    failedValidation |= validate(&Settings::chain0Count, 0ul, (uint32_t)MAX_BUFFER_LENGTH, 0ul);
    failedValidation |= validate(&Settings::chain1Count, 0ul, (uint32_t)MAX_BUFFER_LENGTH, 0ul);
    failedValidation |= validate(&Settings::chain2Count, 0ul, (uint32_t)MAX_BUFFER_LENGTH, 0ul);
    failedValidation |= validate(&Settings::chain3Count, 0ul, (uint32_t)MAX_BUFFER_LENGTH, 0ul);
    failedValidation |= validate(&Settings::chain0Offset, 0, MAX_BUFFER_LENGTH-(int)current.chain0Count, 0);
    failedValidation |= validate(&Settings::chain1Offset, 0, MAX_BUFFER_LENGTH-(int)current.chain1Count, 0);
    failedValidation |= validate(&Settings::chain2Offset, 0, MAX_BUFFER_LENGTH-(int)current.chain2Count, 0);
    failedValidation |= validate(&Settings::chain3Offset, 0, MAX_BUFFER_LENGTH-(int)current.chain3Count, 0);
    failedValidation |= validate(&Settings::param, 0.0f, 1.0f, 0.0f);

    if (failedValidation)
    {
      std::cout << "Warning: some settings failed validation and have been reset." << std::endl;
    }

    if (dirty)
    {
      writeToFlash(true);
    }
  }

  void setDefaults(bool invalidateAll, int currentVersion)
  {
    set(&Settings::version, CURRENT_SETTINGS_VERSION);

    if (invalidateAll)
    {
      std::cout << "First launch, reset requested, or corrupted settings detected. Writing defaults..." << std::endl;
      currentVersion = 0;
    }

    if (currentVersion < 1)
    {
      set(&Settings::chain0Count, 1ul);
      set(&Settings::autosave, false);
      set(&Settings::scene, 0);
      set(&Settings::brightness, 1.0f);
    }
    if (currentVersion < 2)
    {
      set(&Settings::chain1Count, 0ul);
      set(&Settings::chain2Count, 0ul);
      set(&Settings::chain3Count, 0ul);
      set(&Settings::chain0Offset, 0);
      set(&Settings::chain1Offset, 0);
      set(&Settings::chain2Offset, 0);
      set(&Settings::chain3Offset, 0);
    }
    if (currentVersion < 3)
    {
      set(&Settings::chain0ColorBalance, {1.0f, 1.0f, 1.0f});
      set(&Settings::chain1ColorBalance, {1.0f, 1.0f, 1.0f});
      set(&Settings::chain2ColorBalance, {1.0f, 1.0f, 1.0f});
      set(&Settings::chain3ColorBalance, {1.0f, 1.0f, 1.0f});
    }
    if (currentVersion < 4)
    {
      set(&Settings::chain0Gamma, 1.0f);
      set(&Settings::chain1Gamma, 1.0f);
      set(&Settings::chain2Gamma, 1.0f);
      set(&Settings::chain3Gamma, 1.0f);
      set(&Settings::param, 0.0f);
    }
  }

  template <typename T>
  T get(T Settings::* fieldPtr) const
  {
    return current.*fieldPtr;
  }

  template <typename T>
  bool set(T Settings::* fieldPtr, T val)
  {
    if (current.*fieldPtr != val)
    {
      current.*fieldPtr = val;
      dirty = true;
      return true;
    }
    return false;
  }

  template <typename T>
  bool validate(T Settings::* fieldPtr, T min, T max, T defaultVal)
  {
    if (current.*fieldPtr < min || current.*fieldPtr > max)
    {
      current.*fieldPtr = defaultVal;
      dirty = true;
      return true;
    }
    return false;
  }

  // Returns true if settings are already up to date or
  // if settings were written. Returns false if settings were
  // not written and the program must try again.
  bool writeToFlash(bool force = false)
  {
    if (!dirty)
    {
      if (force)
      {
        std::cout << "dirty flag not set, skipping write" << std::endl;
      }
      return true;
    }

    if (memcmp(&current, (Settings*)(XIP_BASE+SettingsOffsetBytes), sizeof(Settings)) == 0)
    {
      if (force)
      {
        std::cout << "settings in flash will be unchanged, skipping write and resetting dirty flag" << std::endl;
      }
      dirty = false;
      return true;
    }

    absolute_time_t now = get_absolute_time();
    bool timeToWrite = (to_ms_since_boot(now) - to_ms_since_boot(lastWriteTime)) > Minimum_Write_Interval_Ms;

    if (!writtenOnce || timeToWrite || force)
    {
      std::vector<uint8_t> buffer((sizeof(Settings)/FLASH_PAGE_SIZE + (sizeof(Settings)%FLASH_PAGE_SIZE > 0 ? 1 : 0)) * FLASH_PAGE_SIZE);
      memcpy(buffer.data(), &current, sizeof(Settings));
      writtenOnce = true;
      lastWriteTime = now;
      dirty = false;

      // Write to flash!
      uint32_t ints = save_and_disable_interrupts();
      flash_range_erase(SettingsOffsetBytes, FLASH_SECTOR_SIZE);
      flash_range_program(SettingsOffsetBytes, buffer.data(), buffer.size());
      restore_interrupts(ints);

      std::cout << "writing settings to flash..." << std::endl;

      return true;
    }

    return false;
  }
};