#pragma once

#include "Color.hpp"
#include "ws2812b.pio.h"

#include <pico/stdlib.h>
#include <hardware/pio.h>
#include <hardware/timer.h>

#include <memory>
#include <vector>
#include <iostream>

using ConfigFuncPtr = pio_sm_config (*)(PIO pio, uint stateMachine, uint offset, uint pin, float clkdiv);

class PioProgram
{
public:
  PioProgram(const PIO& pio, const pio_program* prog)
  {
    // Load the PIO program
    pio_ = pio;
    prog_ = prog;
    offset_ = pio_add_program(pio_, prog_);
    loaded_ = true;
  }

  ~PioProgram()
  {
    if (loaded_)
    {
      pio_remove_program(pio_, prog_, offset_);
    }
    loaded_ = false;
  }

  PIO pio()
  {
    return pio_;
  }

  uint offset()
  {
    return offset_;
  }

  // Cannot copy or move
  PioProgram(const PioProgram& o) = delete;
  PioProgram(PioProgram&& other) = delete;
  PioProgram& operator=(PioProgram&& other) = delete;

private:
  const pio_program* prog_;
  PIO pio_;
  uint offset_;
  bool loaded_ = false;
};

class PioMachine
{
public:
  
  PioMachine() = default;
  
  PioMachine(std::shared_ptr<PioProgram>& prog, ConfigFuncPtr configFunc, uint pin, float clkdiv = 16.625f)
  {
    // Load and start the PIO program
    prog_ = prog;
    pio_ = prog_->pio();  // Used so frequently we have to cache it
    sm_ = pio_claim_unused_sm(pio_, true);
    config_ = configFunc(pio_, sm_, prog_->offset(), pin, clkdiv);
    loaded_ = true;
  }

  ~PioMachine()
  {
    if (loaded_)
    {
      pio_sm_unclaim(pio_, sm_);
    }
    loaded_ = false;
  }

  PioMachine(const PioMachine& o) = delete;

  PioMachine(PioMachine&& other):
    sm_(other.sm_),
    config_(other.config_),
    loaded_(other.loaded_),
    prog_(other.prog_),
    pio_(other.pio_)
  {
    other.prog_.reset();
    other.loaded_ = false;
  }

  PioMachine& operator=(PioMachine&& other)
  {
    if (loaded_)
    {
      pio_sm_unclaim(pio_, sm_);
    }
    sm_ = other.sm_;
    config_ = other.config_;
    loaded_ = other.loaded_;
    prog_ = other.prog_;
    pio_ = other.pio_;
    other.prog_.reset();
    other.loaded_ = false;
    return *this;
  }

  inline bool write(uint32_t data, int timeoutMs = -1)
  {
    auto start = to_ms_since_boot(get_absolute_time());
    while (1) 
    {  
      auto now = to_ms_since_boot(get_absolute_time());
      if (!pio_sm_is_tx_fifo_full(pio_, sm_))
      {
        break;
      }
      if (timeoutMs > 0 && (now - start) > timeoutMs)
      {
        return false;
      }
    }
    pio_sm_put(pio_, sm_, data);
    return true;
  }

  inline bool write(uint8_t data, int timeoutMs = -1)
  {
    return write((uint32_t)data << 24, timeoutMs);
  }

  inline bool read(uint32_t& data, int timeoutMs = -1)
  {
    auto start = to_ms_since_boot(get_absolute_time());
    while (1) 
    {  
      auto now = to_ms_since_boot(get_absolute_time());
      if (!pio_sm_is_rx_fifo_empty(pio_, sm_))
      {
        break;
      }
      if (timeoutMs > 0 && (now - start) > timeoutMs)
      {
        return false;
      }
    }
    data = pio_sm_get(pio_, sm_);
    return true;
  }

  void reset()
  {
    pio_sm_set_enabled(pio_, sm_, false);
    pio_sm_clear_fifos(pio_, sm_);
    pio_sm_restart(pio_, sm_);
    pio_sm_init(pio_, sm_, prog_->offset(), &config_);
    pio_sm_set_enabled(pio_, sm_, true);
  }

protected:
  uint sm_;
  pio_sm_config config_;
  bool loaded_ = false;
  std::shared_ptr<PioProgram> prog_;
  PIO pio_;
};

class Ws2812bOutput : PioMachine
{
public:

  struct BufferMapping
  {
    Ws2812bOutput* output;
    int size;
    int offset;
    int index;
  };

  static Ws2812bOutput create(uint pin)
  {
    std::shared_ptr<PioProgram> prog = prog_.lock();
    if (!prog)
    {
      // TBD: support autoselection of pio0 and pio1
      prog = std::make_shared<PioProgram>(pio0, &ws2812b_program);
      prog_ = prog;
    }
    return Ws2812bOutput(prog, pin);
  }

  inline void writeColors(const LEDBuffer& buffer, float brightness = 1.0f)
  {
    RGBColor calibrated;
    uint32_t data;
    // Send the colors
    for (int i=0; i < buffer.size(); ++i)
    {
      // Format the color for I/O
      calibrated = buffer[i] * colorBalance_ * brightness;
      calibrated.applyGamma(gamma_);
      data = calibrated.G << 16 | calibrated.R << 8 | calibrated.B;
      pio_sm_put_blocking(pio_, sm_, data);
    }
    // Send a reset when done
    data = 0xFF << 24;
    pio_sm_put_blocking(pio_, sm_, data);
  }

  static inline void writeColorsParallel(const LEDBuffer& buffer, std::vector<BufferMapping>& mappings, float brightness = 1.0f)
  {
    for (BufferMapping& m : mappings)
    {
      m.index = 0;
    }

    while (true)
    {
      for (BufferMapping& m : mappings)
      {
        while (m.index < m.size && !pio_sm_is_tx_fifo_full(m.output->pio_, m.output->sm_))
        {
          int bufferIndex = std::clamp((m.index++) + m.offset, 0, (int)buffer.size()-1);
          RGBColor calibrated = buffer[bufferIndex] * m.output->colorBalance_ * brightness;
          calibrated.applyGamma(m.output->gamma_);
          uint32_t data = calibrated.G << 16 | calibrated.R << 8 | calibrated.B;
          pio_sm_put(m.output->pio_, m.output->sm_, data);
        }
      }

      int doneCount = 0;
      for (BufferMapping& m : mappings)
      {
        if (m.index == m.size) ++doneCount;
      }
      if (doneCount == mappings.size())
      {
        break;
      }
    }
  }

  inline void gamma(float gamma)
  {
    gamma_ = gamma;
  }

  inline void colorBalance(const Vec3f& colorBalance)
  {
    colorBalance_ = colorBalance;
  }

private:
  static std::weak_ptr<PioProgram> prog_;
  Vec3f colorBalance_ {1.0f, 1.0f, 1.0f};
  float gamma_ {1.0f};
  Ws2812bOutput(std::shared_ptr<PioProgram>& prog, uint pin) :
    PioMachine(prog, ws2812b_program_init, pin, 5.0) {}
};

std::weak_ptr<PioProgram> Ws2812bOutput::prog_;