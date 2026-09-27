/* bh1750.c — BH1750 Ambient Light Sensor Driver
 * Project : AI-Enhanced Smart Street Lighting System (ISS)
 * Board   : STM32F411CEU6 (Black Pill)
 *
 * CHANGE LOG vs original:
 *  - Removed redundant re-send of measurement command inside ReadLux().
 *    In Continuous H-Res mode the sensor self-triggers every ~120ms.
 *    Sending the command again mid-read caused a measurement restart,
 *    wasting 180 ms every call and occasionally corrupting the result.
 *  - ReadLux() now just waits for the conversion window then reads 2 bytes.
 *  - Init unchanged except comment cleanup.
 */

#include "bh1750.h"

void BH1750_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t cmd;

    cmd = BH1750_POWER_ON;
    HAL_I2C_Master_Transmit(hi2c, BH1750_ADDR, &cmd, 1, 10);
    HAL_Delay(10);

    cmd = BH1750_CONT_HRES;                          /* start continuous measurement */
    HAL_I2C_Master_Transmit(hi2c, BH1750_ADDR, &cmd, 1, 10);
    HAL_Delay(180);                                  /* first measurement settle time */
}

float BH1750_ReadLux(I2C_HandleTypeDef *hi2c)
{
    uint8_t buf[2] = {0, 0};

    /*
     * In CONT_HRES mode the sensor updates internally every ~120 ms.
     * We wait 180 ms to be safe, then read the last completed result.
     */
    HAL_Delay(180);
    HAL_I2C_Master_Receive(hi2c, BH1750_ADDR, buf, 2, 10);

    uint16_t raw = ((uint16_t)buf[0] << 8) | buf[1];
    return (float)raw / 1.2f;
}
