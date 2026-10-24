/**
 * @file    max30102.h
 * @brief   Driver header for MAX30102 Pulse Oximeter & Heart Rate Sensor
 *          Target: STM32F7 (HAL I2C + GPIO EXTI)
 *          Project: MAD Assignment - Pulse Wave Velocity (Group A-08)
 */

#ifndef MAX30102_H
#define MAX30102_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* =========================================================================
 * 1. I2C Address and Register Definitions
 * ========================================================================= */
#define MAX30102_I2C_ADDR           (0x57 << 1)  /* 8-bit write address: 0xAE, read: 0xAF */

/* Status Registers */
#define MAX30102_REG_INTR_STATUS_1  0x00
#define MAX30102_REG_INTR_STATUS_2  0x01
#define MAX30102_REG_INTR_ENABLE_1  0x02
#define MAX30102_REG_INTR_ENABLE_2  0x03

/* FIFO Registers */
#define MAX30102_REG_FIFO_WR_PTR    0x04
#define MAX30102_REG_OVF_COUNTER    0x05
#define MAX30102_REG_FIFO_RD_PTR    0x06
#define MAX30102_REG_FIFO_DATA      0x07

/* Configuration Registers */
#define MAX30102_REG_FIFO_CONFIG    0x08
#define MAX30102_REG_MODE_CONFIG    0x09
#define MAX30102_REG_SPO2_CONFIG    0x0A
#define MAX30102_REG_LED1_PA        0x0C  /* Red LED Pulse Amplitude */
#define MAX30102_REG_LED2_PA        0x0D  /* IR LED Pulse Amplitude  */
#define MAX30102_REG_MULTI_LED_1    0x11
#define MAX30102_REG_MULTI_LED_2    0x12

/* Die Temperature Registers */
#define MAX30102_REG_TEMP_INT       0x1F
#define MAX30102_REG_TEMP_FRAC      0x20
#define MAX30102_REG_TEMP_CONFIG    0x21

/* Identification Registers */
#define MAX30102_REG_REV_ID         0xFE
#define MAX30102_REG_PART_ID        0xFF  /* Expected: 0x15 */

#define MAX30102_EXPECTED_PART_ID   0x15

/* =========================================================================
 * 2. Configuration Bitmasks
 * ========================================================================= */
/* Mode Configuration (0x09) */
#define MAX30102_MODE_SHDN          0x80
#define MAX30102_MODE_RESET         0x40
#define MAX30102_MODE_HR_ONLY       0x02
#define MAX30102_MODE_SPO2          0x03
#define MAX30102_MODE_MULTI_LED     0x07

/* SpO2 Configuration (0x0A) */
/* ADC Range: 00 = 2048nA, 01 = 4096nA, 10 = 8192nA, 11 = 16384nA */
#define MAX30102_ADC_RGE_2048       0x00
#define MAX30102_ADC_RGE_4096       0x20
#define MAX30102_ADC_RGE_8192       0x40
#define MAX30102_ADC_RGE_16384      0x60

/* Sample Rate (Hz) */
#define MAX30102_SR_50              0x00
#define MAX30102_SR_100             0x04
#define MAX30102_SR_200             0x08
#define MAX30102_SR_400             0x0C
#define MAX30102_SR_800             0x10
#define MAX30102_SR_1000            0x14
#define MAX30102_SR_1600            0x18
#define MAX30102_SR_3200            0x1C

/* LED Pulse Width (us) & Resolution (bits) */
#define MAX30102_PW_69_15BIT        0x00
#define MAX30102_PW_118_16BIT       0x01
#define MAX30102_PW_215_17BIT       0x02
#define MAX30102_PW_411_18BIT       0x03

/* Interrupt Enable 1 (0x02) */
#define MAX30102_INT_A_FULL         0x80
#define MAX30102_INT_PPG_RDY        0x40
#define MAX30102_INT_ALC_OVF        0x20

/* =========================================================================
 * 3. Ring Buffer Configuration for Asynchronous Sampling
 * ========================================================================= */
#define MAX30102_RING_SIZE          32U

typedef struct {
    uint32_t red;
    uint32_t ir;
    uint32_t timestamp_us;  /* ค่าที่ได้จาก TIM2 CNT ขณะ INT เกิด */
} MAX30102_Sample_t;

/* =========================================================================
 * 4. Exported Functions
 * ========================================================================= */
/**
 * @brief  เริ่มต้นการทำงานของ MAX30102
 * @param  hi2c  Pointer ไปยัง I2C Handle (เช่น &hi2c1)
 * @return HAL_OK เมื่อพบ Part ID 0x15 และตั้งค่าสำเร็จ
 */
HAL_StatusTypeDef max30102_init(I2C_HandleTypeDef *hi2c);

/**
 * @brief  สั่ง Reset ชิป MAX30102 ผ่านซอฟต์แวร์
 */
HAL_StatusTypeDef max30102_reset(void);

/**
 * @brief  อ่าน Part ID จากรีจิสเตอร์ 0xFF
 */
HAL_StatusTypeDef max30102_read_part_id(uint8_t *part_id);

/**
 * @brief  อ่านตัวอย่าง 1 ชุด (Red + IR 6 ไบต์) ตรงจาก FIFO
 */
HAL_StatusTypeDef max30102_read_fifo(uint32_t *red, uint32_t *ir);

/**
 * @brief  ฟังก์ชันเรียกใน EXTI ISR (เมื่อขา INT ตก) เพื่อแสตมป์เวลา TIM2 ทันที
 * @param  timestamp_us  ค่าจาก __HAL_TIM_GET_COUNTER(&htim2)
 */
void max30102_interrupt_handler(uint32_t timestamp_us);

/**
 * @brief  ประมวลผลอ่าน FIFO เข้า Ring Buffer (เรียกใน while(1) ของ main)
 * @return จำนวนตัวอย่างใหม่ที่อ่านเข้ามาได้
 */
uint8_t max30102_process_fifo(void);

/**
 * @brief  ดึงตัวอย่างพร้อม timestamp ออกจาก Ring Buffer ทีละตัว
 * @return 1 ถ้ามีข้อมูล, 0 ถ้าบัฟเฟอร์ว่าง
 */
uint8_t max30102_pop_sample(uint32_t *red, uint32_t *ir, uint32_t *timestamp_us);

/**
 * @brief  ตรวจสอบว่ามีตัวอย่างพร้อมประมวลผลหรือไม่
 */
uint8_t max30102_has_data(void);

/**
 * @brief  ปรับระดับกระแสขับ LED (0x00 = 0mA ถึง 0xFF = 50mA)
 */
HAL_StatusTypeDef max30102_set_led_current(uint8_t red_pa, uint8_t ir_pa);

/**
 * @brief  ตรวจสอบสถานะว่าเซนเซอร์เชื่อมต่อและตั้งค่าเรียบร้อยแล้วหรือไม่
 */
uint8_t max30102_is_ready(void);

/**
 * @brief  อ่านค่าดิบอินฟราเรด (IR) ล่าสุดที่อ่านได้จากชิป
 */
uint32_t max30102_get_last_ir(void);

/**
 * @brief  อ่านค่าดิบแสงแดง (Red) ล่าสุดที่อ่านได้จากชิป
 */
uint32_t max30102_get_last_red(void);

#ifdef __cplusplus
}
#endif

#endif /* MAX30102_H */
