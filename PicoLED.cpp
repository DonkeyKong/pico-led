#include "Scene.hpp"
#include "Settings.hpp"

#include <cpp/BatteryMonitor.hpp>
#include <cpp/BootSelButton.hpp>
#include <cpp/Button.hpp>
#include <cpp/Color.hpp>
#include <cpp/DiscreteOut.hpp>
#include <cpp/CommandParser.hpp>
#include <cpp/FlashStorage.hpp>
#include <cpp/LedStripWs2812b.hpp>
#include <cpp/Logging.hpp>

#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <pico/bootrom.h>
#include <pico/unique_id.h>
#include <hardware/watchdog.h>

#include <iostream>
#include <cmath>
#include <memory>

constexpr uint64_t TargetFPS = 20;
constexpr uint64_t TargetFrameTimeUs = 1000000 / TargetFPS;
constexpr float TargetFrameTimeSec = 1.0f / (float)TargetFPS;

constexpr float MinBrightness = 0.1;
constexpr float MaxBrightness = 1.0;

inline float roundToInterval(float val, float interval)
{
  return std::round(val / interval) * interval;
}

void rebootIntoProgMode(uint32_t displayBufferSize, std::vector<LedStripWs2812b::BufferMapping>& mappings)
{
  // Flash thru a rainbow to indicate programming mode
  LEDBuffer red(displayBufferSize);
  LEDBuffer black(displayBufferSize);
  for (int i=0; i < red.size(); ++i)
  {
    red[i] = {255, 0, 0};
  }

  // Flash red 3x
  LedStripWs2812b::writeColorsParallel(black, mappings, 0.5f);
  sleep_until(make_timeout_time_ms(200));
  LedStripWs2812b::writeColorsParallel(red, mappings, 0.5f);
  sleep_until(make_timeout_time_ms(100));
  LedStripWs2812b::writeColorsParallel(black, mappings, 0.5f);
  sleep_until(make_timeout_time_ms(200));
  LedStripWs2812b::writeColorsParallel(red, mappings, 0.5f);
  sleep_until(make_timeout_time_ms(100));
  LedStripWs2812b::writeColorsParallel(black, mappings, 0.5f);
  sleep_until(make_timeout_time_ms(200));
  LedStripWs2812b::writeColorsParallel(red, mappings, 0.5f);
  sleep_until(make_timeout_time_ms(100));
  LedStripWs2812b::writeColorsParallel(black, mappings, 0.5f);
  sleep_until(make_timeout_time_ms(200));

  // Reboot
  reset_usb_boot(0,0);
}

Vec3f indexToPosition(int index)
{
  // Each loop has an 8cm radius and is centered about 10cm from the lamp center
  const float loopRadius = 8.0f;
  const float loopOffsetR = 10.0f;
  const float loopOffsetZ = 8.0f;

  // This is easiest done in cylindrical coordinates
  const float angleOffset = -(8.0f / 13.0f * (float)M_PI) / 3.0f;
  float theta = remap(index, 0, 78, 0.0f, (float)M_PI * 2.0f) + angleOffset;
  float r = std::cos(3.0f*theta) * loopRadius + loopOffsetR;
  float z = std::sin(3.0f*theta) * loopRadius + loopOffsetZ;

  // Convert to X, Y, Z
  return {std::cos(theta) * r, std::sin(theta) * r, z};
}

int main()
{
  // Configure stdio
  stdio_init_all();
  
  BatteryMonitor battery(26, 5.55f, 9.0f, 12.6f);
  MovingAverage<20> batteryLevel(battery.readingToPercentage(battery.read()));
  DiscreteOut powerSaveDisable(23, false, false, false);
  powerSaveDisable.set(true);

  // Setup the LED strip hardware
  LEDBuffer drawBuffer;
  LedStripWs2812b chain0(0);
  LedStripWs2812b chain1(2);
  LedStripWs2812b chain2(3);
  LedStripWs2812b chain3(4);
  std::vector<LedStripWs2812b::BufferMapping> mappings { {&chain0}, {&chain1}, {&chain2}, {&chain3} };
  std::vector<Vec3f> spatialLut;


  // Setup the buttons
  GPIOButton sceneBrightnessButton(1, true);
  GPIOButton paramButton(15, true);
  BootSelButton bootSelButton;
  
  // Setup other loop vars
  bool halt = false;
  bool firstFrame = true;
  absolute_time_t nextFrameTime = get_absolute_time();

  // Set up the various scenes
  Scene warmWhiteScene("WarmWhite", [&](LEDBuffer& buffer, float /* deltaTime */, float param, bool /* firstFrame */)
  {
    float colorTempK = (param*7000.0f) + 2000.0f;
    auto color = GetColorFromTemperature(colorTempK);
    for (int i = 0; i < buffer.size(); ++i)
    {
      buffer[i] = color;
    }
  });

  // Set up the various scenes
  Scene shapedWarmScene("ShapedWarm", [&](LEDBuffer& buffer, float /* deltaTime */, float param, bool /* firstFrame */)
  {
    auto color = GetColorFromTemperature(2800.0f);
    constexpr float horizScale = 2.0f * (float)M_PI / 16.0f;
    for (int i = 0; i < buffer.size(); ++i)
    {
      float atten = std::pow(0.45f * std::sin((spatialLut[i].Z + param * 16.0f)  * horizScale) + 0.55f, 2.0f);
      buffer[i] = color * atten;
    }
  });

  Scene pureColorScene("PureColor", [&](LEDBuffer& buffer, float /* deltaTime */, float param, bool /* firstFrame */)
  {
    float hue = param * 360.0f;
    auto color = HSVColor{hue, 1.0f, 1.0f}.toRGB();
    for (int i = 0; i < buffer.size(); ++i)
    {
      buffer[i] = color;
    }
  });

  Scene gamerRgbScene("GamerRGB", [&](LEDBuffer& buffer, float deltaTime, float param, bool /* firstFrame */)
  {
    static float t = 0.0f;
    float tMax = param * 19.0f + 1.0f;
    t = fmodf(t + deltaTime, tMax);
    if (isnanf(t))
    {
      t = 0;
    }
    float baseHue = t * (360.0f / tMax);
    for (int i = 0; i < buffer.size(); ++i)
    {
      float locationOffsetHue = (float)i * (360.0f / (float)buffer.size());
      buffer[i] = HSVColor{ fmodf(baseHue + locationOffsetHue, 360.0f) , 1.0f, 1.0f }.toRGB();
    }
  });

  Scene chaseScene("Chase", [&](LEDBuffer& buffer, float deltaTime, float param, bool /* firstFrame */)
  {
    constexpr float width = 1.5f;
    static float t = 0.0f;
    t = t + param * 0.5f;
    while (t >= (float)buffer.size())
    {
      t -= (float)buffer.size();
    }

    for (int i = 0; i < buffer.size(); ++i)
    {
      auto color = RGBColor{255,255,255};
      float dist = std::min(
        std::abs((float)i - t),
        std::abs(std::abs((float)i - t) - (float)buffer.size()));
      if (dist < width)
      {
        buffer[i] = {255,255,255};
      }
      else if (dist < (width + 1.0f))
      {
        float atten = (width - dist + 1.0f);
        buffer[i] = color * atten;
      }
      else
      {
        buffer[i] = {0,0,0};
      }
    }
  });

  Scene batteryLevelScene("BatteryLevel", [&](LEDBuffer& buffer, float deltaTime, float param, bool firstFrame)
  {
    if (firstFrame)
    {
      batteryLevel.clear();
    }

    float z = remapClamp((float)batteryLevel, 0.0f, 100.0f, 1.0f, 16.0f);

    for (int i = 0; i < drawBuffer.size(); ++i)
    {
      if (spatialLut[i].Z <= z)
      {
        if (spatialLut[i].Z <= 1.0)
        {
          drawBuffer[i] = {255, 0, 0};
        }
        else if (spatialLut[i].Z <= 3.0)
        {
          drawBuffer[i] = {255, 255, 0};
        }
        else
        {
          drawBuffer[i] = {0, 255, 0};
        }
      }
      else
      {
        drawBuffer[i] = {0,0,0};
      }
    }
  });

  // Init the settings object
  FlashStorage<Settings> settingsMgr;
  Settings& settings = settingsMgr.data;
  if (!settingsMgr.readFromFlash())
  {
    settings.setDefaults();
  }
  settings.validateAll();
  settings.updateCalibrations(chain0, chain1, chain2, chain3);
  settings.updateMappings(mappings, drawBuffer, spatialLut, indexToPosition);

  absolute_time_t dirtySaveTime = 0;
  absolute_time_t cooldownTimer = get_absolute_time();

  auto markSettingsDirty = [&]()
  {
    dirtySaveTime = make_timeout_time_ms(2000);
  };

  auto tryAutosave = [&](bool ignoreCooldowns = false)
  {
    if (settings.autosave)
    {
      if (ignoreCooldowns || (dirtySaveTime != 0 && time_reached(dirtySaveTime) && time_reached(cooldownTimer)))
      {
        if (settingsMgr.writeToFlash())
        {
          DEBUG_LOG("Autosaved settings to flash!");
          cooldownTimer = make_timeout_time_ms(15000);
        }
        dirtySaveTime = 0;
      }
    }
  };

  // With everything else setup, create the command parser
  CommandParser parser;

  parser.addCommand("info", "", "Print information about this system", [&]()
  {
    std::cout << "PicoLED by Donkey Kong" << std::endl;
    std::cout << "https://github.com/DonkeyKong/pico-led" << std::endl;
    std::cout << std::endl;
    settings.print();
    std::cout << std::endl;
    std::cout << "Runtime Data:" << std::endl;
    std::cout << "    " << "status:    " << (halt ? "halted" : "running") << std::endl;
    std::cout << "    " << "scene count:    " << Scenes.size() << std::endl;
    std::cout << "    " << "scene names:";
    for (auto& name : SceneNames) std::cout << "    " << name;
    std::cout << std::endl;
    std::cout << "    " << "draw buffer size:    " << drawBuffer.size() << std::endl;
    std::cout << "    " << "max draw buffer size:    " << MAX_BUFFER_LENGTH << std::endl;
    std::cout << "    " << "target fps:    " << TargetFPS << std::endl;
    std::cout;
  });

  parser.addCommand("count", "[strip-id] [num-leds]", "Set number of LEDs per strip", [&](int id, uint32_t count)
  {
      if (count < 0 || count > MAX_BUFFER_LENGTH) 
      {
        std::cout << "error bad count" << std::endl; 
        return false;
      }
      switch (id)
      {
        case 0: settings.chain0Count = count; break;
        case 1: settings.chain1Count = count; break;
        case 2: settings.chain2Count = count; break;
        case 3: settings.chain3Count = count; break;
        default: std::cout << "error bad strip id" << std::endl; return false;
      }

      std::cout << "strip " << id << " count set: " << count << std::endl;
      settings.updateMappings(mappings, drawBuffer, spatialLut, indexToPosition);
      markSettingsDirty();
      return true;
  });

  parser.addCommand("offset", "[strip-id] [offset]", "Set offset relative to other LED strips)", [&](int id, int offset)
  {
      if (offset < 0 || offset > MAX_BUFFER_LENGTH) 
      {
        std::cout << "error bad offset" << std::endl; 
        return false;
      }
      switch (id)
      {
        case 0: settings.chain0Offset = offset; break;
        case 1: settings.chain1Offset = offset; break;
        case 2: settings.chain2Offset = offset; break;
        case 3: settings.chain3Offset = offset; break;
        default: std::cout << "error bad strip id" << std::endl; return false;
      }
      std::cout << "strip " << id << " offset set: " << offset << std::endl;
      settings.updateMappings(mappings, drawBuffer, spatialLut, indexToPosition);
      markSettingsDirty();
      return true;
  });

  parser.addCommand("color", "[strip-id] [red-atten] [green-atten] [blue-atten]", "Set LED strip color balance", [&](int id, float r, float g, float b)
  {
    switch (id)
    {
      case 0: settings.chain0ColorBalance = {r, g, b}; break;
      case 1: settings.chain1ColorBalance = {r, g, b}; break;
      case 2: settings.chain2ColorBalance = {r, g, b}; break;
      case 3: settings.chain3ColorBalance = {r, g, b}; break;
      default: std::cout << "error bad strip id" << std::endl; return;
    }
    std::cout << "chain " << id << " color balance set: " << r << ", " << g << ", " << b << std::endl;
    settings.updateCalibrations(chain0, chain1, chain2, chain3);
    markSettingsDirty();
  });
  
  parser.addCommand("gamma", "[strip-id] [gamma]", "Set LED strip gamma correction", [&](int id, float gamma)
  {
      switch (id)
      {
        case 0: settings.chain0Gamma = gamma; break;
        case 1: settings.chain1Gamma = gamma; break;
        case 2: settings.chain2Gamma = gamma; break;
        case 3: settings.chain3Gamma = gamma; break;
        default: std::cout << "error bad strip id" << std::endl; return false;
      }
      std::cout << "chain " << id << " gamma set: " << gamma << std::endl;
      settings.updateCalibrations(chain0, chain1, chain2, chain3);
      markSettingsDirty();
      return true;
  });

  parser.addCommand("scene", "[scene-id]", "Change current lighting scene", [&](int scene)
  {
    settings.scene = scene;
    firstFrame = true;
    std::cout << "scene set: " << settings.scene << std::endl;
    markSettingsDirty();
  });

  parser.addCommand("brightness", "[brightness]", "Change maximum brightness", [&](float brightness)
  {
    settings.brightness = brightness;
    std::cout << "brightness set: " << settings.brightness << std::endl;
    markSettingsDirty();
  });
  
  parser.addCommand("param", "[float-value]", "Change scene-specific parameter", [&](float param)
  {
    settings.setCurrentParam(param);
    std::cout << "param["<< settings.scene << "] set: " << settings.getCurrentParam() << std::endl;
    markSettingsDirty();
  });

  parser.addCommand("autosave", "[0 or 1]", "Enable/Disable autosave of settings", [&](bool autosave)
  {
    settings.autosave = autosave;
    std::cout << "autosave set: " << (settings.autosave ? 1 : 0) << std::endl;
    markSettingsDirty();
  });

  parser.addCommand("defaults", "", "Restore all settings to their factory state", [&]()
  {
    settings.setDefaults();
    settings.updateMappings(mappings, drawBuffer, spatialLut, indexToPosition);
    settings.updateCalibrations(chain0, chain1, chain2, chain3);
    markSettingsDirty();
  });
  
  parser.addCommand("flash", "", "Save current settings to flash", [&]()
  {
    // Write the settings to flash
    if (settingsMgr.writeToFlash())
      std::cout << "Wrote settings to flash!" << std::endl;
    else
      std::cout << "Skipped writing to flash because contents were already correct." << std::endl;
  });

  parser.addCommand("poke", "[index] [r] [g] [b]", "Set the RGB color of a single LED", [&](int i, uint r, uint g, uint b)
  {
    if (i >= 0 && i < drawBuffer.size())
    {
      drawBuffer[i] = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
    }
    else
    {
      std::cout << "error invalid index" << std::endl;
      return false;
    }
    markSettingsDirty();
    return true;
  });

  parser.addCommand("fill", "[r] [g] [b]", "Set the RGB color all LEDs", [&](uint r, uint g, uint b)
  {
    std::fill(drawBuffer.begin(), drawBuffer.end(), RGBColor{(uint8_t)r, (uint8_t)g, (uint8_t)b});
  });

  parser.addCommand("fillr", "[begin] [end] [r] [g] [b]", "Set the RGB color value of LEDs within a range", [&](int begin, int end, uint r, uint g, uint b)
  {
    if (begin >= 0 && begin < drawBuffer.size() && end >= 0 && end < drawBuffer.size() && begin <= end)
    {
      std::fill(drawBuffer.begin()+begin, drawBuffer.begin()+end, RGBColor{(uint8_t)r, (uint8_t)g, (uint8_t)b});
    }
    else
    {
      std::cout << "error invalid index" << std::endl;
      return false;
    }
    return true;
  });

  parser.addCommand("grad", "[r1] [g1] [b1] [r2] [g2] [b2]", "Draw a gradient across all chains", [&](uint r1, uint g1, uint b1, uint r2, uint g2, uint b2)
  {
    RGBColor color1{(uint8_t)r1, (uint8_t)g1, (uint8_t)b1};
    RGBColor color2{(uint8_t)r2, (uint8_t)g2, (uint8_t)b2};
    for (int i=0; i < drawBuffer.size(); ++i)
    {
      float t = (float) i / (float) (drawBuffer.size()-1);
      drawBuffer[i] = RGBColor::blend(color1, color2, t);
    }
  });

  parser.addCommand("dump", "", "Print the whole color buffer to stdout", [&]()
  {
    std::cout << "Dumping display buffer..." << std::endl;
    for (int i=0; i < drawBuffer.size(); ++i)
    {
      std::cout << "idx " << i << " (" << (int)drawBuffer[i].R << " , " 
                                       << (int)drawBuffer[i].G << " , " 
                                       << (int)drawBuffer[i].B << " )" << std::endl;
    }
    std::cout << "End of display buffer" << std::endl;
  });

  parser.addCommand("halt", "", "Stop scenes, allow manual drawing", [&]()
  {
    halt = true;
  });

  parser.addCommand("resume", "", "Resume scenes, override manual drawing", [&]()
  {
    halt = false;
  });
  
  parser.addCommand("reboot", "", "Reboot the microcontroller right away", [&]()
  {
    tryAutosave(true);
    watchdog_reboot(0,0,0);
  });
  
  parser.addCommand("prog", "", "Reboot to pi pico bootloader for firmware programming", [&]()
  {
    tryAutosave(true);
    std::cout << "Rebooting into programming mode..." << std::endl;
    rebootIntoProgMode(drawBuffer.size(), mappings);
  });

  parser.addCommand("batt", "", "Reboot to pi pico bootloader for firmware programming", [&]()
  {
    auto reading = battery.read();
    std::cout << "Battery Status: " << (int)battery.readingToPercentage(reading) << "% (" << battery.readingToVoltage(reading) << " V)" << std::endl;
  });

  while (1)
  {
    // Wait
    sleep_until(nextFrameTime);
    nextFrameTime = make_timeout_time_us(TargetFrameTimeUs);

    // Process input
    parser.processStdIo();

    batteryLevel.update(battery.readingToPercentage(battery.read()));

    paramButton.update();
    if (paramButton.heldActivate())
    {
      float param = settings.getCurrentParam() + (0.2f * TargetFrameTimeSec);
      if (param > 1.0f ) param = 0.0f;
      settings.setCurrentParam(param);
      markSettingsDirty();
    }
    if (paramButton.buttonUp())
    {
      float param = roundToInterval(settings.getCurrentParam() + 0.1f, 0.1f);
      if (param > 1.0f ) param = 0.0f;
      settings.setCurrentParam(param);
      DEBUG_LOG("param[" << settings.scene << "] set: " << settings.getCurrentParam());
      markSettingsDirty();
    }

    sceneBrightnessButton.update();
    if (sceneBrightnessButton.heldActivate())
    {
      float brightness = settings.brightness - (0.2f * TargetFrameTimeSec);
      if (brightness < MinBrightness ) brightness = MaxBrightness;
      settings.brightness = brightness;
      markSettingsDirty();
    }
    if (sceneBrightnessButton.buttonUp())
    {
      settings.scene = (settings.scene + 1) % (int)Scenes.size();
      firstFrame = true;
      DEBUG_LOG("scene set: " << settings.scene);
      markSettingsDirty();
    }
    
    bootSelButton.update();
    if (bootSelButton.pressed())
    {
      tryAutosave(true);
      rebootIntoProgMode(drawBuffer.size(), mappings);
    }

    // If configured to autosave, try to write settings to flash
    // every frame. It'll only actually do it if the flash payload
    // has changed and even then only once every 15 seconds.
    tryAutosave();

    // Update and draw
    if (!halt) Scenes[settings.scene]->update(drawBuffer, TargetFrameTimeSec, settings.getCurrentParam(), firstFrame);
    firstFrame = false;
    LedStripWs2812b::writeColorsParallel(drawBuffer, mappings, settings.brightness);
  }
  return 0;
}
