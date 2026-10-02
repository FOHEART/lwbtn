# lwbtn Library — STM32 (HAL) Porting Guide

> **Target MCU**: STM32 + STM32Cube **HAL**（样例：STM32H563ZI / NUCLEO-H563ZI，MB1404 C01，Cortex-M33，250 MHz）
> **Toolchain**: GCC arm-none-eabi + CMake/Ninja（STM32CubeCLT 或 STM32 VS Code 扩展内置工具链）
> **Library Version**: lwbtn v1.3.2
> **Porting Layer**: `Drivers/BSP/lwbtn_port.c` + `Drivers/BSP/lwbtn_port.h`（可复制模板在本目录）

---

## Overview

[lwbtn](https://github.com/MaJerle/lwbtn) 是一个轻量、平台无关的按键/输入引脚管理库，提供：

- 按下与释放事件的软件去抖
- 单击、连击（multi-click）检测
- 长按（keep-alive，周期重复上报）
- 回调驱动的单一事件出口
- 无动态内存分配

本指南记录 **STM32（HAL 工程）** 的移植方式。参考实现使用**回调模式**、**单个板载用户按键**
（NUCLEO 系列的 B1：`BUTTON_USER` → PC13），由**主循环周期调用**驱动状态机。

**与 AT32 参考实现的差异**：

| 项 | AT32 版 | 本指南（STM32 / HAL） |
|----|---------|------------------------|
| 构建系统 | EIDE（`.eide/eide.yml` 的 `srcDirs` / `incList` / `defineList`） | CMake（根 `CMakeLists.txt`）（CubeMX 生成工程） |
| 按键来源 | 自定义 GPIO（`gpio_input_data_bit_read()`） | STM32Cube BSP 的板载按键（`BSP_PB_Init()` / `BSP_PB_GetState()`） |
| 时基 | `getSysTick()` | `HAL_GetTick()` |
| 日志 | `printf` | EasyLogger（`elog_*`，可选） |
| 用户配置 | `LWBTN_IGNORE_USER_OPTS` 全局宏 | 同左 |

**资源占用**（AT32F435 实测、优化构建；本例的 lwbtn 核心与之同源）：

| Component | Flash | RAM |
|-----------|-------|-----|
| lwbtn core (`lwbtn.o`) | ~2 KB | 0 bytes（静态） |
| Portable layer (`lwbtn_port.o`) | ~0.5 KB | ~48 B（1 个 `lwbtn_t` + 1 个 `lwbtn_btn_t`） |

---

## Architecture

```
┌─────────────────────────────────────┐
│             main.c                   │
│  lwbtn_port_init()      ← 初始化     │
│  while (1):                          │
│    lwbtn_port_process() ← 周期处理    │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   lwbtn_port.c / .h                 │  ← 移植层（你写的这一层）
│  ┌─────────────────────────────┐    │
│  │ s_lwbtn / s_btns[1]         │    │  ← 实例与按键数组（静态）
│  │ prv_get_state()             │────│──→ BSP_PB_GetState() 读引脚
│  │ prv_evt()                   │────│──→ elog_i/d() 输出事件日志
│  │ lwbtn_port_init()           │────│──→ BSP_PB_Init() + lwbtn_init_ex()
│  │ lwbtn_port_process()        │────│──→ lwbtn_process_ex(&s_lwbtn, HAL_GetTick())
│  └─────────────────────────────┘    │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│       lwbtn core library            │  ← 库代码（不要修改）
│  lwbtn_init_ex() / lwbtn_process_ex()│
│  lwbtn_btn_t 状态机                  │
└─────────────────────────────────────┘
```

`lwbtn` 的取状态模式默认为 `LWBTN_GET_STATE_MODE_CALLBACK`：库在需要时通过你注册的
`prv_get_state()` 询问引脚是「按下」还是「未按下」，因此**不需要**也**不应该**在 EXTI 中断里
调用库函数。

---

## Porting Steps

### Step 1: Add lwbtn Source to the Build System (CMake)

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    lwbtn/lwbtn/src/lwbtn/lwbtn.c    # 库核心（仅此一个 .c）
    Drivers/BSP/lwbtn_port.c         # 移植层
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Drivers/BSP
    lwbtn/lwbtn/src/include          # 库头文件目录（lwbtn/lwbtn.h 的相对根）
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    LWBTN_IGNORE_USER_OPTS           # 见下方说明，不可省
)
```

> **⚠ `LWBTN_IGNORE_USER_OPTS` 不可省**：库内 `lwbtn/src/include/lwbtn/lwbtn_opt.h` 在未定义该宏时
> 会 `#include "lwbtn_opts.h"`，而 **库仓库里并没有这个文件**（只有 `lwbtn_opts_template.h`）。
> 不定义该宏就会直接编译失败（找不到 `lwbtn_opts.h`）。定义后使用库内默认配置（见
> [Configuration Options](#configuration-options)）。
> 该宏必须在**编译库源码时**可见 —— 把库源码直接加进主 target 时用 `PRIVATE` 即可；
> 若把库做成独立 target，则要写成 `PUBLIC`。

> **⚠ 不要改 CubeMX 生成的 `cmake/stm32cubemx/CMakeLists.txt`** —— 它会被 CubeMX 覆盖；
> 所有手写源码与头文件路径都加在**根** `CMakeLists.txt`。

### Step 2: Create the Portable Layer Files

把本目录下的 `lwbtn_port.c` / `lwbtn_port.h` 复制到你的工程（参考实现放在 `Drivers/BSP/`），
然后按文件头部的「移植修改清单」改：按键来源与有效电平、日志接口、按键个数、事件策略。

### Step 3: Implement Button State Reading Callback

```c
static uint8_t prv_get_state(struct lwbtn *lwobj, struct lwbtn_btn *btn)
{
    (void)lwobj;
    (void)btn;

    return (BSP_PB_GetState(LWBTN_PORT_BTN_BSP) == LWBTN_PORT_BTN_PRESSED_LEVEL) ? 1U : 0U;
}
```

**契约**：返回 `1` 表示「按下（active）」，`0` 表示未按下。库只关心这两个值。

> **⚠ 有效电平要按硬件事实判断，不要照抄注释**：NUCLEO-H563ZI 的 BSP 把该引脚配成输入 +
> `GPIO_PULLDOWN`，且 EXTI 模式用 `GPIO_MODE_IT_RISING`（上升沿）—— 只有「按下 = 高电平」
> 才自洽，故取 `GPIO_PIN_SET`。而 `BSP_PB_GetState()` 上方 ST 写的
> `@retval GPIO_PIN_RESET = button pressed` 是**过期拷贝**（沿用了其它 Nucleo BSP 的注释），
> 与其自身配置矛盾。
>
> **自检**：不按按键时该回调应返回 `0`。若空闲时就返回 `1`（表现为上电即不断产生按下/长按事件），
> 说明有效电平选反了 —— 改 `LWBTN_PORT_BTN_PRESSED_LEVEL` 即可（单点修改）。

### Step 4: Implement Button Event Callback

```c
static void prv_evt(struct lwbtn *lwobj, struct lwbtn_btn *btn, lwbtn_evt_t evt)
{
    (void)lwobj;

    switch (evt) {
        case LWBTN_EVT_ONPRESS:    elog_d("btn", "pressed"); break;
        case LWBTN_EVT_ONRELEASE:  elog_d("btn", "released"); break;
        case LWBTN_EVT_ONCLICK:
            elog_i("btn", "click (count=%u)", (unsigned int)lwbtn_click_get_count(btn));
            break;
        case LWBTN_EVT_KEEPALIVE:
            elog_d("btn", "keep-alive #%u", (unsigned int)lwbtn_keepalive_get_count(btn));
            break;
        default: break;
    }
}
```

**契约**：`LWBTN_EVT_ONCLICK` 是**每次有效单击**都会上报的（连击不清零时陆续上报），
连击次数用 `lwbtn_click_get_count()` 取；达到连击上限（默认 3）时库会立即上报而不等连击窗口结束。
`LWBTN_EVT_KEEPALIVE` 在按住期间按周期（默认 100 ms）重复上报。

> 事件回调在**主循环上下文**执行。日志若为阻塞发送，长按的周期性事件会持续占用串口 ——
> 与共用同一串口的 AT 通道/其它输出会按行交错，属预期；不要在回调里做重活。

### Step 5: Implement Init and Process Functions

```c
static struct lwbtn s_lwbtn;
static lwbtn_btn_t  s_btns[1];

void lwbtn_port_init(void)
{
    (void)BSP_PB_Init(LWBTN_PORT_BTN_BSP, BUTTON_MODE_GPIO);
    (void)lwbtn_init_ex(&s_lwbtn, s_btns, 1U, prv_get_state, prv_evt);
}

void lwbtn_port_process(void)
{
    (void)lwbtn_process_ex(&s_lwbtn, (lwbtn_time_t)HAL_GetTick());
}
```

时基来自 `HAL_GetTick()`（1 ms SysTick 计数，满足 `lwbtn_time_t = uint32_t`）。

> **调用间隔**：应远小于去抖时间（默认 20 ms）。放在主循环里即可；主循环中的长阻塞
> （如 `HAL_Delay(500)`）会让按键事件整体推迟，甚至把多次跳变合并成一次。

### Step 6: Configure the Button GPIO

两条路，任选其一：

- **板载按键（推荐，样板做法）**：CubeMX 里把 BSP 的 BUTTON 组件选中（`.ioc` 中
  `NUCLEO-xxx.BUTTON=1`），由 `BSP_PB_Init()` 负责时钟与引脚配置 —— **不需要**手工配 GPIO，
  也不需要定义引脚宏。
- **自绘板 / 外接按键**：在 `.ioc` 里配一个 GPIO 输入（按硬件接法选上拉或下拉），
  然后在 `prv_get_state()` 里换成 `HAL_GPIO_ReadPin()`，并把
  `LWBTN_PORT_BTN_PRESSED_LEVEL` 按实际有效电平改掉。

### Step 7: Integrate into main.c

只改 `/* USER CODE BEGIN/END */` 区域（CubeMX 重新生成时只有这些区域会被保留）：

```c
/* USER CODE BEGIN Includes */
#include "lwbtn_port.h"
/* USER CODE END Includes */

int main(void)
{
    /* ... CubeMX 初始化 ... */
    /* USER CODE BEGIN 2 */
    lwbtn_port_init();          /* 在 GPIO/BSP 初始化之后、日志初始化之后 */
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN WHILE */
        lwbtn_port_process();   /* 非阻塞；无事件时立即返回 */
        /* ... 其它任务 ... */
    }
}
```

> **⚠ 清理 CubeMX 的按键演示代码**：当 `.ioc` 中 `BSP_Common_DEMO=true` 时，CubeMX 会在
> `main.c` 里生成示例代码：`BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI)`（在 USER CODE 区之外）、
> `BspButtonState`、`BSP_PB_Callback()`，以及主循环里「按下按键翻转 LED」的片段。
> 这些是**示例行为**，会与你的按键用途冲突（例如让按键同时点灯）。要么在 CubeMX 里把
> demonstration code 关掉再重新生成，要么至少删掉主循环里那段 LED 翻转（该片段位于
> `USER CODE BEGIN WHILE` 内，可以安全手改）。
> 另外 `BSP_PB_Init()` 会被调用两次（示例的 EXTI 模式 + 本移植层的 GPIO 模式）：引脚仍可正常
> 读取，但若两者模式冲突，以**后调用者**为准，建议只保留一处。

---

## Configuration Options

默认配置来自库内 `lwbtn/src/include/lwbtn/lwbtn_opt.h`（定义 `LWBTN_IGNORE_USER_OPTS` 时生效）：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `LWBTN_CFG_GET_STATE_MODE` | `LWBTN_GET_STATE_MODE_CALLBACK` | 由回调提供状态（本指南用法） |
| `LWBTN_CFG_TIME_DEBOUNCE_PRESS` | `20` ms | 按下去抖 |
| `LWBTN_CFG_TIME_DEBOUNCE_RELEASE` | `0` ms | 释放去抖 |
| `LWBTN_CFG_TIME_CLICK_MIN` / `MAX` | `20` / `300` ms | 有效单击时长窗口 |
| `LWBTN_CFG_TIME_CLICK_MULTI_MAX` | `400` ms | 连击间隔窗口 |
| `LWBTN_CFG_CLICK_MAX_CONSECUTIVE` | `3` | 连击上限（达到即立即上报） |
| `LWBTN_CFG_TIME_KEEPALIVE_PERIOD` | `100` ms | 长按上报周期 |
| `LWBTN_CFG_TIME_VARTYPE` | `uint32_t` | 时间变量类型 |

**要自定义**：不要改库内文件。在工程里新增一个 `lwbtn_opts.h`（可复制
`lwbtn_opts_template.h`），放到 `lwbtn/src/include/lwbtn/` 可见的 include 路径上，并**去掉**
`LWBTN_IGNORE_USER_OPTS` 宏。少量参数也可在运行期用 `lwbtn_debounce_set_press_time()`、
`lwbtn_click_set_time_max()` 等接口按按键调整。

---

## Callback Reference

| 回调 | 原型 | 契约 |
|------|------|------|
| 取状态 | `uint8_t (*)(struct lwbtn *, struct lwbtn_btn *)` | `1` = 按下（active），`0` = 未按下；非阻塞；只读引脚 |
| 事件 | `void (*)(struct lwbtn *, struct lwbtn_btn *, lwbtn_evt_t)` | 在 `lwbtn_process_ex()` 内同步调用；事件见 `LWBTN_EVT_*` |

---

## API Reference

| 函数 | 用途 |
|------|------|
| `lwbtn_init_ex(lwobj, btns, cnt, get_state_fn, evt_fn)` | 初始化实例与按键数组 |
| `lwbtn_process_ex(lwobj, mstime)` | 周期处理（传入毫秒时基） |
| `lwbtn_is_btn_active(btn)` | 当前是否处于按下 |
| `lwbtn_click_get_count(btn)` | 本次连击计数 |
| `lwbtn_keepalive_get_count(btn)` | 长按已上报次数 |
| `lwbtn_reset(lwobj, btn)` | 复位按键状态机 |
| `lwbtn_debounce_set_press_time()` / `lwbtn_click_set_time_max()` / `lwbtn_keepalive_set_period()` 等 | 运行期调整时序 |

---

## Integration Checklist

| # | 步骤 | 文件 | 完成 |
|---|------|------|------|
| 1 | 把 `lwbtn/lwbtn/src/lwbtn/lwbtn.c` 加入构建 | 根 `CMakeLists.txt` | ☐ |
| 2 | 把 `lwbtn/lwbtn/src/include` 加入头文件路径 | 根 `CMakeLists.txt` | ☐ |
| 3 | 定义 `LWBTN_IGNORE_USER_OPTS`（否则报缺 `lwbtn_opts.h`） | 根 `CMakeLists.txt` | ☐ |
| 4 | 复制并修改 `lwbtn_port.{c,h}`（按键源、有效电平、日志） | `Drivers/BSP/` | ☐ |
| 5 | 确认有效电平：空闲时 `prv_get_state()` 返回 0 | `lwbtn_port.c` | ☐ |
| 6 | `lwbtn_port_init()` 在 GPIO/BSP 与日志初始化之后调用 | `main.c`（USER CODE 2） | ☐ |
| 7 | `lwbtn_port_process()` 在主循环内周期调用 | `main.c`（USER CODE WHILE） | ☐ |
| 8 | 清理 CubeMX 生成的按键演示行为（LED 翻转等） | `main.c` / `.ioc` | ☐ |
| 9 | 编译通过，且 `.bss` 中出现移植层的实例与按键数组、无新增 `malloc` | `DHCap.map` | ☐ |
| 10 | 上板逐条验证：单击、双击、连击上限、长按周期、抖动不误触发 | 串口输出 | ☐ |

---

## Potential Pitfalls

1. **没有定义 `LWBTN_IGNORE_USER_OPTS`** → 编译报找不到 `lwbtn_opts.h`（库内只有 template）。见 Step 1。
2. **有效电平选反** → 空闲即持续上报按下/长按。判断依据看硬件接法与 BSP 的 pull 配置、
   EXTI 触发沿，**不要**照抄 BSP 注释。见 Step 3。
3. **把库调用放进 EXTI 中断** → 回调模式下不需要中断；在中断里跑状态机 + 打日志会拉长中断延迟，
   并可能与主循环的输出交错。
4. **主循环里长阻塞** → 按键事件整体推迟，快速连击可能被合并。
5. **`lwbtn_process_ex()` 传入错误的时基单位** → 必须是**毫秒**（`HAL_GetTick()`），传秒或 tick 会
   让去抖/连击窗口完全失真。
6. **在 ISR 或高频路径里读日志** → 事件回调里的日志是阻塞发送，长按期间每周期一行。
7. **CubeMX 重新生成后示例代码回归** → `BSP_Common_DEMO=true` 会把「按键翻转 LED」的示例带回
   `main.c` 的生成区。见 Step 7。
8. **多按键时忘了扩大数组** → `lwbtn_init_ex()` 的 `btns_cnt` 与实际数组长度不符会越界。
9. **手改了 `cmake/stm32cubemx/CMakeLists.txt`** → 会被 CubeMX 覆盖；源码清单加在根 `CMakeLists.txt`。

## See Also

- [AT32 Porting Guide](../at32/AT32_PortGuide.md) —— 同一库的 AT32（EIDE + 自定义 GPIO）参考移植
- [lwbtn 官方文档](https://docs.majerle.eu/projects/lwbtn/)
- [上游项目](https://github.com/MaJerle/lwbtn)
