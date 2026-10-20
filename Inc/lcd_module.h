#ifndef LCD_MODULE_H
#define LCD_MODULE_H

/**
 * Call once, after the MX_*_Init() calls (before the main while(1)).
 * Initialises the ILI9341 driver.
 */
void LCD_Module_Init(void);

/**
 * Call from the main while(1).
 * Runs every demo once, in order. The last demo (touch drawing) never
 * returns, exactly like the original main.c.
 */
void LCD_Module_Loop(void);

#endif /* LCD_MODULE_H */
