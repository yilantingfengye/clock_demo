/**
  ******************************************************************************
  * @file    app_alarm.c
  * @brief   闹钟匹配 / 鸣响节拍 / 贪睡 + 时钟与闹钟的设置模式机
  ******************************************************************************
  */
#include "app_alarm.h"
#include <string.h>

/* ===================== 宏工具 ===================== */
#define BCD2DEC(x)  (((((x) >> 4) & 0x0F) * 10) + ((x) & 0x0F))
#define DEC2BCD(x)  ((((x) / 10) << 4) | ((x) % 10))

#if ALARM_TIME_IS_BCD
  #define T2DEC(x)  BCD2DEC(x)
  #define D2T(x)    DEC2BCD(x)
#else
  #define T2DEC(x)  (x)
  #define D2T(x)    (x)
#endif

#define WRAP_INC(v, max)  do { (v)++; if ((v) >= (max)) (v) = 0; } while (0)
#define WRAP_DEC(v, max)  do { if ((v) == 0) (v) = (max) - 1; else (v)--; } while (0)

/* ===================== 全局变量 ===================== */
volatile uint8_t g_time_data[6]  = {0};   /* DS1302 原始时间               */
volatile uint8_t g_bell_time[3]  = {0};   /* 闹钟：十进制 [时,分,秒(保留)] */
volatile uint8_t g_bell_enabled  = 0;

/* ===================== 内部状态 ===================== */
static AlarmState_t s_state      = ALARM_IDLE;
static AppMode_t    s_mode       = MODE_NORMAL;

static uint32_t     s_ring_start_ms = 0;
static uint8_t      s_beep_on       = 0;

/* 防重复触发：记录"已经响过的那个分钟" */
static uint8_t      s_fired_h = 0xFF;
static uint8_t      s_fired_m = 0xFF;

/* 贪睡目标时间（十进制） */
static uint8_t      s_snooze_h = 0xFF;
static uint8_t      s_snooze_m = 0xFF;

/* ===================== 外部依赖（按你的工程改名即可） ===================== */
extern void DS1302Z_SetTime(uint8_t *time);   /* 把 g_time_data 写回 DS1302 */

/* ===================== 内部函数 ===================== */
static void Alarm_StartRinging(uint32_t now_ms)
{
    s_state          = ALARM_RINGING;
    s_ring_start_ms  = now_ms;
    s_beep_on        = 1;
    BEEP_ON();
}

/**
 * @brief  停止本次闹铃；to_snooze = 1 表示 5 分钟后再响
 */
static void Alarm_Stop(uint8_t to_snooze)
{
    BEEP_OFF();
    s_beep_on = 0;

    if (to_snooze)
    {
        uint8_t h = g_bell_time[0];
        uint8_t m = g_bell_time[1] + 5;
        if (m >= 60) { m -= 60; WRAP_INC(h, 24); }

        s_snooze_h = h;
        s_snooze_m = m;
        s_state    = ALARM_SNOOZED;
    }
    else
    {
        s_snooze_h = 0xFF;
        s_snooze_m = 0xFF;
        s_state    = ALARM_IDLE;
    }
}

/* ===================== 对外接口 ===================== */
void Alarm_Init(void)
{
    s_state = ALARM_IDLE;
    s_mode  = MODE_NORMAL;
    s_fired_h = s_fired_m = 0xFF;
    s_snooze_h = s_snooze_m = 0xFF;
    BEEP_OFF();
}

/**
 * @brief  每 100ms 调用一次
 * @param  now_ms   : HAL_GetTick()
 * @param  cur_time : DS1302 读到的 6 字节时间数组
 */
void Alarm_Process(uint32_t now_ms, const uint8_t *cur_time)
{
    uint8_t ch, cm;

    if (cur_time == NULL) return;

    memcpy((void *)g_time_data, cur_time, sizeof(g_time_data));

    ch = T2DEC(g_time_data[TIME_IDX_HOUR]);
    cm = T2DEC(g_time_data[TIME_IDX_MIN]);

    /* ---------- 1. 正在响：驱动鸣响节奏 + 超时保护 ---------- */
    if (s_state == ALARM_RINGING)
    {
        uint32_t elapsed = now_ms - s_ring_start_ms;

        if (elapsed >= ALARM_RING_TOTAL_MS)          /* 无人应答，自动停 */
        {
            Alarm_Stop(0);
        }
        else
        {
            uint32_t phase = elapsed % (ALARM_RING_ON_MS + ALARM_RING_OFF_MS);
            uint8_t  want  = (phase < ALARM_RING_ON_MS) ? 1 : 0;   /* 响 500ms / 停 500ms */

            if (want != s_beep_on)
            {
                s_beep_on = want;
                if (want) BEEP_ON(); else BEEP_OFF();
            }
        }
        return;                                      /* 响铃期间不做匹配 */
    }

    /* ---------- 2. 匹配闹钟 ---------- */
    uint8_t target_h, target_m;

    if (s_state == ALARM_SNOOZED)
    {
        target_h = s_snooze_h;
        target_m = s_snooze_m;
    }
    else
    {
        target_h = g_bell_time[0];
        target_m = g_bell_time[1];
    }

    if (!g_bell_enabled || target_h > 23 || target_m > 59) return;

    /* 同一分钟只触发一次：分钟变了自然解锁 */
    if (ch == target_h && cm == target_m && !(ch == s_fired_h && cm == s_fired_m))
    {
        s_fired_h = ch;
        s_fired_m = cm;
        Alarm_StartRinging(now_ms);
    }
}

/**
 * @brief  按键处理：模式切换 / 调时 / 闹钟开关 / 停闹
 */
void App_KeyProcess(KeyMsg_t *msg)
{
    uint8_t *p;
    uint8_t  max;

    if (msg == NULL) return;

    /* ---------- 响铃中：任意键处理 ---------- */
    if (s_state == ALARM_RINGING)
    {
        if (msg->evt == KEY_EVT_SHORT)
        {
            /* 短按 = 贪睡 5 分钟；长按 = 彻底关闭本次 */
            Alarm_Stop(1);
        }
        else if (msg->evt == KEY_EVT_LONG)
        {
            Alarm_Stop(0);
        }
        return;
    }

    /* ---------- 长按连发 / 短按都当作一次加减 ---------- */
    int8_t delta = 0;
    if (msg->evt == KEY_EVT_SHORT || msg->evt == KEY_EVT_REPEAT)
    {
        if (msg->id == KEY_UP)   delta =  1;
        if (msg->id == KEY_DOWN) delta = -1;
    }

    switch (s_mode)
    {
    /* ============ 正常模式 ============ */
    case MODE_NORMAL:
        if (msg->evt != KEY_EVT_SHORT) break;

        if (msg->id == KEY_MODE)
        {
            s_mode = MODE_SET_CLOCK_HOUR;
        }
        else if (msg->id == KEY_ALARM)
        {
            g_bell_enabled = !g_bell_enabled;      /* 闹钟开 / 关 */
        }
        break;

    /* ============ 调时钟 ============ */
    case MODE_SET_CLOCK_HOUR:
    case MODE_SET_CLOCK_MIN:
        p   = (s_mode == MODE_SET_CLOCK_HOUR)
              ? (uint8_t *)&g_time_data[TIME_IDX_HOUR]
              : (uint8_t *)&g_time_data[TIME_IDX_MIN];
        max = (s_mode == MODE_SET_CLOCK_HOUR) ? 24 : 60;

        if (delta != 0)
        {
            uint8_t v = T2DEC(*p);
            if (delta > 0) WRAP_INC(v, max); else WRAP_DEC(v, max);
            *p = D2T(v);
        }
        else if (msg->id == KEY_MODE && msg->evt == KEY_EVT_SHORT)
        {
            s_mode = (s_mode == MODE_SET_CLOCK_HOUR) ? MODE_SET_CLOCK_MIN
                                                     : MODE_SET_ALARM_HOUR;
        }
        else if (msg->id == KEY_ALARM && msg->evt == KEY_EVT_SHORT)
        {
            DS1302Z_SetTime((uint8_t *)g_time_data);   /* 保存并退出 */
            s_mode = MODE_NORMAL;
        }
        break;

    /* ============ 调闹钟 ============ */
    case MODE_SET_ALARM_HOUR:
    case MODE_SET_ALARM_MIN:
        p   = (s_mode == MODE_SET_ALARM_HOUR)
              ? (uint8_t *)&g_bell_time[0]
              : (uint8_t *)&g_bell_time[1];
        max = (s_mode == MODE_SET_ALARM_HOUR) ? 24 : 60;

        if (delta != 0)
        {
            uint8_t v = *p;                          /* 闹钟用十进制存 */
            if (delta > 0) WRAP_INC(v, max); else WRAP_DEC(v, max);
            *p = v;
        }
        else if (msg->id == KEY_MODE && msg->evt == KEY_EVT_SHORT)
        {
            s_mode = (s_mode == MODE_SET_ALARM_HOUR) ? MODE_SET_ALARM_MIN
                                                     : MODE_NORMAL;
        }
        else if (msg->id == KEY_ALARM && msg->evt == KEY_EVT_SHORT)
        {
            g_bell_enabled = 1;                      /* 设完默认打开 */
            s_mode = MODE_NORMAL;
        }
        break;

    default:
        s_mode = MODE_NORMAL;
        break;
    }
}

/* ===================== 查询接口 ===================== */
AlarmState_t Alarm_GetState(void)  { return s_state;      }
AppMode_t    App_GetMode(void)     { return s_mode;       }
uint8_t      Alarm_IsEnabled(void) { return g_bell_enabled; }
uint8_t      Alarm_GetHour(void)   { return g_bell_time[0]; }
uint8_t      Alarm_GetMin(void)    { return g_bell_time[1]; }
