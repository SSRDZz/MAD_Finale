/**
 * @file    pwv_metrics.h
 * @brief   Core algorithms for Pulse Wave Velocity (PWV), PTT, SpO2, PI, and Vascular Age
 *          Target: STM32F7 / Group A-08 MAD Assignment
 *          Reference: Group A-08 MAD Assignment Progress Report 2568 (1)
 */

#ifndef PWV_METRICS_H
#define PWV_METRICS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* =========================================================================
 * 1. Output Data Structure
 * ========================================================================= */
typedef struct {
    float    hr_bpm;            /**< อัตราการเต้นของหัวใจ (bpm) จาก AD8232 ECG เท่านั้น */
    uint16_t rr_ms;             /**< RR interval ล่าสุด (ms)                           */
    
    uint32_t ptt_us;            /**< Pulse Transit Time (us)                          */
    float    ptt_ms;            /**< Pulse Transit Time (ms)                          */
    
    float    pwv_mps;           /**< Pulse Wave Velocity (m/s) = L / PTT              */
    float    vascular_age;      /**< อายุหลอดเลือดประเมิน (ปี)                         */
    float    user_height_cm;    /**< ส่วนสูงผู้ใช้งาน (cm) สำหรับคำนวณแขน * 0.45       */
    float    arm_length_m;      /**< ช่วงความยาวแขน L (เมตร) = height_cm * 0.45 / 100   */
    uint8_t  height_ready;      /**< 1 = มีข้อมูลส่วนสูงแล้ว, 0 = ยังรอเพื่อนใส่ส่วนสูง */

    float    spo2_pct;          /**< ค่าความอิ่มตัวออกซิเจนในเลือด SpO2 (%)            */
    float    pi_pct;            /**< Perfusion Index (%)                              */
    
    uint8_t  finger_detected;   /**< 1 = นิ้วแนบสนิท (PI >= 0.4%), 0 = หลุด/ไม่แนบ     */
    uint32_t ptt_count;         /**< จำนวนรอบที่ตรวจจับ PTT สำเร็จ                     */
    uint32_t last_update_tick;  /**< เวลา HAL_GetTick() ล่าสุดที่อัปเดตผลลัพธ์         */
    
    /* ระบบยึดค่า 10 วินาทีแรก และเฉลี่ย SpO2 ทุก 10 วินาที */
    uint8_t  baseline_locked;   /**< 1 = ล็อกค่า PTT และอายุหลอดเลือดจาก 10 วิแรกแล้ว */
    float    ptt_baseline_ms;   /**< ค่าเฉลี่ย PTT ที่ล็อกไว้จาก 10 วินาทีแรก (ms)    */
    float    spo2_avg_10s;      /**< ค่าเฉลี่ย SpO2 จากรอบ 10 วินาทีล่าสุด (%)        */
    uint8_t  calib_progress_pct;/**< ความคืบหน้า 0 - 100% ในช่วงเก็บ 10 วินาทีแรก      */
} PWV_Metrics_t;

/* =========================================================================
 * 2. Exported Functions
 * ========================================================================= */
/**
 * @brief  เริ่มต้นระบบอัลกอริทึม (พักการคำนวณอายุไว้ รอรับส่วนสูงจากเพื่อน)
 */
void pwv_metrics_init(float default_arm_length_m);

/**
 * @brief  กำหนดส่วนสูงของผู้ใช้ (cm) เพื่อคำนวณช่วงความยาวแขน: L = (height_cm * 0.45) / 100
 *         (เตรียมฟังก์ชันนี้ไว้ให้เพื่อนเรียกใช้เมื่อโมดูลรับค่าส่วนสูงเสร็จ)
 * @param  height_cm  ส่วนสูงผู้ใช้งาน เช่น 170.0f, 175.0f
 */
void pwv_metrics_set_height_cm(float height_cm);

/**
 * @brief  กำหนดช่วงความยาวแขน L โดยตรง (เมตร)
 */
void pwv_metrics_set_arm_length(float arm_length_m);

/**
 * @brief  สูตรคำนวณอายุหลอดเลือดประเมิน: 20 + ((PWV - 5.5) / 0.1)
 *         (เขียนเตรียมฟังก์ชันไว้ รอคำนวณเมื่อได้ส่วนสูงครบ)
 */
float pwv_calculate_vascular_age(float pwv_mps);

/**
 * @brief  ส่งข้อมูล R-peak จาก ECG (ecg_rpeak.c / AD8232) เข้ามาบันทึกเวลา
 */
void pwv_metrics_feed_ecg_rpeak(uint32_t r_peak_time_us, uint16_t hr_bpm, uint16_t rr_ms);

/**
 * @brief  ส่งตัวอย่าง PPG (Red & IR) พร้อม Timestamp จาก TIM2 เข้ามาวิเคราะห์
 */
void pwv_metrics_feed_ppg(uint32_t red, uint32_t ir, uint32_t timestamp_us);

/**
 * @brief  อ่านผลการคำนวณล่าสุดทั้งหมด
 */
void pwv_metrics_get(PWV_Metrics_t *out);

/**
 * @brief  ตรวจสอบว่ามีผลการคำนวณรอบใหม่พร้อมส่งออกหรือไม่
 */
uint8_t pwv_metrics_has_new_result(void);

/**
 * @brief  อ่านค่าสัญญาณ PPG AC ล่าสุดที่ปรับระดับกึ่งกลาง (2048) เพื่อนำไปพล็อตกราฟคู่กับ ECG
 * @return ค่า 0 - 4095 (สเกลเดียวกับ 12-bit ADC ของ ECG)
 */
uint16_t pwv_metrics_get_ppg_plot_val(void);
uint16_t pwv_metrics_get_ir_ac_plot(void);
uint16_t pwv_metrics_get_red_ac_plot(void);

#ifdef __cplusplus
}
#endif

#endif /* PWV_METRICS_H */
