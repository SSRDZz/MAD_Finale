# STM32-ILI9341 — Function Usage Guide

A practical guide to every function in the [martnak/STM32-ILI9341](https://github.com/martnak/STM32-ILI9341) library, based on reading the source and the demo in `Src/main.c`.

The library drives a **320×240 ILI9341 TFT** (with XPT2046-style touch) over SPI using the STM32 HAL. It uses **no DMA and no interrupts**.

> Notes marked **(by reading, untested)** are things I spotted in the source but have not run on hardware. Verify before relying on them.

---

## Contents

1. [Quick start](#1-quick-start)
2. [Concepts: rotation, coordinates, colours](#2-concepts-rotation-coordinates-colours)
3. [Driver functions](#3-driver-functions-ili9341_stm32_driver)
4. [GFX functions](#4-gfx-functions-ili9341_gfx)
5. [Touchscreen functions](#5-touchscreen-functions-ili9341_touchscreen)
6. [Patterns from main.c](#6-patterns-from-mainc)
7. [Gotchas and known quirks](#7-gotchas-and-known-quirks)
8. [Porting checklist](#8-porting-checklist)
9. [Starter template](#9-starter-template)

---

## 1. Quick start

### Files

| File | Purpose |
|---|---|
| `ILI9341_STM32_Driver.c/.h` | Init, rotation, pixels, rectangles, lines, fills |
| `ILI9341_GFX.c/.h` | Circles, coordinate rectangles, text, full-screen images |
| `ILI9341_Touchscreen.c/.h` | Touch reading (bit-banged SPI on plain GPIO) |
| `5x5_font.h` | Font table used by `Draw_Char` / `Draw_Text` |
| `snow_tiger.h` | Sample 320×240 image array |

### CubeMX requirements

- One SPI peripheral (the example uses **SPI5**), Master, transmit-capable.
- Three GPIO outputs for the LCD: **CS**, **DC**, **RST** (set speed to *Very High*).
- For touch: GPIO outputs **T_CLK, T_MOSI, T_CS** and inputs **T_MISO, T_IRQ**.
- In CubeMX *Project Manager → Advanced Settings*, tick **"Generate peripheral initialization as a pair of .c/.h files"**. The library calls `MX_SPI5_Init()` and `MX_GPIO_Init()` itself.

### Includes

```c
#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"
#include "ILI9341_Touchscreen.h"   // only if you use touch
```

### Minimal usage

```c
ILI9341_Init();                          // once, before any drawing
ILI9341_Set_Rotation(SCREEN_HORIZONTAL_2);
ILI9341_Fill_Screen(WHITE);
ILI9341_Draw_Text("Hello", 10, 10, BLACK, 2, WHITE);
```

---

## 2. Concepts: rotation, coordinates, colours

### Rotation

`ILI9341_Set_Rotation()` changes the coordinate system for **every draw call after it**. The origin (0,0) and the screen size both change.

| Constant | Value | Width × Height |
|---|---|---|
| `SCREEN_VERTICAL_1` | 0 | 240 × 320 |
| `SCREEN_HORIZONTAL_1` | 1 | 320 × 240 |
| `SCREEN_VERTICAL_2` | 2 | 240 × 320 |
| `SCREEN_HORIZONTAL_2` | 3 | 320 × 240 |

- `ILI9341_Init()` ends by setting `SCREEN_VERTICAL_1`.
- The touchscreen code is calibrated for `SCREEN_VERTICAL_1`.
- The globals `LCD_WIDTH` and `LCD_HEIGHT` always hold the current size.

### Coordinates

- (0,0) is the top-left corner of the current rotation. X goes right, Y goes down.
- Out-of-bounds drawing is **silently clipped** by pixels, lines and rectangles.

### Colours (RGB565, 16-bit)

Predefined: `BLACK`, `WHITE`, `RED`, `GREEN`, `BLUE`, `CYAN`, `MAGENTA`, `YELLOW`, `ORANGE`, `NAVY`, `DARKGREEN`, `DARKCYAN`, `MAROON`, `PURPLE`, `OLIVE`, `LIGHTGREY`, `DARKGREY`, `GREENYELLOW`, `PINK`.

Bit layout: `RRRRR GGGGGG BBBBB` (5 bits red, 6 green, 5 blue). `main.c` builds colours like this:

```c
uint16_t colour = (Red << 11) + (Green << 5) + Blue;   // Red 0-31, Green 0-63, Blue 0-31
```

Optional helper (not part of the library, add it yourself):

```c
#define RGB565(r, g, b)  ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
// RGB565(255, 128, 0) -> orange-ish
```

---

## 3. Driver functions (`ILI9341_STM32_Driver`)

### `void ILI9341_Init(void)`

Full LCD start-up: hardware reset, software reset (~1 s delay), power/timing/gamma registers, 16-bit pixel format, sleep-out, display-on, then `Set_Rotation(SCREEN_VERTICAL_1)`.

```c
HAL_Init();
SystemClock_Config();
MX_GPIO_Init();
MX_SPI5_Init();
ILI9341_Init();      // call once, after peripherals are initialised
```

Notes:
- Takes over a second because of the reset delays.
- Calls `MX_SPI5_Init()` and `MX_GPIO_Init()` internally. Calling them yourself first is harmless.

### `void ILI9341_Set_Rotation(uint8_t Rotation)`

Sets orientation (see [Rotation](#rotation)). Takes effect for all later drawing.

```c
ILI9341_Set_Rotation(SCREEN_HORIZONTAL_2);   // landscape, 320x240
ILI9341_Set_Rotation(SCREEN_VERTICAL_1);     // portrait, 240x320 (needed for touch)
```

Notes:
- An invalid value is ignored.
- Already-drawn content is not redrawn or rotated.
- `main.c` switches rotation back and forth, e.g. landscape for drawing text and portrait for touch.

### `void ILI9341_Fill_Screen(uint16_t Colour)`

Fills the entire screen with one colour. This is the fastest operation in the library (about 40 FPS at 50 MHz SPI).

```c
ILI9341_Fill_Screen(WHITE);   // clear
ILI9341_Fill_Screen(BLACK);
```

### `void ILI9341_Draw_Pixel(uint16_t X, uint16_t Y, uint16_t Colour)`

Draws one pixel.

```c
ILI9341_Draw_Pixel(100, 50, RED);
```

Notes:
- **Slow**: it re-sends the full address window for every pixel. Avoid it for large shapes.
- Out-of-bounds coordinates are ignored.
- `main.c` demos this by drawing alternating columns and rows, and by plotting 500,000 random pixels.

### `void ILI9341_Draw_Rectangle(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height, uint16_t Colour)`

Draws a **filled** rectangle. `X, Y` is the top-left corner; `Width` and `Height` are in pixels.

```c
ILI9341_Draw_Rectangle(20, 30, 100, 50, BLUE);   // 100x50 box at (20,30)
```

Notes:
- The source comment says "hollow", but it is **filled**. For an outline use `Draw_Hollow_Rectangle_Coord` (GFX).
- Clips to the screen edges.
- Fast, because it uses the burst fill.
- `main.c` also uses it to paint a colour gradient one 1-pixel-wide column at a time: `ILI9341_Draw_Rectangle(i, 0, 1, 240, colour)`.

### `void ILI9341_Draw_Horizontal_Line(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Colour)`

Draws a 1-pixel-high line from `(X, Y)` to `(X + Width - 1, Y)`.

```c
ILI9341_Draw_Horizontal_Line(10, 100, 200, BLACK);
```

### `void ILI9341_Draw_Vertical_Line(uint16_t X, uint16_t Y, uint16_t Height, uint16_t Colour)`

Draws a 1-pixel-wide line from `(X, Y)` down to `(X, Y + Height - 1)`.

```c
ILI9341_Draw_Vertical_Line(100, 10, 150, BLACK);
```

Both line functions are fast (burst fill) and clip to the screen. For diagonal lines you have to write your own (e.g. Bresenham on top of `Draw_Pixel`).

### Lower-level driver functions

You normally don't need these, but they are useful for custom drawing such as sprites or your own image routines.

| Function | What it does |
|---|---|
| `ILI9341_Set_Address(X1, Y1, X2, Y2)` | Defines the drawing window and starts a memory write. The next colour bytes sent fill that window left-to-right, top-to-bottom. |
| `ILI9341_Draw_Colour_Burst(Colour, Size)` | Sends `Size` **pixels** of one colour. Call right after `Set_Address`. |
| `ILI9341_Draw_Colour(Colour)` | Sends a single pixel's colour. Marked internal in the source. |
| `ILI9341_Write_Command(cmd)` / `ILI9341_Write_Data(data)` | Send a raw command or data byte (DC pin handled for you). |
| `ILI9341_SPI_Send(byte)` | Raw SPI byte, no CS or DC handling. |
| `ILI9341_Reset()` / `ILI9341_Enable()` | Hardware reset pulse / set RST high. Used by `Init`. |
| `ILI9341_SPI_Init()` | Re-initialises SPI5 and GPIO. Used by `Init`. |

Example: custom filled box using the low-level pair (this is what `Draw_Rectangle` does):

```c
ILI9341_Set_Address(50, 50, 99, 99);          // 50x50 window
ILI9341_Draw_Colour_Burst(GREEN, 50 * 50);    // 2500 pixels
```

---

## 4. GFX functions (`ILI9341_GFX`)

### `void ILI9341_Draw_Text(const char* Text, uint8_t X, uint8_t Y, uint16_t Colour, uint16_t Size, uint16_t Background_Colour)`

Draws a string starting at `(X, Y)`.

| Parameter | Meaning |
|---|---|
| `Text` | Null-terminated string |
| `X, Y` | Top-left of the first character (**`uint8_t`**, so 0-255 only) |
| `Colour` | Text colour |
| `Size` | Integer scale: 1, 2, 3 ... |
| `Background_Colour` | Fill behind each character cell |

```c
ILI9341_Draw_Text("FPS TEST", 10, 10, BLACK, 1, WHITE);
ILI9341_Draw_Text("Big text", 10, 30, RED, 3, WHITE);
```

Notes:
- Each character cell is **6 × 8 pixels × Size**. The cursor advances `6 * Size` pixels per character.
- Approximate characters per line at 320 px wide: Size 1 → 53, Size 2 → 26, Size 3 → 17.
- No line wrapping and no `\n` support. Call `Draw_Text` once per line.
- Because each cell paints its own background, **you can redraw changing text in place without clearing** (see the counting examples). But a shorter new string leaves old characters behind, so pad with spaces: `"%-10d"` or `"%3d "`.
- Size 1 text is drawn pixel by pixel (slow); Size 2 and up uses small rectangles.

Formatting numbers (as `main.c` does):

```c
char buf[30];
sprintf(buf, "FPS:  %.2f", fps);
ILI9341_Draw_Text(buf, 10, 50, BLACK, 2, WHITE);
```

Needs `#include <stdio.h>`. Floating-point `printf` may require enabling float support in your toolchain.

### `void ILI9341_Draw_Char(char Character, uint8_t X, uint8_t Y, uint16_t Colour, uint16_t Size, uint16_t Background_Colour)`

Draws a single character; `Draw_Text` calls it in a loop. Same parameters and cell size as above.

```c
ILI9341_Draw_Char('A', 20, 20, BLUE, 4, WHITE);
```

Only printable ASCII is supported. Control characters (below space) are not handled properly **(by reading, untested)**.

### `void ILI9341_Draw_Hollow_Circle(uint16_t X, uint16_t Y, uint16_t Radius, uint16_t Colour)`

Circle outline. `X, Y` is the **centre**.

```c
ILI9341_Draw_Hollow_Circle(160, 120, 40, RED);
```

Slow-ish (many `Draw_Pixel` calls). `main.c` passes `radius * 2` for the random circle demo.

### `void ILI9341_Draw_Filled_Circle(uint16_t X, uint16_t Y, uint16_t Radius, uint16_t Colour)`

Filled circle. `X, Y` is the **centre**.

```c
ILI9341_Draw_Filled_Circle(160, 120, 20, GREEN);
```

**Slow**: it plots every pixel with `Draw_Pixel`. The source has a TODO to speed it up. Fine for a handful of circles or a small touch cursor (`main.c` uses radius 2), not for animation.

### `void ILI9341_Draw_Hollow_Rectangle_Coord(uint16_t X0, uint16_t Y0, uint16_t X1, uint16_t Y1, uint16_t Colour)`

Rectangle outline between two corner **coordinates** (not width/height).

```c
ILI9341_Draw_Hollow_Rectangle_Coord(50, 50, 100, 100, BLACK);
```

Notes:
- Pass `X0 <= X1` and `Y0 <= Y1`. With reversed corners the lines are drawn from `X0`/`Y0` in the wrong direction **(by reading, untested)**.
- Width is `X1 - X0` and height `Y1 - Y0`, and the bottom-right corner pixel is added separately.

### `void ILI9341_Draw_Filled_Rectangle_Coord(uint16_t X0, uint16_t Y0, uint16_t X1, uint16_t Y1, uint16_t Colour)`

Filled rectangle between two corner coordinates. Handles reversed corners correctly (it swaps them).

```c
ILI9341_Draw_Filled_Rectangle_Coord(20, 20, 50, 50, BLACK);
```

Notes:
- It calls `Draw_Rectangle` with width `X1 - X0` and height `Y1 - Y0`, so the **end column and row (X1, Y1) are not painted** (by reading, untested). If you need inclusive corners, use `X1 + 1, Y1 + 1`.
- `main.c`'s "alignment check" draws a hollow box, a filled box and a small hollow box side by side to let you verify edges on a real screen. Run it to check this behaviour.

### `void ILI9341_Draw_Image(const char* Image_Array, uint8_t Orientation)`

Draws a **full-screen** 320×240 image from a C array in flash.

```c
#include "snow_tiger.h"
ILI9341_Draw_Image((const char*)snow_tiger, SCREEN_VERTICAL_2);
ILI9341_Set_Rotation(SCREEN_VERTICAL_1);   // restore rotation afterwards (main.c does this)
```

Image format:
- RGB565, 2 bytes per pixel, **high byte first**, 320 × 240 × 2 = 153,600 bytes total.
- Make one with an online "picture to C array" converter. The source mentions `digole.com/tools/PicturetoC_Hex_converter.php`. Choose 16-bit/65K colour output.
- The array must match the orientation you pass, since `Draw_Image` calls `Set_Rotation(Orientation)` itself.

Notes:
- Uses hard-coded `hspi5` and `GPIOC` with `CS_Pin` / `DC_Pin` rather than the macros in the driver header, so porting means editing this function too.
- The loop sends `153600 / 500` = 307 whole chunks, so the last 100 bytes (50 pixels) are likely never sent **(by reading, untested)**.
- It is full-screen only. There is no partial image or sprite function.

### Declared but not implemented

`ILI9341_Draw_Filled_Rectangle_Size_Text(...)` appears in `ILI9341_GFX.h` but I found no implementation. Don't call it.

---

## 5. Touchscreen functions (`ILI9341_Touchscreen`)

Setup:
- Map the five touch pins at the top of `ILI9341_Touchscreen.h` (`TP_CLK`, `TP_CS`, `TP_MISO`, `TP_MOSI`, `TP_IRQ`: port and pin).
- Initialise those GPIOs before use. There is no separate init call.
- Use `SCREEN_VERTICAL_1`. Other rotations need you to remap X and Y yourself.

### `uint8_t TP_Touchpad_Pressed(void)`

Reads the IRQ pin (active low). Returns `TOUCHPAD_PRESSED` (1) or `TOUCHPAD_NOT_PRESSED` (0). Fast and non-blocking.

```c
if (TP_Touchpad_Pressed()) {
    // finger is on the screen
}
```

### `uint8_t TP_Read_Coordinates(uint16_t Coordinates[2])`

Reads the touch position into `Coordinates[0] = X`, `Coordinates[1] = Y`. Returns `TOUCHPAD_DATA_OK` (1) or `TOUCHPAD_DATA_NOISY` (0).

```c
uint16_t pos[2];
if (TP_Read_Coordinates(pos) == TOUCHPAD_DATA_OK) {
    uint16_t x = pos[0];   // 0-239 in SCREEN_VERTICAL_1
    uint16_t y = pos[1];   // 0-319
    ILI9341_Draw_Filled_Circle(x, y, 2, BLACK);
}
```

Notes:
- **Blocking**: it takes up to `NO_OF_POSITION_SAMPLES` (default 1000) readings and averages them. If the finger lifts before all samples are taken, it returns `TOUCHPAD_DATA_NOISY` with coordinates (0,0). Always check the return value.
- Lower `NO_OF_POSITION_SAMPLES` for a faster response (noisier). Raise it for steadier readings.
- Calibration is hard-coded in the header (`X_OFFSET`, `Y_OFFSET`, `X_MAGNITUDE`, `Y_MAGNITUDE`, `X_TRANSLATION`, `Y_TRANSLATION`). Tune them if the dot lands away from your finger.

### Touch loop pattern from `main.c`

```c
ILI9341_Set_Rotation(SCREEN_VERTICAL_1);
while (1) {
    if (TP_Touchpad_Pressed()) {
        uint16_t pos[2];
        if (TP_Read_Coordinates(pos) == TOUCHPAD_DATA_OK) {
            ILI9341_Draw_Filled_Circle(pos[0], pos[1], 2, BLACK);   // draw
            // To print coordinates, switch to landscape, draw text, switch back:
            ILI9341_Set_Rotation(SCREEN_HORIZONTAL_2);
            char b[30];
            sprintf(b, "POS X: %.3d", pos[0]);
            ILI9341_Draw_Text(b, 10, 80, BLACK, 2, WHITE);
            ILI9341_Set_Rotation(SCREEN_VERTICAL_1);
        }
    }
}
```

`main.c` also toggles the board LEDs (`LD2`, `LD3`) while touched. That is optional and board-specific.

---

## 6. Patterns from `main.c`

The demo runs these sections in an endless loop. Each one is a reusable pattern.

| Section | Functions used | What to learn |
|---|---|---|
| **Performance test** | `Fill_Screen`, `Draw_Text`, `HAL_TIM_Base_Start/Stop`, `__HAL_TIM_GET_COUNTER` | Times 20 full-screen fills with TIM1 and computes FPS and MB/s. TIM1 is set up with prescaler 10000 so it ticks about 20 kHz, per the source comment. Copy this to benchmark your own drawing. |
| **Counting multiple segments** | `Draw_Text` | Many text lines updated in place at once, with different colour and background pairs. |
| **Counting single segment** | `Draw_Text` | One number updated in place at Size 3, with no clear needed. |
| **Rectangle alignment check** | `Draw_Hollow_Rectangle_Coord`, `Draw_Filled_Rectangle_Coord` | Verifies edges and corners. |
| **Random lines** | `Draw_Horizontal_Line`, `Draw_Vertical_Line` | 30,000 random lines using the hardware RNG. Values masked with `& 0x01FF` often fall off-screen, which shows the clipping. |
| **Hollow / filled circles** | `Draw_Hollow_Circle`, `Draw_Filled_Circle` | 3,000 / 1,000 random circles. |
| **Hollow / filled rectangles** | `Draw_Hollow_Rectangle_Coord`, `Draw_Rectangle` | 20,000 random rectangles. |
| **Pixel examples** | `Draw_Pixel` | Alternating columns/rows (slow), then 500,000 random pixels. |
| **Colour gradient** | `Draw_Rectangle` | Builds RGB565 values and paints 1-pixel columns. |
| **Image** | `Draw_Image`, `Set_Rotation` | Full-screen picture, then restore rotation. |
| **Touchscreen** | `TP_Touchpad_Pressed`, `TP_Read_Coordinates`, `Draw_Filled_Circle` | Touch-to-draw with a coordinate readout. |

Random-position helper used by the demos:

```c
uint32_t r = HAL_RNG_GetRandomNumber(&hrng);   // needs RNG enabled in CubeMX (F7 has it)
uint16_t x = r & 0x01FF;                       // 0-511
```

You can swap this for `rand()` if your MCU has no RNG.

---

## 7. Gotchas and known quirks

1. **Rotation is global state.** Set it before drawing, and set it back when you switch screens. Touch expects `SCREEN_VERTICAL_1`.
2. **`Draw_Rectangle` is filled** even though its comment says hollow.
3. **Filled coordinate rectangles exclude the end row/column** `(X1, Y1)`. Hollow ones need `X0 <= X1` and `Y0 <= Y1`. (by reading, untested)
4. **Text `X`/`Y` are `uint8_t`**, so a start position above 255 is impossible. There is no wrapping, no `\n`, and shorter strings leave leftovers.
5. **Slow functions:** `Draw_Pixel`, both circles, and Size 1 text. Prefer rectangles, lines and Size 2+ text for speed.
6. **`Draw_Image` is full-screen only**, hard-codes `hspi5` and `GPIOC`, and likely drops the last 100 bytes. (by reading, untested)
7. **Possible 1-byte overflow in `Draw_Colour_Burst`** when the pixel count is odd and under 250, because the 2-byte fill loop can write one byte past the buffer. (by reading, untested)
8. **`ILI9341_SPI_Init()` hard-codes `MX_SPI5_Init()`.** Changing `HSPI_INSTANCE` alone is not enough on another SPI.
9. **Touch reads block** for up to 1000 samples and return zeros when noisy, so always check the return code.
10. **Everything is blocking.** The CPU is busy during all SPI transfers, so there is no background drawing.
11. **`Draw_Filled_Rectangle_Size_Text` is declared but missing.**

---

## 8. Porting checklist

- [ ] Change `#include "stm32f7xx_hal.h"` in the driver and touch headers to your MCU family (e.g. `stm32f4xx_hal.h`).
- [ ] Set `HSPI_INSTANCE` and the `LCD_CS` / `LCD_DC` / `LCD_RST` port and pin macros in `ILI9341_STM32_Driver.h`.
- [ ] In `ILI9341_STM32_Driver.c`, replace `MX_SPI5_Init()` in `ILI9341_SPI_Init()` with your SPI init.
- [ ] In `ILI9341_GFX.c` `Draw_Image`, replace hard-coded `hspi5`, `GPIOC`, `CS_Pin`, `DC_Pin` with your macros.
- [ ] Map the five touch pins in `ILI9341_Touchscreen.h`.
- [ ] Set GPIO speed to *Very High* and SPI to a rate your wiring can handle (the demo uses 50 MHz).
- [ ] Add the `ILI9341` folder to your include paths and the `.c` files to the build.
- [ ] Re-tune touch calibration constants for your panel.

---

## 9. Starter template

```c
#include <stdio.h>
#include "ILI9341_STM32_Driver.h"
#include "ILI9341_GFX.h"
#include "ILI9341_Touchscreen.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI5_Init();

    ILI9341_Init();
    ILI9341_Set_Rotation(SCREEN_HORIZONTAL_2);
    ILI9341_Fill_Screen(WHITE);

    ILI9341_Draw_Text("My project", 10, 10, BLACK, 2, WHITE);
    ILI9341_Draw_Filled_Rectangle_Coord(10, 40, 110, 90, BLUE);
    ILI9341_Draw_Hollow_Circle(200, 120, 30, RED);

    while (1)
    {
        // your code here
    }
}
```
