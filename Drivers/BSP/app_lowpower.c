/**
  ******************************************************************************
  * @file    app_lowpower.c
  * @brief   显示低功耗:无操作超时 OLED 灭屏,任意按键唤醒亮屏
  *
  * 说明:
  *  - 灭屏只关 OLED 面板(OLED_DisPlay_Off),DS1302 走时与闹钟状态机照常运行,
  *    灭屏期间闹钟到点仍会响铃并强制亮屏
  *  - 唤醒键被"吞掉":只负责亮屏,不会触发进设置页等误操作;
  *    用户松开该键后自动恢复正常的按键事件处理
  *  - 灭屏期间主循环跳过 OLED 刷新(软件 I2C 全屏传输最耗电)以省电
  *  - 灭屏时长档位(10s/30s/60s/常亮)存 DS1302 RAM 0xE0/0xE1,掉电保持
  ******************************************************************************
  */

#include "main.h"
#include "app_lowpower.h"
#include "ds1302z.h"

/* 档位对应的灭屏超时(ms),0 = 常亮永不灭屏 */
static const uint32_t  s_off_ms_tab[LP_IDX_MAX + 1] = {10000u, 30000u, 60000u, 0u};
/* 档位显示字符串(供设置页展示) */
static const char     *s_off_str_tab[LP_IDX_MAX + 1] = {"10s", "30s", "60s", "Never"};

static uint8_t  s_screen_on   = 1;     /* 当前 OLED 亮屏状态       */
static uint32_t s_last_act_ms = 0;     /* 最近一次交互(按键)时刻   */
static uint8_t  s_swallow     = 0;     /* 1=正在吞掉唤醒按键的消息 */
static uint8_t  s_lp_idx      = LP_IDX_DEFAULT;  /* 当前灭屏时长档位 */

/* 亮屏并重置灭屏计时(整屏重绘由调用方置 s_bg_drawn=0 完成) */
static void LP_ScreenOn(uint32_t now)
{
    if (!s_screen_on)
    {
        OLED_DisPlay_On();
        s_screen_on = 1;
    }
    s_last_act_ms = now;
}

void LP_Init(uint32_t now)
{
    uint8_t v = DS1302_Read_rig(LP_RAM_ADDR_RD);

    s_lp_idx      = (v <= LP_IDX_MAX) ? v : LP_IDX_DEFAULT;  /* RAM 无效值回退默认 */
    s_screen_on   = 1;
    s_last_act_ms = now;
    s_swallow     = 0;
}

uint8_t LP_Process(uint32_t now)
{
    if (s_screen_on)
    {
        /* 亮屏:超时无操作则灭屏(档位为"常亮"时永不灭屏) */
        uint32_t tmo = s_off_ms_tab[s_lp_idx];

        if (tmo != 0u && (now - s_last_act_ms) >= tmo)
        {
            OLED_DisPlay_Off();
            s_screen_on = 0;
        }
        return 0;
    }

    /* 灭屏:消抖确认到任意键按下(约 40ms)即唤醒 */
    if (KEY_AnyPressed())
    {
        LP_ScreenOn(now);
        s_swallow = 1;      /* 本次按键只用于唤醒,吞掉其后续消息 */
        return 1;
    }
    return 0;
}

uint8_t LP_IsScreenOn(void)
{
    return s_screen_on;
}

void LP_NoteActivity(uint32_t now)
{
    s_last_act_ms = now;
}

void LP_ForceWake(uint32_t now)
{
    LP_ScreenOn(now);
}

uint8_t LP_KeyMsgFilter(KeyMsg_t *msg)
{
    if (!s_swallow)
        return 0;

    /* 吞掉唤醒按键产生的所有消息,直到该键完全松开 */
    if (!KEY_AnyPressed())
    {
        KEY_FlushAll();     /* 清掉残留的单击/双击待确认状态 */
        s_swallow = 0;
    }
    (void)msg;
    return 1;
}

/* ===================== 灭屏时长档位 ===================== */

uint8_t LP_GetTimeoutIdx(void)
{
    return s_lp_idx;
}

void LP_SetTimeoutIdx(uint8_t idx)
{
    if (idx > LP_IDX_MAX)
        return;

    s_lp_idx = idx;

    DS1302_Write_rig(0x8E, 0x00);              /* 关写保护 */
    DS1302_Write_rig(LP_RAM_ADDR_WR, idx);     /* 存 DS1302 RAM,掉电保持 */
    DS1302_Write_rig(0x8E, 0x80);              /* 开写保护 */
}

const char *LP_TimeoutStr(uint8_t idx)
{
    return (idx <= LP_IDX_MAX) ? s_off_str_tab[idx] : "30s";
}
