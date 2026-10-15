/**
  ******************************************************************************
  * @file    app_lowpower.h
  * @brief   显示低功耗:无操作超时 OLED 灭屏,任意按键唤醒亮屏
  *
  * 灭屏时长四档可选:10s / 30s / 60s / 常亮(永不灭屏),
  * 可在独立设置页(长按 K1)按键调整,档位存 DS1302 RAM,掉电保持。
  *
  * 使用方法(在 main.c 主循环里):
  *   1) KEY_Scan() 之后调用 LP_Process(now),返回 1 表示刚唤醒亮屏,需整屏重绘
  *   2) 取到按键消息后先过 LP_KeyMsgFilter(),返回 1 表示该消息被吞掉(唤醒键)
  *   3) 交互有效的按键事件后调用 LP_NoteActivity(now) 重置灭屏计时
  *   4) 响铃/贪睡期间调用 LP_NoteActivity() 保持亮屏;闹钟触发时用 LP_ForceWake()
  *   5) 刷新显示前用 LP_IsScreenOn() 判断,灭屏时跳过 OLED 刷新以省电
  ******************************************************************************
  */
#ifndef __APP_LOWPOWER_H
#define __APP_LOWPOWER_H

#include <stdint.h>
#include "key.h"

/* ===================== 灭屏时长档位 ===================== */
#define LP_IDX_10S        0       /* 10 秒       */
#define LP_IDX_30S        1       /* 30 秒       */
#define LP_IDX_60S        2       /* 60 秒       */
#define LP_IDX_NEVER      3       /* 常亮不灭屏  */
#define LP_IDX_MAX        3
#define LP_IDX_DEFAULT    LP_IDX_30S

/* 档位存 DS1302 内部 RAM 空闲字节(写 0xE0 / 读 0xE1)。
 * 注意:闹钟配置区 DS1302_SaveCfg 从 0xD0 起按 +2 递增,已占用
 * 0xD0/0xD2/.../0xDE(共 8 字节),故此处从 0xE0 起,互不冲突且掉电保持 */
#define LP_RAM_ADDR_WR    0xE0
#define LP_RAM_ADDR_RD    0xE1

/* 上电初始化:默认亮屏,并从 DS1302 RAM 加载档位(无效则用默认 30s) */
void    LP_Init(uint32_t now);

/* 主循环每帧调用(放在 KEY_Scan 之后);返回 1 = 刚唤醒亮屏,需整屏重绘 */
uint8_t LP_Process(uint32_t now);

/* 1 = 亮屏;0 = 灭屏(调用方应跳过 OLED 刷新) */
uint8_t LP_IsScreenOn(void);

/* 交互按键/贪睡等有效操作时调用,重置灭屏计时 */
void    LP_NoteActivity(uint32_t now);

/* 强制亮屏(闹钟触发):同时重置计时,贪睡期间保持亮屏 */
void    LP_ForceWake(uint32_t now);

/* 按键消息过滤器:返回 1 = 吞掉该消息(灭屏唤醒键不算本次操作) */
uint8_t LP_KeyMsgFilter(KeyMsg_t *msg);

/* ===================== 灭屏时长档位接口 ===================== */
uint8_t      LP_GetTimeoutIdx(void);            /* 当前生效档位 0..3   */
void         LP_SetTimeoutIdx(uint8_t idx);     /* 生效并写 DS1302 RAM */
const char  *LP_TimeoutStr(uint8_t idx);        /* "10s"/"30s"/"60s"/"Never" */

#endif /* __APP_LOWPOWER_H */
