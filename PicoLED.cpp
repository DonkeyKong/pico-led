#include "PioProgram.hpp"
#include "Button.hpp"
#include "Color.hpp"
#include "Scene.hpp"
#include "Settings.hpp"

#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <pico/bootrom.h>
#include <pico/multicore.h>
#include <pico/unique_id.h>
#include <hardware/watchdog.h>

#include <iostream>
#include <cmath>
#include <memory>
#include <sstream>

constexpr uint64_t TargetFPS = 20;
constexpr uint64_t TargetFrameTimeUs = 1000000 / TargetFPS;
constexpr float TargetFrameTimeSec = 1.0f / (float)TargetFPS;
using SceneCollection = std::vector<std::unique_ptr<Scene>>;

LEDBuffer drawBuffer;
Ws2812bOutput chain0 = Ws2812bOutput::create(22);
Ws2812bOutput chain1 = Ws2812bOutput::create(26);
Ws2812bOutput chain2 = Ws2812bOutput::create(27);
Ws2812bOutput chain3 = Ws2812bOutput::create(28);

std::vector<Ws2812bOutput::BufferMapping> mappings { {&chain0}, {&chain1}, {&chain2}, {&chain3} };

int drawBufSize = 0;
std::vector<std::unique_ptr<Scene>> scenes;
bool halt = false;

inline float roundToInterval(float val, float interval)
{
  return std::round(val / interval) * interval;
}

void updateCalibrationsFromSettings(const SettingsManager& settings)
{
  chain0.colorBalance(settings.get(&Settings::chain0ColorBalance));
  chain1.colorBalance(settings.get(&Settings::chain1ColorBalance));
  chain2.colorBalance(settings.get(&Settings::chain2ColorBalance));
  chain3.colorBalance(settings.get(&Settings::chain3ColorBalance));
  chain0.gamma(settings.get(&Settings::chain0Gamma));
  chain1.gamma(settings.get(&Settings::chain1Gamma));
  chain2.gamma(settings.get(&Settings::chain2Gamma));
  chain3.gamma(settings.get(&Settings::chain3Gamma));
}

void updateMappingsFromSettings(const SettingsManager& settings)
{
  // Refresh the scene mappings
  mappings[0].size = (int)settings.get(&Settings::chain0Count);
  mappings[1].size = (int)settings.get(&Settings::chain1Count);
  mappings[2].size = (int)settings.get(&Settings::chain2Count);
  mappings[3].size = (int)settings.get(&Settings::chain3Count);
  mappings[0].offset = (int)settings.get(&Settings::chain0Offset);
  mappings[1].offset = (int)settings.get(&Settings::chain1Offset);
  mappings[2].offset = (int)settings.get(&Settings::chain2Offset);
  mappings[3].offset = (int)settings.get(&Settings::chain3Offset);

  drawBufSize = std::max(drawBufSize, (int)settings.get(&Settings::chain0Count) + settings.get(&Settings::chain0Offset));
  drawBufSize = std::max(drawBufSize, (int)settings.get(&Settings::chain1Count) + settings.get(&Settings::chain1Offset));
  drawBufSize = std::max(drawBufSize, (int)settings.get(&Settings::chain2Count) + settings.get(&Settings::chain2Offset));
  drawBufSize = std::max(drawBufSize, (int)settings.get(&Settings::chain3Count) + settings.get(&Settings::chain3Offset));
  drawBufSize = std::min(drawBufSize, MAX_BUFFER_LENGTH);

  drawBuffer.resize(drawBufSize);
}

void rebootIntoProgMode(uint32_t ledCount)
{
  // Flash thru a rainbow to indicate programming mode
  LEDBuffer red(ledCount);
  LEDBuffer black(ledCount);
  for (int i=0; i < red.size(); ++i)
  {
    red[i] = {128, 0, 0};
  }

  // Flash red 3x
  chain0.writeColors(black);
  sleep_until(make_timeout_time_ms(200));
  chain0.writeColors(red);
  sleep_until(make_timeout_time_ms(100));
  chain0.writeColors(black);
  sleep_until(make_timeout_time_ms(200));
  chain0.writeColors(red);
  sleep_until(make_timeout_time_ms(100));
  chain0.writeColors(black);
  sleep_until(make_timeout_time_ms(200));
  chain0.writeColors(red);
  sleep_until(make_timeout_time_ms(100));
  chain0.writeColors(black);
  sleep_until(make_timeout_time_ms(100));

  // Reboot
  multicore_reset_core1();
  reset_usb_boot(0,0);
}

void processCommand(std::string cmdAndArgs, SettingsManager& settings)
{
  std::stringstream ss(cmdAndArgs);
  std::string cmd;
  ss >> cmd;
  if (cmd == "count")
  {
    int id;
    uint32_t val;
    uint32_t Settings::* prop;
    ss >> id;
    ss >> val;
    if (!ss.fail())
    {
      switch (id)
      {
        case 0: prop = &Settings::chain0Count; break;
        case 1: prop = &Settings::chain1Count; break;
        case 2: prop = &Settings::chain2Count; break;
        case 3: prop = &Settings::chain3Count; break;
        default: std::cout << "error bad chain id" << std::endl << std::flush; return;
      }
      settings.set(prop, val);
      std::cout << "chain " << id << " count set: " << settings.get(prop) << std::endl << std::flush;
      updateMappingsFromSettings(settings);
    }
  }
  else if (cmd == "offset")
  {
    int id;
    int val;
    int Settings::* prop;
    ss >> id;
    ss >> val;
    if (!ss.fail())
    {
      switch (id)
      {
        case 0: prop = &Settings::chain0Offset; break;
        case 1: prop = &Settings::chain1Offset; break;
        case 2: prop = &Settings::chain2Offset; break;
        case 3: prop = &Settings::chain3Offset; break;
        default: std::cout << "error bad chain id" << std::endl << std::flush; return;
      }
      if (val < 0 || val > MAX_BUFFER_LENGTH) std::cout << "error bad offset" << std::endl << std::flush; return;
      settings.set(prop, val);
      std::cout << "chain " << id << " offset set: " << settings.get(prop) << std::endl << std::flush;
      updateMappingsFromSettings(settings);
    }
  }
  else if (cmd == "color")
  {
    int id;
    float r, g, b;
    Vec3f Settings::* prop;
    ss >> id >> r >> g >> b;
    if (!ss.fail())
    {
      switch (id)
      {
        case 0: prop = &Settings::chain0ColorBalance; break;
        case 1: prop = &Settings::chain1ColorBalance; break;
        case 2: prop = &Settings::chain2ColorBalance; break;
        case 3: prop = &Settings::chain3ColorBalance; break;
        default: std::cout << "error bad chain id" << std::endl << std::flush; return;
      }
      settings.set(prop, Vec3f{r,g,b});
      std::cout << "chain " << id << " color balance set: " << r << ", " << g << ", " << b << std::endl << std::flush;
      updateCalibrationsFromSettings(settings);
    }
  }
  else if (cmd == "gamma")
  {
    int id;
    float gamma;
    float Settings::* prop;
    ss >> id >> gamma;
    if (!ss.fail())
    {
      switch (id)
      {
        case 0: prop = &Settings::chain0Gamma; break;
        case 1: prop = &Settings::chain1Gamma; break;
        case 2: prop = &Settings::chain2Gamma; break;
        case 3: prop = &Settings::chain3Gamma; break;
        default: std::cout << "error bad chain id" << std::endl << std::flush; return;
      }
      settings.set(prop, gamma);
      std::cout << "chain " << id << " gamma set: " << gamma << std::endl << std::flush;
      updateCalibrationsFromSettings(settings);
    }
  }
  else if (cmd == "scene")
  {
    int val;
    ss >> val;
    if (!ss.fail()) 
    {
      settings.set(&Settings::scene, val);
      std::cout << "scene set: " << settings.get(&Settings::scene) << std::endl << std::flush;
    }
  }
  else if (cmd == "brightness")
  {
    float brightness;
    ss >> brightness;
    if (!ss.fail())
    {
      settings.set(&Settings::brightness, brightness);
      std::cout << "brightness set: " << settings.get(&Settings::brightness) << std::endl << std::flush;
    }
  }
  else if (cmd == "param")
  {
    float param;
    ss >> param;
    if (!ss.fail())
    {
      settings.set(&Settings::param, param);
      std::cout << "param set: " << settings.get(&Settings::param) << std::endl << std::flush;
    }
  }
  else if (cmd == "autosave")
  {
    int val;
    ss >> val;
    if (!ss.fail())
    {
      settings.set(&Settings::autosave, val == 0 ? false : true);
      std::cout << "autosave set: " << (settings.get(&Settings::autosave) ? 1 : 0) << std::endl << std::flush;
    }
  }
  else if (cmd == "defaults")
  {
    settings.setDefaults(true, 0);
    updateMappingsFromSettings(settings);
    updateCalibrationsFromSettings(settings);
  }
  else if (cmd == "flash")
  {
    // Write the settings to flash
    settings.writeToFlash(true);
  }
  else if (cmd == "poke")
  {
    unsigned int r, g, b;
    int i;
    ss >> i >> r >> g >> b;
    if (!ss.fail())
    {
      if (i >= 0 && i < drawBuffer.size())
      {
        drawBuffer[i] = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
      }
      else
      {
        std::cout << "error invalid index" << std::endl << std::flush;
      }
    }
  }
  else if (cmd == "fill")
  {
    unsigned int r, g, b;
    ss >> r >> g >> b;
    if (!ss.fail())
    {
      std::fill(drawBuffer.begin(), drawBuffer.end(), RGBColor{(uint8_t)r, (uint8_t)g, (uint8_t)b});
    }
  }
  else if (cmd == "fillr")
  {
    int begin, end;
    unsigned int r, g, b;
    ss >> begin >> end >> r >> g >> b;
    if (!ss.fail())
    {
      if (begin >= 0 && begin < drawBuffer.size() && end >= 0 && end < drawBuffer.size() && begin <= end)
      {
        std::fill(drawBuffer.begin()+begin, drawBuffer.begin()+end, RGBColor{(uint8_t)r, (uint8_t)g, (uint8_t)b});
      }
      else
      {
        std::cout << "error invalid index" << std::endl << std::flush;
      }

    }
  }
  else if (cmd == "grad")
  {
    unsigned int r1, g1, b1;
    unsigned int r2, g2, b2;
    ss >> r1 >> g1 >> b1 >> r2 >> g2 >> b2;
    if (!ss.fail())
    {
      RGBColor color1{(uint8_t)r1, (uint8_t)g1, (uint8_t)b1};
      RGBColor color2{(uint8_t)r2, (uint8_t)g2, (uint8_t)b2};
      for (int i=0; i < drawBuffer.size(); ++i)
      {
        float t = (float) i / (float) (drawBuffer.size()-1);
        drawBuffer[i] = RGBColor::blend(color1, color2, t);
      }
    }
  }
  else if (cmd == "dump")
  {
    // Print the whole display buffer to stdout
    std::cout << "Dumping display buffer..." << std::endl << std::flush;
    for (int i=0; i < drawBuffer.size(); ++i)
    {
      std::cout << "idx " << i << " (" << (int)drawBuffer[i].R << " , " 
                                       << (int)drawBuffer[i].G << " , " 
                                       << (int)drawBuffer[i].B << " )" << std::endl << std::flush;
    }
    std::cout << "End of display buffer" << std::endl << std::flush;
  }
  else if (cmd == "halt")
  {
    halt = true;
  }
  else if (cmd == "resume")
  {
    halt = false;
  }
  else if (cmd == "reboot")
  {
    // Reboot the system immediately
    std::cout << "ok" << std::endl << std::flush;
    watchdog_reboot(0,0,0);
  }
  else if (cmd == "prog")
  {
    // Reboot into programming mode
    std::cout << "ok" << std::endl << std::flush;
    rebootIntoProgMode(settings.get(&Settings::chain0Count));
  }
  else
  {
    std::cout << "unknown command" << std::endl << std::flush;
    return;
  }

  if (!ss.fail())
  {
    std::cout << "ok" << std::endl << std::flush;
  }
  else
  {
    std::cout << "error" << std::endl << std::flush;
  }
}

void processStdIo(SettingsManager& settings)
{
  static char inBuf[1024];
  static int pos = 0;

  while (true)
  {
    int inchar = getchar_timeout_us(0);
    if (inchar > 31 && inchar < 127 && pos < 1023)
    {
      inBuf[pos++] = (char)inchar;
      std::cout << (char)inchar << std::flush; // echo to client
    }
    else if (inchar == '\n')
    {
      inBuf[pos] = '\0';
      std::cout << std::endl << std::flush; // echo to client
      processCommand(inBuf, settings);
      pos = 0;
    }
    else
    {
      return;
    }
  }
}

int main()
{
  // Configure stdio
  stdio_init_all();

  // Init the settings object
  SettingsManager settings;

  // Initialize the vector holding all the LED values
  scenes.push_back(std::make_unique<WarmWhite>());
  scenes.push_back(std::make_unique<Halloween>());
  scenes.push_back(std::make_unique<GamerRGB>());
  scenes.push_back(std::make_unique<PureColor>());

  // Load and start the PIO program
  GPIOButton flashButton(16);
  GPIOButton paramButton(17, true);
  GPIOButton sceneBrightnessButton(18, true);
  GPIOButton sceneButton(19);
  GPIOButton brightnessButton(20, true);
 
  BootSelButton bootSelButton;

  absolute_time_t nextFrameTime = get_absolute_time();
  
  updateCalibrationsFromSettings(settings);
  updateMappingsFromSettings(settings);

  while (1)
  {
    // Wait
    sleep_until(nextFrameTime);
    nextFrameTime = make_timeout_time_us(TargetFrameTimeUs);

    // Process input
    processStdIo(settings);

    sceneButton.update();
    if (sceneButton.buttonUp())
    {
      settings.set(&Settings::scene, (settings.get(&Settings::scene) + 1) % (int)scenes.size());
      std::cout << "scene set: " << settings.get(&Settings::scene) << std::endl << std::flush;
    }

    paramButton.update();
    if (paramButton.heldActivate())
    {
      float param = settings.get(&Settings::param) + (0.2f * TargetFrameTimeSec);
      if (param > 1.0f ) param = 0.0f;
      settings.set(&Settings::param, param);
    }
    if (paramButton.buttonUp())
    {
      float param = roundToInterval(settings.get(&Settings::param) + 0.1f, 0.1f);
      if (param > 1.0f ) param = 0.0f;
      settings.set(&Settings::param, param);
      std::cout << "param set: " << settings.get(&Settings::param) << std::endl << std::flush;
    }

    brightnessButton.update();
    if (brightnessButton.heldActivate())
    {
      float brightness = settings.get(&Settings::brightness) - (0.2f * TargetFrameTimeSec);
      if (brightness < 0.0f ) brightness = 1.0f;
      settings.set(&Settings::brightness, brightness);
    }
    if (brightnessButton.buttonUp())
    {
      float brightness = roundToInterval(settings.get(&Settings::brightness) - 0.1f, 0.1f);
      if (brightness < 0.0f ) brightness = 1.0f;
      settings.set(&Settings::brightness, brightness);
      std::cout << "brightness set: " << settings.get(&Settings::brightness) << std::endl << std::flush;
    }

    sceneBrightnessButton.update();
    if (sceneBrightnessButton.heldActivate())
    {
      float brightness = settings.get(&Settings::brightness) - (0.2f * TargetFrameTimeSec);
      if (brightness < 0 ) brightness = 1.0f;
      settings.set(&Settings::brightness, brightness);
    }
    if (sceneBrightnessButton.buttonUp())
    {
      settings.set(&Settings::scene, (settings.get(&Settings::scene) + 1) % (int)scenes.size());
      std::cout << "scene set: " << settings.get(&Settings::scene) << std::endl << std::flush;
    }

    flashButton.update();
    if (flashButton.buttonUp())
    {
      settings.writeToFlash(true);
    }
    
    bootSelButton.update();
    if (bootSelButton.pressed())
    {
      rebootIntoProgMode(drawBufSize);
    }

    // If configured to autosave, try to write settings to flash
    // every frame. It'll only actually do it if the flash payload
    // has changed and even then only once every 15 seconds.
    if (settings.get(&Settings::autosave))
    {
      settings.writeToFlash();
    }

    // Update and draw
    if (!halt) scenes[settings.get(&Settings::scene)]->update(drawBuffer, TargetFrameTimeSec, settings.get(&Settings::param));
    Ws2812bOutput::writeColorsSerial(drawBuffer, mappings, settings.get(&Settings::brightness));
  }
  return 0;
}
