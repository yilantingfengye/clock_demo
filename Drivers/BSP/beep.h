#ifndef __BEEP_H
#define __BEEP_H 

#include "main.h"

/* ============ 三路蜂鸣器(均为 NPN 低边三极管驱动) ============
 *   PA3 = 有源, PB5 = 有源, PB4 = 无源
 *   有源:给电平即响;  无源:需要方波驱动才能响
 *   NPN 低边为高电平导通,若实测相反只需互换下面两个宏
 */
#define BEEP_LVL_ON    GPIO_PIN_SET
#define BEEP_LVL_OFF   GPIO_PIN_RESET

#define BEEP_PORT_A    GPIOA
#define BEEP_PIN_A     GPIO_PIN_3     /* 有源 1 */
#define BEEP_PORT_B2   GPIOB
#define BEEP_PIN_B2    GPIO_PIN_5     /* 有源 2 */
#define BEEP_PORT_P    GPIOB
#define BEEP_PIN_P     GPIO_PIN_4     /* 无源   */

/* 蜂鸣器选择编号 */
#define BEEP_SEL_ACTIVE1   0          /* PA3 有源 */
#define BEEP_SEL_ACTIVE2   1          /* PB5 有源 */
#define BEEP_SEL_PASSIVE   2          /* PB4 无源 */
#define BEEP_SEL_MAX       3

#define BEEP_TONE_FREQ_HZ  2700       /* 无源方波频率(Hz) */

void    BEEP_Init(void);                  /* 初始化三路,上电全灭 */
void    BEEP_Select(uint8_t sel);         /* 切换当前使用的蜂鸣器 */
uint8_t BEEP_GetSelect(void);
void    BEEP_On(void);                    /* 拉响当前蜂鸣器(有源=给电平;无源请用 BEEP_ToneSlice) */
void    BEEP_Off(void);                   /* 三路全部关闭 */
void    BEEP_ToneSlice(uint16_t ms);      /* 发声 ms 毫秒:有源=拉响即返回;无源=方波阻塞 ms */

#endif
