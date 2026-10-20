#ifndef LCD_MODULE_H
# define LCD_MODULE_H

#include <stdio.h>
#include <stdint.h>

typedef struct s_display_data
{
    uint16_t	bpm;          /* HR = 60 / RR                         [bpm]   */
    uint8_t 	spo2;         /* SpO2 = 104 - 17*R                    [%]     */
    uint16_t	bp_sys;       /* BP_sys = a_sys*PTT + b_sys           [mmHg]  */
    uint16_t	bp_dia;       /* BP_dia = a_dia*PTT + b_dia           [mmHg]  */
    float		hrv_rmssd;    /* RMSSD                                [ms]    */
    float		pi;           /* PI = ACir/DCir*100                   [%]     */
    float		vascular_age; /* 20 + (PWV-5.5)/0.1                   [years] */
    uint8_t 	is_finger;    /* 0 when PI < 0.4 % (finger not placed firmly) */

}	t_display_data;

typedef enum e_display_screen
{
    SCREEN_MAIN = 0,       /* BPM + SpO2 + ECG graph */
    SCREEN_SUMMARY         /* all final values       */
}	t_display_screen;

typedef enum
{
    ECG_SCALE_FIXED = 0,   /* y range = full ADC 0..4095               */
    ECG_SCALE_AUTO         /* y range = min..max of last interval      */
} t_display_ecg_scale;


//====================//
// Prototype Function //
//====================//
void				display_init(void);
void				display_next_screen(void);
t_display_screen	display_get_screen(void);
void				display_update(const t_display_data *d);
void				display_ecg_step(void);
void				display_ecg_push(uint16_t adc12);
void				display_ecg_set_scale(t_display_ecg_scale mode, uint16_t interval_sample);

#endif
