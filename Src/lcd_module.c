#include "lcd_module.h"

#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"
#include "stdio.h"
#include "stdlib.h"

//===== Screen =====//
#define SCREEN_W			320
#define SCREEN_H			240

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
#define SUM_HINT_Y			208         /* "Place finger firmly" row, under the last value row */


//===== Height screen layout =====//
#define HGT_MIN				100         /* [cm] */
#define HGT_MAX				220         /* [cm] */
#define HGT_DEFAULT			175         /* [cm] same value the sensor code used before the page existed */
#define HGT_VALUE_X			70
#define HGT_VALUE_Y			60
#define HGT_VALUE_SIZE		5
#define HGT_BTN_Y			130
#define HGT_BTN_W			64
#define HGT_BTN_LABEL_DX	0           /* (HGT_BTN_W - 2 chars * 12 px) / 2 */
#define HGT_BTN_H			40
#define HGT_OK_X			90
#define HGT_OK_Y			190
#define HGT_OK_W			140
#define HGT_OK_H			40
#define COLOR_BTN			0x2945      /* dark grey */

#ifndef TOUCH_X_OFFSET
#define TOUCH_X_OFFSET		0           /* [px] added to x after mapping; 0 = off */
#endif

#define DEBUG_TOUCH			1           /* 1 = plot a white dot at every touch position */

/* touch lib output (portrait 240x320) -> landscape 320x240. Toggle until dot = finger */
#define TOUCH_SWAP_XY		0           /* 1: x = raw[1], y = raw[0]; 0: x = raw[0], y = raw[1] (rescaled) */
#define TOUCH_INV_X			1           /* x = 319 - x */
#define TOUCH_INV_Y			1           /* y = 239 - y */


//===== HRV status thresholds [ms] =====//
#define HRV_RELAXED_MIN		40.0f
#define HRV_STRESSED_MAX	25.0f


//===== State =====//
static t_display_screen		s_screen   = SCREEN_HEIGHT;   /* height first */
static uint8_t				s_redraw   = 1;      /* need full clear */
static uint16_t				s_height   = HGT_DEFAULT;   /* [cm] */
static uint16_t				s_ecg_x    = 0;
static int16_t				s_prev_y   = ECG_BASE_Y;


//===== ECG scaling state =====//
static t_display_ecg_scale	s_scale_mode = ECG_SCALE_FIXED;
static uint16_t				s_interval   = ECG_DEF_INTERVAL;
static uint16_t				s_win_count  = 0;
static uint16_t				s_win_min    = 4095, s_win_max = 0;    /* running interval */
static uint16_t				s_act_min    = 0,    s_act_max = 4095; /* range used to draw */
static uint8_t				s_act_valid  = 0;


//===== Height buttons =====//
static const struct { uint16_t x; int8_t step; const char *label; } s_hbtn[4] = {
	{  17, -5, "-5" }, {  91, -1, "-1" }, { 165, +1, "+1" }, { 239, +5, "+5" }
};


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

static void draw_main(const t_collected_data *d)
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

/* one summary row, padded to 20 chars so a shorter text fully overwrites the previous one */
static void sum_row(uint16_t y, uint16_t col, const char *txt)
{
	char	buf[24];

	snprintf(buf, sizeof(buf), "%-20s", txt);
	ILI9341_Draw_Text(buf, SUM_X, y, col, SUM_TEXT_SIZE, COLOR_BG);
}

/* Every row is always drawn: a value that is not available (0 / no finger) is shown as "---"
 * instead of hiding the whole page, so a stale or missing value can never be mistaken for a reading.
 *   ECG based     : HR, HRV
 *   MAX30102 based: SpO2, PI (need a valid finger contact)
 *   always shown   : vascular age (0 until the first baseline lock, then held by main.c)
 *   no source yet : BP                                                                             */
static void draw_summary(const t_collected_data *d)
{
	char		buf[24];
	uint16_t	y = SUM_FIRST_Y;
	const char	*st;
	uint16_t	col;
	int			pi10;
	uint8_t		has_ecg = (d->bpm > 0);
	uint8_t		has_ppg = (d->is_finger != 0);

	if (s_redraw)
		ILI9341_Draw_Text("SUMMARY (tap = back)", SUM_X, SUM_TITLE_Y, COLOR_LABEL, 2, COLOR_BG);

	/* HR (ECG) */
	if (has_ecg)
		snprintf(buf, sizeof(buf), "HR   %3u bpm", (unsigned)d->bpm);
	else
		snprintf(buf, sizeof(buf), "HR   --- bpm");
	sum_row(y, COLOR_BPM, buf);
	y += SUM_STEP_Y;

	/* SpO2 (MAX30102) */
	if (has_ppg && d->spo2 > 0)
		snprintf(buf, sizeof(buf), "SpO2 %3u %%", (unsigned)d->spo2);
	else
		snprintf(buf, sizeof(buf), "SpO2 --- %%");
	sum_row(y, COLOR_SPO2, buf);
	y += SUM_STEP_Y;

	/* HRV + status (ECG, int math, no %f needed). 0 = not enough R-R intervals yet. */
	if (d->hrv_rmssd > 0.0f)
	{
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
	}
	else
	{
		st  = "        ";
		col = COLOR_BG;
		snprintf(buf, sizeof(buf), "HRV  ---    ");
	}
	ILI9341_Draw_Text(buf, SUM_X, y, COLOR_VALUE, SUM_TEXT_SIZE, COLOR_BG);
	ILI9341_Draw_Text(st, SUM_X + 12 * 12, y, col, SUM_TEXT_SIZE, COLOR_BG);
	y += SUM_STEP_Y;

	/* PI (MAX30102) */
	if (has_ppg)
	{
		pi10 = (int)(d->pi * 10.0f + 0.5f);
		snprintf(buf, sizeof(buf), "PI   %d.%d %%", pi10 / 10, pi10 % 10);
	}
	else
		snprintf(buf, sizeof(buf), "PI   --- %%");
	sum_row(y, COLOR_VALUE, buf);
	y += SUM_STEP_Y;

	/* Vascular age: always shown. main.c holds the last locked value (0 until the first lock),
	 * so it stays on screen after the finger is lifted. */
	snprintf(buf, sizeof(buf), "Vascular age %2d", (int)(d->vascular_age + 0.5f));
	sum_row(y, COLOR_VALUE, buf);

	/* hint row: text while there is no valid finger contact, blank otherwise */
	if (!has_ppg)
		sum_row(SUM_HINT_Y, COLOR_WARN, "Place finger firmly");
	else
		sum_row(SUM_HINT_Y, COLOR_BG, "");
}


static void draw_height_value(void)
{
	char	buf[12];

	snprintf(buf, sizeof(buf), "%3u cm", (unsigned)s_height);
	ILI9341_Draw_Text(buf, HGT_VALUE_X, HGT_VALUE_Y, COLOR_VALUE, HGT_VALUE_SIZE, COLOR_BG);
}

static void draw_height(void)
{
	if (s_redraw)
	{
		ILI9341_Draw_Text("ENTER YOUR HEIGHT", 40, 15, COLOR_LABEL, 2, COLOR_BG);
		for (uint8_t i = 0; i < 4; i++)
		{
			ILI9341_Draw_Rectangle(s_hbtn[i].x, HGT_BTN_Y, HGT_BTN_W, HGT_BTN_H, COLOR_BTN);
			ILI9341_Draw_Text(s_hbtn[i].label, s_hbtn[i].x + HGT_BTN_LABEL_DX, HGT_BTN_Y + 13,
							  COLOR_LABEL, 2, COLOR_BTN);
		}
		ILI9341_Draw_Rectangle(HGT_OK_X, HGT_OK_Y, HGT_OK_W, HGT_OK_H, COLOR_OK);
		ILI9341_Draw_Text("OK", HGT_OK_X + 58, HGT_OK_Y + 13, COLOR_BG, 2, COLOR_OK);
	}
	draw_height_value();
}


//===== Public Function =====//
void display_init(void)
{
	ILI9341_Set_Rotation(SCREEN_HORIZONTAL_2);
	ILI9341_Fill_Screen(COLOR_BG);
	s_screen = SCREEN_HEIGHT;
	s_redraw = 1;
}

void display_next_screen(void)
{
	if (s_screen == SCREEN_HEIGHT)      /* locked until height confirmed */
		return ;
	s_screen = (s_screen == SCREEN_MAIN) ? SCREEN_SUMMARY : SCREEN_MAIN;
	s_redraw = 1;
}

t_display_screen display_get_screen(void)
{
	return (s_screen);
}

uint16_t display_get_height(void)
{
	return (s_height);
}

void display_touch(uint16_t raw_x, uint16_t raw_y)
{
	uint16_t	x;
	uint16_t	y;

#if TOUCH_SWAP_XY
	x = raw_y;
	y = raw_x;
#else
	/* lib range is 240 x 320 (portrait): rescale to 320 x 240 when not swapping */
	x = (uint16_t)(((uint32_t)raw_x * SCREEN_W) / SCREEN_H);
	y = (uint16_t)(((uint32_t)raw_y * SCREEN_H) / SCREEN_W);
#endif
	if (x > SCREEN_W - 1)
		x = SCREEN_W - 1;
	if (y > SCREEN_H - 1)
		y = SCREEN_H - 1;
#if TOUCH_INV_X
	x = (SCREEN_W - 1) - x;
#endif
#if TOUCH_INV_Y
	y = (SCREEN_H - 1) - y;
#endif
#if TOUCH_X_OFFSET
	{
		int16_t xo = (int16_t)x + TOUCH_X_OFFSET;

		x = (xo < 0) ? 0 : (xo > SCREEN_W - 1) ? SCREEN_W - 1 : (uint16_t)xo;
	}
#endif
#if DEBUG_TOUCH
	ILI9341_Draw_Pixel(x, y, WHITE);
#endif
	if (s_screen != SCREEN_HEIGHT || s_redraw)
		return ;
	if (y >= HGT_BTN_Y && y < HGT_BTN_Y + HGT_BTN_H)
	{
		for (uint8_t i = 0; i < 4; i++)
		{
			if (x >= s_hbtn[i].x && x < s_hbtn[i].x + HGT_BTN_W)
			{
				int16_t h = (int16_t)s_height + s_hbtn[i].step;
				s_height = (h < HGT_MIN) ? HGT_MIN : (h > HGT_MAX) ? HGT_MAX : (uint16_t)h;
				draw_height_value();
				return ;
			}
		}
	}
	else if (y >= HGT_OK_Y && y < HGT_OK_Y + HGT_OK_H && x >= HGT_OK_X && x < HGT_OK_X + HGT_OK_W)
	{
		s_screen = SCREEN_MAIN;
		s_redraw = 1;
	}
}

void display_update(const t_collected_data *d)
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
	if (s_screen == SCREEN_HEIGHT)
		draw_height();
	else if (s_screen == SCREEN_MAIN)
		draw_main(d);
	else
		draw_summary(d);
	s_redraw = 0;
}

void display_ecg_step(void)
{
	if (s_screen != SCREEN_MAIN || s_redraw)
		return ;
	/* random row inside the band, same 4 px padding that display_ecg_push() uses */
	ecg_draw_column((ECG_TOP + 4) + (rand() % (ECG_BAND_H - 8 + 1)));
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
