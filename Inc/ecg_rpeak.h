/**
 * @file    ecg_rpeak.h
 * @brief   R-peak detector for ECG — Pan-Tompkins-inspired (slope-energy method)
 *
 * วิธีใช้:
 *   1. เรียก rpeak_init() ครั้งเดียวใน main() ก่อน start timer
 *   2. ในทุก ISR / task ที่ได้ตัวอย่าง ECG กรองแล้ว ให้เรียก rpeak_update(y)
 *   3. อ่านผลได้จาก heart_rate_bpm, rr_last_ms, rr_ms[]
 */

#ifndef ECG_RPEAK_H
#define ECG_RPEAK_H

#include <stdint.h>

/* ────────────────────────────────────────────────────────────────
   1)  พารามิเตอร์ (ปรับได้)
   ──────────────────────────────────────────────────────────────── */
#define ECG_FS_HZ       400U      /**< อัตราสุ่มตัวอย่าง (Hz) ต้องตรงกับ TIM2      */
#define ECG_INTEG_N     32U       /**< หน้าต่างรวมความชัน 32 ตัวอย่าง = 80 ms      */
#define ECG_HIST_N      128U      /**< ประวัติสัญญาณ 128 ตัวอย่าง = 320 ms                   */
#define ECG_WARMUP      64U       /**< ข้ามตัวอย่างแรกที่ตัวกรองยังไม่นิ่ง          */
#define ECG_LEARN       800U      /**< 2 วินาทีแรกใช้เรียนรู้ระดับ QRS              */
#define ECG_MIN_ENERGY  1250UL    /**< เกณฑ์ขั้นต่ำกัน noise                        */
#define ECG_REFRACT     120U      /**< ระยะห้าม R-R (samples) ≈ 300 ms              */
#define ECG_RR_MIN      120U      /**< R-R ต่ำสุดที่รับได้ (samples) ≈ 200 bpm     */
#define ECG_RR_MAX      800U      /**< R-R สูงสุดที่รับได้ (samples) ≈ 30  bpm     */
#define ECG_TIMEOUT     (3U * ECG_FS_HZ) /**< ไม่เจอ R เกิน 3 วิ = สัญญาณหาย      */
#define ECG_RR_N        8U        /**< เก็บ R-R ล่าสุดกี่ค่า                        */

/* ────────────────────────────────────────────────────────────────
   2)  ตัวแปรผลลัพธ์ (volatile — CubeMonitor / debugger อ่านได้)
   ──────────────────────────────────────────────────────────────── */
extern volatile uint16_t rr_ms[ECG_RR_N]; /**< array R-R ล่าสุด (ms)              */
extern volatile uint16_t rr_last_ms;       /**< R-R interval ล่าสุด (ms)           */
extern volatile uint16_t heart_rate_bpm;   /**< HR (bpm); 0 = ยังไม่มีค่า/หาย     */
extern volatile uint32_t r_peak_time_us;   /**< เวลาของ R-peak ล่าสุด (us) ในฐานเวลาเดียวกับ TIM2 (1 MHz, 32 บิต) วนรอบทุก ~71.58 นาที
                                                ต้องลบกันด้วย uint32_t เสมอ: (uint32_t)(t_b - t_a) */
extern volatile uint32_t r_peak_count;     /**< จำนวน R-peak ที่ตรวจเจอ (เพิ่มทีละ 1) ใช้เช็คว่ามี R ใหม่ */

/* ────────────────────────────────────────────────────────────────
   3)  API
   ──────────────────────────────────────────────────────────────── */

/**
 * @brief  รีเซ็ตสถานะทั้งหมดกลับเป็นค่าเริ่มต้น
 *         เรียกครั้งเดียวใน main() ก่อนเปิด timer
 */
void rpeak_init(void);

/**
 * @brief  กำหนดเวลาอ้างอิง (us) ของจุดเริ่มนับตัวอย่าง = ค่า TIM2->CNT ขณะสั่ง TIM6 เริ่มนับ
 *         ตัวอย่างที่ k เกิดที่ t0 + k * (1000000 / ECG_FS_HZ) us
 *         เรียกทันทีหลังเริ่ม TIM6 (ถ้าไม่เรียก t0 = 0 คือนับจาก rpeak_init)
 */
void rpeak_set_t0(uint32_t t0_us);

/**
 * @brief  ส่งตัวอย่าง ECG ที่ผ่านการกรองแล้วเข้าตรวจจับ R-peak
 *         เรียกหนึ่งครั้งต่อหนึ่ง ISR / task tick (ตามลำดับเวลา)
 * @param  y   ค่าสัญญาณ ECG หลัง filter (uint16_t, 0–4095 สำหรับ ADC 12-bit)
 */
void rpeak_update(uint16_t y);

#endif /* ECG_RPEAK_H */
