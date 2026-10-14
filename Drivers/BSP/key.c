/**
  ******************************************************************************
  * @file    key.c
  * @brief   ADC 按键采样 + 状态机判定,实现 单击/双击/长按 事件
  *
  * 移植步骤:
  *   1) 实现 KEY_ReadAdcValue() 返回当前 ADC 原始值
  *   2) 根据板子实测每个按键按下时的 ADC 值,更新 s_key_map[]
  *   3) 主循环中每 10ms 调用一次 KEY_Scan(HAL_GetTick())
  ******************************************************************************
  */

#include "main.h"

#define ADC_TIMEOUT_MAX     0xFFFFFFFF
#define ADCx                ADC
#define CONVERSION_NUM      1

#define ADC_KEY_SAMPLE_N    8       /* 每次取 8 个采样值 */

ADC_HandleTypeDef   sAdcHandle = {0};

/* ==========================================================================
 * 1. ADC -> 按键映射表
 *    请按实测修改:基于 12bit ADC(0~4095) 分压
 *    越接近哪个 center 就返回哪个 id,id = KEY_NONE 表示"无键"
 * ========================================================================== */
typedef struct
{
    uint16_t center;    /* 实际按下 ADC 值 */
    uint16_t tol;       /* 容差 */
    KeyId_t  id;
} KeyAdcMap_t;

static const KeyAdcMap_t s_key_map[] =
{
    { 4095, 70, KEY_NONE   },   /* 无键(分压网络浮空到 VCC,接近满量程) */
    {  0,   70, KEY_ALARM  },   /* K1 */
    { 365,  70, KEY_MODE   },   /* K2 */
    { 694,  70, KEY_UP     },   /* K3 */
    { 1317, 70, KEY_DOWN   },   /* K4 */
};

void ADC_KEY_Init(void)
{
    __HAL_RCC_ADC_CLK_ENABLE();

    ADC_ThresholdConfTypeDef        sAdcThreasHoldInit;
    GPIO_InitTypeDef gpio = {0};                 /* !!! 复用提醒 !!! */

    /* ---- GPIO 设为模拟输入,必须为浮空,避免上拉/下拉影响 ADC 采样 ---- */
    gpio.Pin         = GPIO_PIN_6;
    gpio.Mode        = GPIO_MODE_ANALOG;         /* 关键:模拟模式 */
    gpio.Pull        = GPIO_NOPULL;              /* 关键:无上下拉,让按键电阻网络决定分压 */
    gpio.OpenDrain   = GPIO_OPENDRAIN;
    gpio.Debounce.Enable = GPIO_DEBOUNCE_DISABLE;
    gpio.SlewRate    = GPIO_SLEW_RATE_HIGH;
    gpio.DrvStrength = GPIO_DRV_STRENGTH_HIGH;
    HAL_GPIO_Init(GPIOC, (GPIO_InitTypeDef *)&gpio);

    sAdcHandle.Instance = ADCx;
    sAdcHandle.Init.SamplingTime = ADC_SAMPLE_8CYCLE;              // 采样时间
    sAdcHandle.Init.ClkSel = ADC_CLOCK_PCLK_DIV16;                 // ADC 时钟分频
    sAdcHandle.Init.SingleContinueMode = ADC_MODE_CONTINUE;        // 启动转换模式
    sAdcHandle.Init.ContinueChannelSel = ADC_CONTINUE_CHANNEL_0;  // 通道选择
    sAdcHandle.Init.NbrOfConversion = CONVERSION_NUM;              // ADC 连续转换数为 1
    sAdcHandle.Init.AutoAccumulation = ADC_AUTOACC_DISABLE;        // 禁止 ADC 转换完成后自动累加
    sAdcHandle.Init.CircleMode = ADC_MULTICHANNEL_CIRCLE;          // 禁止 ADC 循环转换模式
    sAdcHandle.Init.ExternalTrigConv1 = ADC_SOFTWARE_START;        // 用软件自动触发 ADC 转换

    sAdcThreasHoldInit.CompareMode = ADC_COMP_THRESHOLD_NONE;      // 禁止 ADC 比较开关
    HAL_ADC_ThresholdConfig(&sAdcHandle, &sAdcThreasHoldInit);

    if(HAL_ADC_Init(&sAdcHandle) != HAL_OK)
    {
       Error_Handler();
    }
}

/* ==========================================================================
 * 2. 内部状态
 * ========================================================================== */
typedef struct
{
    KeyId_t  stable_id;       /* 当前已确认的按键 */
    KeyId_t  candidate;       /* 正在确认中的候选键 */
    uint8_t  deb_cnt;         /* 候选键连续成立次数 */
    uint8_t  rel_cnt;         /* 无键连续次数(抬起计数) */
    uint32_t press_ms;       /* 按键按下时间戳 */
    uint32_t last_repeat_ms; /* 上次 REPEAT 发送时间 */
    uint8_t  long_sent;      /* 长按事件已发送标记 */
} KeyState_t;

static KeyState_t s_key = {KEY_NONE, KEY_NONE, 0, 0, 0, 0, 0};

static uint8_t   s_pending_click  = 0;        /* 短按待确认标记(可能升级为双击) */
static KeyId_t   s_pending_id     = KEY_NONE;/* 待确认的键 id */
static uint32_t  s_pending_rel_ms = 0;       /* 上次短按抬起时间戳 */

/* 事件队列 */
#define KEY_QUEUE_DEPTH 4
static KeyMsg_t   s_queue[KEY_QUEUE_DEPTH];
static uint8_t    s_q_head = 0;
static uint8_t    s_q_tail = 0;
static uint8_t    s_q_size = 0;

static void KEY_PushMsg(KeyId_t id, KeyEvent_t evt)
{
    if (s_q_size >= KEY_QUEUE_DEPTH) return;      /* 满了就丢弃,避免覆盖旧消息 */
    s_queue[s_q_tail].id  = id;
    s_queue[s_q_tail].evt = evt;
    s_q_tail = (s_q_tail + 1) % KEY_QUEUE_DEPTH;
    s_q_size++;
}

/* ==========================================================================
 * 3. 测试辅助:读取当前 ADC 原始值
 * ========================================================================== */

uint16_t KEY_ReadAdcValue(void)
{
    uint16_t ADCdata = 0;
    HAL_ADC_Start(&sAdcHandle);                    // 启用 ADC 转换

    while(!__HAL_ADC_GET_RAWFLAG(&sAdcHandle, ADC_RAWINTFLAG_CHANNEL0));

    __HAL_ADC_CLEAR_FLAG(&sAdcHandle, ADC_RAWINTFLAG_CHANNEL0);

    if (HAL_ADC_PollForConversion(&sAdcHandle, ADC_TIMEOUT_MAX) == HAL_OK)
        ADCdata = (uint16_t)HAL_ADC_GetValue(&sAdcHandle, ADC_CONTINUE_CHANNEL_0);
    HAL_ADC_Stop(&sAdcHandle);

    #ifdef _debug_
    printf("ADC 原始值为:%d\n", ADCdata);
    #endif

    return ADCdata;
}

/**
 * @brief  取一次按键 ADC 的 N 个采样,去掉最大最小后求平均
 * @retval 0~4095
 */
uint16_t ADC_KEY_GetValue(void)
{
    uint16_t s[ADC_KEY_SAMPLE_N];
    uint16_t mn, mx, sum;
    uint8_t  i;

    sum = 0; mn = 0xFFFF; mx = 0;
    for (i = 0; i < ADC_KEY_SAMPLE_N; i++)
    {
        s[i] = KEY_ReadAdcValue();
        if (s[i] < mn) mn = s[i];
        if (s[i] > mx) mx = s[i];
        sum += s[i];
    }
    /* 去掉一个最大和一个最小,剩余求平均 */
    return (uint16_t)((sum - mn - mx) / (ADC_KEY_SAMPLE_N - 2));
}

/* ==========================================================================
 * 4. 对外接口
 * ========================================================================== */
void KEY_Init(void)
{
    s_key.stable_id = KEY_NONE;
    s_key.candidate = KEY_NONE;
    s_key.deb_cnt   = 0;
    s_key.rel_cnt   = 0;
    s_key.long_sent = 0;
    s_pending_click  = 0;
    s_pending_id     = KEY_NONE;
    s_pending_rel_ms = 0;
    s_q_head = s_q_tail = s_q_size = 0;
}

/**
 * @brief  ADC 与按键映射:落在 [center-tol, center+tol] 范围内即视为该键
 * @note   容差窗口不可相交,否则会出现"双键"误判
 *         若采样值没有命中任何窗口,返回 KEY_NONE,避免误触发
 */
static KeyId_t KEY_MapAdc(uint16_t adc)
{
    uint8_t i;
    for (i = 0; i < (uint8_t)(sizeof(s_key_map) / sizeof(s_key_map[0])); i++)
    {
        int32_t d = (int32_t)adc - (int32_t)s_key_map[i].center;
        if (d < 0) d = -d;
        if (d <= (int32_t)s_key_map[i].tol)
            return s_key_map[i].id;
    }
    return KEY_NONE;
}

static volatile uint16_t s_raw_adc = 0;

uint16_t KEY_GetRawAdc(void)
{
    return s_raw_adc;
}

/**
 * @brief 每 KEY_SCAN_PERIOD_MS 调一次
 */
void KEY_Scan(uint32_t now_ms)
{
    uint16_t adc = KEY_ReadAdcValue();
    KeyId_t  raw;

    s_raw_adc = adc;
    raw = KEY_MapAdc(adc);

    /* ---------- A. 消抖判决:连续 N 次确认为同一按键 ---------- */
    if (raw == s_key.candidate)
    {
        if (s_key.deb_cnt < 0xFF) s_key.deb_cnt++;
    }
    else
    {
        s_key.candidate = raw;
        s_key.deb_cnt   = 1;
    }

    if (s_key.deb_cnt < KEY_DEBOUNCE_CNT) return;   /* 未达稳定,本次不输出 */
    raw = s_key.candidate;

    /* ---------- B. 按键变化(按下沿) ---------- */
    if (raw != KEY_NONE && s_key.stable_id == KEY_NONE)
    {
        if (s_pending_click && raw != s_pending_id)
        {
            KEY_PushMsg(s_pending_id, KEY_EVT_SHORT);
            s_pending_click = 0;
        }
        s_key.stable_id      = raw;
        s_key.press_ms       = now_ms;
        s_key.last_repeat_ms = now_ms;
        s_key.long_sent      = 0;
        s_key.rel_cnt        = 0;
        return;
    }

    /* ---------- C. 延长按住(按住期间输出长按/重复) ---------- */
    if (raw != KEY_NONE && s_key.stable_id == raw)
    {
        uint32_t held = now_ms - s_key.press_ms;   /* 即使支持 tick 回绕也要避免溢出 */

        if (!s_key.long_sent && held >= KEY_LONG_PRESS_MS)
        {
            s_pending_click = 0;
            s_key.long_sent = 1;
            KEY_PushMsg(raw, KEY_EVT_LONG);
        }
        else if (s_key.long_sent && (now_ms - s_key.last_repeat_ms) >= KEY_LONG_REPEAT_MS)
        {
            s_key.last_repeat_ms = now_ms;
            KEY_PushMsg(raw, KEY_EVT_REPEAT);
        }
        return;
    }

    /* ---------- D. 抬起判决 ---------- */
    if (s_pending_click && raw == KEY_NONE && s_key.stable_id == KEY_NONE)
    {
        if ((now_ms - s_pending_rel_ms) >= KEY_DOUBLE_GAP_MS)
        {
            KEY_PushMsg(s_pending_id, KEY_EVT_SHORT);
            s_pending_click = 0;
        }
    }

    if (raw == KEY_NONE && s_key.stable_id != KEY_NONE)
    {
        if (s_key.rel_cnt < 0xFF) s_key.rel_cnt++;
        if (s_key.rel_cnt < KEY_RELEASE_CNT) return;

        if (s_key.long_sent)
        {
            KEY_PushMsg(s_key.stable_id, KEY_EVT_RELEASE);
        }
        else if (s_pending_click && s_pending_id == s_key.stable_id)
        {
            KEY_PushMsg(s_key.stable_id, KEY_EVT_DOUBLE);
            s_pending_click = 0;
        }
        else
        {
            s_pending_click  = 1;
            s_pending_id     = s_key.stable_id;
            s_pending_rel_ms = now_ms;
        }

        s_key.stable_id = KEY_NONE;
        s_key.rel_cnt   = 0;
        s_key.deb_cnt   = 0;
    }
}

uint8_t KEY_GetMsg(KeyMsg_t *msg)
{
    if (s_q_size == 0) return 0;
    *msg     = s_queue[s_q_head];
    s_q_head = (s_q_head + 1) % KEY_QUEUE_DEPTH;
    s_q_size--;
    return 1;
}

uint8_t KEY_IsPressed(KeyId_t id)
{
    return (s_key.stable_id == id) ? 1 : 0;
}

/* ==========================================================================
 * 5. 调试输出用具
 * ========================================================================== */
/**
 * @brief  每 500ms 打印出 ADC 原始值 / 按键电压 / 当前命中按键 / 是否抖动过大
 *         在 main 循环里调用 KEY_DebugPrint(now),逐次按住 K1~K4 约 2 秒
 */
void KEY_DebugPrint(uint32_t now_ms)
{
    static uint32_t s_last = 0;
    static uint16_t s_min = 0xFFFF, s_max = 0;
    uint16_t adc;

    if (now_ms - s_last < 500) return;
    s_last = now_ms;

    adc = KEY_GetRawAdc();

    /* 统计采样窗口内最大最小值,用于观察抖动 */
    if (adc < s_min) s_min = adc;
    if (adc > s_max) s_max = adc;

    printf("ADC=%4u  V=%u.%02uV  KEY=%d  %s\r\n",
           adc,
           (unsigned)((uint32_t)adc * 330 / 4095),
           (unsigned)(((uint32_t)adc * 33000 / 4095) % 100),
           (int)KEY_MapAdc(adc),
           (s_max - s_min > (uint16_t)(s_key_map[0].tol * 2)) ? "!! 抖动过大" : "");

    s_min = 0xFFFF; s_max = 0;
}

