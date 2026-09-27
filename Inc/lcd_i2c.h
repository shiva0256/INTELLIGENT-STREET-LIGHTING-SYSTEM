/* lcd_i2c.h — 16x2 I2C LCD Driver (PCF8574 backpack)
 * Project : AI-Enhanced Smart Street Lighting System (ISS)
 * Board   : STM32F411CEU6 (Black Pill)
 * Bus     : I2C1 (PB6=SCL, PB7=SDA)   Address: 0x27
 *
 * CHANGE LOG vs original:
 *  - Added LCD_PrintNum() helper — print integer directly, no sprintf needed in main.
 *  - Added LCD_PrintFloat() helper — print float with 1 decimal place.
 *  - Renamed guard from LCD_I2C_H → LCD_I2C_H (no change, kept consistent).
 */

#ifndef LCD_I2C_H
#define LCD_I2C_H

#include "stm32f4xx_hal.h"

#define LCD_ADDR        (0x27 << 1)
#define LCD_BACKLIGHT   0x08
#define ENABLE          0x04
#define RS              0x01

void LCD_Init(I2C_HandleTypeDef *hi2c);
void LCD_Clear(I2C_HandleTypeDef *hi2c);
void LCD_SetCursor(I2C_HandleTypeDef *hi2c, uint8_t row, uint8_t col);
void LCD_Print(I2C_HandleTypeDef *hi2c, char *str);
void LCD_PrintNum(I2C_HandleTypeDef *hi2c, int32_t num);
void LCD_PrintFloat(I2C_HandleTypeDef *hi2c, float val, uint8_t decimals);

#endif /* LCD_I2C_H */
