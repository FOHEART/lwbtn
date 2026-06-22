# LwBTN Library — AT32 Porting Guide

> **Target MCU**: AT32F435 (ARM Cortex-M4, 288 MHz)  
> **Toolchain**: GCC ARM Embedded (arm-none-eabi), EIDE / CMake  
> **Library Version**: LwBTN v1.2.1  
> **Reference Implementation**: See `project/usersrc/lwbtn_portable.c` + `project/userinc/lwbtn_portable.h` in MC1618APP project

---

## Overview

[LwBTN](https://github.com/MaJerle/lwbtn) is a lightweight, feature-rich button manager written in pure C. It provides debounce, click detection, multi-click, long-press hold (keep-alive), and event-driven callbacks — all in a small footprint suitable for embedded MCUs.

**Resource Usage** (measured on AT32F435 with default config, 1 button):
| Component | Flash | RAM |
|-----------|-------|-----|
| LwBTN core (`lwbtn.o`) | ~0.4 KB | 0 bytes (heap) |
| User portable layer (`lwbtn_portable.o`) | ~0.2 KB | ~44 bytes (BSS) |

**Key Features**:
- Debounce support for press and release events (configurable per-button)
- Single-click, multi-click (up to N consecutive clicks), and long-press (keep-alive) detection
- Three operating modes: callback only, manual only, or callback-or-manual
- Configurable timing parameters (debounce, click min/max, multi-click interval, keep-alive period)
- Dynamic per-button timing (optional, adds ~2 bytes per config field per button)
- No heap usage — all structures are statically allocated
- Single C file (`lwbtn.c`) — easy to integrate into any build system

---

## Architecture

```
┌─────────────────────────────┐
│        main.c               │
│  lwbtn_portable_init()      │
│  lwbtn_portable_process()   │  ← called periodically in main loop
└──────────┬──────────────────┘
           │
┌──────────▼──────────────────┐
│    lwbtn_portable.c/h       │  ← Porting layer (you write this)
│  ┌──────────────────────┐   │
│  │ prv_get_state()      │───│──→ Reads GPIO pin state
│  │ prv_event()          │───│──→ Handles LwBTN events (click, press, etc.)
│  └──────────────────────┘   │
└──────────┬──────────────────┘
           │
┌──────────▼──────────────────┐
│    LwBTN core (lwbtn.c)     │  ← Library code (do not modify)
│  ┌──────────────────────┐   │
│  │ Debounce FSM         │   │
│  │ Click / multi-click  │   │
│  │ Keep-alive timer     │   │
│  └──────────────────────┘   │
└─────────────────────────────┘
```

---

## Porting Steps

### Step 1: Add LwBTN as Git Submodule

```bash
# From your project root
git submodule add https://github.com/FOHEART/lwbtn.git lwbtn
git submodule update --init --recursive
```

> If using the upstream directly: `https://github.com/MaJerle/lwbtn.git`

### Step 2: Add LwBTN Source to Build System

#### For EIDE (Embedded IDE)

Add `lwbtn` to source directories in `.eide/eide.yml`:

```yaml
srcDirs:
  - lwbtn            # ← Add this line
```

Add the include path:

```yaml
targets:
  Debug:
    cppPreprocessAttrs:
      incList:
        - lwbtn/lwbtn/src/include   # ← Add this include path
```

If you don't need custom LwBTN configuration, add to the define list:

```yaml
defineList:
  - LWBTN_IGNORE_USER_OPTS   # ← Use defaults, skip lwbtn_opts.h
```

#### For CMake

```cmake
# LwBTN library sources
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/lwbtn/lwbtn/src/lwbtn/lwbtn.c
)

# LwBTN library headers
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/lwbtn/lwbtn/src/include
)
```

---

### Step 3: (Optional) Create Custom `lwbtn_opts.h`

To override timing parameters, create a `lwbtn_opts.h` in your project's include path and **remove** `LWBTN_IGNORE_USER_OPTS`.

Example `project/userinc/lwbtn_opts.h`:

```c
#ifndef LWBTN_HDR_OPTS_H
#define LWBTN_HDR_OPTS_H

/* Custom timing overrides */
#define LWBTN_CFG_TIME_DEBOUNCE_PRESS     15    /* Default: 20 ms */
#define LWBTN_CFG_TIME_CLICK_MIN          30    /* Default: 20 ms */
#define LWBTN_CFG_TIME_CLICK_MAX          500   /* Default: 300 ms */

#endif /* LWBTN_HDR_OPTS_H */
```

> See `lwbtn/lwbtn/src/include/lwbtn/lwbtn_opt.h` for the full option list.

---

### Step 4: Create the Portable Layer Header

**File**: `project/userinc/lwbtn_portable.h`

```c
#ifndef __LWBTN_PORTABLE_H
#define __LWBTN_PORTABLE_H

#include <stdint.h>

void lwbtn_portable_init(void);
int  lwbtn_portable_register_click_callback(int (*f)(unsigned int tick_ms));
void lwbtn_portable_process(void);

#endif /* __LWBTN_PORTABLE_H */
```

---

### Step 5: Implement the Portable Layer Source

**File**: `project/usersrc/lwbtn_portable.c`

#### 5.1 Includes & Static Data

```c
#include "lwbtn_portable.h"
#include "lwbtn/lwbtn.h"
#include "at32f435_437_wk_config.h"   /* For pin/port defines */
#include "user_crm.h"                  /* For getSysTick() */

static lwbtn_btn_t s_btns[1];          /* Button array */
static int (*s_click_cb)(unsigned int) = NULL;
```

#### 5.2 Button State Callback

```c
static uint8_t prv_get_state(struct lwbtn *lwobj, struct lwbtn_btn *btn)
{
    /* Startup protection: ignore button for first 3000ms */
    if (getSysTick() < 3000) {
        return 0;
    }

    /* AT32 GPIO: pull-up, active-low */
    if (gpio_input_data_bit_read(PBOUT_GPIO_PORT, PBOUT_PIN) == SET) {
        return 0;   /* Not pressed */
    } else {
        return 1;   /* Pressed */
    }
}
```

> **AT32 GPIO API**: `gpio_input_data_bit_read(GPIOx, pin_mask)` returns `SET` (high) or `RESET` (low).

#### 5.3 Event Callback

```c
static void prv_event(struct lwbtn *lwobj, struct lwbtn_btn *btn, lwbtn_evt_t evt)
{
    switch (evt) {
        case LWBTN_EVT_ONPRESS:
            /* Button pressed (after debounce) */
            break;

        case LWBTN_EVT_ONRELEASE:
            /* Button released */
            break;

        case LWBTN_EVT_ONCLICK:
            /* Valid click detected */
            if (s_click_cb) {
                s_click_cb(getSysTick());
            }
            printf("[%u] Button Clicked\r\n", getSysTick());
            break;

#if LWBTN_CFG_USE_KEEPALIVE
        case LWBTN_EVT_KEEPALIVE:
            /* Button held — fires every KEEPALIVE_PERIOD ms */
            break;
#endif

        default:
            break;
    }
}
```

#### 5.4 Initialization & Processing

```c
void lwbtn_portable_init(void)
{
    gpio_init_type gpio_init_struct;

    /* Configure GPIO as input with pull-up */
    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
    gpio_init_struct.gpio_pins = PBOUT_PIN;
    gpio_init_struct.gpio_pull = GPIO_PULL_UP;
    gpio_init(PBOUT_GPIO_PORT, &gpio_init_struct);

    /* Init LwBTN with 1 button, callback mode */
    lwbtn_init_ex(NULL, s_btns, 1, prv_get_state, prv_event);
}

int lwbtn_portable_register_click_callback(int (*f)(unsigned int))
{
    s_click_cb = f;
    return 0;
}

void lwbtn_portable_process(void)
{
    lwbtn_process_ex(NULL, getSysTick());
}
```

---

### Step 6: Integrate into `main()`

```c
/* Private includes */
#include "lwbtn_portable.h"

int main(void)
{
    /* ... system init ... */

    /* User code section 2: init */
    lwbtn_portable_init();

    while (1) {
        /* User code section 3: periodic */
        lwbtn_portable_process();

        /* ... other tasks ... */
    }
}
```

---

## Available LwBTN Events

| Event | When Fired | Description |
|-------|-----------|-------------|
| `LWBTN_EVT_ONPRESS` | After valid debounce of press | Button is actively pressed |
| `LWBTN_EVT_ONRELEASE` | After valid debounce of release | Button transitioned from active to inactive |
| `LWBTN_EVT_ONCLICK` | After valid press+release sequence | **Requires**: `LWBTN_CFG_USE_CLICK` = 1 |
| `LWBTN_EVT_KEEPALIVE` | Periodically while button held | **Requires**: `LWBTN_CFG_USE_KEEPALIVE` = 1 |

## Multi-Click & Long-Press Example

```c
/* In lwbtn_opts.h */
#define LWBTN_CFG_TIME_CLICK_MULTI_MAX    400
#define LWBTN_CFG_CLICK_MAX_CONSECUTIVE   2
#define LWBTN_CFG_TIME_KEEPALIVE_PERIOD   250

/* In event callback */
static void prv_event(struct lwbtn *lw, struct lwbtn_btn *btn, lwbtn_evt_t evt)
{
    if (evt == LWBTN_EVT_ONCLICK) {
        uint8_t clicks = lwbtn_click_get_count(btn);
        if (clicks == 1)      /* Single click  */;
        else if (clicks == 2) /* Double click  */;
    }
}
```

## Configuration Reference

| Macro | Default | Description |
|-------|---------|-------------|
| `LWBTN_CFG_TIME_DEBOUNCE_PRESS` | 20 ms | Min stable active time for valid press |
| `LWBTN_CFG_TIME_DEBOUNCE_RELEASE` | 0 ms | Min stable inactive time for valid release |
| `LWBTN_CFG_TIME_CLICK_MIN` | 20 ms | Min press duration for valid click |
| `LWBTN_CFG_TIME_CLICK_MAX` | 300 ms | Max press duration for valid click |
| `LWBTN_CFG_TIME_CLICK_MULTI_MAX` | 400 ms | Max interval for multi-click |
| `LWBTN_CFG_CLICK_MAX_CONSECUTIVE` | 3 | Max consecutive clicks detected |
| `LWBTN_CFG_TIME_KEEPALIVE_PERIOD` | 100 ms | Keep-alive interval while held |
| `LWBTN_CFG_GET_STATE_MODE` | `CALLBACK` | `CALLBACK` / `MANUAL` / `CALLBACK_OR_MANUAL` |
| `LWBTN_CFG_USE_CLICK` | 1 | Enable click detection |
| `LWBTN_CFG_USE_KEEPALIVE` | 1 | Enable keep-alive events |
| `LWBTN_CFG_TIME_VARTYPE` | `uint32_t` | Time variable type |

## Troubleshooting

| Symptom | Likely Cause | Solution |
|---------|-------------|----------|
| No events fired | `lwbtn_process_ex()` not called periodically | Call it in main loop (at least every ~10ms) |
| Click never detected | `LWBTN_CFG_USE_CLICK` disabled | Enable in `lwbtn_opts.h` |
| Buttons trigger randomly | No pull-up/pull-down configured | Check GPIO pull config |
| First press ignored | First-state-inactive protection | Normal behavior |
| Keep-alive not firing | `LWBTN_CFG_USE_KEEPALIVE` disabled | Enable in `lwbtn_opts.h` |
| `lwbtn_opts.h` not found | `LWBTN_IGNORE_USER_OPTS` defined | Remove define or create `lwbtn_opts.h` |

## Complete Integration Checklist

| # | Step | File | Done? |
|---|------|------|-------|
| 1 | Add submodule | `.gitmodules` | ☐ |
| 2 | Add source to build | `.eide/eide.yml` / `CMakeLists.txt` | ☐ |
| 3 | Add include path | `.eide/eide.yml` / `CMakeLists.txt` | ☐ |
| 4 | (Optional) Create `lwbtn_opts.h` | `project/userinc/lwbtn_opts.h` | ☐ |
| 5 | Create portable header | `project/userinc/lwbtn_portable.h` | ☐ |
| 6 | Create portable source | `project/usersrc/lwbtn_portable.c` | ☐ |
| 7 | Add include in `main.c` | `main.c` | ☐ |
| 8 | Call init in `main()` | `main.c` | ☐ |
| 9 | Call process in main loop | `main.c` | ☐ |

## Reference

- **LwBTN Repository**: [https://github.com/MaJerle/lwbtn](https://github.com/MaJerle/lwbtn)
- **FOHEART Fork**: [https://github.com/FOHEART/lwbtn](https://github.com/FOHEART/lwbtn)
- **AT32 Example Project**: [https://github.com/FOHEART/MC1618APP](https://github.com/FOHEART/MC1618APP)
- **STM32 Example**: `examples/LwBTN-NUCLEO-L011K4/`
