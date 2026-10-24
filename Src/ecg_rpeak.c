/**
 * @file    ecg_rpeak.c
 * @brief   R-peak detector implementation (slope-energy / Pan-Tompkins-inspired)
 */

#include "ecg_rpeak.h"
#include <string.h>   /* memset */

/* ════════════════════════════════════════════════════════════════
   ตัวแปรผลลัพธ์ (ประกาศ extern ใน .h)
   ════════════════════════════════════════════════════════════════ */
volatile uint16_t rr_ms[ECG_RR_N];
volatile uint16_t rr_last_ms  = 0;
volatile uint16_t heart_rate_bpm = 0;
volatile uint32_t r_peak_time_us = 0;
volatile uint32_t r_peak_count   = 0;

/* ════════════════════════════════════════════════════════════════
   ตัวแปรสถานะภายใน (static — มองเห็นเฉพาะในไฟล์นี้)
   ════════════════════════════════════════════════════════════════ */
static uint32_t sample_n;
static uint32_t t0_us;      /* ค่า TIM2 (us) ตอนเริ่มนับตัวอย่าง */
static uint32_t abs_n;      /* นับตัวอย่างต่อเนื่อง ไม่ถูกรีเซ็ตตอนเรียนรู้ใหม่ ใช้ทำ timestamp */
static uint16_t y_prev;

static uint32_t en_buf[ECG_INTEG_N];
static uint32_t en_sum;
static uint8_t  en_k;

static uint16_t y_hist[ECG_HIST_N];
static uint8_t  y_k;

static uint32_t learn_max;
static uint32_t m_peak_est;

static uint8_t  in_region;
static uint16_t region_len;
static uint32_t region_max_m;

static uint8_t  have_last;
static uint32_t last_r_n;
static uint32_t last_ref_n;

static uint8_t  rr_k;
static uint8_t  rr_cnt;

/* ════════════════════════════════════════════════════════════════
   rpeak_init()
   ════════════════════════════════════════════════════════════════ */
void rpeak_init(void)
{
    /* ล้างตัวแปรสถานะทั้งหมด */
    sample_n    = 0;
    abs_n       = 0;
    t0_us       = 0;
    r_peak_time_us = 0;
    r_peak_count   = 0;
    y_prev      = 0;
    en_sum      = 0;
    en_k        = 0;
    y_k         = 0;
    learn_max   = 0;
    m_peak_est  = 0;
    in_region   = 0;
    region_len  = 0;
    region_max_m = 0;
    have_last   = 0;
    last_r_n    = 0;
    last_ref_n  = 0;
    rr_k        = 0;
    rr_cnt      = 0;

    memset((void *)rr_ms, 0, sizeof(rr_ms));
    memset(en_buf,  0, sizeof(en_buf));
    memset(y_hist,  0, sizeof(y_hist));

    rr_last_ms      = 0;
    heart_rate_bpm  = 0;
}

/* ════════════════════════════════════════════════════════════════
   rpeak_set_t0()
   ════════════════════════════════════════════════════════════════ */
void rpeak_set_t0(uint32_t t0)
{
    t0_us = t0;
}

/* ════════════════════════════════════════════════════════════════
   rpeak_update()
   ════════════════════════════════════════════════════════════════ */
void rpeak_update(uint16_t y)
{
    sample_n++;
    abs_n++;

    /* ── เก็บประวัติสัญญาณ (ใหม่สุดอยู่ที่ index y_k - 1) ── */
    y_hist[y_k] = y;
    y_k = (y_k + 1) & (ECG_HIST_N - 1);

    /* ── ข้ามช่วง warm-up ── */
    if (sample_n <= ECG_WARMUP)
    {
        y_prev = y;
        return;
    }

    /* ── 1) ความชัน → ยกกำลังสอง ── */
    int32_t  d = (int32_t)y - (int32_t)y_prev;
    y_prev = y;
    uint32_t e = (uint32_t)(d * d);

    /* ── 2) Moving-average integrator (running sum) ── */
    en_sum     += e - en_buf[en_k];
    en_buf[en_k] = e;
    en_k = (en_k + 1) & (ECG_INTEG_N - 1);
    uint32_t m = en_sum / ECG_INTEG_N;

    /* ── 3) ช่วงเรียนรู้ 2 วินาทีแรก ── */
    if (sample_n <= ECG_WARMUP + ECG_LEARN)
    {
        if (m > learn_max) learn_max = m;

        if (sample_n == ECG_WARMUP + ECG_LEARN)
        {
            if (learn_max < 2UL * ECG_MIN_ENERGY)
            {
                /* สัญญาณอ่อนเกินไป → เริ่มใหม่ */
                sample_n  = 0;
                learn_max = 0;
            }
            else
            {
                m_peak_est = learn_max;
                last_ref_n = sample_n;
            }
        }
        return;
    }

    /* ── 4) Timeout: ไม่เจอ R เกิน 3 วิ → ลดเกณฑ์ + ล้าง HR ── */
    if ((sample_n - last_ref_n) > ECG_TIMEOUT)
    {
        m_peak_est >>= 1;
        last_ref_n   = sample_n;
        have_last    = 0;
        rr_cnt       = 0;
        rr_k         = 0;
        heart_rate_bpm = 0;
    }

    /* ── 5) เกณฑ์ตรวจจับ = 40% ของ QRS level ── */
    uint32_t thr = (m_peak_est * 2UL) / 5UL;
    if (thr < ECG_MIN_ENERGY) thr = ECG_MIN_ENERGY;

    /* ── 6) ตรวจจับ QRS region ── */
    if (m > thr)
    {
        if (!in_region)
        {
            in_region    = 1;
            region_len   = 0;
            region_max_m = 0;
        }
        region_len++;
        if (m > region_max_m) region_max_m = m;
    }
    else if (in_region)
    {
        /* QRS จบ → หาตำแหน่ง R-peak จากประวัติสัญญาณ */
        in_region = 0;

        uint16_t span = region_len + 24U;   /* เผื่อย้อน 60 ms ที่ 400 Hz */
        if (span > ECG_HIST_N) span = (uint16_t)ECG_HIST_N;

        uint16_t best     = 0;
        uint16_t best_age = 0;
        for (uint16_t a = 0; a < span; a++)
        {
            uint16_t v = y_hist[(y_k - 1U - a) & (ECG_HIST_N - 1)];
            if (v > best) { best = v; best_age = a; }
        }
        uint32_t r_n   = sample_n - best_age;  /* sample index ของ R-peak */
        uint32_t r_abs = abs_n - best_age;     /* เหมือน r_n แต่ไม่ถูกรีเซ็ต */

        /* ── Refractory check ── */
        if (have_last && (r_n - last_r_n) < ECG_REFRACT)
        {
            /* ใกล้ R ก่อนหน้าเกินไป → ถือเป็น noise / ตรวจซ้ำ */
            return;
        }

        /* ── Timestamp ของ R-peak (us) = เลขตัวอย่าง x คาบสุ่ม ── */
        r_peak_time_us = t0_us + (uint32_t)(((uint64_t)r_abs * 1000000ULL) / ECG_FS_HZ);   /* บวกแล้ววนรอบ 2^32 เอง */
        r_peak_count++;

        /* ── คำนวณ R-R และ HR ── */
        if (have_last)
        {
            uint32_t rr = r_n - last_r_n;
            if (rr >= ECG_RR_MIN && rr <= ECG_RR_MAX)
            {
                uint16_t rr_val = (uint16_t)(rr * 1000UL / ECG_FS_HZ);   /* samples → ms */

                rr_ms[rr_k] = rr_val;
                rr_k = (rr_k + 1) % ECG_RR_N;
                if (rr_cnt < ECG_RR_N) rr_cnt++;
                rr_last_ms = rr_val;

                /* HR = 60000 / mean(R-R) */
                uint32_t sum = 0;
                for (uint8_t i = 0; i < rr_cnt; i++) sum += rr_ms[i];
                heart_rate_bpm = (uint16_t)(60000UL * rr_cnt / sum);
            }
        }

        last_r_n   = r_n;
        have_last  = 1;
        last_ref_n = sample_n;

        /* ── ปรับระดับ QRS (adaptive threshold) ── */
        m_peak_est = (3UL * m_peak_est + region_max_m) / 4UL;
    }
}
