# Lightweight button manager

LwBTN is a lightweight, platform independent library for button and input-pin management in embedded systems.
It debounces raw pin state changes and turns them into click, multi-click and keep-alive (long-press) events, delivered through a single application callback.

[Open documentation](https://docs.majerle.eu/projects/lwbtn/)

## Features

* Written in C (C11), compatible with `stdint.h` data types
* Platform independent, requires user to provide millisecond timing source
* No dynamic memory allocation
* Callback-driven event management
* Support for click, multi-click and keep-alive (long press) events
* Support for software debounce for press and release events
* Runtime configurable debounce, click and keep-alive timing, per button
* Selectable button state acquisition: callback, manual or hybrid mode
* Support for multiple independent button-group instances
* Easy to use and maintain
* User friendly MIT license

## Porting guides

Step-by-step guides for integrating lwbtn into specific MCU projects are collected in the [add_to_main_codespace](./add_to_main_codespace) folder, one sub-folder per MCU family:

| MCU family | Guide | Description |
|------------|-------|-------------|
| AT32 | [AT32_PortGuide.md](./add_to_main_codespace/at32/AT32_PortGuide.md) | Porting lwbtn to AT32F435 (ARM Cortex-M4F) with a portable layer, callback mode and periodic processing from the main loop |
| STM32 | [STM32_PortGuide.md](./add_to_main_codespace/stm32h5/STM32_PortGuide.md) | Porting lwbtn to STM32 (HAL) with a portable layer, BSP user button, callback mode and periodic processing from the main loop |

Each guide documents the reference setup, the portable layer (`lwbtn_portable.c` / `lwbtn_portable.h` on AT32, `lwbtn_port.c` / `lwbtn_port.h` on STM32) and the pitfalls to watch out for, so new ports can follow an existing template.

## Contribute

Fresh contributions are always welcome. Simple instructions to proceed:

1. Fork Github repository
2. Follow [C style & coding rules](https://github.com/MaJerle/c-code-style) and use `clang-format` to format the code
3. Create a pull request to `develop` branch with new features or bug fixes

Alternatively you may:

1. Report a bug
2. Ask for a feature request 
