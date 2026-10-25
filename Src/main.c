/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body (LCD + sensors merged)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "rng.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>

/* Sensor side (included first so the ILI9341 colour macros RED/GREEN/... cannot leak into these headers) */
#include "ecg_rpeak.h"
#include "max30102.h"
#include "pwv_metrics.h"

/* LCD side */
#include "lcd_module.h"
#include "ILI9341_Touchscreen.h"      // TP_Touchpad_Pressed
#include "ILI9341_STM32_Driver.h"     // ILI9341_Init
#include "ILI9341_GFX.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ECG_BUF_LEN      512
#define ECG_HALF         (ECG_BUF_LEN / 2)
#define ECG_AVG_N        8      /* ต้องเป็นยกกำลัง 2 (ที่ 200 Hz ค่า 4 ตัด 50 Hz พอดี)  ไฟบ้าน 50 เลยใช่ 4 200/50 = 4*/
#define ECG_UART_ENABLE  1
#define ECG_FILTER_DELAY_US  (((ECG_AVG_N - 1U) * 1000000UL) / (2U * ECG_FS_HZ))

/* LCD: draw ~100 ECG columns per second (one sweep of 320 px = ~3.2 s). Increase to sweep slower. */
#define ECG_DISP_DECIM   ((ECG_FS_HZ) / 100U)

/* Pins used by the sensor side (PE13 = D3 buzzer, PB0 = LD1). Skipped if main.h already defines them. */
#ifndef BUZZER_PORT
#define BUZZER_PORT      GPIOE
#define BUZZER_PIN       GPIO_PIN_13
#endif
#ifndef LED1_PORT
#define LED1_PORT        GPIOB
#define LED1_PIN         GPIO_PIN_0
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* ---- LCD ---- */
/* Everything the LCD draws (t_collected_data from lcd_module.h), filled by lcd_collect() */
static t_collected_data data = { .is_finger = 0, .height = 0 };

/* ---- Sensors ---- */
/* ตัวแปรและฟังก์ชันควบคุม Buzzer แจ้งเตือน 3 วินาที (Non-blocking) */
static uint32_t s_buzzer_off_tick = 0;
static uint8_t  s_session_started = 0;

static inline void buzzer_trigger_3s(void)
{
  HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_SET);
  s_buzzer_off_tick = HAL_GetTick() + 3000;
}

static inline void buzzer_update(void)
{
//  if (s_buzzer_off_tick > 0 && HAL_GetTick() >= s_buzzer_off_tick)
//  {
//    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
//    HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_RESET);
//    s_buzzer_off_tick = 0;
//  }
}

__attribute__((aligned(32))) uint16_t ecg_buf[ECG_BUF_LEN];

/* ค่าที่กรองแล้ว ตำแหน่งตรงกับ ecg_buf ใช้ดูเป็น Array ใน CubeMonitor */
volatile uint16_t ecg_filt[ECG_BUF_LEN];
/* ค่าล่าสุดที่กรองแล้ว ใช้ทำกราฟ time series ใน CubeMonitor */
volatile uint16_t ecg_monitor = 0;

/* ตัวแปรสำหรับแสดงผลกราฟ PPG, IR AC, Red AC ใน STM32CubeMonitor และ Serial Plotter */
volatile uint16_t ppg_monitor    = 2048;         /* สัญญาณ PPG AC (IR AC) สเกลเดียวกับ ECG (0 - 4095) */
volatile uint16_t ir_ac_monitor  = 2048;         /* สัญญาณ IR AC (Infrared) สเกล 0 - 4095 กึ่งกลางที่ 2048 */
volatile uint16_t red_ac_monitor = 2048;         /* สัญญาณ Red AC (แสงสีแดง) สเกล 0 - 4095 กึ่งกลางที่ 2048 */
volatile uint32_t ppg_ir_raw     = 0;            /* สัญญาณดิบ Infrared จาก MAX30102 */
volatile uint16_t ppg_filt[ECG_BUF_LEN];        /* บัฟเฟอร์ PPG array สำหรับดูรูปคลื่นคู่กับ ecg_filt */

static uint16_t last_idx = 0;
static uint16_t avg_hist[ECG_AVG_N];
static uint32_t avg_sum = 0;
static uint8_t  avg_k = 0;

#if ECG_UART_ENABLE
static uint32_t last_r_cnt = 0;
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */
static uint8_t contact_valid(const PWV_Metrics_t *m, uint32_t cur_ir);
static void    lcd_collect(const PWV_Metrics_t *m, uint32_t cur_ir);
static void    height_apply_once(void);
static void    sensor_task(void);
static void    uart_report_task(void);
static void    lcd_task(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
* @brief  TIM2 Period-Elapsed callback — เรียกทุก 5 ms (200 Hz)
*         อ่าน ADC แล้วส่งผ่าน ecg_filter() → rpeak_update()
*/

/* moving average ECG_AVG_N จุด */
static inline uint16_t ecg_filter(uint16_t x)
{
  avg_sum += (uint32_t)x - avg_hist[avg_k];
  avg_hist[avg_k] = x;
  avg_k = (avg_k + 1) & (ECG_AVG_N - 1);
  return (uint16_t)(avg_sum / ECG_AVG_N);
}

/* ประมวลผลทุกตัวอย่างใหม่ที่ DMA เขียนเข้ามา */
static void ecg_process(void)
{
  uint16_t idx = ECG_BUF_LEN - (uint16_t)__HAL_DMA_GET_COUNTER(hadc1.DMA_Handle);
  if (idx >= ECG_BUF_LEN) idx = 0;

  if (idx == last_idx) return;

  /* ถ้าเปิด D-Cache ไว้ ต้อง invalidate ก่อนอ่านข้อมูลที่ DMA เขียน */
  if (SCB->CCR & SCB_CCR_DC_Msk)
  {
    SCB_InvalidateDCache_by_Addr((uint32_t *)ecg_buf, sizeof(ecg_buf));
  }


  while (last_idx != idx)
  {
    uint16_t y = ecg_filter(ecg_buf[last_idx]);
    ecg_filt[last_idx] = y;
    ecg_monitor = y;
    ppg_filt[last_idx] = ppg_monitor;

    rpeak_update(y);
    if (r_peak_count != last_r_cnt)
    {
        last_r_cnt = r_peak_count;
        uint32_t t_r = r_peak_time_us - ECG_FILTER_DELAY_US;      /* หักดีเลย์ตัวกรองแล้ว */
        pwv_metrics_feed_ecg_rpeak(t_r, heart_rate_bpm, rr_last_ms);
    }

    /* LCD: draw every ECG_DISP_DECIM-th filtered sample (does nothing unless the main screen is showing) */
    {
      static uint8_t disp_cnt = 0;
      if (++disp_cnt >= ECG_DISP_DECIM)
      {
        disp_cnt = 0;
        display_ecg_push(y);
      }
    }

    last_idx = (last_idx + 1) % ECG_BUF_LEN;
  }
}

#if ECG_UART_ENABLE
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  (void)hadc;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  (void)hadc;
}
#endif
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_RNG_Init();
  MX_SPI5_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  MX_TIM6_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  /* ---- LCD first: ILI9341_Init() blocks for > 1 s, so it must run before the ADC/DMA and timers start ---- */
  ILI9341_Init();
  display_init();
  display_ecg_set_scale(ECG_SCALE_AUTO, 250);

  /* ---- Sensors ---- */
  rpeak_init();
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)ecg_buf, ECG_BUF_LEN);
  HAL_TIM_Base_Start(&htim2);
  HAL_TIM_Base_Start(&htim6);                        /* เริ่ม trigger ADC 400 Hz */
  rpeak_set_t0(__HAL_TIM_GET_COUNTER(&htim2));       /* จดเวลา TIM2 ทันทีหลังเริ่ม TIM6 */

  /* PWV metrics init. Height = default of the height page (175 cm) until the user presses OK. */
  pwv_metrics_init(0.0f);
  pwv_metrics_set_height_cm((float)display_get_height());   /* arm length L = height * 0.45 / 100 m */

  /* เริ่มต้นเซนเซอร์ MAX30102 */
  if (max30102_init(&hi2c1) == HAL_OK)
  {
    const char *msg = "MAX30102 Init OK\r\n";
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, (uint16_t)strlen(msg), 100);
  }
  else
  {
    const char *msg = "MAX30102 Init Failed\r\n";
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, (uint16_t)strlen(msg), 100);
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    sensor_task();          /* ECG DMA samples, MAX30102 FIFO, PWV metrics: keep this loop fast, no HAL_Delay here */
    uart_report_task();     /* Tera Term report + finger-loss handling, every 2 s */
    lcd_task();             /* touch + screen refresh */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 96;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_15) /* สัญญาณ Interrupt จาก MAX30102 */
  {
    max30102_interrupt_handler(__HAL_TIM_GET_COUNTER(&htim2));
  }
}

/* เงื่อนไขสัญญาณผ่านเกณฑ์ทางการแพทย์: มีนิ้ว และ PI >= 0.4% (ไม่เกิน 20.0%) */
static uint8_t contact_valid(const PWV_Metrics_t *m, uint32_t cur_ir)
{
  return (cur_ir >= 10000 && m->finger_detected && m->pi_pct >= 0.4f && m->pi_pct <= 20.0f);
}

/* Sensors + height page -> t_collected_data (what display_update() draws) */
static void lcd_collect(const PWV_Metrics_t *m, uint32_t cur_ir)
{
  data.is_finger = contact_valid(m, cur_ir);
  data.bpm       = (uint16_t)m->hr_bpm;
  data.pi        = m->pi_pct;

  /* SpO2: 10 s average once the baseline is locked, running value before that */
  float spo2 = m->baseline_locked ? m->spo2_avg_10s : m->spo2_pct;
  data.spo2 = (uint8_t)(spo2 + 0.5f);

  /* Vascular age is only valid after the baseline lock (0 until then) */
  data.vascular_age = (m->baseline_locked && m->height_ready && m->pwv_mps > 0.0f) ? m->vascular_age : 0.0f;

  /* Height [cm]: stays 0 until OK is pressed on the height page, then holds the entered value.
   * (display_update() takes a const pointer, so the display layer cannot write it for us.) */
  if (display_get_screen() != SCREEN_HEIGHT)
  {
    data.height = display_get_height();
  }

  /* TODO: not provided by the sensor code yet -> data.bp_sys, data.bp_dia, data.hrv_rmssd stay at 0 */
}

static void sensor_task(void)
{
  ecg_process();

  /* อ่านข้อมูลตัวอย่างจาก MAX30102 เข้า Ring Buffer */
  max30102_process_fifo();

  uint32_t ppg_red = 0, ppg_ir = 0, ppg_ts = 0;
  while (max30102_pop_sample(&ppg_red, &ppg_ir, &ppg_ts))
  {
    /* ป้อนข้อมูล PPG พร้อม Timestamp จาก TIM2 เข้าสู่ระบบประมวลผล */
    pwv_metrics_feed_ppg(ppg_red, ppg_ir, ppg_ts);
    ppg_ir_raw = ppg_ir;
    ir_ac_monitor = pwv_metrics_get_ir_ac_plot();
    red_ac_monitor = pwv_metrics_get_red_ac_plot();
    ppg_monitor = ir_ac_monitor;
  }

  /* อัปเดตสถานะ Buzzer (ดับเองอัตโนมัติเมื่อครบ 3 วินาทีแบบ Non-blocking ไม่บล็อกระบบ) */
  buzzer_update();
}

/* ตรวจสอบสถานะและรายงานผลออกทาง UART (Tera Term) ทุกๆ 2 วินาที */
static void uart_report_task(void)
{
  static uint32_t last_report_tick = 0;
  if (HAL_GetTick() - last_report_tick < 2000)
  {
    return;
  }
  last_report_tick = HAL_GetTick();

  if (!max30102_is_ready())
  {
    max30102_init(&hi2c1);
  }

  PWV_Metrics_t m;
  pwv_metrics_get(&m);
  uint32_t cur_ir = max30102_get_last_ir();
  char rep_str[220];
  int len;

  /* เงื่อนไขสัญญาณผ่านเกณฑ์ทางการแพทย์: มีนิ้ว และ PI >= 0.4% (ไม่เกิน 20.0%) */
  uint8_t valid = contact_valid(&m, cur_ir);

  if (valid)
  {
    /* วางนิ้วแล้วตรวจพบสัญญาณผ่านเกณฑ์รอบแรก -> บันทึกว่า session เริ่มต้นแล้ว */
    s_session_started = 1;

    int spo2_10s_i = (int)m.spo2_avg_10s;
    int spo2_10s_f = (int)((m.spo2_avg_10s - (float)spo2_10s_i) * 10.0f + 0.5f);
    if (spo2_10s_f >= 10) { spo2_10s_i++; spo2_10s_f = 0; }

    int pi_i = (int)m.pi_pct;
    int pi_f = (int)((m.pi_pct - (float)pi_i) * 100.0f + 0.5f);
    if (pi_f >= 100) { pi_i++; pi_f = 0; }

    int ptt_i = (int)m.ptt_ms;
    int ptt_f = (int)((m.ptt_ms - (float)ptt_i) * 10.0f + 0.5f);
    if (ptt_f >= 10) { ptt_i++; ptt_f = 0; }

    if (m.baseline_locked)
    {
      /* หลัง 20 วินาที: ล็อก PTT และ อายุหลอดเลือดไว้คงที่ พร้อมแสดง SpO2 เฉลี่ยทุก 10 วินาที */
      if (m.height_ready && m.pwv_mps > 0.0f)
      {
        int pwv_i = (int)m.pwv_mps;
        int pwv_f = (int)((m.pwv_mps - (float)pwv_i) * 100.0f + 0.5f);
        if (pwv_f >= 100) { pwv_i++; pwv_f = 0; }
        int vage = (int)(m.vascular_age + 0.5f);
        len = snprintf(rep_str, sizeof(rep_str),
                       "[A-08 2s] ECG_HR:%u bpm | PTT:%d.%d ms (LOCKED) | PWV:%d.%02d m/s | V-Age:%d yr (LOCKED) | SpO2:%d.%d%% (10s Avg) | PI:%d.%02d%%\r\n",
                       (unsigned int)m.hr_bpm, ptt_i, ptt_f, pwv_i, pwv_f, vage, spo2_10s_i, spo2_10s_f, pi_i, pi_f);
      }
      else
      {
        len = snprintf(rep_str, sizeof(rep_str),
                       "[A-08 2s] ECG_HR:%u bpm | PTT:%d.%d ms (LOCKED) | SpO2:%d.%d%% (10s Avg) | PI:%d.%02d%%\r\n",
                       (unsigned int)m.hr_bpm, ptt_i, ptt_f, spo2_10s_i, spo2_10s_f, pi_i, pi_f);
      }
    }
    else
    {
      /* ในช่วง 20 วินาทีแรก: แสดงค่า PTT สะสม และเปอร์เซ็นต์นับถอยหลังการเก็บ Baseline */
      int spo2_i = (int)m.spo2_pct;
      int spo2_f = (int)((m.spo2_pct - (float)spo2_i) * 10.0f + 0.5f);
      if (spo2_f >= 10) { spo2_i++; spo2_f = 0; }

      len = snprintf(rep_str, sizeof(rep_str),
                     "[A-08 2s] ECG_HR:%u bpm | PTT:%d.%d ms | SpO2:%d.%d%% | PI:%d.%02d%% | [Baseline 20s: %u%%]\r\n",
                     (unsigned int)m.hr_bpm, ptt_i, ptt_f, spo2_i, spo2_f, pi_i, pi_f, (unsigned int)m.calib_progress_pct);
    }
  }
  else
  {
    /* สัญญาณไม่ผ่านเกณฑ์ (PI < 0.4% หรือ ไม่มีนิ้ว) -> ห้ามขึ้นค่าต่างๆ ที่ MAX30102 อ่านได้เลย! */
    if (s_session_started)
    {
      /* เคยตรวจพบ finger detect รอบแรกแล้ว แต่เกิดขยับนิ้ว หรือ PI < 0.4% หรือนิ้วหลุด:
       * 1. สั่ง Buzzer ดัง 3 วินาที
       * 2. Reset ค่า finger detect รอบแรกและรีเซ็ต baseline เพื่อเริ่มใหม่ */
      buzzer_trigger_3s();
      s_session_started = 0;
      pwv_metrics_init(0.0f);
      pwv_metrics_set_height_cm((float)display_get_height());   /* keep the height the user entered */

      int pi_i = (int)m.pi_pct;
      int pi_f = (int)((m.pi_pct - (float)pi_i) * 100.0f + 0.5f);
      if (pi_f >= 100) { pi_i++; pi_f = 0; }

      len = snprintf(rep_str, sizeof(rep_str),
                     "[A-08 2s] ECG_HR:%u bpm | [WARN] PI < 0.4%% (PI:%d.%02d%%) / Finger Lost -> BUZZER ON (3s) & RESET!\r\n",
                     (unsigned int)m.hr_bpm, pi_i, pi_f);
    }
    else
    {
      /* สถานะรอนิ้วรอบแรก: แสดงเฉพาะ ECG_HR ห้ามแสดงค่าใดๆ ของ MAX30102 */
      if (cur_ir < 10000)
      {
        len = snprintf(rep_str, sizeof(rep_str),
                       "[A-08 2s] ECG_HR:%u bpm | MAX30102: No Finger Touch\r\n",
                       (unsigned int)m.hr_bpm);
      }
      else
      {
        int pi_i = (int)m.pi_pct;
        int pi_f = (int)((m.pi_pct - (float)pi_i) * 100.0f + 0.5f);
        if (pi_f >= 100) { pi_i++; pi_f = 0; }
        len = snprintf(rep_str, sizeof(rep_str),
                       "[A-08 2s] ECG_HR:%u bpm | MAX30102: PI < 0.4%% (PI:%d.%02d%%) - Waiting for stable contact...\r\n",
                       (unsigned int)m.hr_bpm, pi_i, pi_f);
      }
    }
  }

  HAL_UART_Transmit(&huart3, (uint8_t *)rep_str, (uint16_t)len, 100);
}

/* Runs once, the moment OK is pressed on the height page: gives the height to the PWV
 * calculation and restarts the measurement, so nothing computed with the default height is kept. */
static void height_apply_once(void)
{
  static uint8_t applied = 0;

  if (!applied && display_get_screen() != SCREEN_HEIGHT)
  {
    applied = 1;
    s_session_started = 0;
    pwv_metrics_init(0.0f);
    pwv_metrics_set_height_cm((float)display_get_height());
  }
}

/* LCD: touch handling (height page / page switch) and periodic screen refresh */
static void lcd_task(void)
{
  static uint8_t  touch_prev = 0;
  static uint32_t t_text  = 0;
  static uint32_t t_touch = 0;

  if (display_get_screen() == SCREEN_HEIGHT)
  {
    /* poll while held (250 ms auto-repeat so +/- can be held) */
    if (TP_Touchpad_Pressed() && HAL_GetTick() - t_touch >= 250)
    {
      uint16_t p[2];
      if (TP_Read_Coordinates(p) == TOUCHPAD_DATA_OK)
      {
        display_touch(p[1], 239 - p[0]);   /* portrait -> landscape, see note */
        t_touch = HAL_GetTick();
      }
    }
    touch_prev = 1;      /* swallow the tap that confirmed OK */
  }
  else
  {
    uint8_t touch_now = TP_Touchpad_Pressed() ? 1 : 0;
    if (touch_now && !touch_prev)
    {
      display_next_screen();
      t_text = 0;
    }
    touch_prev = touch_now;
  }

  height_apply_once();   /* no-op until OK is pressed on the height page */

  if (HAL_GetTick() - t_text >= 200)
  {
    PWV_Metrics_t m;
    t_text = HAL_GetTick();
    pwv_metrics_get(&m);
    lcd_collect(&m, max30102_get_last_ir());
    display_update(&data);
  }
}
/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
    ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
