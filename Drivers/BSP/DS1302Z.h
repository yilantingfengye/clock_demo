#ifndef __DS1302Z_H
#define __DS1302Z_H 

#include "main.h"

//-----------------DS1302Z端口定义---------------- 

#define DS1302Z_SCLK_Clr()  HAL_GPIO_WritePin(GPIOC,GPIO_PIN_3,GPIO_PIN_RESET)
#define DS1302Z__SCLK_Set() HAL_GPIO_WritePin(GPIOC,GPIO_PIN_3,GPIO_PIN_SET)//SCL

#define DS1302Z__DATAOUT_Clr() HAL_GPIO_WritePin(GPIOC,GPIO_PIN_4,GPIO_PIN_RESET)//DIN
#define DS1302Z__DATAOUT_Set() HAL_GPIO_WritePin(GPIOC,GPIO_PIN_4,GPIO_PIN_SET)

#define DS1302Z__DATAIN_ReadPin() HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_4)//DIN


#define DS1302Z__RES_Clr() HAL_GPIO_WritePin(GPIOC,GPIO_PIN_5,GPIO_PIN_RESET)//RES
#define DS1302Z__RES_Set() HAL_GPIO_WritePin(GPIOC,GPIO_PIN_5,GPIO_PIN_SET)

struct TIMEData
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t week;
};


struct TIMERAM
{
    uint8_t hour_kai;
    uint8_t minute_kai;
    uint8_t hour_guan;
    uint8_t minute_guan;
    uint8_t kai;
    uint8_t guan;
};

extern struct TIMERAM TimeRAM;
extern struct TIMEData TimeData;
void DS1302Z_GPIO_Init(void);
void DS1302_Write_rig(unsigned char address, unsigned char data);   /* 写一字节(BCD) */
unsigned char DS1302_Read_rig(unsigned char address);                /* 读一字节(BCD) */
void DS1302Z_Init(void);
void DS1302Z_DATAOUT_Init(void);
void DS1302Z_DATAIN_Init(void);
void DS1302_read_realTime(void);
void DS1302_wirteRAM(void);
void DS1302_readRAM(void);
void DS1302Z_SetTime(void);


/* 用十进制写 DS1302,内部转 BCD */
void DS1302Z_SetTimeFromDec(struct TIMEData *t);
extern uint8_t read_time[7];              /* 数组外部声明 */
void DS1302_read_time(void);              /* 函数原型 */

/* 通用 RAM 存取:配置区从 0xD0(写)/0xD1(读) 起,共 len 字节
 * (DS1302 内部 RAM 有后备电池,掉电不丢) */
void DS1302_SaveCfg(uint8_t *buf, uint8_t len);
void DS1302_LoadCfg(uint8_t *buf, uint8_t len);


#endif
