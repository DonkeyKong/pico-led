#pragma once

#include <pico/stdlib.h>
#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <hardware/structs/ioqspi.h>
#include <hardware/structs/sio.h>

namespace
{
  bool __no_inline_not_in_flash_func(get_bootsel_button)() {
      const uint CS_PIN_INDEX = 1;

      // Must disable interrupts, as interrupt handlers may be in flash, and we
      // are about to temporarily disable flash access!
      uint32_t flags = save_and_disable_interrupts();

      // Set chip select to Hi-Z
      hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                      GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                      IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

      // Note we can't call into any sleep functions in flash right now
      for (volatile int i = 0; i < 1000; ++i);

      // The HI GPIO registers in SIO can observe and control the 6 QSPI pins.
      // Note the button pulls the pin *low* when pressed.
      bool button_state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));

      // Need to restore the state of chip select, else we are going to have a
      // bad time when we return to code in flash!
      hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                      GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                      IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

      restore_interrupts(flags);

      return button_state;
  } 
}

class Button
{
public:
  virtual ~Button() = default;

  bool pressed()
  {
    return state_;
  }

  uint32_t heldTimeMs()
  {
    if (!state_) return 0;
    return to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(stateTime_);
  }

  uint32_t releasedTimeMs()
  {
    if (state_) return 0;
    return to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(stateTime_);
  }

  bool heldActivate()
  {
    return holdActivate_;
  }

  bool buttonDown()
  {
    return state_ && !lastState_;
  }
  
  bool buttonUp()
  {
    return !state_ && lastState_;
  }

  void update()
  {
    lastState_ = state_;
    state_ = getButtonState();
    if (lastState_ != state_)
    {
      stateTime_ = get_absolute_time();
    }

    if (enableHoldAction_)
    {
      if (buttonDown())
      {
        holdActivationTime_ = make_timeout_time_ms(holdActivationMs_);
      }

      if (buttonUp() && holdSuppressButtonUp_)
      {
        lastState_ = state_;
        holdSuppressButtonUp_ = false;
      }

      if (state_ && to_ms_since_boot(holdActivationTime_) <= to_ms_since_boot(get_absolute_time()))
      {
        holdActivate_ = true;
        holdActivationTime_ = make_timeout_time_ms(holdActivationRepeatMs_);
        holdSuppressButtonUp_ = true;
      }
      else
      {
        holdActivate_ = false;
      }
    }
  }

protected:
  Button(bool enableHoldAction = false) : enableHoldAction_(enableHoldAction) {}
  virtual bool getButtonState() = 0;
  bool state_;
  bool lastState_;
  absolute_time_t stateTime_;

  bool enableHoldAction_ = false;
  int holdActivationMs_ = 1000;
  int holdActivationRepeatMs_ = 0;
  absolute_time_t holdActivationTime_;
  bool holdActivate_ = false;
  bool holdSuppressButtonUp_ = false;
};

class BootSelButton : public Button
{
  virtual bool getButtonState() override
  {
    return get_bootsel_button();
  }
};

class GPIOButton : public Button
{
public:
  GPIOButton(uint32_t pin, bool enableHoldAction = false, bool pullUp = true, bool pullDown = false, bool invert = true):
    Button(enableHoldAction),
    pin_(pin)
  {
    gpio_init(pin_);
    gpio_set_dir(pin_, GPIO_IN);
    if (pullUp)
    {
      gpio_pull_up(pin_);
    }
    if (pullDown)
    {
      gpio_pull_down(pin_);
    }
    if (invert)
    {
      gpio_set_inover(pin_, GPIO_OVERRIDE_INVERT);
    }
    else
    {
      gpio_set_inover(pin_, GPIO_OVERRIDE_NORMAL);
    }
    
    // Give the gpio set operations a chance to settle!
    sleep_until(make_timeout_time_ms(1));

    update();
    lastState_ = state_;
    stateTime_ = get_absolute_time();
  }

  virtual bool getButtonState() override
  {
    return gpio_get(pin_);
  }

  ~GPIOButton()
  {
    gpio_deinit(pin_);
  }

private:
  uint32_t pin_;
};