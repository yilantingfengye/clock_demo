#include "main.h"

struct TIMEData TimeData;
struct TIMERAM TimeRAM;
struct TIMEData SetTimeData;
uint8_t read_time[7];

void delay_us(uint32_t delay)
{
    uint32_t wait = delay;

    while (wait--)
    {
    }
}

void DS1302Z_GPIO_Init(void)
{

    GPIO_InitTypeDef  GPIO_InitStructure;
    GPIO_InitStructure.Pin = GPIO_PIN_3 | GPIO_PIN_5;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT; 		 //模拟输入
    GPIO_InitStructure.OpenDrain = GPIO_PUSHPULL; //推挽输出
    GPIO_InitStructure.Debounce.Enable = GPIO_DEBOUNCE_DISABLE;
    GPIO_InitStructure.SlewRate = GPIO_SLEW_RATE_HIGH;
    GPIO_InitStructure.DrvStrength = GPIO_DRV_STRENGTH_HIGH;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStructure);

    DS1302Z_SCLK_Clr();
    DS1302Z__RES_Clr();

}

void DS1302Z_DATAOUT_Init(void)
{

    GPIO_InitTypeDef  GPIO_InitStructure;
    GPIO_InitStructure.Pin = GPIO_PIN_4;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT; 		 //模拟输入
    GPIO_InitStructure.OpenDrain = GPIO_PUSHPULL; //推挽输出
    GPIO_InitStructure.Debounce.Enable = GPIO_DEBOUNCE_DISABLE;
    GPIO_InitStructure.SlewRate = GPIO_SLEW_RATE_HIGH;
    GPIO_InitStructure.DrvStrength = GPIO_DRV_STRENGTH_HIGH;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStructure);

    DS1302Z__DATAOUT_Clr();

}

void DS1302Z_DATAIN_Init(void)
{

    GPIO_InitTypeDef  GPIO_InitStructure;
    GPIO_InitStructure.Pin = GPIO_PIN_4;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT; 		 //模拟输入
    GPIO_InitStructure.OpenDrain = GPIO_OPENDRAIN; //推挽输出
    GPIO_InitStructure.Debounce.Enable = GPIO_DEBOUNCE_DISABLE;
    GPIO_InitStructure.SlewRate = GPIO_SLEW_RATE_HIGH;
    GPIO_InitStructure.DrvStrength = GPIO_DRV_STRENGTH_HIGH;
    GPIO_InitStructure.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStructure);

}

/*
*********************************************************************************************************
*	函 数 名:
*	功能说明:
*	形    参:
*	返 回 值:
*********************************************************************************************************
*/
void DS1302Z_Write__OneByte(unsigned  char data)
{
    unsigned char i = 0;
    DS1302Z_SCLK_Clr();
    DS1302Z_DATAOUT_Init();

    for(i = 0; i < 8; i++)
    {
        DS1302Z_SCLK_Clr();

        if(data & 0x01)
        {
            DS1302Z__DATAOUT_Set();
        }
        else
        {
            DS1302Z__DATAOUT_Clr();
        }

        DS1302Z__SCLK_Set();
        data >>= 1;
    }
}

/*
*********************************************************************************************************
*	函 数 名:
*	功能说明:
*	形    参:
*	返 回 值:
*********************************************************************************************************
*/
void DS1302_Write_rig( unsigned char address, unsigned char data )
{
    unsigned char temp1 = address;
    unsigned char temp2 = data;
    DS1302Z__RES_Clr();
    DS1302Z_SCLK_Clr();
    DS1302Z__RES_Set();
    DS1302Z_Write__OneByte(temp1);
    DS1302Z_Write__OneByte(temp2);
    DS1302Z__RES_Clr();
}

unsigned char DS1302_Read_rig( unsigned char address )
{
    unsigned char temp3 = address;
    unsigned char i = 0;
    unsigned char return_data = 0x00;
    DS1302Z__RES_Clr();
    DS1302Z_SCLK_Clr();
    DS1302Z__RES_Set();
    DS1302Z_Write__OneByte(temp3);
    DS1302Z_DATAIN_Init();

    for (i = 0; i < 8; i++)
    {
        delay_us(3);
        return_data >>= 1;
        DS1302Z__SCLK_Set();
        delay_us(5);
        DS1302Z_SCLK_Clr();
        delay_us(30);

        if(DS1302Z__DATAIN_ReadPin())
        {
            return_data = return_data | 0x80;
        }

    }

    delay_us(2);
    DS1302Z__RES_Clr();
    DS1302Z__DATAOUT_Clr();
    return return_data;
}

void DS1302Z_Init(void)
{
    DS1302_Write_rig(0X8E, 0x00); //关闭写保护

    DS1302_Write_rig(0x8c, 0x25); //年份
    DS1302_Write_rig(0x88, 0x12); //月份
    DS1302_Write_rig(0x86, 0x14); //天数
    DS1302_Write_rig(0x8a, 0x07); //星期

    DS1302_Write_rig(0x84, 0x00); //小时
    DS1302_Write_rig(0x82, 0x40); //分
    DS1302_Write_rig(0x80, 0x00); //秒

    DS1302_Write_rig(0x8e, 0x80); //关闭写保护
}

/* 时钟突发读(命令 0xBF):一次连读回 7 个时间寄存器,
 * 避免逐个寄存器读取时跨越进位而产生"撕裂"快照 */
void DS1302_read_time(void)
{
    unsigned char i, j;

    DS1302Z__RES_Clr();
    DS1302Z_SCLK_Clr();
    DS1302Z__RES_Set();
    DS1302Z_Write__OneByte(0xBF);          /* 时钟突发读命令 */
    DS1302Z_DATAIN_Init();

    for (i = 0; i < 7; i++)
    {
        read_time[i] = 0x00;
        for (j = 0; j < 8; j++)
        {
            delay_us(3);
            read_time[i] >>= 1;
            DS1302Z__SCLK_Set();
            delay_us(5);
            DS1302Z_SCLK_Clr();
            delay_us(30);

            if (DS1302Z__DATAIN_ReadPin())
                read_time[i] |= 0x80;
        }
    }

    delay_us(2);
    DS1302Z__RES_Clr();
    DS1302Z_DATAOUT_Init();
}

void DS1302_read_realTime(void)
{
    DS1302_read_time();  //BCD码转换为10进制
    TimeData.second = (read_time[0] >> 4) * 10 + (read_time[0] & 0x0f);
    TimeData.minute = ((read_time[1] >> 4)) * 10 + (read_time[1] & 0x0f);
    TimeData.hour = (read_time[2] >> 4) * 10 + (read_time[2] & 0x0f);
    TimeData.day = (read_time[3] >> 4) * 10 + (read_time[3] & 0x0f);
    TimeData.month = (read_time[4] >> 4) * 10 + (read_time[4] & 0x0f);
    TimeData.week = read_time[5];
    TimeData.year = (read_time[6] >> 4) * 10 + (read_time[6] & 0x0f) +2000;
}

void DS1302_wirteRAM(void)
{
    DS1302_Write_rig(0x8e, 0x00); //关闭写保护
    DS1302_Write_rig(0xC0, TimeRAM.hour_kai); //开时
    DS1302_Write_rig(0xC2, TimeRAM.minute_kai); //开分
    DS1302_Write_rig(0xC4, TimeRAM.hour_guan); //关时
    DS1302_Write_rig(0xC6, TimeRAM.minute_guan); //关分
    DS1302_Write_rig(0xC8, TimeRAM.kai); //关分
    DS1302_Write_rig(0xCA, TimeRAM.guan); //关分
    DS1302_Write_rig(0x8e, 0x80); //关闭写保护
}
void DS1302_readRAM(void)
{
    TimeRAM.hour_kai = DS1302_Read_rig(0xC1); //读秒
    TimeRAM.minute_kai = DS1302_Read_rig(0xC3); //读分
    TimeRAM.hour_guan = DS1302_Read_rig(0xC5); //读时
    TimeRAM.minute_guan = DS1302_Read_rig(0xC7); //读日
    TimeRAM.kai = DS1302_Read_rig(0xC9); //读日
    TimeRAM.guan = DS1302_Read_rig(0xCB); //读日
}


void DS1302Z_SetTime(void)
{

    DS1302_Write_rig(0X8E, 0x00); //关闭写保护

    DS1302_Write_rig(0x8c, SetTimeData.year); //年份
    DS1302_Write_rig(0x88, SetTimeData.month ); //月份
    DS1302_Write_rig(0x86, SetTimeData.day); //天数
    DS1302_Write_rig(0x8a, SetTimeData.week); //星期

    DS1302_Write_rig(0x84, SetTimeData.hour ); //小时
    DS1302_Write_rig(0x82, SetTimeData.minute); //分
    DS1302_Write_rig(0x80, SetTimeData.second ); //秒

    DS1302_Write_rig(0x8e, 0x80); //关闭写保护
}

/* 十进制转 BCD */
static uint8_t dec2bcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

/*
 * 接收十进制时间(年以 2000 为基准,例如 2026),
 * 内部转 BCD 后写回 DS1302,second 的 bit7 保持 0 让时钟继续走
 */
void DS1302Z_SetTimeFromDec(struct TIMEData *t)
{
    uint8_t year2 = (uint8_t)(t->year - 2000);

    DS1302_Write_rig(0X8E, 0x00);                  /* 关写保护   */
    DS1302_Write_rig(0x8c, dec2bcd(year2));         /* 年         */
    DS1302_Write_rig(0x88, dec2bcd(t->month));      /* 月         */
    DS1302_Write_rig(0x86, dec2bcd(t->day));        /* 日         */
    DS1302_Write_rig(0x8a, dec2bcd(t->week));       /* 周         */
    DS1302_Write_rig(0x84, dec2bcd(t->hour));       /* 时         */
    DS1302_Write_rig(0x82, dec2bcd(t->minute));     /* 分         */
    DS1302_Write_rig(0x80, dec2bcd(t->second) & 0x7F); /* 秒(bit7=0 走时) */
    /* 防止晶振被 CH 位置 1,再写一次强制启动时钟 */
    DS1302_Write_rig(0x80, dec2bcd(t->second) & 0x7F);
    DS1302_Write_rig(0x8E, 0x80);                  /* 开写保护   */
    /* 再读回 seconds,确认 CH 位为 0 */
    {
        uint8_t chk = DS1302_Read_rig(0x81);
        if (chk & 0x80) {
            /* 仍然 CH=1,说明晶振问题,重复写一次 */
            DS1302_Write_rig(0x8E, 0x00);
            DS1302_Write_rig(0x80, 0x00);   /* 强制秒=0,CH=0 */
            DS1302_Write_rig(0x8E, 0x80);
        }
    }
}

/* ===================== 通用 RAM 存取(配置区) =====================
 * DS1302 内部 RAM:偶地址写、奇地址读(如写 0xD0 / 读 0xD1)
 * 写前关写保护(0x8E=0x00),写后开写保护(0x8E=0x80)
 */
void DS1302_SaveCfg(uint8_t *buf, uint8_t len)
{
    uint8_t i;
    DS1302_Write_rig(0x8E, 0x00);              /* 关写保护 */
    for (i = 0; i < len; i++)
        DS1302_Write_rig((unsigned char)(0xD0 + 2 * i), buf[i]);
    DS1302_Write_rig(0x8E, 0x80);              /* 开写保护 */
}

void DS1302_LoadCfg(uint8_t *buf, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < len; i++)
        buf[i] = DS1302_Read_rig((unsigned char)(0xD1 + 2 * i));
}
