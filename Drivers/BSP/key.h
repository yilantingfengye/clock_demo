#ifndef __KEY_H
#define __KEY_H

#include "main.h"
#include <stdint.h>

/* 旧版阈值宏(实际映射已改由 key.c 中 s_key_map[] 管理,保留仅为兼容) */
#define KEY_ADC_1  250
#define KEY_ADC_2   0
#define KEY_ADC_3   0
#define KEY_ADC_4   0

/**
  ******************************************************************************
  * @file    key.h
  * @brief   ADC 按键扫描驱动:按键识别 + 消抖 + 单击/双击/长按 事件
  * @note    移植只需实现一个函数 KEY_ReadAdcValue()(详见 key.c 文件头说明),
  *          主循环每 KEY_SCAN_PERIOD_MS 调用一次 KEY_Scan(HAL_GetTick())
  ******************************************************************************
  */

/* ===================== 可调参数(根据手感微调) ===================== */
#define KEY_SCAN_PERIOD_MS   10     /* 扫描周期:主循环每 tick 调一次        */
#define KEY_DEBOUNCE_CNT     4      /* 按下确认次数:4 次 10ms = 40ms       */
#define KEY_LONG_PRESS_MS    800    /* 按住超过此时长判为长按               */
#define KEY_LONG_REPEAT_MS   150    /* 长按后连发间隔:每 150ms 一次        */
#define KEY_RELEASE_CNT      3      /* 抬起确认次数:3 次 10ms = 30ms       */
#define KEY_DOUBLE_GAP_MS    300    /* 双击间隔窗口:两次单击在此时间内算双击 */

/* ===================== 按键编号(与 ADC 分压档位对应) ===================== */
typedef enum
{
    KEY_NONE = 0,
    KEY_MODE ,       /* K1:进入设置 / 切换设置字段            */
    KEY_UP   ,       /* K2:当前字段 +1(短按或长按连发)      */
    KEY_DOWN ,       /* K3:当前字段 -1(短按或长按连发)      */
    KEY_ALARM        /* K4:闹钟键(长按 = 放弃修改退出设置)   */
} KeyId_t;

/* ===================== 按键事件 ===================== */
typedef enum
{
    KEY_EVT_NONE   = 0,
    KEY_EVT_DOUBLE,     /* 双击:第二次抬起时发送一次            */
    KEY_EVT_SHORT,      /* 单击:抬起后过了双击窗口才发送一次    */
    KEY_EVT_LONG,       /* 长按:达到长按阈值时发送一次          */
    KEY_EVT_REPEAT,     /* 长按连发:按住期间每 150ms 一次       */
    KEY_EVT_RELEASE     /* 抬起:长按结束后发送一次              */
} KeyEvent_t;

typedef struct
{
    KeyId_t    id;
    KeyEvent_t evt;
} KeyMsg_t;

/* ===================== 对外接口 ===================== */
void     KEY_Init(void);                    /* 按键状态机复位(不含 ADC 初始化) */
void     ADC_KEY_Init(void);                /* ADC + 按键分压 GPIO 初始化       */

uint16_t KEY_ReadAdcValue(void);            /* 读一次 ADC 原始值(移植接口)     */

void     KEY_Scan(uint32_t now_ms);         /* 周期调用,间隔 KEY_SCAN_PERIOD_MS */
uint8_t  KEY_GetMsg(KeyMsg_t *msg);         /* 取一条按键消息,无则返回 0        */
uint8_t  KEY_IsPressed(KeyId_t id);         /* 查询某键当前是否处于按下态        */

/**
 * @brief  调试用:每 500ms 打印一次 ADC 值与识别到的按键
 *         打开 _debug_ 宏后,依次按住 K1~K4 约 2 秒,
 *         把稳定值填入 key.c 的 s_key_map[] 的 center 字段
 */
void     KEY_DebugPrint(uint32_t now_ms);
/* ---- 灭屏唤醒支持(app_lowpower 模块使用) ---- */
uint8_t  KEY_AnyPressed(void);              /* 查询是否有任意键处于稳定按下态     */
void     KEY_FlushAll(void);                /* 清空事件队列与单击待确认状态       */


#endif /* __KEY_H */
