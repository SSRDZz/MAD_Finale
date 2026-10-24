/**
 * @file    pwv_metrics.c
 * @brief   Implementation of Pulse Wave Velocity (PWV), PTT, SpO2, PI, and Vascular Age
 *          Target: STM32F7 / Group A-08 MAD Assignment
 *          Reference: Group A-08 MAD Assignment Progress Report 2568 (1)
 */

#include "pwv_metrics.h"
#include "main.h"
#include <math.h>
#include <string.h>

/* =========================================================================
 * Private Definitions and Constants
 * ========================================================================= */
#define MIN_FINGER_DC           10000.0f   /* ระดับแสงอินฟราเรดขั้นต่ำที่บ่งบอกว่ามีนิ้วสัมผัส */
#define MIN_PERFUSION_INDEX     0.4f       /* เกณฑ์ PI ขั้นต่ำ 0.4% เข้มงวดตามรายงานกลุ่ม A-08 */       /* เกณฑ์ PI ขั้นต่ำ 0.2% เพื่อความไวและเสถียรภาพ */
#define PTT_CALIB_DURATION_MS   20000UL    /* ขยายระยะเวลาเก็บ Baseline PTT เป็น 20 วินาทีตามที่ผู้ใช้สั่ง */
#define MAX_PERFUSION_INDEX     20.0f      /* เกณฑ์ PI สูงสุดในทางสรีรวิทยา (ไม่เกิน 20%)     */
#define PPG_FS_HZ               400.0f     /* อัตราสุ่มตัวอย่าง PPG 400 Hz                  */
#define PPG_REFRACTORY_SAMPLES  100U       /* ระยะห้ามตรวจพีกซ้ำ (~250 ms ที่ 400 Hz)       */

#define PTT_MIN_US              80000UL    /* PTT ต่ำสุดที่สมเหตุสมผลทางสรีรวิทยา (80 ms)    */
#define PTT_MAX_US              500000UL   /* PTT สูงสุดที่สมเหตุสมผลทางสรีรวิทยา (500 ms)   */
#define PPG_FILTER_DELAY_US     7500UL     /* Group delay ของ IIR alpha=0.25 ที่ 400 Hz (7.5 ms) */

/* =========================================================================
 * Private Variables
 * ========================================================================= */
static PWV_Metrics_t s_metrics;

/* ส่วนจัดการความสูงและช่วงความยาวแขน (พักไว้รอรับค่าจากเพื่อน) */
static float s_user_height_cm = 0.0f;       /* ส่วนสูงผู้ใช้ (cm)                    */
static float s_arm_length_m   = 0.0f;       /* ช่วงความยาวแขน L (เมตร) = height * 0.45 */
static uint8_t s_height_ready = 0;          /* 0 = ยังไม่มีส่วนสูง (ไม่คำนวณอายุ), 1 = พร้อมคำนวณ */

static uint8_t s_new_result_flag = 0;

/* สัญญาณ DC และ AC Tracker */
static float s_dc_ir = 0.0f;
static float s_dc_red = 0.0f;
static float s_ac_ir_filt = 0.0f;
static float s_ac_red_filt = 0.0f;

/* ตัวแปรสำหรับหา Peak-to-Peak ของแต่ละรอบการเต้น */
static float s_ir_max = -1e6f;
static float s_ir_min = 1e6f;
static float s_red_max = -1e6f;
static float s_red_min = 1e6f;

/* ตัวแปรตรวจจับจุดยอด Systolic Peak ของ PPG */
static float s_last_ac_ir = 0.0f;
static float s_slope = 0.0f;
static uint32_t s_peak_sample_cnt = 0;
static uint32_t s_last_ppg_peak_time_us = 0;

/* ตัวแปรเชื่อมโยงกับ ECG R-peak (จาก AD8232 เท่านั้น) */
static uint32_t s_last_ecg_r_time_us = 0;
static uint8_t  s_ecg_r_pending = 0;

/* ตัวแปรระบบล็อกค่า 10 วินาทีแรก (PTT & Vascular Age) */
static uint32_t s_calib_start_tick = 0;
static float    s_ptt_calib_sum = 0.0f;
static uint32_t s_ptt_calib_cnt = 0;
static uint32_t s_finger_off_tick = 0;

/* ตัวแปรระบบหาค่าเฉลี่ย SpO2 ทุก 10 วินาที */
static uint32_t s_spo2_window_tick = 0;
static float    s_spo2_window_sum = 0.0f;
static uint32_t s_spo2_window_cnt = 0;

/* =========================================================================
 * Public Functions
 * ========================================================================= */
void pwv_metrics_init(float default_arm_length_m)
{
    memset(&s_metrics, 0, sizeof(s_metrics));
    (void)default_arm_length_m; /* ยกเลิกการใช้ค่าคงที่ 0.80m ไว้ก่อนตามคำสั่ง */

    s_user_height_cm = 0.0f;
    s_arm_length_m   = 0.0f;
    s_height_ready   = 0;

    s_dc_ir = 0.0f;
    s_dc_red = 0.0f;
    s_ac_ir_filt = 0.0f;
    s_ac_red_filt = 0.0f;
    s_ir_max = -1e6f;
    s_ir_min = 1e6f;
    s_red_max = -1e6f;
    s_red_min = 1e6f;
    s_peak_sample_cnt = 0;
    s_ecg_r_pending = 0;
    s_new_result_flag = 0;
    s_calib_start_tick = 0;
    s_ptt_calib_sum = 0.0f;
    s_ptt_calib_cnt = 0;
    s_finger_off_tick = 0;
    s_spo2_window_tick = 0;
    s_spo2_window_sum = 0.0f;
    s_spo2_window_cnt = 0;
}

void pwv_metrics_set_height_cm(float height_cm)
{
    if (height_cm >= 50.0f && height_cm <= 250.0f) {
        s_user_height_cm = height_cm;
        /* สูตรที่กำหนด: ช่วงความยาวแขน (เมตร) = ส่วนสูง (cm) * 0.45 / 100 */
        s_arm_length_m = (height_cm * 0.45f) / 100.0f;
        s_height_ready = 1;

        s_metrics.user_height_cm = s_user_height_cm;
        s_metrics.arm_length_m   = s_arm_length_m;
        s_metrics.height_ready   = 1;
    }
}

void pwv_metrics_set_arm_length(float arm_length_m)
{
    if (arm_length_m > 0.3f && arm_length_m < 1.5f) {
        s_arm_length_m = arm_length_m;
        s_height_ready = 1;
        s_metrics.arm_length_m = s_arm_length_m;
        s_metrics.height_ready = 1;
    }
}

float pwv_calculate_vascular_age(float pwv_mps)
{
    /* สูตร: VascularAge = 20 + ((PWV - 5.5) / 0.1)
     * เขียนเตรียมไว้พร้อมใช้งานเมื่อเพื่อนใส่โมดูลความสูงเสร็จ */
    if (pwv_mps <= 0.0f) return 0.0f;
    float v_age = 20.0f + ((pwv_mps - 5.5f) / 0.1f);
    if (v_age < 18.0f) v_age = 18.0f;
    if (v_age > 99.0f) v_age = 99.0f;
    return v_age;
}

void pwv_metrics_feed_ecg_rpeak(uint32_t r_peak_time_us, uint16_t hr_bpm, uint16_t rr_ms)
{
    s_last_ecg_r_time_us = r_peak_time_us;
    s_ecg_r_pending = 1;

    /* Heart Rate รับจาก AD8232 เท่านั้น */
    s_metrics.hr_bpm = (float)hr_bpm;
    s_metrics.rr_ms = rr_ms;
}

void pwv_metrics_feed_ppg(uint32_t red, uint32_t ir, uint32_t timestamp_us)
{
    float f_red = (float)red;
    float f_ir  = (float)ir;

    /* 1. ตรวจสอบเบื้องต้นว่ามีวัตถุ/นิ้วสัมผัสเซนเซอร์หรือไม่ */
    if (f_ir < MIN_FINGER_DC) {
        s_metrics.finger_detected = 0;
        s_metrics.pi_pct = 0.0f;
        s_metrics.spo2_pct = 0.0f;
        s_dc_ir = 0.0f;
        s_dc_red = 0.0f;
        s_ac_ir_filt = 0.0f;
        s_ac_red_filt = 0.0f;

        /* ถ้ายกนิ้วออกเกิน 3 วินาที ให้รีเซ็ตระบบ 10 วินาที เพื่อเริ่มวัดใหม่เมื่อวางนิ้วครั้งต่อไป */
        if (s_finger_off_tick == 0) {
            s_finger_off_tick = HAL_GetTick();
        } else if (HAL_GetTick() - s_finger_off_tick >= 3000) {
            s_calib_start_tick = 0;
            s_ptt_calib_sum = 0.0f;
            s_ptt_calib_cnt = 0;
            s_metrics.baseline_locked = 0;
            s_metrics.calib_progress_pct = 0;
            s_spo2_window_tick = 0;
            s_spo2_window_sum = 0.0f;
            s_spo2_window_cnt = 0;
        }
        return;
    }
    s_finger_off_tick = 0;

    /* 2. ดึงระดับแสงพื้นหลัง (DC Tracker) */
    if (s_dc_ir < MIN_FINGER_DC) {
        /* เพิ่งแตะนิ้วใหม่: ปรับระดับ DC เข้าหาค่าแสงทันที (Fast Settle) ป้องกันค่ากระชาก */
        s_dc_ir = f_ir;
        s_dc_red = f_red;
        s_ac_ir_filt = 0.0f;
        s_ac_red_filt = 0.0f;
        s_ir_max = -1e6f;
        s_ir_min = 1e6f;
        s_red_max = -1e6f;
        s_red_min = 1e6f;
        return;
    } else {
        s_dc_ir += 0.005f * (f_ir - s_dc_ir);
        s_dc_red += 0.005f * (f_red - s_dc_red);
    }

    /* 3. ดึงสัญญาณพลวัต (AC Component) */
    float ac_ir_raw = f_ir - s_dc_ir;
    float ac_red_raw = f_red - s_dc_red;

    /* กรองความถี่สูงของ AC (Smooth filter เพื่อจับพีกได้แม่นยำ) */
    s_ac_ir_filt += 0.25f * (ac_ir_raw - s_ac_ir_filt);
    s_ac_red_filt += 0.25f * (ac_red_raw - s_ac_red_filt);

    /* ติดตามค่า Min / Max ของรอบการเต้น */
    if (ac_ir_raw > s_ir_max) s_ir_max = ac_ir_raw;
    if (ac_ir_raw < s_ir_min) s_ir_min = ac_ir_raw;
    if (ac_red_raw > s_red_max) s_red_max = ac_red_raw;
    if (ac_red_raw < s_red_min) s_red_min = ac_red_raw;

    s_peak_sample_cnt++;

    /* 4. ตรวจจับจุดยอด Systolic Peak ของคลื่น PPG */
    float curr_slope = s_ac_ir_filt - s_last_ac_ir;
    
    /* เงื่อนไขการเกิดพีก: ความชันเปลี่ยนจากบวกเป็นลบ ผ่านช่วงห้ามซ้ำ และสัญญาณมีแอมพลิจูดสูงพอ */
    if (s_peak_sample_cnt > PPG_REFRACTORY_SAMPLES && 
        s_slope > 0.0f && curr_slope <= 0.0f && 
        s_ac_ir_filt > 50.0f)
    {
        /* หักลบ Filter Delay (7.5 ms) ของตัวกรอง PPG เพื่อให้เวลาตรงกับสรีรวิทยาจริง */
        s_last_ppg_peak_time_us = (timestamp_us >= PPG_FILTER_DELAY_US) ? 
                                  (timestamp_us - PPG_FILTER_DELAY_US) : timestamp_us;
        s_peak_sample_cnt = 0;

        /* 4.1 คำนวณ Perfusion Index (PI) และ SpO2 ของรอบที่ผ่านมา */
        float ac_ir_pp = s_ir_max - s_ir_min;
        float ac_red_pp = s_red_max - s_red_min;

        if (s_dc_ir > 10.0f && ac_ir_pp > 1.0f) {
            /* PI = (AC_ir / DC_ir) * 100 */
            float calc_pi = (ac_ir_pp / s_dc_ir) * 100.0f;
            if (calc_pi <= MAX_PERFUSION_INDEX) {
                s_metrics.pi_pct = calc_pi;
            }
        }

        /* ตรวจสอบเกณฑ์ PI ทางการแพทย์ (0.4% - 20.0%) */
        if (s_metrics.pi_pct >= MIN_PERFUSION_INDEX && s_metrics.pi_pct <= MAX_PERFUSION_INDEX) {
            s_metrics.finger_detected = 1;

            /* SpO2 = 104 - 17 * R */
            if (s_dc_red > 10.0f && ac_ir_pp > 1.0f) {
                float r_ratio = (ac_red_pp / s_dc_red) / (ac_ir_pp / s_dc_ir);
                float spo2 = 104.0f - (17.0f * r_ratio);
                if (spo2 > 100.0f) spo2 = 100.0f;
                if (spo2 < 70.0f)  spo2 = 70.0f;
                s_metrics.spo2_pct = spo2;

                /* สะสมค่า SpO2 ลงหน้าต่าง 10 วินาที */
                s_spo2_window_sum += spo2;
                s_spo2_window_cnt++;
                if (s_spo2_window_tick == 0) {
                    s_spo2_window_tick = HAL_GetTick();
                    s_metrics.spo2_avg_10s = spo2; /* ค่าเริ่มต้นรอบแรก */
                }
            }

            /* ตรวจสอบการครบ 10 วินาทีของรอบเฉลี่ย SpO2 */
            if (s_spo2_window_tick > 0 && (HAL_GetTick() - s_spo2_window_tick >= 10000)) {
                if (s_spo2_window_cnt > 0) {
                    s_metrics.spo2_avg_10s = s_spo2_window_sum / (float)s_spo2_window_cnt;
                }
                s_spo2_window_sum = 0.0f;
                s_spo2_window_cnt = 0;
                s_spo2_window_tick = HAL_GetTick();
            }

            /* จัดการตัวจับเวลา 10 วินาทีแรกสำหรับ PTT และอายุหลอดเลือด */
            if (s_calib_start_tick == 0) {
                s_calib_start_tick = HAL_GetTick();
                s_ptt_calib_sum = 0.0f;
                s_ptt_calib_cnt = 0;
                s_metrics.baseline_locked = 0;
                s_metrics.calib_progress_pct = 0;
            }

            uint32_t calib_elapsed = HAL_GetTick() - s_calib_start_tick;
            if (!s_metrics.baseline_locked) {
                s_metrics.calib_progress_pct = (calib_elapsed >= PTT_CALIB_DURATION_MS) ? 100 : 
                                               (uint8_t)((calib_elapsed * 100UL) / PTT_CALIB_DURATION_MS);
            }

            /* ตั้ง Flag พร้อมส่งผลลัพธ์รอบใหม่ (SpO2, PI และ PTT) */
            s_new_result_flag = 1;

            /* 4.2 คำนวณ PTT เสมอเมื่อมี R-peak ค้างอยู่ */
            if (s_ecg_r_pending) {
                uint32_t dt_us = (uint32_t)(s_last_ppg_peak_time_us - s_last_ecg_r_time_us);

                /* ตรวจสอบความถูกต้องทางสรีรวิทยา (PTT ควรอยู่ระหว่าง 80 - 500 ms) */
                if (dt_us >= PTT_MIN_US && dt_us <= PTT_MAX_US) {
                    float cur_ptt_ms = (float)dt_us / 1000.0f;

                    /* ใน 10 วินาทีแรก: สะสมค่า PTT เพื่อหาค่าเฉลี่ยและยึดเป็น Baseline */
                    if (!s_metrics.baseline_locked) {
                        s_ptt_calib_sum += cur_ptt_ms;
                        s_ptt_calib_cnt++;
                        s_metrics.ptt_us = dt_us;
                        s_metrics.ptt_ms = s_ptt_calib_sum / (float)s_ptt_calib_cnt;

                        /* ถ้าครบ 20 วินาทีแล้ว และมีข้อมูล PTT อย่างน้อย 1 ครั้ง -> ล็อกค่าทันที! */
                        if (calib_elapsed >= PTT_CALIB_DURATION_MS && s_ptt_calib_cnt > 0) {
                            s_metrics.baseline_locked = 1;
                            s_metrics.ptt_baseline_ms = s_ptt_calib_sum / (float)s_ptt_calib_cnt;
                            s_metrics.ptt_ms = s_metrics.ptt_baseline_ms;
                            s_metrics.calib_progress_pct = 100;

                            /* ล็อกค่า PWV และ อายุหลอดเลือดจาก PTT 20 วินาทีแรก */
                            float locked_ptt_sec = s_metrics.ptt_baseline_ms / 1000.0f;
                            if (s_height_ready && s_arm_length_m > 0.3f && locked_ptt_sec > 0.05f) {
                                s_metrics.pwv_mps = s_arm_length_m / locked_ptt_sec;
                                s_metrics.vascular_age = pwv_calculate_vascular_age(s_metrics.pwv_mps);
                            }
                        }
                    } else {
                        /* หลัง 10 วินาที: ยึดค่า PTT และอายุหลอดเลือดจาก 10 วินาทีแรกไว้คงที่ */
                        s_metrics.ptt_ms = s_metrics.ptt_baseline_ms;
                    }

                    s_metrics.ptt_count++;
                }
                s_ecg_r_pending = 0; /* เคลียร์สถานะ R-peak หลังจับคู่แล้ว */
            }
        } else {
            /* กรณี PI < 0.4% */
            s_metrics.finger_detected = 0;
            s_ecg_r_pending = 0;
        }

        /* รีเซ็ตค่า Min/Max สำหรับรอบถัดไป */
        s_ir_max = -1e6f;
        s_ir_min = 1e6f;
        s_red_max = -1e6f;
        s_red_min = 1e6f;
    }

    s_slope = curr_slope;
    s_last_ac_ir = s_ac_ir_filt;
}

void pwv_metrics_get(PWV_Metrics_t *out)
{
    if (out != NULL) {
        *out = s_metrics;
        s_new_result_flag = 0;
    }
}

uint8_t pwv_metrics_has_new_result(void)
{
    return s_new_result_flag;
}

uint16_t pwv_metrics_get_ppg_plot_val(void)
{
    return pwv_metrics_get_ir_ac_plot();
}

uint16_t pwv_metrics_get_ir_ac_plot(void)
{
    if (s_dc_ir < MIN_FINGER_DC) return 2048;
    int32_t val = 2048 + (int32_t)(s_ac_ir_filt * 1.0f);
    if (val < 0) val = 0;
    if (val > 4095) val = 4095;
    return (uint16_t)val;
}

uint16_t pwv_metrics_get_red_ac_plot(void)
{
    if (s_dc_ir < MIN_FINGER_DC) return 2048;
    int32_t val = 2048 + (int32_t)(s_ac_red_filt * 1.0f);
    if (val < 0) val = 0;
    if (val > 4095) val = 4095;
    return (uint16_t)val;
}
