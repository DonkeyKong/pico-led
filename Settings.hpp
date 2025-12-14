#pragma once

#include <cpp/Color.hpp>
#include <cpp/LedStripWs2812b.hpp>
#include "Scene.hpp"

#include "hardware/flash.h"
#include <pico/stdlib.h>
#include <pico/unique_id.h>
#include <hardware/sync.h>

#include <vector>
#include <type_traits>
#include <cstring>
#include <stdio.h>

#define MAX_BUFFER_LENGTH 10000

template <typename T>
bool validate(T& field, T min, T max, T defaultVal)
{
  if (field < min || field > max)
  {
    field = defaultVal;
    return true;
  }
  return false;
}

struct Settings
{
public:
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
  void setDefaults()
  {
    autosave = false;
    scene = 0;
    brightness = 1.0f;
    param = 0.0f;
    chain0Count = 1ul;
    chain1Count = 0ul;
    chain2Count = 0ul;
    chain3Count = 0ul;
    chain0Offset = 0;
    chain1Offset = 0;
    chain2Offset = 0;
    chain3Offset = 0;
    chain0ColorBalance = {1.0f, 1.0f, 1.0f};
    chain1ColorBalance = {1.0f, 1.0f, 1.0f};
    chain2ColorBalance = {1.0f, 1.0f, 1.0f};
    chain3ColorBalance = {1.0f, 1.0f, 1.0f};
    chain0Gamma = 1.0f;
    chain1Gamma = 1.0f;
    chain2Gamma = 1.0f;
    chain3Gamma = 1.0f;
  }

  // Returns true if all settings are ok, false if any had to be changed 
  bool validateAll()
  {
    // Validate some settings to stop the system from crashing by trying to allocate too much memory
    bool failedValidation = false;
    failedValidation |= validate(scene, 0, (int)Scenes.size()-1, 0);
    failedValidation |= validate(brightness, 0.0f, 1.0f, 1.0f);
    failedValidation |= validate(param, 0.0f, 1.0f, 0.0f);
    failedValidation |= validate(chain0Count, 0ul, (uint32_t)MAX_BUFFER_LENGTH, 1ul);
    failedValidation |= validate(chain1Count, 0ul, (uint32_t)MAX_BUFFER_LENGTH, 0ul);
    failedValidation |= validate(chain2Count, 0ul, (uint32_t)MAX_BUFFER_LENGTH, 0ul);
    failedValidation |= validate(chain3Count, 0ul, (uint32_t)MAX_BUFFER_LENGTH, 0ul);
    failedValidation |= validate(chain0Offset, 0, MAX_BUFFER_LENGTH-(int)chain0Count, 0);
    failedValidation |= validate(chain1Offset, 0, MAX_BUFFER_LENGTH-(int)chain1Count, 0);
    failedValidation |= validate(chain2Offset, 0, MAX_BUFFER_LENGTH-(int)chain2Count, 0);
    failedValidation |= validate(chain3Offset, 0, MAX_BUFFER_LENGTH-(int)chain3Count, 0);
    return !failedValidation;
  }

  // Print all the settings to cout
  void print()
  {
    std::cout << "Settings:" << std::endl;
    std::cout << "    " << "autosave:    " << autosave << std::endl;
    std::cout << "    " << "scene:    " << scene << std::endl;
    std::cout << "    " << "brightness:    " << brightness << std::endl;
    std::cout << "    " << "param:    " << param << std::endl;

    std::cout << "    " << "chain0Count:    " << chain0Count << std::endl;
    std::cout << "    " << "chain0Offset:    " << chain0Offset << std::endl;
    std::cout << "    " << "chain0ColorBalance:    " << "( " 
                      << chain0ColorBalance.X << " , " 
                      << chain0ColorBalance.Y << " , " 
                      << chain0ColorBalance.Z << " )" << std::endl;
    std::cout << "    " << "chain0Gamma:    " << chain0Gamma << std::endl;

    std::cout << "    " << "chain1Count:    " << chain1Count << std::endl;
    std::cout << "    " << "chain1Offset:    " << chain1Offset << std::endl;
    std::cout << "    " << "chain1ColorBalance:    " << "( " 
                      << chain1ColorBalance.X << " , " 
                      << chain1ColorBalance.Y << " , " 
                      << chain1ColorBalance.Z << " )" << std::endl;
    std::cout << "    " << "chain1Gamma:    " << chain1Gamma << std::endl;

    std::cout << "    " << "chain2Count:    " << chain2Count << std::endl;
    std::cout << "    " << "chain2Offset:    " << chain2Offset << std::endl;
    std::cout << "    " << "chain2ColorBalance:    " << "( " 
                      << chain2ColorBalance.X << " , " 
                      << chain2ColorBalance.Y << " , " 
                      << chain2ColorBalance.Z << " )" << std::endl;
    std::cout << "    " << "chain2Gamma:    " << chain2Gamma << std::endl;

    std::cout << "    " << "chain3Count:    " << chain3Count << std::endl;
    std::cout << "    " << "chain3Offset:    " << chain3Offset << std::endl;
    std::cout << "    " << "chain3ColorBalance:    " << "( " 
                      << chain3ColorBalance.X << " , " 
                      << chain3ColorBalance.Y << " , " 
                      << chain3ColorBalance.Z << " )" << std::endl;
    std::cout << "    " << "chain3Gamma:    " << chain3Gamma << std::endl << std::flush;
  }

  void updateCalibrations(LedStripWs2812b& chain0, LedStripWs2812b& chain1, LedStripWs2812b& chain2, LedStripWs2812b& chain3)
  {
    chain0.colorBalance(chain0ColorBalance);
    chain1.colorBalance(chain1ColorBalance);
    chain2.colorBalance(chain2ColorBalance);
    chain3.colorBalance(chain3ColorBalance);
    chain0.gamma(chain0Gamma);
    chain1.gamma(chain1Gamma);
    chain2.gamma(chain2Gamma);
    chain3.gamma(chain3Gamma);
  }

  void updateMappings(std::vector<LedStripWs2812b::BufferMapping>& mappings, LEDBuffer& drawBuffer)
  {
    // Refresh the scene mappings
    mappings[0].size = (int)chain0Count;
    mappings[1].size = (int)chain1Count;
    mappings[2].size = (int)chain2Count;
    mappings[3].size = (int)chain3Count;
    mappings[0].offset = (int)chain0Offset;
    mappings[1].offset = (int)chain1Offset;
    mappings[2].offset = (int)chain2Offset;
    mappings[3].offset = (int)chain3Offset;

    int drawBufSize = 0;
    drawBufSize = std::max(drawBufSize, (int)chain0Count + chain0Offset);
    drawBufSize = std::max(drawBufSize, (int)chain1Count + chain1Offset);
    drawBufSize = std::max(drawBufSize, (int)chain2Count + chain2Offset);
    drawBufSize = std::max(drawBufSize, (int)chain3Count + chain3Offset);
    drawBufSize = std::min(drawBufSize, MAX_BUFFER_LENGTH);

    drawBuffer.resize(drawBufSize);
  }

};
