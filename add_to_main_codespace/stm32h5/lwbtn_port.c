/**
 * @file    lwbtn_port.c
 * @brief   lwbtn 硬件适配层模板实现 —— STM32 (HAL)，样例平台 STM32H563ZI + 板载用户按键
 * @date    2026-10-02
 *
 * 移植方式：把本文件与 lwbtn_port.h 复制到你的工程（参考实现放在 Drivers/BSP/），
 * 按下面「移植修改清单」逐项改，再把源文件加进构建系统即可。
 *
 * 组成：
 *   - 读引脚回调：读按键引脚，按有效电平换算成 1 = 按下（lwbtn 的约定）；
 *   - 事件回调：按下 / 释放 / 单击（含连击计数）/ 长按（keep-alive）→ 输出日志行；
 *   - 初始化与周期处理：初始化引脚，主循环用 HAL_GetTick() 驱动 lwbtn 状态机。
 *
 * 移植修改清单：
 *   [1] 按键来源与有效电平（下方「板级常量」段）—— 最关键的修改点
 *   [2] 日志接口：样例用 EasyLogger；无日志库时删掉 <elog.h> 与 elog_* 调用
 *   [3] 按键个数：样例为单按键；多按键时扩大 s_btns[] 并逐个填写参数
 *   [4] 事件策略：样例对所有事件打日志；按需减少（例如只保留单击/长按）
 *
 * 构建系统集成（CMake 示例，注意 LWBTN_IGNORE_USER_OPTS 不可省）：
 *   target_sources(${CMAKE_PROJECT_NAME} PRIVATE
 *       lwbtn/lwbtn/src/lwbtn/lwbtn.c
 *       Drivers/BSP/lwbtn_port.c
 *   )
 *   target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
 *       lwbtn/lwbtn/src/include
 *   )
 *   target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
 *       LWBTN_IGNORE_USER_OPTS
 *   )
 */

#include "lwbtn_port.h"

#include "main.h"
/* [1] 样例用 STM32Cube BSP 的板载按键（NUCLEO 系列：B1 = BUTTON_USER → PC13）。
 *     换成自绘板/外接按键时，把这个头与下方的 BSP_* 调用一起换掉。 */
#include "stm32h5xx_nucleo.h" /* BSP_PB_* / BUTTON_USER / BUTTON_MODE_GPIO */
#include <elog.h>             /* [2] 可选 */
#include "lwbtn/lwbtn.h"

/* ================================================================== */
/*  板级常量（换板时只需改这一段）                                       */
/* ================================================================== */

/** [1] 按键对象（样例：BSP 的板载用户按键）。 */
#define LWBTN_PORT_BTN_BSP          BUTTON_USER

/**
 * @brief [1] 按键「按下」时的引脚电平。
 *
 * 判断依据要取硬件事实，不要照抄注释：例如 NUCLEO-H563ZI 的 BSP 把该引脚配成
 * 输入 + `GPIO_PULLDOWN`，且 EXTI 模式用 `GPIO_MODE_IT_RISING`（上升沿）——
 * 两者只有在「按下 = 高电平」时才自洽，因此这里取 `GPIO_PIN_SET`。
 * （对照：`BSP_PB_GetState()` 上方的 `@retval GPIO_PIN_RESET = button pressed`
 *  是 ST 从其它 Nucleo BSP 沿用的过期注释，与本板实际连接不符。）
 *
 * 自检方法：不按按键时该回调应返回 0；若空闲时就返回 1，说明有效电平选反了。
 */
#define LWBTN_PORT_BTN_PRESSED_LEVEL   GPIO_PIN_SET

/* ================================================================== */
/*  静态状态（全部静态分配，无动态内存）                                 */
/* ================================================================== */

static struct lwbtn s_lwbtn;
static lwbtn_btn_t s_btns[1]; /* [3] 多按键时扩大此数组 */

/* ================================================================== */
/*  lwbtn 回调                                                         */
/* ================================================================== */

/**
 * @brief 读引脚状态回调：由 lwbtn 在需要判断按键状态时调用。
 * @return 1 = 按键处于按下（active）状态；0 = 未按下
 */
static uint8_t prv_get_state(struct lwbtn *lwobj, struct lwbtn_btn *btn)
{
  (void)lwobj;
  (void)btn;

  return (BSP_PB_GetState(LWBTN_PORT_BTN_BSP) == LWBTN_PORT_BTN_PRESSED_LEVEL) ? 1U : 0U;
}

/**
 * @brief 按键事件回调：默认只输出日志行。
 * @note  keep-alive（长按）由库按周期（默认 100 ms）上报，因此每个周期只打印
 *        一行，不需要额外节流。
 */
static void prv_evt(struct lwbtn *lwobj, struct lwbtn_btn *btn, lwbtn_evt_t evt)
{
  (void)lwobj;

  switch (evt)
  {
    case LWBTN_EVT_ONPRESS:
      elog_d("btn", "pressed");
      break;

    case LWBTN_EVT_ONRELEASE:
      elog_d("btn", "released");
      break;

    case LWBTN_EVT_ONCLICK:
      elog_i("btn", "click (count=%u)", (unsigned int)lwbtn_click_get_count(btn));
      break;

    case LWBTN_EVT_KEEPALIVE:
      elog_d("btn", "keep-alive #%u", (unsigned int)lwbtn_keepalive_get_count(btn));
      break;

    default:
      break;
  }
}

/* ================================================================== */
/*  对外接口                                                           */
/* ================================================================== */

void lwbtn_port_init(void)
{
  (void)BSP_PB_Init(LWBTN_PORT_BTN_BSP, BUTTON_MODE_GPIO);
  (void)lwbtn_init_ex(&s_lwbtn, s_btns, 1U, prv_get_state, prv_evt);
}

void lwbtn_port_process(void)
{
  (void)lwbtn_process_ex(&s_lwbtn, (lwbtn_time_t)HAL_GetTick());
}
