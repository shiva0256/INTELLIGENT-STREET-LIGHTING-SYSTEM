/* bh1750.h — BH1750 Ambient Light Sensor Driver
 * Project : AI-Enhanced Smart Street Lighting System (ISS)
 * Board   : STM32F411CEU6 (Black Pill)
 * Bus     : I2C1 (PB6=SCL, PB7=SDA)
 * No changes needed from original — kept clean.
 */

#ifndef BH1750_H
#define BH1750_H

#include "stm32f4xx_hal.h"

#define BH1750_ADDR         (0x23 << 1)
#define BH1750_POWER_ON     0x01
#define BH1750_RESET        0x07
#define BH1750_CONT_HRES    0x10

/* Lux threshold below which Night Mode is activated */
#define BH1750_NIGHT_THRESHOLD  50.0f   /* lux — tune to your environment */

void  BH1750_Init(I2C_HandleTypeDef *hi2c);
float BH1750_ReadLux(I2C_HandleTypeDef *hi2c);

#endif /* BH1750_H */
