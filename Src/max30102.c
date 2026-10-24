/**
 * @file    max30102.c
 * @brief   Driver implementation for MAX30102 Pulse Oximeter & Heart Rate Sensor
 *          Target: STM32F7 (HAL I2C + GPIO EXTI)
 *          Project: MAD Assignment - Pulse Wave Velocity (Group A-08)
 */

#include "max30102.h"
#include <string.h>

/* I2C Handle Reference */
static I2C_HandleTypeDef *s_hi2c = NULL;

/* Asynchronous Timestamp FIFO (บันทึกเวลาจาก ISR โดยตรง) */
#define TS_QUEUE_SIZE 32U
static volatile uint32_t s_ts_queue[TS_QUEUE_SIZE];
static volatile uint8_t  s_ts_head = 0;
static volatile uint8_t  s_ts_tail = 0;

/* Main Sample Ring Buffer (จับคู่ Red, IR และ Timestamp ให้พร้อมใช้งาน) */
static MAX30102_Sample_t s_sample_ring[MAX30102_RING_SIZE];
static volatile uint8_t  s_ring_head = 0;
static volatile uint8_t  s_ring_tail = 0;

static uint8_t  s_is_initialized = 0;
static uint32_t s_last_ir = 0;
static uint32_t s_last_red = 0;

/* -------------------------------------------------------------------------
 * Low-level I2C read/write helpers
 * ------------------------------------------------------------------------- */
static HAL_StatusTypeDef max_write(uint8_t reg, uint8_t val)
{
    if (s_hi2c == NULL) return HAL_ERROR;
    return HAL_I2C_Mem_Write(s_hi2c, MAX30102_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &val, 1, 10);
}

static HAL_StatusTypeDef max_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (s_hi2c == NULL) return HAL_ERROR;
    return HAL_I2C_Mem_Read(s_hi2c, MAX30102_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, buf, len, 20);
}

/* -------------------------------------------------------------------------
 * Public APIs
 * ------------------------------------------------------------------------- */
HAL_StatusTypeDef max30102_read_part_id(uint8_t *part_id)
{
    if (part_id == NULL) return HAL_ERROR;
    return max_read(MAX30102_REG_PART_ID, part_id, 1);
}

HAL_StatusTypeDef max30102_reset(void)
{
    HAL_StatusTypeDef res = max_write(MAX30102_REG_MODE_CONFIG, MAX30102_MODE_RESET);
    if (res != HAL_OK) return res;

    /* รอจนกว่าบิต RESET จะกลับเป็น 0 */
    uint8_t mode = 0;
    uint32_t t_start = HAL_GetTick();
    do {
        if (max_read(MAX30102_REG_MODE_CONFIG, &mode, 1) != HAL_OK) return HAL_ERROR;
        if ((mode & MAX30102_MODE_RESET) == 0) break;
        HAL_Delay(1);
    } while ((HAL_GetTick() - t_start) < 100);

    return HAL_OK;
}

HAL_StatusTypeDef max30102_init(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL) return HAL_ERROR;
    s_hi2c = hi2c;

    /* 1. ตรวจสอบ Part ID */
    uint8_t part_id = 0;
    if (max30102_read_part_id(&part_id) != HAL_OK || part_id != MAX30102_EXPECTED_PART_ID)
    {
        return HAL_ERROR;
    }

    /* 2. Soft Reset ชิป */
    if (max30102_reset() != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 3. ล้างพอยน์เตอร์ FIFO */
    max_write(MAX30102_REG_FIFO_WR_PTR, 0x00);
    max_write(MAX30102_REG_OVF_COUNTER, 0x00);
    max_write(MAX30102_REG_FIFO_RD_PTR, 0x00);

    /* 4. ตั้งค่า FIFO Configuration:
     *    - SMP_AVE = 0 (1 sample per FIFO entry)
     *    - FIFO_ROLLOVER_EN = 1 (เปิด rollover ข้อมูลไม่ขาดตอน)
     *    - FIFO_A_FULL = 0x0F
     */
    max_write(MAX30102_REG_FIFO_CONFIG, 0x10);

    /* 5. ตั้งค่า SpO2 Configuration:
     *    - ADC Range: 4096 nA (0x20)
     *    - Sample Rate: 400 Hz (0x0C) -> ตรงกับอัตราสุ่ม ECG 400 Hz ของ TIM6
     *    - Pulse Width: 215 us / 17-bit resolution (0x02)
     *    ผลลัพธ์: 0x20 | 0x0C | 0x02 = 0x2E
     */
    max_write(MAX30102_REG_SPO2_CONFIG, (MAX30102_ADC_RGE_4096 | MAX30102_SR_400 | MAX30102_PW_215_17BIT));

    /* 6. ตั้งค่ากระแสไฟ LED (Red และ IR)
     *    0x32 = ~10.0 mA (เพิ่มความสว่างให้แสงทะลุลงชั้นหลอดเลือดแดงได้ชัดเจน PI สูงขึ้น)
     */
    max_write(MAX30102_REG_LED1_PA, 0x32);
    max_write(MAX30102_REG_LED2_PA, 0x32);

    /* 7. เปิด Interrupt PPG_RDY */
    max_write(MAX30102_REG_INTR_ENABLE_1, MAX30102_INT_PPG_RDY);
    max_write(MAX30102_REG_INTR_ENABLE_2, 0x00);

    /* 8. เคลียร์ Interrupt ที่อาจค้างอยู่ */
    uint8_t clr[2];
    max_read(MAX30102_REG_INTR_STATUS_1, clr, 2);

    /* 9. เริ่มต้นการทำงานในโหมด SpO2 (วัดทั้ง Red และ IR) */
    if (max_write(MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SPO2) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* รีเซ็ตคิวและริงบัฟเฟอร์ */
    s_ts_head = 0;
    s_ts_tail = 0;
    s_ring_head = 0;
    s_ring_tail = 0;

    s_is_initialized = 1;
    return HAL_OK;
}

HAL_StatusTypeDef max30102_set_led_current(uint8_t red_pa, uint8_t ir_pa)
{
    HAL_StatusTypeDef r1 = max_write(MAX30102_REG_LED1_PA, red_pa);
    HAL_StatusTypeDef r2 = max_write(MAX30102_REG_LED2_PA, ir_pa);
    return (r1 == HAL_OK && r2 == HAL_OK) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef max30102_read_fifo(uint32_t *red, uint32_t *ir)
{
    if (red == NULL || ir == NULL) return HAL_ERROR;

    uint8_t d[6];
    if (max_read(MAX30102_REG_FIFO_DATA, d, 6) != HAL_OK) return HAL_ERROR;

    /* ข้อมูล Red: ไบต์ 0..2 (18 บิต) */
    *red = ((((uint32_t)d[0] << 16) | ((uint32_t)d[1] << 8) | (uint32_t)d[2]) & 0x03FFFF);
    /* ข้อมูล IR:  ไบต์ 3..5 (18 บิต) */
    *ir  = ((((uint32_t)d[3] << 16) | ((uint32_t)d[4] << 8) | (uint32_t)d[5]) & 0x03FFFF);

    return HAL_OK;
}

void max30102_interrupt_handler(uint32_t timestamp_us)
{
    /* บันทึกค่าเวลาทันทีใน ISR */
    uint8_t next = (s_ts_head + 1) % TS_QUEUE_SIZE;
    if (next != s_ts_tail)
    {
        s_ts_queue[s_ts_head] = timestamp_us;
        s_ts_head = next;
    }
}

uint8_t max30102_process_fifo(void)
{
    if (!s_is_initialized) return 0;
    uint8_t samples_read = 0;

    /* 1. กรณีมี Timestamp จาก Hardware EXTI Interrupt (โหมดปกติความแม่นยำสูง) */
    while (s_ts_tail != s_ts_head)
    {
        uint32_t r = 0, ir = 0;
        if (max30102_read_fifo(&r, &ir) == HAL_OK)
        {
            s_last_ir = ir;
            s_last_red = r;
            uint32_t ts = s_ts_queue[s_ts_tail];
            s_ts_tail = (s_ts_tail + 1) % TS_QUEUE_SIZE;

            uint8_t next_ring = (s_ring_head + 1) % MAX30102_RING_SIZE;
            if (next_ring != s_ring_tail)
            {
                s_sample_ring[s_ring_head].red = r;
                s_sample_ring[s_ring_head].ir  = ir;
                s_sample_ring[s_ring_head].timestamp_us = ts;
                s_ring_head = next_ring;
                samples_read++;
            }
        }
        else
        {
            break;
        }
    }

    /* 2. Fallback: กรณีขา INT ยังไม่ได้ต่อ หรือหลวม ให้เช็คตัวนับ FIFO ของฮาร์ดแวร์โดยตรง */
    if (samples_read == 0)
    {
        uint8_t wr_ptr = 0, rd_ptr = 0;
        if (max_read(MAX30102_REG_FIFO_WR_PTR, &wr_ptr, 1) == HAL_OK &&
            max_read(MAX30102_REG_FIFO_RD_PTR, &rd_ptr, 1) == HAL_OK)
        {
            int8_t num_samples = (int8_t)(wr_ptr - rd_ptr);
            if (num_samples < 0) num_samples += 32;

            if (num_samples > 4) num_samples = 4;

            extern TIM_HandleTypeDef htim2;
            uint32_t now_us = __HAL_TIM_GET_COUNTER(&htim2);

            for (int8_t i = 0; i < num_samples; i++)
            {
                uint32_t r = 0, ir = 0;
                if (max30102_read_fifo(&r, &ir) == HAL_OK)
                {
                    s_last_ir = ir;
                    s_last_red = r;
                    uint8_t next_ring = (s_ring_head + 1) % MAX30102_RING_SIZE;
                    if (next_ring != s_ring_tail)
                    {
                        s_sample_ring[s_ring_head].red = r;
                        s_sample_ring[s_ring_head].ir  = ir;
                        s_sample_ring[s_ring_head].timestamp_us = now_us;
                        s_ring_head = next_ring;
                        samples_read++;
                    }
                }
            }
        }
    }

    return samples_read;
}

uint8_t max30102_is_ready(void)
{
    return s_is_initialized;
}

uint32_t max30102_get_last_ir(void)
{
    return s_last_ir;
}

uint32_t max30102_get_last_red(void)
{
    return s_last_red;
}

uint8_t max30102_has_data(void)
{
    return (s_ring_head != s_ring_tail);
}

uint8_t max30102_pop_sample(uint32_t *red, uint32_t *ir, uint32_t *timestamp_us)
{
    if (s_ring_head == s_ring_tail) return 0;

    if (red != NULL)          *red = s_sample_ring[s_ring_tail].red;
    if (ir != NULL)           *ir  = s_sample_ring[s_ring_tail].ir;
    if (timestamp_us != NULL) *timestamp_us = s_sample_ring[s_ring_tail].timestamp_us;

    s_ring_tail = (s_ring_tail + 1) % MAX30102_RING_SIZE;
    return 1;
}
