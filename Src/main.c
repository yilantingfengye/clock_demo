/**
  ******************************************************************************
  * @file    main.c
  * @author  MCU Software Team
  * @Version V1.2.0
  * @Date    2021-01-11
  * @brief   main function
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "app_lowpower.h"

/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private user code ---------------------------------------------------------*/

/**
  * @brief  The application entry point.
  * @retval int
  */

uint8_t time_data[6] = {0};  /* 时间数据缓存 */
uint8_t bell_data[3] = {0};  /* 铃声数据缓存 */
uint8_t bell_flag = 0;
uint8_t bell_onoff = 0;

/* ===================== 设置字段 / 闹钟配置 ===================== */
typedef enum
{
    SET_NONE = 0,
    SET_YEAR,
    SET_MONTH,
    SET_DAY,
    SET_HOUR,
    SET_MINUTE,
    SET_SECOND,
    SET_WEEK,
    /* ---- 闹钟相关字段 ---- */
    SET_AL1_EN,      /* 闹钟1:使能开关 */
    SET_AL1_H,       /* 闹钟1:小时 */
    SET_AL1_M,       /* 闹钟1:分钟 */
    SET_AL2_EN,      /* 闹钟2:使能开关 */
    SET_AL2_H,       /* 闹钟2:小时 */
    SET_AL2_M,       /* 闹钟2:分钟 */
    SET_BEEP,        /* 蜂鸣器选择(有源1/有源2/无源) */
    SET_FIELD_MAX
} SetField_t;

static SetField_t       s_set_field = SET_NONE;
static struct TIMEData  s_edit;            /* 设置模式下正在编辑的时间副本 */
static uint32_t        s_last_refresh_ms = 0;
static uint32_t        s_last_blink_ms   = 0;  /* 闪烁计时基准 */
static uint8_t         s_blink_hide = 0;   /* 闪烁隐藏:1=隐藏当前字段 */

/* ===================== 闹钟配置(保存在 DS1302 RAM) =====================
 * 共 8 字节: 魔数 + 蜂鸣器选择 + 闹钟1(使能/时/分) + 闹钟2(使能/时/分)
 * DS1302 内部 RAM 从 0xD0(写)/0xD1(读) 起连续 8 字节,掉电保持
 *   闹钟1 = 起床闹钟(持续响 + 贪睡)   闹钟2 = 待办闹钟(短促3声自动停)
 */
#define AL_CFG_LEN        8
#define AL_CFG_MAGIC      0xA5
#define AL_CFG_IDX_MAGIC  0
#define AL_CFG_IDX_BEEP   1
#define AL_CFG_IDX_AL1_EN 2
#define AL_CFG_IDX_AL1_H  3
#define AL_CFG_IDX_AL1_M  4
#define AL_CFG_IDX_AL2_EN 5
#define AL_CFG_IDX_AL2_H  6
#define AL_CFG_IDX_AL2_M  7

/* 响铃时序参数(单位:毫秒/次数) */
#define AL_WAKE_ON_MS     500u                   /* 起床闹钟:响 500ms */
#define AL_WAKE_OFF_MS    500u                   /* 起床闹钟:停 500ms */
#define AL_WAKE_TOTAL_MS  60000u                 /* 起床闹钟最长响 60s */
#define AL_TODO_ON_MS     200u                   /* 待办闹钟:响 200ms */
#define AL_TODO_OFF_MS    200u                   /* 待办闹钟:停 200ms */
#define AL_TODO_BEEPS     3u                     /* 待办闹钟:共响 3 声自动停 */
#define AL_SNOOZE_MIN     5u                     /* 贪睡:5 分钟后重响 */

static uint8_t s_cfg[AL_CFG_LEN];        /* 当前生效的配置 */
static uint8_t s_edit_cfg[AL_CFG_LEN];   /* 设置模式下的配置副本 */

/* 闹钟状态机 */
typedef enum { AL_IDLE = 0, AL_RING, AL_SNOOZE } AlarmState_t;

static AlarmState_t s_al_state      = AL_IDLE;
static uint8_t      s_al_which      = 0;     /* 0=起床闹钟, 1=待办闹钟 */
static uint8_t      s_al_on         = 0;     /* 1=当前处于"响"相位 */
static uint32_t     s_al_phase_ms   = 0;     /* 当前相位起始时刻 */
static uint32_t     s_al_start_ms   = 0;     /* 响铃开始时刻 */
static uint8_t      s_al_beeps      = 0;     /* 待办闹钟已响次数 */
static uint8_t      s_al_fired_h[2] = {0xFF, 0xFF};  /* 已触发闹钟的时(防重复) */
static uint8_t      s_al_fired_m[2] = {0xFF, 0xFF};  /* 已触发闹钟的分(防重复) */
static uint16_t     s_snooze_min    = 0;     /* 贪睡目标分钟(0~1439) */
static uint8_t      s_al_ui_dirty   = 0;     /* 界面需重绘标志 */

/* 设置字段在 OLED 上的显示位置(line 0/1/2/3,字符宽 8 像素) */
typedef struct
{
    uint8_t line;
    uint8_t x_char;
    uint8_t len;
} FieldPos_t;

static const FieldPos_t s_field_pos[SET_FIELD_MAX - 1] =
{
    { 0,  0, 4 },  /* SET_YEAR   */
    { 0,  5, 2 },  /* SET_MONTH  */
    { 0,  8, 2 },  /* SET_DAY    */
    { 1,  0, 2 },  /* SET_HOUR   */
    { 1,  3, 2 },  /* SET_MINUTE */
    { 1,  6, 2 },  /* SET_SECOND */
    { 0, 12, 1 },  /* SET_WEEK   */
    { 3,  0, 1 },  /* SET_AL1_EN */
    { 3,  2, 2 },  /* SET_AL1_H  */
    { 3,  5, 2 },  /* SET_AL1_M  */
    { 3,  9, 1 },  /* SET_AL2_EN */
    { 3, 11, 2 },  /* SET_AL2_H  */
    { 3, 14, 2 },  /* SET_AL2_M  */
    { 2,  0,16 },  /* SET_BEEP(独占第 2 行) */
};

/* 循环回绕:delta 为增=+1/减=-1,超出范围时回绕到 lo/hi */
static int32_t clamp_wrap(int32_t v, int32_t lo, int32_t hi, int32_t delta)
{
    v += delta;
    if (v < lo) v = hi;
    if (v > hi) v = lo;
    return v;
}

/* 调整字段:t 为时间结构,cfg 为配置缓冲区 */
static void adjust_field(struct TIMEData *t, uint8_t *cfg, SetField_t f, int32_t delta)
{
    switch (f)
    {
        case SET_YEAR:    t->year   = (uint16_t)clamp_wrap((int32_t)t->year,   2000, 2099, delta); break;
        case SET_MONTH:   t->month  = (uint8_t) clamp_wrap((int32_t)t->month,  1,    12,   delta); break;
        case SET_DAY:     t->day    = (uint8_t) clamp_wrap((int32_t)t->day,    1,    31,   delta); break;
        case SET_HOUR:    t->hour   = (uint8_t) clamp_wrap((int32_t)t->hour,   0,    23,   delta); break;
        case SET_MINUTE:  t->minute = (uint8_t) clamp_wrap((int32_t)t->minute, 0,    59,   delta); break;
        case SET_SECOND:  t->second = (uint8_t) clamp_wrap((int32_t)t->second, 0,    59,   delta); break;
        case SET_WEEK:    t->week   = (uint8_t) clamp_wrap((int32_t)t->week,   1,    7,    delta); break;

        case SET_AL1_EN:  cfg[AL_CFG_IDX_AL1_EN] = (uint8_t)clamp_wrap(cfg[AL_CFG_IDX_AL1_EN], 0, 1, delta); break;
        case SET_AL1_H:   cfg[AL_CFG_IDX_AL1_H]  = (uint8_t)clamp_wrap(cfg[AL_CFG_IDX_AL1_H],  0, 23, delta); break;
        case SET_AL1_M:   cfg[AL_CFG_IDX_AL1_M]  = (uint8_t)clamp_wrap(cfg[AL_CFG_IDX_AL1_M],  0, 59, delta); break;
        case SET_AL2_EN:  cfg[AL_CFG_IDX_AL2_EN] = (uint8_t)clamp_wrap(cfg[AL_CFG_IDX_AL2_EN], 0, 1, delta); break;
        case SET_AL2_H:   cfg[AL_CFG_IDX_AL2_H]  = (uint8_t)clamp_wrap(cfg[AL_CFG_IDX_AL2_H],  0, 23, delta); break;
        case SET_AL2_M:   cfg[AL_CFG_IDX_AL2_M]  = (uint8_t)clamp_wrap(cfg[AL_CFG_IDX_AL2_M],  0, 59, delta); break;
        case SET_BEEP:    cfg[AL_CFG_IDX_BEEP]   = (uint8_t)clamp_wrap(cfg[AL_CFG_IDX_BEEP],   0, BEEP_SEL_MAX - 1, delta); break;
        default: break;
    }
}

/* 背景重绘标志(全局/上次是否在设置模式) */
static uint8_t s_bg_drawn = 0;
static uint8_t s_last_help_set = 0xFF;   /* 上次提示状态:0=正常,1=设置 */
static uint8_t s_lp_set = 0;   /* 1=处于"灭屏时长"设置页(独立入口,长按 K1 进入) */
static uint8_t s_lp_idx = 0;   /* 设置页正在编辑的档位(0..3) */

/* 显示单个字段(hide=1 时用空格覆盖,实现闪烁) */
static void display_field(struct TIMEData *t, const uint8_t *cfg, SetField_t f, uint8_t hide)
{
    char buf[20];
    const FieldPos_t *p;
    uint8_t len = 0;

    if (f == SET_NONE || f >= SET_FIELD_MAX) return;
    p = &s_field_pos[f - 1];

    switch (f)
    {
        case SET_YEAR:    len = snprintf(buf, sizeof(buf), hide ? "    " : "%04u", (unsigned)t->year); break;
        case SET_MONTH:   len = snprintf(buf, sizeof(buf), hide ? "  "   : "%02u", (unsigned)t->month); break;
        case SET_DAY:     len = snprintf(buf, sizeof(buf), hide ? "  "   : "%02u", (unsigned)t->day); break;
        case SET_HOUR:    len = snprintf(buf, sizeof(buf), hide ? "  "   : "%02u", (unsigned)t->hour); break;
        case SET_MINUTE:  len = snprintf(buf, sizeof(buf), hide ? "  "   : "%02u", (unsigned)t->minute); break;
        case SET_SECOND:  len = snprintf(buf, sizeof(buf), hide ? "  "   : "%02u", (unsigned)t->second); break;
        case SET_WEEK:    len = snprintf(buf, sizeof(buf), hide ? " "    : "%u",   (unsigned)t->week); break;

        case SET_AL1_EN:  len = snprintf(buf, sizeof(buf), hide ? " " : "%c", cfg[AL_CFG_IDX_AL1_EN] ? '1' : '-'); break;
        case SET_AL1_H:   len = snprintf(buf, sizeof(buf), hide ? "  " : "%02u", (unsigned)cfg[AL_CFG_IDX_AL1_H]); break;
        case SET_AL1_M:   len = snprintf(buf, sizeof(buf), hide ? "  " : "%02u", (unsigned)cfg[AL_CFG_IDX_AL1_M]); break;
        case SET_AL2_EN:  len = snprintf(buf, sizeof(buf), hide ? " " : "%c", cfg[AL_CFG_IDX_AL2_EN] ? '2' : '-'); break;
        case SET_AL2_H:   len = snprintf(buf, sizeof(buf), hide ? "  " : "%02u", (unsigned)cfg[AL_CFG_IDX_AL2_H]); break;
        case SET_AL2_M:   len = snprintf(buf, sizeof(buf), hide ? "  " : "%02u", (unsigned)cfg[AL_CFG_IDX_AL2_M]); break;
        case SET_BEEP:
            if (hide) len = snprintf(buf, sizeof(buf), "                ");
            else      len = snprintf(buf, sizeof(buf), "BEEP:%-11s",
                                     (cfg[AL_CFG_IDX_BEEP] == BEEP_SEL_ACTIVE1) ? "A1"
                                   : (cfg[AL_CFG_IDX_BEEP] == BEEP_SEL_ACTIVE2) ? "A2" : "P ");
            break;
        default: break;
    }
    if (len == 0) return;
    OLED_ShowString(p->x_char * 8, p->line * 16, (u8 *)buf, 16);
}

/* 显示第 4 行的两个闹钟状态(共 16 字符):
 *   例: "1 07:30  2 08:00"; 使能显示 '1'/'2',未使能显示 '-' */
static void display_alarm_line(const uint8_t *cfg)
{
    char buf[20];
    snprintf(buf, sizeof(buf), "%c %02u:%02u  %c %02u:%02u",
             (cfg[AL_CFG_IDX_AL1_EN] ? '1' : '-'),
             (unsigned)cfg[AL_CFG_IDX_AL1_H], (unsigned)cfg[AL_CFG_IDX_AL1_M],
             (cfg[AL_CFG_IDX_AL2_EN] ? '2' : '-'),
             (unsigned)cfg[AL_CFG_IDX_AL2_H], (unsigned)cfg[AL_CFG_IDX_AL2_M]);
    OLED_ShowString(0, 48, (u8 *)buf, 16);
}

/* 刷新全部时间字段 + 第 4 行闹钟状态(用于非设置模式) */
static void display_all(struct TIMEData *t, const uint8_t *cfg)
{
    display_field(t, cfg, SET_YEAR,   0);
    display_field(t, cfg, SET_MONTH,  0);
    display_field(t, cfg, SET_DAY,    0);
    display_field(t, cfg, SET_HOUR,   0);
    display_field(t, cfg, SET_MINUTE, 0);
    display_field(t, cfg, SET_SECOND, 0);
    display_field(t, cfg, SET_WEEK,   0);
    display_alarm_line(cfg);
}

/* 绘制静态背景:日期分隔符 + 状态提示行 */
static void display_bg(uint8_t in_set_mode, SetField_t field)
{
    /* 第 1 行:"YYYY-MM-DD Wn" 的日期分隔符与星期标记 W */
    OLED_ShowString(4 * 8,  0, (u8 *)"-", 16);
    OLED_ShowString(7 * 8,  0, (u8 *)"-", 16);
    OLED_ShowString(10 * 8, 0, (u8 *)" ", 16);
    OLED_ShowString(11 * 8, 0, (u8 *)"W", 16);
    /* 第 2 行:"HH:MM:SS" 的冒号分隔符 */
    OLED_ShowString(2 * 8, 16, (u8 *)":", 16);
    OLED_ShowString(5 * 8, 16, (u8 *)":", 16);

    /* 第 3 行:状态提示(响铃/贪睡/设置/正常) */
    if (s_al_state == AL_RING)
    {
        if (s_al_which == 0)
            OLED_ShowString(0, 32, (u8 *)"ALARM! K1:Snoze ", 16);   /* 起床闹钟:短按贪睡 */
        else
            OLED_ShowString(0, 32, (u8 *)"TODO!  K1:Stop  ", 16);   /* 待办闹钟:短按停止 */
    }
    else if (s_al_state == AL_SNOOZE)
    {
        OLED_ShowString(0, 32, (u8 *)"SNOOZE K1:Cncl  ", 16);
    }
    else if (in_set_mode)
    {
        if (field >= SET_AL1_EN)
            OLED_ShowString(0, 32, (u8 *)"Alm:K1+ K2- K4X ", 16);
        else
            OLED_ShowString(0, 32, (u8 *)"Set:K1+ K2- K4X ", 16);
    }
    else
    {
        OLED_ShowString(0, 32, (u8 *)"K1:Time K4:Alm  ", 16);
    }
}

/* 刷新 OLED 显示(仅在内容变化时重绘) */
static void display_time(struct TIMEData *t, const uint8_t *cfg, SetField_t field, uint8_t blink_hide)
{
    uint8_t in_set = (field != SET_NONE) ? 1 : 0;

    /* 静态背景或模式变化时:重绘背景 + 全部字段 */
    if (!s_bg_drawn || s_last_help_set != in_set)
    {
        display_bg(in_set, field);
        display_all(t, cfg);
        if (in_set)
            display_field(t, cfg, field, blink_hide);   /* 设置模式下叠加当前字段(带闪烁) */
        s_bg_drawn      = 1;
        s_last_help_set = in_set;
    }
    else if (in_set)
    {
        /* 设置模式:仅重绘当前字段(避免整屏闪烁) */
        if (field >= SET_AL1_EN)
            display_all(t, cfg);
        if (field != SET_NONE)
            display_field(t, cfg, field, blink_hide);
    }
    else
    {
        /* 非设置模式:仅刷新全部字段 */
        display_all(t, cfg);
    }

    OLED_Refresh();
}

/* 绘制"灭屏时长"设置页(进入时已 OLED_Clear,此处只负责画满各行) */
static void display_lp_set(void)
{
    char buf[20];
    OLED_ShowString(0, 0,  (u8 *)"Screen Time Set ", 16);
    snprintf(buf, sizeof(buf), "OFF in:%-8s ", LP_TimeoutStr(s_lp_idx));
    OLED_ShowString(0, 16, (u8 *)buf, 16);
    OLED_ShowString(0, 32, (u8 *)"K1:Save  K4:Quit", 16);
}

/* ===================== 闹钟逻辑 ===================== */

static uint16_t alarm_on_ms(uint8_t which)
{
    return (which == 0u) ? AL_WAKE_ON_MS : AL_TODO_ON_MS;
}

static uint16_t alarm_off_ms(uint8_t which)
{
    return (which == 0u) ? AL_WAKE_OFF_MS : AL_TODO_OFF_MS;
}

static void Alarm_Stop(void)
{
    BEEP_Off();
    s_al_state    = AL_IDLE;
    s_al_on       = 0;
    s_al_ui_dirty = 1;
}

static void Alarm_Start(uint8_t which, uint32_t now)
{
    s_al_which    = which;
    s_al_state    = AL_RING;
    s_al_on       = 0;
    s_al_beeps    = 0;
    s_al_start_ms = now;
    s_al_phase_ms = now - alarm_off_ms(which);   /* 回拨相位,使 Process 立刻进入"响"相位 */
    LP_ForceWake(now);   /* 响铃时强制亮屏,确保能看到响铃提示 */
    s_al_ui_dirty = 1;
}

/* 贪睡设置:将 now+5min 存入目标分钟,退出响铃 */
static void Alarm_Snooze(void)
{
    uint16_t nowm = (uint16_t)((uint32_t)TimeData.hour * 60u + TimeData.minute);

    s_snooze_min  = (uint16_t)((nowm + AL_SNOOZE_MIN) % 1440u);
    BEEP_Off();
    s_al_state    = AL_SNOOZE;
    s_al_on       = 0;
    s_al_ui_dirty = 1;
}

/* 闹钟处理:由主循环周期调用(基于 HAL_GetTick()) */
static void Alarm_Process(uint32_t now)
{
    uint16_t nowm;

    switch (s_al_state)
    {
        case AL_RING:
        {
            uint8_t  passive = (BEEP_GetSelect() == BEEP_SEL_PASSIVE) ? 1u : 0u;
            uint16_t on_ms   = alarm_on_ms(s_al_which);
            uint16_t off_ms  = alarm_off_ms(s_al_which);

            /* 起床闹钟超过 AL_WAKE_TOTAL_MS 则自动停 */
            if (s_al_which == 0u && (now - s_al_start_ms) >= AL_WAKE_TOTAL_MS)
            {
                Alarm_Stop();
                return;
            }

            if (s_al_on == 0u)
            {
                /* "静"相位:等待 off_ms 后进入"响"相位 */
                if ((now - s_al_phase_ms) < (uint32_t)off_ms) return;

                s_al_on       = 1u;
                s_al_phase_ms = now;

                if (passive)
                {
                    /* 无源蜂鸣器:先按 on_ms 方波发声(如 600ms 一短声) */
                    BEEP_ToneSlice(on_ms);
                    BEEP_Off();
                    s_al_on       = 0u;
                    s_al_phase_ms = HAL_GetTick();
                    if (s_al_which == 1u)
                    {
                        s_al_beeps++;
                        if (s_al_beeps >= AL_TODO_BEEPS) { Alarm_Stop(); return; }
                    }
                }
                else
                {
                    BEEP_On();      /* 有源蜂鸣器:直接通电即响,等待关断 */
                }
                return;
            }
            else
            {
                /* "响"相位:保持 on_ms 后关断 */
                if ((now - s_al_phase_ms) < (uint32_t)on_ms) return;

                BEEP_Off();
                s_al_on       = 0u;
                s_al_phase_ms = now;
                if (s_al_which == 1u)
                {
                    s_al_beeps++;
                    if (s_al_beeps >= AL_TODO_BEEPS) { Alarm_Stop(); return; }
                }
                return;
            }
        }

        case AL_SNOOZE:
        {
            nowm = (uint16_t)((uint32_t)TimeData.hour * 60u + TimeData.minute);
            if (nowm == s_snooze_min)
                Alarm_Start(0u, now);      /* 贪睡到点:按起床闹钟重新启动 */
            return;
        }

        case AL_IDLE:
        default:
        {
            /* 闹钟 1(起床):时/分匹配且尚未触发则启动 */
            if (s_cfg[AL_CFG_IDX_AL1_EN] &&
                TimeData.hour   == s_cfg[AL_CFG_IDX_AL1_H] &&
                TimeData.minute == s_cfg[AL_CFG_IDX_AL1_M])
            {
                if (s_al_fired_h[0] != TimeData.hour || s_al_fired_m[0] != TimeData.minute)
                {
                    s_al_fired_h[0] = TimeData.hour;
                    s_al_fired_m[0] = TimeData.minute;
                    Alarm_Start(0u, now);
                    return;
                }
            }
            /* 闹钟 2(待办):时/分匹配且尚未触发则启动 */
            if (s_cfg[AL_CFG_IDX_AL2_EN] &&
                TimeData.hour   == s_cfg[AL_CFG_IDX_AL2_H] &&
                TimeData.minute == s_cfg[AL_CFG_IDX_AL2_M])
            {
                if (s_al_fired_h[1] != TimeData.hour || s_al_fired_m[1] != TimeData.minute)
                {
                    s_al_fired_h[1] = TimeData.hour;
                    s_al_fired_m[1] = TimeData.minute;
                    Alarm_Start(1u, now);
                    return;
                }
            }
            return;
        }
    }
}

/* 从 DS1302 RAM 读取闹钟配置(首次上电时写入默认值) */
static void Alarm_CfgLoad(void)
{
    DS1302_LoadCfg(s_cfg, AL_CFG_LEN);

    if (s_cfg[AL_CFG_IDX_MAGIC] != AL_CFG_MAGIC)
    {
        s_cfg[AL_CFG_IDX_MAGIC]  = AL_CFG_MAGIC;
        s_cfg[AL_CFG_IDX_BEEP]   = BEEP_SEL_ACTIVE1;   /* 默认 PA3 有源 */
        s_cfg[AL_CFG_IDX_AL1_EN] = 0;                  /* 起床闹钟默认关闭 */
        s_cfg[AL_CFG_IDX_AL1_H]  = 7;                  /* 07:00          */
        s_cfg[AL_CFG_IDX_AL1_M]  = 0;
        s_cfg[AL_CFG_IDX_AL2_EN] = 0;                  /* 待办闹钟默认关闭 */
        s_cfg[AL_CFG_IDX_AL2_H]  = 8;                  /* 08:00          */
        s_cfg[AL_CFG_IDX_AL2_M]  = 0;
        DS1302_SaveCfg(s_cfg, AL_CFG_LEN);
    }
    if (s_cfg[AL_CFG_IDX_BEEP] >= BEEP_SEL_MAX)
        s_cfg[AL_CFG_IDX_BEEP] = BEEP_SEL_ACTIVE1;

    BEEP_Select(s_cfg[AL_CFG_IDX_BEEP]);
}

/* 把当前配置写回 DS1302 RAM */
static void Alarm_CfgSave(void)
{
    s_cfg[AL_CFG_IDX_MAGIC] = AL_CFG_MAGIC;
    DS1302_SaveCfg(s_cfg, AL_CFG_LEN);
}

/* 按键事件处理入口,在主循环中调用 */
static void handle_key_event(KeyMsg_t *msg)
{
    if (msg->evt != KEY_EVT_SHORT &&
        msg->evt != KEY_EVT_LONG &&
        msg->evt != KEY_EVT_REPEAT)
        return;

    /* ---- 响铃/贪睡中:优先处理按键,屏蔽其它 ---- */
    if (s_al_state != AL_IDLE)
    {
        if (msg->id == KEY_MODE)
        {
            if (msg->evt == KEY_EVT_SHORT)
            {
                if (s_al_state == AL_RING && s_al_which == 0u)
                    Alarm_Snooze();          /* 起床闹钟:短按=贪睡 */
                else
                    Alarm_Stop();            /* 待办闹钟/贪睡:短按=停止 */
            }
            else if (msg->evt == KEY_EVT_LONG)
            {
                Alarm_Stop();                /* 长按=退出响铃 */
            }
            s_bg_drawn = 0;
        }
        return;
    }

    /* ---- 灭屏时长设置页(独立入口,不干扰调时钟/调闹钟) ---- */
    if (s_lp_set)
    {
        if (msg->id == KEY_UP && (msg->evt == KEY_EVT_SHORT || msg->evt == KEY_EVT_REPEAT))
            s_lp_idx = (uint8_t)((s_lp_idx + 1u) % (LP_IDX_MAX + 1u));
        else if (msg->id == KEY_DOWN && (msg->evt == KEY_EVT_SHORT || msg->evt == KEY_EVT_REPEAT))
            s_lp_idx = (s_lp_idx == 0u) ? LP_IDX_MAX : (uint8_t)(s_lp_idx - 1u);
        else if (msg->id == KEY_MODE && msg->evt == KEY_EVT_SHORT)
        {
            LP_SetTimeoutIdx(s_lp_idx);   /* K1 短按:保存并退出 */
            s_lp_set   = 0;
            s_bg_drawn = 0;
            OLED_Clear();                 /* 清掉设置页文字,时钟整屏重画无残留 */
        }
        else if (msg->id == KEY_ALARM && (msg->evt == KEY_EVT_SHORT || msg->evt == KEY_EVT_LONG))
        {
            s_lp_set   = 0;               /* K4:放弃修改退出 */
            s_bg_drawn = 0;
            OLED_Clear();                 /* 同上:清掉设置页文字 */
        }
        return;
    }

    /* ---- 正常模式:K1 进入时间页, K4 进入闹钟页 ---- */
    if (s_set_field == SET_NONE)
    {
        if (msg->id == KEY_MODE && msg->evt == KEY_EVT_SHORT)
        {
            DS1302_read_realTime();
            s_edit = TimeData;
            memcpy(s_edit_cfg, s_cfg, AL_CFG_LEN);
            s_set_field = SET_YEAR;
            s_bg_drawn  = 0;
        }
        else if (msg->id == KEY_ALARM && msg->evt == KEY_EVT_SHORT)
        {
            DS1302_read_realTime();
            s_edit = TimeData;
            memcpy(s_edit_cfg, s_cfg, AL_CFG_LEN);
            s_set_field = SET_AL1_EN;
            s_bg_drawn  = 0;
        }
        else if (msg->id == KEY_MODE && msg->evt == KEY_EVT_LONG)
        {
            s_lp_set   = 1;                  /* 长按 K1:进入灭屏时长设置页 */
            s_lp_idx   = LP_GetTimeoutIdx();
            s_bg_drawn = 0;
            OLED_Clear();                    /* 清掉时钟画面,避免与设置页文字重叠 */
        }
        return;
    }

    /* ---- 设置模式 ---- */
    switch (msg->id)
    {
        case KEY_MODE:
            if (msg->evt == KEY_EVT_SHORT)
            {
                if (s_set_field == SET_WEEK)
                {
                    DS1302Z_SetTimeFromDec(&s_edit);
                    TimeData = s_edit;
                    s_set_field = SET_NONE;
                }
                else if (s_set_field == SET_BEEP)
                {
                    memcpy(s_cfg, s_edit_cfg, AL_CFG_LEN);
                    Alarm_CfgSave();
                    BEEP_Select(s_cfg[AL_CFG_IDX_BEEP]);
                    s_set_field = SET_NONE;
                }
                else
                {
                    display_field(&s_edit, s_edit_cfg, s_set_field, 0);
                    s_set_field = (SetField_t)(s_set_field + 1);
                }
                s_bg_drawn = 0;   /* 背景重绘以便刷新提示行 */
            }
            break;

        case KEY_UP:
            if (msg->evt == KEY_EVT_SHORT || msg->evt == KEY_EVT_REPEAT)
            {
                adjust_field(&s_edit, s_edit_cfg, s_set_field, +1);
                s_bg_drawn = 0;
            }
            break;

        case KEY_DOWN:
            if (msg->evt == KEY_EVT_SHORT || msg->evt == KEY_EVT_REPEAT)
            {
                adjust_field(&s_edit, s_edit_cfg, s_set_field, -1);
                s_bg_drawn = 0;
            }
            break;

        case KEY_ALARM:
            if (msg->evt == KEY_EVT_LONG)
            {
                s_set_field = SET_NONE;   /* K4 长按:退出设置模式 */
                s_bg_drawn  = 0;
            }
            break;

        default:
            break;
    }
}


int main(void)
{

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* Configure the system clock to HIRC 24MHz*/
     SystemClock_Config();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    /* Initialize BSP Led for LED1 */
//    BSP_LED_Init(LED1);
    struct TIMEData default_time;

    ADC_KEY_Init();
    DS1302Z_GPIO_Init();

    BEEP_Init();          /* 初始化蜂鸣器(含引脚配置)                 */
    Alarm_CfgLoad();      /* 加载闹钟配置(首次上电写默认值到 DS1302 RAM) */

    OLED_Init();
    OLED_ColorTurn(0);//正常显示
    OLED_DisplayTurn(0);//OLED不旋转
    /* Configure uart0 for printf */
    LogInit();

    /* 读取 RTC 时间(带后备电池,掉电保持);
       仅当数据无效(首次上电/电池丢失)才写一次默认时间 */
    DS1302_read_realTime();
    if (TimeData.month  < 1 || TimeData.month  > 12 ||
        TimeData.day    < 1 || TimeData.day    > 31 ||
        TimeData.hour   > 23 ||
        TimeData.minute > 59 ||
        TimeData.second > 59 ||
        TimeData.year   < 2000 || TimeData.year > 2099)
    {
        default_time.year   = 2026;
        default_time.month  = 10;
        default_time.day    = 13;
        default_time.week   = 2;
        default_time.hour   = 14;
        default_time.minute = 30;
        default_time.second = 0;
        DS1302Z_SetTimeFromDec(&default_time);
        printf("[RTC] invalid, set default time\r\n");
        DS1302_read_realTime();
    }
    s_edit = TimeData;

    OLED_Refresh();
    OLED_DisPlay_On();
    LP_Init(HAL_GetTick());   /* 低功耗模块:默认亮屏,开始 30s 计时 */

    /* 上电立即绘制一帧:显示电池保持的时间 */
    s_bg_drawn = 0;
    display_time(&TimeData, s_cfg, SET_NONE, 0);
    

    while (1)
    {
        uint32_t now = HAL_GetTick();
        KeyMsg_t  msg;
        uint8_t   need_refresh = 0;

        KEY_Scan(now);

        /* 低功耗:30s 无操作灭屏;灭屏中检测到按键则唤醒(返回1=刚唤醒,需整屏重绘) */
        if (LP_Process(now))
        {
            s_bg_drawn = 0;
        }

        /* 每 500ms 读 DS1302;设置页也要走时(闹钟匹配依赖它) */
        if (now - s_last_refresh_ms >= 500)
        {
            s_last_refresh_ms = now;

            /* 无论是否在设置页都读取实时时间 */
            DS1302_read_realTime();
            if (s_set_field == SET_NONE)
            {
                if (s_edit.second != TimeData.second)
                {
                    s_edit = TimeData;
                    need_refresh = 1;
                }
            }
            else if (s_set_field >= SET_AL1_EN)
            {
                need_refresh = 1;
            }
        }

        /* 设置模式:每 1000ms 翻转一次闪烁标志(肉眼约 2s 周期) */
        if (s_set_field != SET_NONE && (now - s_last_blink_ms >= 1000))
        {
            s_last_blink_ms = now;
            s_blink_hide    = !s_blink_hide;
            need_refresh    = 1;
        }

        /* 闹钟处理:驱动状态机与蜂鸣器 */
        Alarm_Process(now);

        /* 响铃/贪睡期间保持亮屏,便于看到操作提示 */
        if (s_al_state != AL_IDLE)
            LP_NoteActivity(now);

        /* 设置页/灭屏设置页期间保持亮屏,避免操作到一半灭屏 */
        if (s_lp_set || s_set_field != SET_NONE)
            LP_NoteActivity(now);

        /* 响铃状态变化时:触发一次界面重绘 */
        if (s_al_ui_dirty)
        {
            s_al_ui_dirty = 0;
            s_bg_drawn    = 0;
            need_refresh  = 1;
        }

        /* 处理按键消息队列 */
        while (KEY_GetMsg(&msg))
        {
            if (LP_KeyMsgFilter(&msg))  /* 唤醒屏幕的那次按键只负责亮屏,不触发功能 */
                continue;
            LP_NoteActivity(now);       /* 有效按键重置灭屏计时 */
            handle_key_event(&msg);
            need_refresh = 1;   /* 按键事件后需要重绘 */
        }

        if (need_refresh && LP_IsScreenOn())   /* 灭屏期间跳过 OLED 刷新以省电 */
        {
            if (s_lp_set)
            {
                display_lp_set();
                OLED_Refresh();
            }
            else if (s_set_field == SET_NONE)
                display_time(&TimeData, s_cfg, SET_NONE, 0);
            else
                display_time((s_set_field >= SET_AL1_EN) ? &TimeData : &s_edit, s_edit_cfg, s_set_field, s_blink_hide);
        }

        HAL_Delay(KEY_SCAN_PERIOD_MS);
        KEY_DebugPrint(now);
    }

}

//#ifdef HAL_OWIRE_MODULE_ENABLED
// #include "cx32l003_hal_owire.h"
//#endif /* HAL_OWIRE_MODULE_ENABLED */


/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HIRC;
    RCC_OscInitStruct.HIRCState = RCC_HIRC_ON;
    RCC_OscInitStruct.HIRCCalibrationValue = RCC_HIRCCALIBRATION_24M;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /**Initializes the CPU, AHB and APB busses clocks
    */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HIRC;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APBCLKDivider = RCC_PCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }
}



/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */

    /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */


/* Private function -------------------------------------------------------*/
