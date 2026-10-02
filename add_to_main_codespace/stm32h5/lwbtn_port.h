/**
 * @file    lwbtn_port.h
 * @brief   lwbtn 硬件适配层模板 —— STM32 (HAL)，样例平台 STM32H563ZI + 板载用户按键
 * @date    2026-10-02
 *
 * 本文件是「复制到你的工程后按目标板卡修改」的参考代码，配合同目录的
 * STM32_PortGuide.md 使用。lwbtn 库源码不直接调用任何外设，硬件相关实现
 * （读引脚 + 事件上报 + 毫秒时基）全部集中在这一对文件里。
 *
 * 常用修改点：
 *   1. 按键来源与有效电平：见 lwbtn_port.c 顶部的「板级常量」段
 *   2. 日志接口：样例用 EasyLogger（elog_*）；不用日志库时删掉 <elog.h> 与 elog_* 调用
 *   3. 函数名如需按外设区分（如 user_lwbtn_port_init），同时改 .c 与调用处
 */

#ifndef __LWBTN_PORT_H__
#define __LWBTN_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 初始化按键管理：配置按键引脚并注册 lwbtn 回调。
 * @note  需在 GPIO / BSP 初始化之后调用；事件回调内会打日志，因此还应在
 *        日志库初始化之后调用。
 */
void lwbtn_port_init(void);

/**
 * @brief 周期处理按键状态机：读取引脚、去抖、产生事件。
 * @note  在主循环中周期调用（非阻塞，无事件时立即返回）；
 *        调用间隔应远小于去抖时间（库默认 20 ms），主循环的长阻塞会推迟按键响应。
 */
void lwbtn_port_process(void);

#ifdef __cplusplus
}
#endif

#endif /* __LWBTN_PORT_H__ */
