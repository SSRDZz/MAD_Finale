#include "lcd_module.h"

#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"

//===== Screen =====//
#define SCREEN_W			320

//===== Color =====//
#define COLOR_BG			BLACK
#define COLOR_BPM			RED
#define COLOR_SPO2			0x1C9F      /* bright blue */
#define COLOR_ECG			YELLOW
#define COLOR_LABEL			WHITE
#define COLOR_VALUE			0x07FF      /* cyan */
#define COLOR_OK			GREEN
#define COLOR_WARN			0xFD20      /* orange */
#define COLOR_BAD			RED


//===== Main screen layout =====//
#define MAIN_X				20
#define MAIN_BPM_Y			15
#define MAIN_SPO2_Y			55
#define MAIN_TEXT_SIZE		3
#define MAIN_MSG_Y			100         /* "Place finger firmly" line */

#define ECG_BASE_Y			165         /* baseline */
#define ECG_TOP				115         /* erase band top */
#define ECG_BAND_H			120         /* erase band height */
#define ECG_THICK			2
#define ECG_ERASE_AHEAD		8
#define ECG_PERIOD			96          /* demo beat width in px */
#define ECG_MIN_SPAN		200         /* AUTO: min ADC span, stop noise being blown up */
#define ECG_MARGIN_PCT		10          /* AUTO: empty margin top/bottom of range [%] */
#define ECG_DEF_INTERVAL	500         /* AUTO: default samples per interval */


//===== Summary screen layout =====//
#define SUM_X				10
#define SUM_TITLE_Y			6
#define SUM_FIRST_Y			38
#define SUM_STEP_Y			28
#define SUM_TEXT_SIZE		2


//===== HRV status thresholds [ms] =====//
#define HRV_RELAXED_MIN		40.0f
#define HRV_STRESSED_MAX	25.0f


//===== State =====//
static t_display_screen		s_screen   = SCREEN_MAIN;
static uint8_t				s_redraw   = 1;      /* need full clear */
static uint16_t				s_ecg_x    = 0;
static int16_t				s_prev_y   = ECG_BASE_Y;


//===== ECG scaling state =====//
static t_display_ecg_scale	s_scale_mode = ECG_SCALE_FIXED;
static uint16_t				s_interval   = ECG_DEF_INTERVAL;
static uint16_t				s_win_count  = 0;
static uint16_t				s_win_min    = 4095, s_win_max = 0;    /* running interval */
static uint16_t				s_act_min    = 0,    s_act_max = 4095; /* range used to draw */
static uint8_t				s_act_valid  = 0;


//===== Other =====//
// One demo heartbeat: {x in period, y offset from baseline (up = negative)}
static const int8_t s_beat[][2] = {
    {  0,   0 }, { 16,   0 }, { 24, -100 }, { 36,  36 },
    { 42, -10 }, { 46,   6 }, { 50,   0 }, { 95,   0 }
};

#define BEAT_PTS	((uint8_t)(sizeof(s_beat) / sizeof(s_beat[0])))


//===== Static Function =====//
static void ecg_scale_reset(void)
{
    s_win_count = 0;
    s_win_min   = 4095;
    s_win_max   = 0;
    s_act_valid = 0;
}

static int16_t ecg_offset(uint16_t phase)
{
	for (uint8_t i = 0; (uint8_t)(i + 1) < BEAT_PTS; i++)
	{
		int16_t x0 = s_beat[i][0],     y0 = s_beat[i][1];
		int16_t x1 = s_beat[i + 1][0], y1 = s_beat[i + 1][1];
		if ((int16_t)phase <= x1)
		{
			return y0 + (int16_t)((y1 - y0) * ((int16_t)phase - x0)) / (x1 - x0);
		}
	}
    return 0;
}

// draw one ECG column at current x, then advance
static void ecg_draw_column(int16_t y)
{
	uint16_t	ew = ECG_ERASE_AHEAD;
	int16_t		y_min;
	int16_t		y_max;

	if (s_ecg_x + ew > SCREEN_W)
		ew = SCREEN_W - s_ecg_x;
	ILI9341_Draw_Rectangle(s_ecg_x, ECG_TOP, ew, ECG_BAND_H, COLOR_BG);

	y_min = (y < s_prev_y) ? y : s_prev_y;
	y_max = (y < s_prev_y) ? s_prev_y : y;
	for (uint8_t t = 0; t < ECG_THICK; t++)
	{
		if (s_ecg_x + t < SCREEN_W)
			ILI9341_Draw_Vertical_Line(s_ecg_x + t, y_min, (y_max - y_min) + ECG_THICK, COLOR_ECG);
	}

	s_prev_y = y;
	s_ecg_x++;
	if (s_ecg_x >= SCREEN_W)
	{
		s_ecg_x  = 0;
		s_prev_y = ECG_BASE_Y;
	}
}

static void draw_main(const t_display_data *d)
{
	char	buf[24];

	if (d->is_finger)
	{
		snprintf(buf, sizeof(buf), "bpm %3u", (unsigned)d->bpm);
		ILI9341_Draw_Text(buf, MAIN_X, MAIN_BPM_Y, COLOR_BPM, MAIN_TEXT_SIZE, COLOR_BG);
		snprintf(buf, sizeof(buf), "SpO2 %3u %%", (unsigned)d->spo2);
		ILI9341_Draw_Text(buf, MAIN_X, MAIN_SPO2_Y, COLOR_SPO2, MAIN_TEXT_SIZE, COLOR_BG);
		ILI9341_Draw_Text("                    ", MAIN_X, MAIN_MSG_Y, COLOR_BG, 2, COLOR_BG);
	}
	else
	{
		ILI9341_Draw_Text("bpm ---", MAIN_X, MAIN_BPM_Y, COLOR_BPM, MAIN_TEXT_SIZE, COLOR_BG);
		ILI9341_Draw_Text("SpO2 --- %", MAIN_X, MAIN_SPO2_Y, COLOR_SPO2, MAIN_TEXT_SIZE, COLOR_BG);
		ILI9341_Draw_Text("Place finger firmly", MAIN_X, MAIN_MSG_Y, COLOR_WARN, 2, COLOR_BG);
	}
}

static void draw_summary(const t_display_data *d)
{
	char		buf[32];
	uint16_t	y = SUM_FIRST_Y;
	const char	*st;
	uint16_t	col;
	int			pi10;

	if (s_redraw)
		ILI9341_Draw_Text("SUMMARY (tap = back)", SUM_X, SUM_TITLE_Y, COLOR_LABEL, 2, COLOR_BG);

	if (!d->is_finger)
	{
		ILI9341_Draw_Text("Place finger firmly", SUM_X, y, COLOR_WARN, SUM_TEXT_SIZE, COLOR_BG);
		return ;
	}

	snprintf(buf, sizeof(buf), "BP   %3u/%3u mmHg ", (unsigned)d->bp_sys, (unsigned)d->bp_dia);
	ILI9341_Draw_Text(buf, SUM_X, y, COLOR_VALUE, SUM_TEXT_SIZE, COLOR_BG);
	y += SUM_STEP_Y;

	snprintf(buf, sizeof(buf), "HR   %3u bpm     ", (unsigned)d->bpm);
	ILI9341_Draw_Text(buf, SUM_X, y, COLOR_BPM, SUM_TEXT_SIZE, COLOR_BG);
	y += SUM_STEP_Y;

	snprintf(buf, sizeof(buf), "SpO2 %3u %%       ", (unsigned)d->spo2);
	ILI9341_Draw_Text(buf, SUM_X, y, COLOR_SPO2, SUM_TEXT_SIZE, COLOR_BG);
	y += SUM_STEP_Y;

	/* HRV + status (int math, no %f needed) */
	if (d->hrv_rmssd >= HRV_RELAXED_MIN)
	{
		st  = "Relaxed ";
		col = COLOR_OK;
	}
	else if (d->hrv_rmssd <= HRV_STRESSED_MAX)
	{
		st  = "Stressed";
		col = COLOR_BAD;
	}
	else
	{
		st  = "Normal  ";
		col = COLOR_WARN;
	}
	snprintf(buf, sizeof(buf), "HRV  %3d ms ", (int)(d->hrv_rmssd + 0.5f));
	ILI9341_Draw_Text(buf, SUM_X, y, COLOR_VALUE, SUM_TEXT_SIZE, COLOR_BG);
	ILI9341_Draw_Text(st, SUM_X + 12 * 12, y, col, SUM_TEXT_SIZE, COLOR_BG);
	y += SUM_STEP_Y;

	pi10 = (int)(d->pi * 10.0f + 0.5f);
	snprintf(buf, sizeof(buf), "PI   %d.%d %%      ", pi10 / 10, pi10 % 10);
	ILI9341_Draw_Text(buf, SUM_X, y, COLOR_VALUE, SUM_TEXT_SIZE, COLOR_BG);
	y += SUM_STEP_Y;

	snprintf(buf, sizeof(buf), "Vascular age %2d  ", (int)(d->vascular_age + 0.5f));
	ILI9341_Draw_Text(buf, SUM_X, y, COLOR_VALUE, SUM_TEXT_SIZE, COLOR_BG);
}


//===== Public Function =====//
void display_init(void)
{
	ILI9341_Set_Rotation(SCREEN_HORIZONTAL_2);
	ILI9341_Fill_Screen(COLOR_BG);
	s_screen = SCREEN_MAIN;
	s_redraw = 1;
}

void display_next_screen(void)
{
	s_screen = (s_screen == SCREEN_MAIN) ? SCREEN_SUMMARY : SCREEN_MAIN;
	s_redraw = 1;
}

t_display_screen display_get_screen(void)
{
	return (s_screen);
}

void display_update(const t_display_data *d)
{
	if (d == 0)
		return ;
	if (s_redraw)
	{
		ILI9341_Fill_Screen(COLOR_BG);
		s_ecg_x  = 0;
		s_prev_y = ECG_BASE_Y;
		ecg_scale_reset();
	}
	if (s_screen == SCREEN_MAIN)
		draw_main(d);
	else
		draw_summary(d);
	s_redraw = 0;
}

void display_ecg_step(void)
{
	if (s_screen != SCREEN_MAIN || s_redraw)
		return ;
	ecg_draw_column(ECG_BASE_Y + ecg_offset(s_ecg_x % ECG_PERIOD));
}

void display_ecg_set_scale(t_display_ecg_scale mode, uint16_t interval_sample)
{
	s_scale_mode = mode;
	s_interval   = (interval_sample == 0) ? ECG_DEF_INTERVAL : interval_sample;
	ecg_scale_reset();
}

void display_ecg_push(uint16_t adc12)
{
	uint16_t	lo = 0;
	uint16_t	hi = 4095;
	int32_t		v;
	int32_t		span;
	int16_t		y;

	if (s_screen != SCREEN_MAIN || s_redraw)
		return ;
	if (adc12 > 4095)
		adc12 = 4095;

	if (s_scale_mode == ECG_SCALE_AUTO)
	{
		/* track min/max of this interval */
		if (adc12 < s_win_min)
			s_win_min = adc12;
		if (adc12 > s_win_max)
			s_win_max = adc12;
		if (++s_win_count >= s_interval)
		{
			s_act_min   = s_win_min;
			s_act_max   = s_win_max;
			s_act_valid = 1;
			s_win_count = 0;
			s_win_min   = 4095;
			s_win_max   = 0;
		}

		/* range to draw: finished interval, else running one (first interval) */
		lo = s_act_valid ? s_act_min : s_win_min;
		hi = s_act_valid ? s_act_max : s_win_max;

		/* min span: keep centered, avoid zooming into noise */
		if ((hi - lo) < ECG_MIN_SPAN)
		{
			int32_t mid = ((int32_t)lo + hi) / 2;
			int32_t l   = mid - ECG_MIN_SPAN / 2;
			int32_t h   = mid + ECG_MIN_SPAN / 2;
			lo = (l < 0) ? 0 : (uint16_t)l;
			hi = (h > 4095) ? 4095 : (uint16_t)h;
		}

		/* margin so peak does not touch band edge */
		{
			uint16_t m = (uint16_t)(((uint32_t)(hi - lo) * ECG_MARGIN_PCT) / 100);
			lo = (lo > m) ? (lo - m) : 0;
			hi = (hi + m > 4095) ? 4095 : (hi + m);
		}
	}

	/* clamp (sample can fall outside previous interval's range) */
	v = adc12;
	if (v < lo)
		v = lo;
	if (v > hi)
		v = hi;

	/* map lo..hi -> band bottom..top, higher ADC = higher on screen */
	span = (hi > lo) ? (hi - lo) : 1;
	y = (ECG_TOP + ECG_BAND_H - 4) - (int16_t)(((v - lo) * (ECG_BAND_H - 8)) / span);
	ecg_draw_column(y);
}
