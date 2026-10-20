#include "lcd_module.h"

#include "main.h"      // HAL, LD2_Pin / LD3_Pin
#include "rng.h"       // hrng
#include "tim.h"       // htim1
#include <stdio.h>

#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"
#include "ILI9341_Touchscreen.h"

#include "snow_tiger.h"   // defines the image array: include it in this file only

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/* 16 random bits from the hardware RNG */
static uint16_t rand16(void)
{
    return (uint16_t)HAL_RNG_GetRandomNumber(&hrng);
}

/* Random position (0-511), size (0-31) and colour, same ranges as the original demo */
static void rand_shape(uint16_t *x, uint16_t *y, uint16_t *size, uint16_t *colour)
{
    *x      = rand16() & 0x01FF;
    *y      = rand16() & 0x01FF;
    *size   = rand16() & 0x001F;
    *colour = rand16();
}

/* Title screen: white background, one or two lines of small text, 2 s pause, clear */
static void show_title(const char *line1, const char *line2)
{
    ILI9341_Fill_Screen(WHITE);
    ILI9341_Set_Rotation(SCREEN_HORIZONTAL_2);
    ILI9341_Draw_Text(line1, 10, 10, BLACK, 1, WHITE);
    if (line2 != NULL)
    {
        ILI9341_Draw_Text(line2, 10, 20, BLACK, 1, WHITE);
    }
    HAL_Delay(2000);
    ILI9341_Fill_Screen(WHITE);
}

/* ------------------------------------------------------------------ */
/* Demos (one per section of the original main.c)                       */
/* ------------------------------------------------------------------ */

/* Needs TIM1 ticking at 20 kHz. Float printf must be enabled in the toolchain. */
static void demo_performance(void)
{
    char buf[40];
    uint32_t timer_counter = 0;

    show_title("FPS TEST, 40 loop 2 screens", NULL);

    for (uint32_t j = 0; j < 2; j++)
    {
        HAL_TIM_Base_Start(&htim1);
        for (uint16_t i = 0; i < 10; i++)
        {
            ILI9341_Fill_Screen(WHITE);
            ILI9341_Fill_Screen(BLACK);
        }
        HAL_TIM_Base_Stop(&htim1);   // timer runs at 20,000 ticks per second
        timer_counter += __HAL_TIM_GET_COUNTER(&htim1);
        __HAL_TIM_SET_COUNTER(&htim1, 0);
    }
    timer_counter /= 2;

    ILI9341_Fill_Screen(WHITE);
    ILI9341_Set_Rotation(SCREEN_HORIZONTAL_2);

    sprintf(buf, "Timer counter value: %lu", (unsigned long)(timer_counter * 2));
    ILI9341_Draw_Text(buf, 10, 10, BLACK, 1, WHITE);

    double seconds_passed = 2 * ((float)timer_counter / 20000);
    sprintf(buf, "Time: %.3f Sec", seconds_passed);
    ILI9341_Draw_Text(buf, 10, 30, BLACK, 2, WHITE);

    double fps = 20 / (((float)timer_counter) / 20000);   // frames per second
    sprintf(buf, "FPS:  %.2f", fps);
    ILI9341_Draw_Text(buf, 10, 50, BLACK, 2, WHITE);

    double mb_per_s = fps * 240 * 320 * 2 / 1000000;
    sprintf(buf, "MB/S: %.2f", mb_per_s);
    ILI9341_Draw_Text(buf, 10, 70, BLACK, 2, WHITE);

    double spi_utilized = (mb_per_s / 6.25) * 100;        // 50 Mbit/s / 8 = 6.25 MB/s
    sprintf(buf, "SPI Utilized: %.2f", spi_utilized);
    ILI9341_Draw_Text(buf, 10, 90, BLACK, 2, WHITE);

    HAL_Delay(10000);
}

static void demo_counting_multiple(void)
{
    char buf[40];

    show_title("Counting multiple segments at once", NULL);

    for (uint16_t i = 0; i <= 10; i++)
    {
        sprintf(buf, "Counting: %d", i);
        ILI9341_Draw_Text(buf, 10,  10, BLACK, 2, WHITE);
        ILI9341_Draw_Text(buf, 10,  30, BLUE,  2, WHITE);
        ILI9341_Draw_Text(buf, 10,  50, RED,   2, WHITE);
        ILI9341_Draw_Text(buf, 10,  70, GREEN, 2, WHITE);
        ILI9341_Draw_Text(buf, 10,  90, BLACK, 2, WHITE);
        ILI9341_Draw_Text(buf, 10, 110, BLUE,  2, WHITE);
        ILI9341_Draw_Text(buf, 10, 130, RED,   2, WHITE);
        ILI9341_Draw_Text(buf, 10, 150, GREEN, 2, WHITE);
        ILI9341_Draw_Text(buf, 10, 170, WHITE, 2, BLACK);
        ILI9341_Draw_Text(buf, 10, 190, BLUE,  2, BLACK);
        ILI9341_Draw_Text(buf, 10, 210, RED,   2, BLACK);
    }
    HAL_Delay(1000);
}

static void demo_counting_single(void)
{
    char buf[40];

    show_title("Counting single segment", NULL);

    for (uint16_t i = 0; i <= 100; i++)
    {
        sprintf(buf, "Counting: %d", i);
        ILI9341_Draw_Text(buf, 10, 10, BLACK, 3, WHITE);
    }
    HAL_Delay(1000);
}

static void demo_alignment(void)
{
    show_title("Rectangle alignment check", NULL);

    ILI9341_Draw_Hollow_Rectangle_Coord(50, 50, 100, 100, BLACK);
    ILI9341_Draw_Filled_Rectangle_Coord(20, 20, 50, 50, BLACK);
    ILI9341_Draw_Hollow_Rectangle_Coord(10, 10, 19, 19, BLACK);
    HAL_Delay(1000);
}

static void demo_lines(void)
{
    show_title("Randomly placed and sized", "Horizontal and Vertical lines");

    for (uint32_t i = 0; i < 30000; i++)
    {
        uint16_t x, y, size, colour;
        rand_shape(&x, &y, &size, &colour);
        ILI9341_Draw_Horizontal_Line(x, y, size, colour);
        ILI9341_Draw_Vertical_Line(x, y, size, colour);
    }
    HAL_Delay(1000);
}

static void demo_hollow_circles(void)
{
    show_title("Randomly placed and sized", "Circles");

    for (uint32_t i = 0; i < 3000; i++)
    {
        uint16_t x, y, size, colour;
        rand_shape(&x, &y, &size, &colour);
        ILI9341_Draw_Hollow_Circle(x, y, size * 2, colour);
    }
    HAL_Delay(1000);
}

static void demo_filled_circles(void)
{
    show_title("Randomly placed and sized", "Filled Circles");

    for (uint32_t i = 0; i < 1000; i++)
    {
        uint16_t x, y, size, colour;
        rand_shape(&x, &y, &size, &colour);
        ILI9341_Draw_Filled_Circle(x, y, size / 2, colour);
    }
    HAL_Delay(1000);
}

static void demo_hollow_rectangles(void)
{
    show_title("Randomly placed and sized", "Rectangles");

    for (uint32_t i = 0; i < 20000; i++)
    {
        uint16_t x, y, size, colour;
        rand_shape(&x, &y, &size, &colour);
        ILI9341_Draw_Hollow_Rectangle_Coord(x, y, x + size, y + size, colour);
    }
    HAL_Delay(1000);
}

static void demo_filled_rectangles(void)
{
    show_title("Randomly placed and sized", "Filled Rectangles");

    for (uint32_t i = 0; i < 20000; i++)
    {
        uint16_t x, y, size, colour;
        rand_shape(&x, &y, &size, &colour);
        ILI9341_Draw_Rectangle(x, y, size, size, colour);
    }
    HAL_Delay(1000);
}

/* Slow: every pixel sets its own address window */
static void demo_pixels_slow(void)
{
    show_title("Slow draw by selecting", "and addressing pixels");

    for (uint16_t y = 0; y < 240; y++)          // vertical stripes
    {
        for (uint16_t x = 0; x < 320; x++)
        {
            if (x % 2)
            {
                ILI9341_Draw_Pixel(x, y, BLACK);
            }
        }
    }

    for (uint16_t y = 0; y < 240; y++)          // horizontal stripes on top
    {
        for (uint16_t x = 0; x < 320; x++)
        {
            if (y % 2)
            {
                ILI9341_Draw_Pixel(x, y, BLACK);
            }
        }
    }
    HAL_Delay(2000);
}

static void demo_pixels_random(void)
{
    show_title("Random position and colour", "500000 pixels");

    for (uint32_t i = 0; i < 500000; i++)
    {
        uint16_t x      = rand16() & 0x01FF;
        uint16_t y      = rand16() & 0x01FF;
        uint16_t colour = rand16();
        ILI9341_Draw_Pixel(x, y, colour);
    }
    HAL_Delay(2000);
}

/* RGB565: 5 bits red << 11, 6 bits green << 5, 5 bits blue */
static void demo_gradient(void)
{
    show_title("Colour gradient", "Grayscale");

    for (uint16_t i = 0; i <= 320; i++)
    {
        uint16_t red   = (uint16_t)((i / 10) << 11);
        uint16_t green = (uint16_t)((i / 5) << 5);
        uint16_t blue  = (uint16_t)(i / 10);

        uint16_t rgb = red + green + blue;
        ILI9341_Draw_Rectangle(i, 0, 1, 240, rgb);   // 1-pixel-wide column
    }
    HAL_Delay(2000);
}

static void demo_image(void)
{
    show_title("RGB Picture", "TIGER");

    ILI9341_Draw_Image((const char *)snow_tiger, SCREEN_VERTICAL_2);
    ILI9341_Set_Rotation(SCREEN_VERTICAL_1);
    HAL_Delay(10000);
}

/* Touch to draw. Never returns (same as the original). */
static void demo_touch(void)
{
    ILI9341_Fill_Screen(WHITE);
    ILI9341_Set_Rotation(SCREEN_HORIZONTAL_2);
    ILI9341_Draw_Text("Touchscreen", 10, 10, BLACK, 2, WHITE);
    ILI9341_Draw_Text("Touch to draw", 10, 30, BLACK, 2, WHITE);
    ILI9341_Set_Rotation(SCREEN_VERTICAL_1);   // touch calibration expects portrait

    while (1)
    {
        if (TP_Touchpad_Pressed())
        {
            uint16_t pos[2];

            HAL_GPIO_WritePin(GPIOB, LD3_Pin | LD2_Pin, GPIO_PIN_SET);

            if (TP_Read_Coordinates(pos) == TOUCHPAD_DATA_OK)
            {
                uint16_t x_pos = pos[0];
                uint16_t y_pos = pos[1];
                char buf[30];

                ILI9341_Draw_Filled_Circle(x_pos, y_pos, 2, BLACK);

                ILI9341_Set_Rotation(SCREEN_HORIZONTAL_2);
                sprintf(buf, "POS X: %.3d", x_pos);
                ILI9341_Draw_Text(buf, 10, 80, BLACK, 2, WHITE);
                sprintf(buf, "POS Y: %.3d", y_pos);
                ILI9341_Draw_Text(buf, 10, 120, BLACK, 2, WHITE);
                ILI9341_Set_Rotation(SCREEN_VERTICAL_1);
            }
        }
        else
        {
            HAL_GPIO_WritePin(GPIOB, LD3_Pin | LD2_Pin, GPIO_PIN_RESET);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void LCD_Module_Init(void)
{
    ILI9341_Init();   // initial driver setup to drive the ILI9341
}

void LCD_Module_Loop(void)
{
    /* Comment out any demo you don't want. demo_touch() must stay last
       because it never returns. */
    demo_performance();
    demo_counting_multiple();
    demo_counting_single();
    demo_alignment();
    demo_lines();
    demo_hollow_circles();
    demo_filled_circles();
    demo_hollow_rectangles();
    demo_filled_rectangles();
    demo_pixels_slow();
    demo_pixels_random();
    demo_gradient();
    demo_image();
    demo_touch();
}
