#include <cpp/LedStripWs2812b.hpp>
#include <cpp/BootSelButton.hpp>
#include <cpp/Button.hpp>
#include <cpp/Color.hpp>
#include <cpp/FlashStorage.hpp>
#include "Scene.hpp"
#include "Settings.hpp"

#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <pico/bootrom.h>
#include <pico/unique_id.h>
#include <hardware/watchdog.h>

#include <iostream>
#include <cmath>
#include <memory>
#include <sstream>

constexpr uint64_t TargetFPS = 20;
constexpr uint64_t TargetFrameTimeUs = 1000000 / TargetFPS;
constexpr float TargetFrameTimeSec = 1.0f / (float)TargetFPS;

LEDBuffer drawBuffer;
LedStripWs2812b chain0(22);
LedStripWs2812b chain1(26);
LedStripWs2812b chain2(27);
LedStripWs2812b chain3(28);

std::vector<LedStripWs2812b::BufferMapping> mappings { {&chain0}, {&chain1}, {&chain2}, {&chain3} };
bool halt = false;

inline float roundToInterval(float val, float interval)
{
  return std::round(val / interval) * interval;
}

void updateCalibrationsFromSettings(const Settings& settings)
{
  chain0.colorBalance(settings.chain0ColorBalance);
  chain1.colorBalance(settings.chain1ColorBalance);
  chain2.colorBalance(settings.chain2ColorBalance);
  chain3.colorBalance(settings.chain3ColorBalance);
  chain0.gamma(settings.chain0Gamma);
  chain1.gamma(settings.chain1Gamma);
  chain2.gamma(settings.chain2Gamma);
  chain3.gamma(settings.chain3Gamma);
}

void updateMappingsFromSettings(const Settings& settings)
{
  // Refresh the scene mappings
  mappings[0].size = (int)settings.chain0Count;
  mappings[1].size = (int)settings.chain1Count;
  mappings[2].size = (int)settings.chain2Count;
  mappings[3].size = (int)settings.chain3Count;
  mappings[0].offset = (int)settings.chain0Offset;
  mappings[1].offset = (int)settings.chain1Offset;
  mappings[2].offset = (int)settings.chain2Offset;
  mappings[3].offset = (int)settings.chain3Offset;

  int drawBufSize = 0;
  drawBufSize = std::max(drawBufSize, (int)settings.chain0Count + settings.chain0Offset);
  drawBufSize = std::max(drawBufSize, (int)settings.chain1Count + settings.chain1Offset);
  drawBufSize = std::max(drawBufSize, (int)settings.chain2Count + settings.chain2Offset);
  drawBufSize = std::max(drawBufSize, (int)settings.chain3Count + settings.chain3Offset);
  drawBufSize = std::min(drawBufSize, MAX_BUFFER_LENGTH);

  drawBuffer.resize(drawBufSize);
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

void processCommand(std::string cmdAndArgs, FlashStorage<Settings>& settingsMgr)
{
  Settings& settings = settingsMgr.data;
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
      if (val < 0 || val > MAX_BUFFER_LENGTH) 
      {
        std::cout << "error bad count" << std::endl << std::flush; 
        return;
      }
      switch (id)
      {
        case 0: settings.chain0Count = val; break;
        case 1: settings.chain1Count = val; break;
        case 2: settings.chain2Count = val; break;
        case 3: settings.chain3Count = val; break;
        default: std::cout << "error bad chain id" << std::endl << std::flush; return;
      }

      std::cout << "chain " << id << " count set: " << val << std::endl << std::flush;
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
      if (val < 0 || val > MAX_BUFFER_LENGTH) 
      {
        std::cout << "error bad offset" << std::endl << std::flush; 
        return;
      }
      switch (id)
      {
        case 0: settings.chain0Offset = val; break;
        case 1: settings.chain1Offset = val; break;
        case 2: settings.chain2Offset = val; break;
        case 3: settings.chain3Offset = val; break;
        default: std::cout << "error bad chain id" << std::endl << std::flush; return;
      }
      std::cout << "chain " << id << " offset set: " << val << std::endl << std::flush;
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
        case 0: settings.chain0ColorBalance = {r, g, b}; break;
        case 1: settings.chain1ColorBalance = {r, g, b}; break;
        case 2: settings.chain2ColorBalance = {r, g, b}; break;
        case 3: settings.chain3ColorBalance = {r, g, b}; break;
        default: std::cout << "error bad chain id" << std::endl << std::flush; return;
      }
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
        case 0: settings.chain0Gamma = gamma; break;
        case 1: settings.chain1Gamma = gamma; break;
        case 2: settings.chain2Gamma = gamma; break;
        case 3: settings.chain3Gamma = gamma; break;
        default: std::cout << "error bad chain id" << std::endl << std::flush; return;
      }
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
      settings.scene = val;
      std::cout << "scene set: " << settings.scene << std::endl << std::flush;
    }
  }
  else if (cmd == "brightness")
  {
    float brightness;
    ss >> brightness;
    if (!ss.fail())
    {
      settings.brightness = brightness;
      std::cout << "brightness set: " << settings.brightness << std::endl << std::flush;
    }
  }
  else if (cmd == "param")
  {
    float param;
    ss >> param;
    if (!ss.fail())
    {
      settings.param = param;
      std::cout << "param set: " << settings.param << std::endl << std::flush;
    }
  }
  else if (cmd == "autosave")
  {
    int val;
    ss >> val;
    if (!ss.fail())
    {
      settings.autosave = (val == 0 ? false : true);
      std::cout << "autosave set: " << (settings.autosave ? 1 : 0) << std::endl << std::flush;
    }
  }
  else if (cmd == "defaults")
  {
    settings.setDefaults();
    updateMappingsFromSettings(settings);
    updateCalibrationsFromSettings(settings);
  }
  else if (cmd == "flash")
  {
    // Write the settings to flash
    if (settingsMgr.writeToFlash())
      std::cout << "Wrote settings to flash!" << std::endl << std::flush;
    else
      std::cout << "Skipped writing to flash because contents were already correct." << std::endl << std::flush;
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
  else if (cmd == "info" || cmd == "about")
  {
    std::cout << "pico-led by Donkey Kong" << std::endl;
    std::cout << "https://github.com/DonkeyKong/pico-led" << std::endl;
    std::cout << std::endl;
    settings.print();
    std::cout << std::endl;
    std::cout << "Runtime Data:" << std::endl;
    std::cout << "    " << "full settings size:    " << sizeof(Settings) << std::endl;
    std::cout << "    " << "status:    " << (halt ? "halted" : "running") << std::endl;
    std::cout << "    " << "scene count:    " << Scenes.size() << std::endl;
    std::cout << "    " << "scene names:";
    for (auto& name : SceneNames) std::cout << "    " << name;
    std::cout << std::endl;
    std::cout << "    " << "draw buffer size:    " << drawBuffer.size() << std::endl;
    std::cout << "    " << "max draw buffer size:    " << MAX_BUFFER_LENGTH << std::endl;
    std::cout << "    " << "target fps:    " << TargetFPS << std::endl;
    std::cout << std::flush;
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
    rebootIntoProgMode(drawBuffer.size(), mappings);
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

void processStdIo(FlashStorage<Settings>& settingsMgr)
{
  Settings& settings = settingsMgr.data;
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
      processCommand(inBuf, settingsMgr);
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
  FlashStorage<Settings> settingsMgr;
  Settings& settings = settingsMgr.data;

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
    processStdIo(settingsMgr);

    sceneButton.update();
    if (sceneButton.buttonUp())
    {
      settings.scene = (settings.scene + 1) % (int)Scenes.size();
      std::cout << "scene set: " << settings.scene << std::endl << std::flush;
    }

    paramButton.update();
    if (paramButton.heldActivate())
    {
      float param = settings.param + (0.2f * TargetFrameTimeSec);
      if (param > 1.0f ) param = 0.0f;
      settings.param = param;
    }
    if (paramButton.buttonUp())
    {
      float param = roundToInterval(settings.param + 0.1f, 0.1f);
      if (param > 1.0f ) param = 0.0f;
      settings.param = param;
      std::cout << "param set: " << settings.param << std::endl << std::flush;
    }

    brightnessButton.update();
    if (brightnessButton.heldActivate())
    {
      float brightness = settings.brightness - (0.2f * TargetFrameTimeSec);
      if (brightness < 0.0f ) brightness = 1.0f;
      settings.brightness = brightness;
    }
    if (brightnessButton.buttonUp())
    {
      float brightness = roundToInterval(settings.brightness - 0.1f, 0.1f);
      if (brightness < 0.0f ) brightness = 1.0f;
      settings.brightness = brightness;
      std::cout << "brightness set: " << settings.brightness << std::endl << std::flush;
    }

    sceneBrightnessButton.update();
    if (sceneBrightnessButton.heldActivate())
    {
      float brightness = settings.brightness - (0.2f * TargetFrameTimeSec);
      if (brightness < 0 ) brightness = 1.0f;
      settings.brightness = brightness;
    }
    if (sceneBrightnessButton.buttonUp())
    {
      settings.scene = (settings.scene + 1) % (int)Scenes.size();
      std::cout << "scene set: " << settings.scene << std::endl << std::flush;
    }

    flashButton.update();
    if (flashButton.buttonUp())
    {
      if (settingsMgr.writeToFlash())
        std::cout << "Wrote settings to flash!" << std::endl << std::flush;
      else
        std::cout << "Skipped writing to flash because contents were already correct." << std::endl << std::flush;
    }
    
    bootSelButton.update();
    if (bootSelButton.pressed())
    {
      rebootIntoProgMode(drawBuffer.size(), mappings);
    }

    // If configured to autosave, try to write settings to flash
    // every frame. It'll only actually do it if the flash payload
    // has changed and even then only once every 15 seconds.
    // if (settings.autosave)
    // {
    //   if (settingsMgr.autosave())
    //   {
    //     std::cout << "autosaved settings to flash!" << std::endl << std::flush;
    //   }
    // }

    // Update and draw
    if (!halt) Scenes[settings.scene]->update(drawBuffer, TargetFrameTimeSec, settings.param);
    LedStripWs2812b::writeColorsParallel(drawBuffer, mappings, settings.brightness);
  }
  return 0;
}
