# lwbtn Library — AT32 Porting Guide

> **Target MCU**: AT32F435ZMT7 (ARM Cortex-M4F, 288 MHz, 4096K Flash, 384K RAM)
> **Toolchain**: GCC ARM Embedded (arm-none-eabi), EIDE
> **Library Version**: lwbtn v1.2.1
> **Porting Layer**: `project/usersrc/lwbtn_portable.c` + `project/userinc/lwbtn_portable.h`

---

## Overview

[lwbtn](https://github.com/MaJerle/lwbtn) is a lightweight, platform-independent button manager written in C. It provides:

- Software debounce for press and release events
- Click, multi-click detection
- Long press / keep-alive events
- Callback-driven event management
- No dynamic memory allocation

This guide documents how to port lwbtn to any AT32 MCU project. The reference implementation uses **callback mode** with a single button on **GPIOD Pin 10** (PBOUT, active-low with pull-up).

**Key Features of this port**:
- Single button instance with callback-based state reading
- Startup protection: no button response within first 3000 ms after boot
- Legacy click callback registration for backward compatibility
- Driven periodically from the main loop

**Resource Usage** (measured on AT32F435):
| Component | Flash | RAM |
|-----------|-------|-----|
| lwbtn core (`lwbtn.o`) | ~2 KB | 0 bytes (static) |
| Portable layer (`lwbtn_portable.o`) | ~0.4 KB | ~0.1 KB (1 button struct + callback ptr) |

---

## Architecture

```
┌─────────────────────────────────────┐
│             main.c                   │
│  lwbtn_portable_init()   ← init     │
│  loop:                               │
│    lwbtn_portable_process() ← periodic│
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   lwbtn_portable.c / .h             │  ← Porting layer (you write this)
│  ┌─────────────────────────────┐    │
│  │ s_btns[1]  (lwbtn_btn_t[])  │    │  ← Button array
│  │ s_click_callback            │    │  ← Legacy click callback pointer
│  │ lwbtn_portable_init()       │────│──→ gpio_init() + lwbtn_init_ex()
│  │ prv_get_state()             │────│──→ gpio_input_data_bit_read()
│  │ prv_event()                 │────│──→ s_click_callback() + printf()
│  │ lwbtn_portable_process()    │────│──→ lwbtn_process_ex(NULL, getSysTick())
│  │ lwbtn_portable_register_    │    │
│  │   click_callback()          │────│──→ Store callback pointer
│  └─────────────────────────────┘    │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│       lwbtn core library            │  ← Library code (do not modify)
│  lwbtn_init_ex()                    │
│  lwbtn_process_ex()                 │
│  lwbtn_btn_t state machine          │
└─────────────────────────────────────┘
```

---

## Porting Steps

### Step 1: Add lwbtn Source to Build System (EIDE)

#### 1.1 Add Source Directory

In `.eide/eide.yml`, under `srcDirs`, add `lwbtn`:

```yaml
srcDirs:
  - lwbtn                         # ← Add this line
```

#### 1.2 Add Include Path

Under `targets.Debug.cppPreprocessAttrs.incList`, add the lwbtn include path:

```yaml
incList:
  - lwbtn/lwbtn/src/include       # ← Add this line
```

#### 1.3 Configure lwbtn Options via Global Define

lwbtn optionally includes user config from `lwbtn_opts.h`. To skip this file and use library defaults, add the following define. In `targets.Debug.cppPreprocessAttrs.defineList`:

```yaml
defineList:
  - LWBTN_IGNORE_USER_OPTS        # ← Add this line (optional)
```

> **Reference**: See `.eide/eide.yml` — line 17 for `defineList` include. If you need custom options, create `lwbtn_opts.h` in your project's include path instead.

#### 1.4 Exclude Non-Essential Subdirectories

Under `targets.Debug.excludeList`, exclude test/example directories:

```yaml
excludeList:
  - lwbtn/cmake                   # ← build system files
  - lwbtn/docs                    # ← documentation
  - lwbtn/examples                # ← example projects
  - lwbtn/tests                   # ← unit tests
```

> **Reference**: See `.eide/eide.yml` lines 56-62 in MC1618APP project.

---

### Step 2: Create Portable Layer Files

#### 2.1 Header: `project/userinc/lwbtn_portable.h`

```c
#ifndef __LWBTN_PORTABLE_H
#define __LWBTN_PORTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>

/**
 * @brief Button click event callback type | 按键点击事件回调类型
 */
typedef void (*lwbtn_portable_click_callback_t)(void);

/**
 * @brief Initialize LwBTN button manager and GPIO | 初始化 LwBTN 按键管理与 GPIO
 * @details Configures button GPIO as input with pull-up, registers LwBTN callbacks
 * @note Must be called once at startup after system clock and GPIO init
 */
void lwbtn_portable_init(void);

/**
 * @brief Register click event callback | 注册点击事件回调
 * @param f Pointer to callback function | 回调函数指针
 */
void lwbtn_portable_register_click_callback(lwbtn_portable_click_callback_t f);

/**
 * @brief Periodic button processing | 周期性按键处理
 * @details Must be called periodically in main loop to drive LwBTN state machine
 */
void lwbtn_portable_process(void);

#ifdef __cplusplus
}
#endif

#endif /* __LWBTN_PORTABLE_H */
```

#### 2.2 Source: `project/usersrc/lwbtn_portable.c`

**Includes**:

```c
#include "lwbtn_portable.h"
#include "user_crm.h"
#include "at32f435_437_wk_config.h"
#include "lwbtn/lwbtn.h"
```

**Private variables**:

```c
static lwbtn_btn_t s_btns[1];                           /* Button array for LwBTN */
static lwbtn_portable_click_callback_t s_click_callback = 0;  /* Legacy click callback */
```

---

### Step 3: Implement Button State Reading Callback

This callback is invoked by lwbtn whenever it needs to read the physical button state.

```c
static uint8_t prv_get_state(struct lwbtn *lwobj, struct lwbtn_btn *btn)
{
    /*
     * Startup protection: no button response within 3000ms after boot
     * 启动保护：开机3000ms以内不会响应按键
     */
    if (getSysTick() < 3000)
    {
        return 0;
    }

    /*
     * Button pressed when pin reads low (pull-up, active low)
     * 按键按下时引脚为低电平（上拉输入，低电平有效）
     */
    if (gpio_input_data_bit_read(PBOUT_GPIO_PORT, PBOUT_PIN) == SET)
    {
        return 0; /* Not pressed | 未按下 */
    }
    else
    {
        return 1; /* Pressed | 按下 */
    }
}
```

> **Note**: The startup protection prevents false button triggers during the initial 3 seconds after boot, when GPIO states may still be settling.

---

### Step 4: Implement Button Event Callback

This callback is invoked by lwbtn when a button event occurs (press, release, click, etc.).

```c
static void prv_event(struct lwbtn *lwobj, struct lwbtn_btn *btn, lwbtn_evt_t evt)
{
    switch (evt)
    {
        case LWBTN_EVT_ONCLICK:
        {
            /* Call legacy callback if registered */
            if (s_click_callback)
            {
                s_click_callback();
            }

            printf("[%d]Button Clicked\r\n", getSysTick());
            break;
        }

        default:
            break;
    }
}
```

---

### Step 5: Implement Init and Process Functions

#### 5.1 Initialization

Configures the button GPIO and registers lwbtn callbacks:

```c
void lwbtn_portable_init(void)
{
    gpio_init_type gpio_init_struct;

    /* Configure button GPIO | 配置按键 GPIO */
    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
    gpio_init_struct.gpio_pins = PBOUT_PIN;
    gpio_init_struct.gpio_pull = GPIO_PULL_UP;
    gpio_init(PBOUT_GPIO_PORT, &gpio_init_struct);

    /* Initialize LwBTN with 1 button using callback mode */
    lwbtn_init_ex(NULL, s_btns, sizeof(s_btns) / sizeof(s_btns[0]),
                  prv_get_state, prv_event);
}
```

#### 5.2 Periodic Processing

Must be called regularly from the main loop:

```c
void lwbtn_portable_process(void)
{
    lwbtn_process_ex(NULL, getSysTick());
}
```

> **Note**: `lwbtn_process_ex()` takes the current system time in milliseconds. The `NULL` first argument uses the default button group.

#### 5.3 Register Legacy Click Callback

```c
void lwbtn_portable_register_click_callback(lwbtn_portable_click_callback_t f)
{
    s_click_callback = f;
}
```

---

### Step 6: Configure GPIO Pin Definitions

The button GPIO pin is defined in `project/inc/at32f435_437_wk_config.h`:

```c
#define PBOUT_PIN         GPIO_PINS_10
#define PBOUT_GPIO_PORT   GPIOD
```

| Signal   | Pin   | GPIO Port | Configuration        | Active Level |
|----------|-------|-----------|----------------------|--------------|
| PBOUT    | PD10  | `GPIOD`   | Input, pull-up       | Low (pressed) |

> **Note**: Adjust these macros to match your hardware button wiring.

---

### Step 7: Integrate into main.c

#### 7.1 Add Include

```c
#include "lwbtn_portable.h"
```

#### 7.2 Initialize After System Config

Call `lwbtn_portable_init()` after system clock, GPIO, and peripheral setup:

```c
    wk_print_clock_freq();
    buzzer_on_block(200);

    lwbtn_portable_init();            /* ← Add here */
```

> **Reference**: See `project/src/main.c` line 354.

#### 7.3 Call Periodically in Main Loop

Add `lwbtn_portable_process()` to the main loop:

```c
    while (1)
    {
        /* ... other tasks ... */

        /* Process LwBTN button state machine | 处理 LwBTN 按键状态机 */
        lwbtn_portable_process();

        /* ... other tasks ... */
    }
```

> **Reference**: See `project/src/main.c` lines 414-415.

---

## Configuration Options

lwbtn provides compile-time configuration via `lwbtn_opt.h`. Key options can be overridden by defining macros in your build system or in a custom `lwbtn_opts.h`:

| Macro | Default | Description |
|-------|---------|-------------|
| `LWBTN_CFG_USE_KEEPALIVE` | `1` | Enable periodic keep-alive events while button is pressed |
| `LWBTN_CFG_USE_CLICK` | `1` | Enable click event detection |
| `LWBTN_CFG_TIME_DEBOUNCE_PRESS` | `20` | Debounce time for press event (ms) |
| `LWBTN_CFG_TIME_DEBOUNCE_RELEASE` | `0` | Debounce time for release event (ms) |
| `LWBTN_CFG_TIME_CLICK_MIN` | `20` | Minimum press time for valid click (ms) |
| `LWBTN_CFG_TIME_CLICK_MAX` | `300` | Maximum press time for valid click (ms) |
| `LWBTN_CFG_TIME_KEEPALIVE_PERIOD` | `500` | Keep-alive event period (ms) |
| `LWBTN_CFG_CLICK_MAX_CONSECUTIVE` | `10` | Max consecutive clicks tracked |

To use custom settings without modifying library files, either:
1. Add defines to `.eide/eide.yml` `defineList` (e.g., `LWBTN_CFG_TIME_DEBOUNCE_PRESS=50`)
2. Create `project/inc/lwbtn_opts.h` with your overrides and **remove** the `- DLWBTN_IGNORE_USER_OPTS` define

---

## Callback Reference

| Callback | Type | Purpose | Called When |
|----------|------|---------|-------------|
| `prv_get_state()` | `lwbtn_get_state_fn` | Read physical button state | Every `lwbtn_process_ex()` cycle |
| `prv_event()` | `lwbtn_evt_fn` | Handle button events | On press, release, click, or keep-alive |

---

## API Reference

| Function | Purpose |
|----------|---------|
| `lwbtn_init_ex(lwobj, btns, cnt, get_state_fn, evt_fn)` | Initialize lwbtn with callbacks |
| `lwbtn_process_ex(lwobj, mstime)` | Periodic processing (call regularly) |
| `lwbtn_is_btn_active(btn)` | Check if button is currently active |
| `lwbtn_click_get_count(btn)` | Get consecutive click count |
| `lwbtn_set_btn_state(btn, state)` | Manually set button state (if not using callback mode) |
| `lwbtn_reset(lwobj, btn)` | Reset button to initial state |

---

## Potential Pitfalls

1. **GPIO pull configuration**: The button GPIO must be configured as `GPIO_PULL_UP` (or external pull-up) since the reference design uses active-low logic.
2. **Startup protection**: The 3000 ms startup guard is implemented in `prv_get_state()`. Adjust the threshold or remove it if your application requires immediate button response.
3. **Timing source**: `lwbtn_process_ex()` requires a millisecond timestamp. The reference uses `getSysTick()`. Ensure this function returns monotonically increasing milliseconds.
4. **Call `lwbtn_portable_process()` frequently**: For responsive button handling, call it at least every 10-20 ms in the main loop. Long blocking delays will cause missed or delayed events.
5. **Button array size**: The `s_btns` array must be large enough for all buttons. The reference uses 1 button (`s_btns[1]`). Increase as needed and update the count in `lwbtn_init_ex()`.
6. **LWBTN_IGNORE_USER_OPTS**: If defined, lwbtn will not include `lwbtn_opts.h`. Either define this in the build system **or** provide the custom config file, but not both.

---

## See Also

- [README.md](./README.md) — brief project description
- `lwbtn/lwbtn/src/include/lwbtn/lwbtn.h` — public API header
- `lwbtn/lwbtn/src/include/lwbtn/lwbtn_opt.h` — configuration options
- `project/usersrc/lwbtn_portable.c` — reference portable layer implementation
- `project/userinc/lwbtn_portable.h` — reference portable layer header
