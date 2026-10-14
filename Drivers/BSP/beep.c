#include "main.h"

/* 当前选中的蜂鸣器(默认 PA3 有源) */
static uint8_t s_beep_sel = BEEP_SEL_ACTIVE1;

/* 微秒级忙等:基于 SysTick->VAL,不改动 SysTick 配置
 * (delay.c 里的 delay_us 会重配 SysTick,会破坏 HAL_Delay/HAL_GetTick) */
static void beep_delay_us(uint32_t us)
{
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    uint32_t load  = SysTick->LOAD;
    uint32_t last, now, acc = 0;

    if (load == 0U) return;         /* SysTick 未启动,直接返回 */
    last = SysTick->VAL;

    while (acc < ticks)
    {
        now = SysTick->VAL;
        if (now <= last)            /* 正常向下计数 */
            acc += (last - now);
        else                        /* 发生过一次重装载(回绕) */
            acc += (last + (load - now));
        last = now;
    }
}

/* 三路蜂鸣器全部关闭 */
static void beep_all_off(void)
{
    HAL_GPIO_WritePin(BEEP_PORT_A,  BEEP_PIN_A,  BEEP_LVL_OFF);
    HAL_GPIO_WritePin(BEEP_PORT_B2, BEEP_PIN_B2, BEEP_LVL_OFF);
    HAL_GPIO_WritePin(BEEP_PORT_P,  BEEP_PIN_P,  BEEP_LVL_OFF);
}

void BEEP_Select(uint8_t sel)
{
    if (sel < BEEP_SEL_MAX)
    {
        beep_all_off();
        s_beep_sel = sel;
    }
}

uint8_t BEEP_GetSelect(void)
{
    return s_beep_sel;
}

/* 拉响当前蜂鸣器:有源=给电平;无源请用 BEEP_ToneSlice 产生方波 */
void BEEP_On(void)
{
    if (s_beep_sel == BEEP_SEL_ACTIVE1)
        HAL_GPIO_WritePin(BEEP_PORT_A,  BEEP_PIN_A,  BEEP_LVL_ON);
    else if (s_beep_sel == BEEP_SEL_ACTIVE2)
        HAL_GPIO_WritePin(BEEP_PORT_B2, BEEP_PIN_B2, BEEP_LVL_ON);
}

void BEEP_Off(void)
{
    beep_all_off();
}

/* 发声 ms 毫秒:
 *   有源:拉响后直接返回(不阻塞,由调用者用时间判断何时关)
 *   无源:PB4 输出方波,阻塞 ms 毫秒 */
void BEEP_ToneSlice(uint16_t ms)
{
    uint16_t half_us;
    uint32_t total_us;

    if (s_beep_sel != BEEP_SEL_PASSIVE)
    {
        BEEP_On();
        return;
    }

    half_us  = (uint16_t)(500000UL / BEEP_TONE_FREQ_HZ);   /* 半周期(us) */
    total_us = (uint32_t)ms * 1000UL;

    while (total_us >= (uint32_t)half_us * 2UL)
    {
        HAL_GPIO_WritePin(BEEP_PORT_P, BEEP_PIN_P, BEEP_LVL_ON);
        beep_delay_us(half_us);
        HAL_GPIO_WritePin(BEEP_PORT_P, BEEP_PIN_P, BEEP_LVL_OFF);
        beep_delay_us(half_us);
        total_us -= (uint32_t)half_us * 2UL;
    }
    HAL_GPIO_WritePin(BEEP_PORT_P, BEEP_PIN_P, BEEP_LVL_OFF);
}

void BEEP_Init(void)
{

    GPIO_InitTypeDef  GPIO_InitStructure;
    GPIO_InitStructure.Pin = GPIO_PIN_3;	 
 	GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT; 		 //模拟输入
//	GPIO_InitStructure.OpenDrain = GPIO_OPENDRAIN;
    GPIO_InitStructure.Debounce.Enable = GPIO_DEBOUNCE_DISABLE;
    GPIO_InitStructure.SlewRate = GPIO_SLEW_RATE_HIGH;
    GPIO_InitStructure.DrvStrength = GPIO_DRV_STRENGTH_HIGH;
    GPIO_InitStructure.OpenDrain = GPIO_PUSHPULL;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
 	HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);	  
    
    GPIO_InitStructure.Pin = GPIO_PIN_4 | GPIO_PIN_5;	 
 	GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT; 		 //模拟输入
//	GPIO_InitStructure.OpenDrain = GPIO_OPENDRAIN;
    GPIO_InitStructure.Debounce.Enable = GPIO_DEBOUNCE_DISABLE;
    GPIO_InitStructure.SlewRate = GPIO_SLEW_RATE_HIGH;
    GPIO_InitStructure.DrvStrength = GPIO_DRV_STRENGTH_HIGH;
    GPIO_InitStructure.OpenDrain = GPIO_PUSHPULL;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
 	HAL_GPIO_Init(GPIOB, &GPIO_InitStructure);	  

}

