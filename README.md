# PicoLED
PicoLED is a firmware for the Raspberry Pi Pico microcontroller capable of
controlling up to 4 chains of individually addressable LED strips using WS2812B 
LEDs (or any similar single wire system using the same communication protocol)

PicoLED does not require recompilation for configuration, and any PC can configure
the board with a simple text-based serial interface over USB.

## Features
- Built-in automatic lighting modes
  - Warm white uniform
  - Gamer RGB animation
  - Halloween
- Brightness control
- Serial over USB configuration
  - No recompile needed to add/remove LED strips
  - Draw directly to LEDs over serial
  - Config saved to flash
- (Optional) Automatically remember last settings on startup
- GPIO buttons to change light mode, brightness, and save config
- Per-strip gamma and color correction

## GPIO Mapping 

GPIO Mode | Pin | Description
-------|----------|-------------
Out | 22 | LED Strip 0
Out | 26 | LED Strip 1
Out | 27 | LED Strip 2
Out | 28 | LED Strip 3
In  | 16 | Write settings to flash
In  | 18 | Lighting mode (tap), Brightness (hold)
In  | 19 | Lighting mode
In  | 20 | Brightness mode

Inputs are assumed to be momentary switches that make a connection to ground when pressed. The lines are internally pulled up to 3.3v.

## Serial Communication Protocol

When connecting to PicoLED via USB, the pico will advertise a serial device upon which commands are accepted. The interace is telnet-like but control characters like arrows, backspace, etc are not supported.

Commands are given all in lower case, parameters separated with a single space, and ending with a single `\n` newline character.

> Warning: Ensure external power supplies, if any, are connected to VSYS on the pico, not VBUS, before connecting USB. Powering the pico on the VBUS pin and then connecting USB may fry the pico, the PC or both.

### Set number of LEDs per strip
`count [strip-id] [num-leds]`

`strip-id` is an integer 0-3
`num-leds` should be set to the number of LEDs in the strip, or 0 for strips that are not connected

### Set LED strip offset
`offset [strip-id] [offest]`

`strip-id` is an integer 0-3

`offset` the offset in number of LEDs

PicoLED creates a single 1D display buffer of LEDs to cover all strips. Its size is determined by whichever strip has the largest (count + offset) value. Each strip's location in this buffer is determined by its offset.

Using offset, strips can be placed serially or in parallel depending on the desired effect.

### Set LED strip color balance
`color [strip-id] [red-atten] [green-atten] [blue-atten]`

`strip-id` is an integer 0-3

`r/g/b-atten` is a float, 0.0 - 1.0

Default is (1.0, 1.0, 1.0)

### Set LED strip gamma correction
`gamma [strip-id] [correction-factor]`

`strip-id` is an integer 0-3

`correction-factor` is a positive float, generally 1.0 - 3.0

### Change current lighting mode
`scene [mode-id]`

`mode-id` must be an integer 0-2
- 0: Warm white
- 1: Gamer RGB
- 2: Halloween

### Change maximum brightness
`brightness [brightness]`

`brightness` is a floating point value between 0.0 and 1.0. Default is 1.0, full brightness.

### Enable/Disable autosave of settings
`autosave [0 or 1]`

When autosave is on, settings are written to flash every time they change (at most once every 5 seconds). Enabling autosave may shorten the lifespan of the pi pico flash, but allows the light to automatically remember its last mode every time it boots.

### Reset all settings to defaults
`defaults`

Restore all settings to their factory state, with just one LED on strip id 0.

### Save current settings to flash
`flash`

### Set the RGB color of a single LED
`poke [index] [r] [g] [b]`

`index` is an index in the display buffer and may light multiple physical LEDs depending on configuration

`r`, `g`, `b` are integer values 0 - 255

### Set the RGB color all LEDs
`fill [r] [g] [b]`

`r`, `g`, `b` are integer values 0 - 255

### Set the RGB color value of LEDs within a range
`fillr [index-start] [index-end] [r] [g] [b]`

`r`, `g`, `b` are integer values 0 - 255

### Draw a gradient across all of the LEDs
`grad [r1] [g1] [b1] [r2] [g2] [b2]`

Draw a gradient from RGB1 to RGB2 across all connected LED strips. Each parameter is an integer, 0-255.

### Print out all 
`dump`

Print the RGB value of all LEDs to the serial console

### halt
`halt`

Stop updating the LED buffer, pausing animations and allowing the poke and fill commands to work

### resume
`resume`

Resume updating the LED buffer, starting automatic animations.

### reboot
`reboot`

Reboot the microcontroller right away

### prog
`prog`

Reboot to pi pico bootloader for firmware programming. Flashes all LEDs red 3 times to confirm.

## Build Requirements
You'll need to clone the [pico-sdk](https://github.com/raspberrypi/pico-sdk) next to this repo on your disk, as build scripts will be looking for `../pico-sdk` for necessary build files.

## Possible Future Development
- Support for up to 8 chains (using all the PIO)
- More and better lighting configurations
- Enhanced serial protocol