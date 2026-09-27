/* lora_ra02.h --- SX1278 / Ra-02 Driver Header */

#ifndef LORA_RA02_H
#define LORA_RA02_H

#include "stm32f4xx_hal.h"
#include "main.h"
#include <stdbool.h>

/* =====================================================================
 *  SX1278 Register Map
 * ===================================================================== */
#define SX1278_REG_FIFO              0x00
#define SX1278_REG_OP_MODE           0x01
#define SX1278_REG_FRF_MSB           0x06
#define SX1278_REG_FRF_MID           0x07
#define SX1278_REG_FRF_LSB           0x08
#define SX1278_REG_PA_CONFIG         0x09
#define SX1278_REG_PA_DAC            0x4D
#define SX1278_REG_LNA               0x0C
#define SX1278_REG_FIFO_ADDR_PTR     0x0D
#define SX1278_REG_FIFO_TX_BASE_ADDR 0x0E
#define SX1278_REG_FIFO_RX_BASE_ADDR 0x0F
#define SX1278_REG_IRQ_FLAGS         0x12
#define SX1278_REG_PAYLOAD_LEN       0x22
#define SX1278_REG_MODEM_CONFIG1     0x1D
#define SX1278_REG_MODEM_CONFIG2     0x1E
#define SX1278_REG_MODEM_CONFIG3     0x26
#define SX1278_REG_PREAMBLE_MSB      0x20
#define SX1278_REG_PREAMBLE_LSB      0x21
#define SX1278_REG_SYNC_WORD         0x39
#define SX1278_REG_DIO_MAPPING1      0x40
#define SX1278_REG_VERSION           0x42

/* =====================================================================
 *  Mode Constants
 * ===================================================================== */
#define SX1278_MODE_LORA             0x80
#define SX1278_MODE_SLEEP            0x00
#define SX1278_MODE_STDBY            0x01
#define SX1278_MODE_TX               0x03

/* =====================================================================
 *  IRQ Flag Bits
 * ===================================================================== */
#define SX1278_IRQ_TX_DONE           0x08
#define SX1278_IRQ_CRC_ERROR         0x20
#define SX1278_IRQ_RX_DONE           0x40

/* =====================================================================
 *  Public API
 * ===================================================================== */
bool    LoRa_Init(SPI_HandleTypeDef *hspi);
bool    LoRa_Transmit(uint8_t *data, uint8_t len);
void    LoRa_SetFrequency(uint32_t freq_hz);
void    LoRa_SetTxPower(uint8_t level);

/**
 * @brief  Read a raw SX1278 register directly via SPI.
 * @note   Used by LoRa_DiagnoseInitFail() in main.c to read VER and
 *         OP_MODE after LoRa_Init() fails, without needing the static
 *         ReadReg() internal function.
 * @param  reg  Register address (0x00–0x7F).
 * @return Register value byte.
 */
uint8_t LoRa_ReadRawReg(uint8_t reg);

#endif /* LORA_RA02_H */
