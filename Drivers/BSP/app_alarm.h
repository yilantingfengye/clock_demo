/**
  ******************************************************************************
  * @file    app_alarm.h
  * @brief   闹钟逻辑 + 时钟/闹钟设置模式机
  *
  * 移植要点：
  *   1) 核对 TIME_IDX_* 与你 DS1302 驱动里 time_data[] 的下标顺序
  *   2) 若你的 time_data[] 已经是十进制，把 ALARM_TIME_IS_BCD 改为 0
  *   3) 在 main.h 里重定义 BEEP_ON() / BEEP_OFF() 指向你的蜂鸣器 IO
  ******************************************************************************
  */
#ifndef __APP_ALARM_H
#define __APP_ALARM_H

#include <stdint.h>
#include "key.h"

/* ===================== 1. 时间数组下标（务必核对！） ===================== */
typedef enum
{
    TIME_IDX_SEC   = 0,
    TIME_IDX_MIN   = 1,
    TIME_IDX_HOUR  = 2,
    TIME_IDX_DAY   = 3,
    TIME_IDX_MONTH = 4,
    TIME_IDX_WEEK  = 5
} TimeIdx_t;

#define ALARM_TIME_IS_BCD   1       /* DS1302 原始值为 BCD → 1；已转十进制 → 0 */

/* ===================== 2. 鸣响参数 ===================== */
#define ALARM_RING_ON_MS    500     /* 响                    */
#define ALARM_RING_OFF_MS   500     /* 停                    */
#define ALARM_RING_TOTAL_MS 60000UL /* 无人应答，最长响 60s  */
#define ALARM_SNOOZE_MS     (5UL * 60UL * 1000UL)  /* 贪睡 5 分钟 */

/* ===================== 3. 蜂鸣器（默认弱实现，建议在 main.h 覆盖） ======= */
#ifndef BEEP_ON
extern void BEEP_On(void);
extern void BEEP_Off(void);
#define BEEP_ON()  BEEP_On()
#define BEEP_OFF() BEEP_Off()
#endif

/* ===================== 4. 设置模式 ===================== */
typedef enum
{
    MODE_NORMAL = 0,        /* 正常走时 */
    MODE_SET_CLOCK_HOUR,    /* 调时钟：时 */
    MODE_SET_CLOCK_MIN,     /* 调时钟：分 */
    MODE_SET_ALARM_HOUR,    /* 调闹钟：时 */
    MODE_SET_ALARM_MIN,     /* 调闹钟：分 */
    MODE_MAX
} AppMode_t;

/* ===================== 5. 闹钟状态 ===================== */
typedef enum
{
    ALARM_IDLE = 0,
    ALARM_RINGING,
    ALARM_SNOOZED
} AlarmState_t;

/* ===================== 6. 对外接口 ===================== */
void     Alarm_Init(void);

/* 主循环里每 100ms 调用一次；cur_time 为 DS1302 读到的 6 字节时间 */
void     Alarm_Process(uint32_t now_ms, const uint8_t *cur_time);

/* 按键入口：把 key.c 取到的消息喂进来即可 */
void     App_KeyProcess(KeyMsg_t *msg);

/* 状态查询 */
AlarmState_t Alarm_GetState(void);
AppMode_t    App_GetMode(void);
uint8_t      Alarm_IsEnabled(void);
uint8_t      Alarm_GetHour(void);
uint8_t      Alarm_GetMin(void);

/* 外部（如 OLED 显示）可直接读这两个数组 */
extern volatile uint8_t g_time_data[6];
extern volatile uint8_t g_bell_time[3];   /* [0]=时 [1]=分 [2]=秒(保留) */
extern volatile uint8_t g_bell_enabled;

#endif /* __APP_ALARM_H */
