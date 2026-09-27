/* lcd_i2c.c — 16x2 I2C LCD Driver (PCF8574 backpack)
 * Project : AI-Enhanced Smart Street Lighting System (ISS)
 * Board   : STM32F411CEU6 (Black Pill)
 *
 * CHANGE LOG vs original:
 *  - All existing init delays kept (they fixed the garbled display).
 *  - Added LCD_PrintNum()  — converts int32_t to string, prints on LCD.
 *  - Added LCD_PrintFloat() — prints float with configurable decimal places.
 *    Uses integer math only (no sprintf / float printf flag needed).
 */

#include "lcd_i2c.h"
#include <string.h>

/* ── low-level helpers ──────────────────────────────────────────────────── */

static void LCD_SendNibble(I2C_HandleTypeDef *hi2c, uint8_t nibble, uint8_t rs)
{
    uint8_t data = nibble | LCD_BACKLIGHT | rs;
    uint8_t buf;

    buf = data | ENABLE;
    HAL_I2C_Master_Transmit(hi2c, LCD_ADDR, &buf, 1, 10);
    HAL_Delay(1);

    buf = data & ~ENABLE;
    HAL_I2C_Master_Transmit(hi2c, LCD_ADDR, &buf, 1, 10);
    HAL_Delay(1);
}

static void LCD_SendByte(I2C_HandleTypeDef *hi2c, uint8_t byte, uint8_t rs)
{
    LCD_SendNibble(hi2c, byte & 0xF0,        rs);
    LCD_SendNibble(hi2c, (byte << 4) & 0xF0, rs);
}

/* ── public API ─────────────────────────────────────────────────────────── */

void LCD_Init(I2C_HandleTypeDef *hi2c)
{
    HAL_Delay(100);              /* wait for LCD power-up */

    /* 8-bit mode init sequence — required before switching to 4-bit */
    LCD_SendNibble(hi2c, 0x30, 0); HAL_Delay(10);
    LCD_SendNibble(hi2c, 0x30, 0); HAL_Delay(5);
    LCD_SendNibble(hi2c, 0x30, 0); HAL_Delay(2);

    /* switch to 4-bit mode */
    LCD_SendNibble(hi2c, 0x20, 0); HAL_Delay(2);

    LCD_SendByte(hi2c, 0x28, 0); HAL_Delay(2);  /* function set: 4-bit, 2-line */
    LCD_SendByte(hi2c, 0x08, 0); HAL_Delay(2);  /* display OFF */
    LCD_SendByte(hi2c, 0x01, 0); HAL_Delay(5);  /* clear display */
    LCD_SendByte(hi2c, 0x06, 0); HAL_Delay(2);  /* entry mode: increment, no shift */
    LCD_SendByte(hi2c, 0x0C, 0); HAL_Delay(2);  /* display ON, cursor OFF */
}

void LCD_Clear(I2C_HandleTypeDef *hi2c)
{
    LCD_SendByte(hi2c, 0x01, 0);
    HAL_Delay(10);
}

void LCD_SetCursor(I2C_HandleTypeDef *hi2c, uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
    LCD_SendByte(hi2c, addr, 0);
}

void LCD_Print(I2C_HandleTypeDef *hi2c, char *str)
{
    while (*str)
    {
        LCD_SendByte(hi2c, (uint8_t)*str++, RS);
    }
}

/* Print a signed integer directly on the LCD */
void LCD_PrintNum(I2C_HandleTypeDef *hi2c, int32_t num)
{
    char buf[12];
    int8_t i = 0;
    uint8_t negative = 0;

    if (num == 0) { LCD_SendByte(hi2c, '0', RS); return; }

    if (num < 0) { negative = 1; num = -num; }

    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    if (negative) buf[i++] = '-';

    /* reverse */
    for (int8_t j = i - 1; j >= 0; j--)
        LCD_SendByte(hi2c, (uint8_t)buf[j], RS);
}

/* Print a float with 'decimals' decimal places (0–3), no sprintf needed */
void LCD_PrintFloat(I2C_HandleTypeDef *hi2c, float val, uint8_t decimals)
{
    if (val < 0) { LCD_SendByte(hi2c, '-', RS); val = -val; }

    int32_t integer = (int32_t)val;
    LCD_PrintNum(hi2c, integer);

    if (decimals > 0)
    {
        LCD_SendByte(hi2c, '.', RS);
        /* shift decimal part into integer range */
        uint32_t mult = 1;
        for (uint8_t d = 0; d < decimals; d++) mult *= 10;
        int32_t frac = (int32_t)((val - (float)integer) * (float)mult + 0.5f);
        /* pad leading zeros in fractional part */
        uint32_t check = mult / 10;
        while (check > 1 && frac < (int32_t)check) {
            LCD_SendByte(hi2c, '0', RS);
            check /= 10;
        }
        LCD_PrintNum(hi2c, frac);
    }
}
