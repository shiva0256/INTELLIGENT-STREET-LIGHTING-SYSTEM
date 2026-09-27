/**
 * @file    main.h
 * @brief   ISS (Intelligent Security System) Node — Master Header
 *
 * @details This header defines all GPIO pin labels, timing constants, and the
 *          shared ISS_Payload_t structure used for LoRa RF transmission between
 *          the STM32F411CEU6 (Black Pill) transmitter node and the ESP32 receiver.
 *
 * @board   STM32F411CEU6 — "Black Pill"
 * @clock   HSE 25 MHz → PLL → SYSCLK 96 MHz
 *
 * @hardware_map
 *   Peripheral   | Pins         | Notes
 *   -------------|--------------|--------------------------------------
 *   UART2 Debug  | PA2/PA3      | TX→Arduino Uno SoftSerial pin 10
 *   ADC1 Voltage | PA4 (CH4)    | ZMPT101B, cal = 218.0
 *   ADC1 Current | PA5 (CH5)    | SCT013, cal = 85.7, 33Ω burden
 *   I2C1         | PB6/PB7      | BH1750 (0x23) + LCD 16x2 (0x27)
 *   SPI1         | PB3/PA6/PA7  | LoRa Ra-02 (SX1278)
 *   LORA_NSS     | PA8          | GPIO Output (active LOW chip-select)
 *   LORA_RST     | PA9          | GPIO Output (active LOW reset)
 *   LORA_DIO0    | PB4          | GPIO Input pull-down (TxDone, polled via SPI)
 *   Relay        | PB0          | Active LOW
 *   PIR          | PB1          | Active HIGH + pull-down
 *   LED          | PC13         | Active LOW (onboard)
 *
 * @fixes_applied
 *   - ISS_Payload_t packed with #pragma pack(1) — exactly 20 bytes on all MCUs
 *   - _Static_assert enforces correct struct size at compile time
 *
 * @version 2.0
 */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdbool.h>

/* =====================================================================
 *  ERROR HANDLER PROTOTYPE
 * ===================================================================== */

/**
 * @brief  Called on any HAL init failure. Blinks PC13 LED rapidly and halts.
 */
void Error_Handler(void);

/* =====================================================================
 *  LORA Ra-02 GPIO PIN LABELS
 *
 *  These macros give human-readable names to the LoRa Ra-02 (SX1278)
 *  control pins used throughout lora_ra02.c.
 * ===================================================================== */

/** @brief LoRa SPI chip-select (NSS) — PA8, active LOW */
#define LORA_NSS_Pin        GPIO_PIN_8
#define LORA_NSS_GPIO_Port  GPIOA

/** @brief LoRa hardware reset — PA9, active LOW */
#define LORA_RST_Pin        GPIO_PIN_9
#define LORA_RST_GPIO_Port  GPIOA

/**
 * @brief LoRa TxDone interrupt output — PB4
 * @note  Wired but NOT used as an interrupt. TxDone is detected by
 *        polling REG_IRQ_FLAGS (0x12) over SPI inside LoRa_Transmit().
 *        Kept in hardware for optional future interrupt use.
 */
#define LORA_DIO0_Pin       GPIO_PIN_4
#define LORA_DIO0_GPIO_Port GPIOB

/* =====================================================================
 *  OTHER GPIO PIN LABELS
 * ===================================================================== */

/** @brief Relay output — PB0, active LOW (RELAY_ON = GPIO_PIN_RESET) */
#define RELAY_Pin           GPIO_PIN_0
#define RELAY_GPIO_Port     GPIOB

/** @brief PIR motion sensor input — PB1, active HIGH with internal pull-down */
#define PIR_Pin             GPIO_PIN_1
#define PIR_GPIO_Port       GPIOB

/** @brief Onboard status LED — PC13, active LOW */
#define LED_Pin             GPIO_PIN_13
#define LED_GPIO_Port       GPIOC

/* =====================================================================
 *  ISS PAYLOAD STRUCTURE
 *
 *  Shared binary packet transmitted over LoRa (433 MHz) from STM32 → ESP32.
 *
 *  Rules:
 *    - #pragma pack(1) removes ALL compiler padding so sizeof() == 20 on
 *      both MCUs (ARM Cortex-M4 and Xtensa LX6).
 *    - Field ORDER must exactly match the ESP32 receiver struct.
 *    - _Static_assert catches any size mismatch at compile time — the
 *      build will FAIL with a clear error if the struct drifts from 20 bytes.
 *
 *  Byte layout (20 bytes total):
 *    Offset  Size  Field         Unit / Notes
 *    ------  ----  ------------  ----------------------------
 *     0– 3    4    voltage_rms   Volts  (float, IEEE 754)
 *     4– 7    4    current_rms   Amps   (float)
 *     8–11    4    power_w       Watts  (float, = V × I)
 *    12–15    4    lux           Lux    (float, from BH1750)
 *    16       1    pir_state     0 = no motion, 1 = motion detected
 *    17       1    relay_state   0 = OFF, 1 = ON
 *    18       1    night_mode    0 = Day (lux ≥ 50), 1 = Night (lux < 50)
 *    19       1    node_id       Unique node identifier (0x01 for this node)
 * ===================================================================== */
#pragma pack(push, 1)
typedef struct {
    float   voltage_rms;   /**< AC mains voltage RMS measured by ZMPT101B (Volts)   */
    float   current_rms;   /**< AC load current RMS measured by SCT013 (Amps)        */
    float   power_w;       /**< Apparent power = voltage_rms × current_rms (Watts)  */
    float   lux;           /**< Ambient light level from BH1750 sensor (Lux)         */
    uint8_t pir_state;     /**< PIR motion: 1 = motion active, 0 = no motion         */
    uint8_t relay_state;   /**< Relay: 1 = energised (load ON), 0 = de-energised     */
    uint8_t night_mode;    /**< 1 = night (lux < 50), 0 = day — enables PIR logic    */
    uint8_t node_id;       /**< Node identifier byte — set to NODE_ID (0x01)         */
} ISS_Payload_t;
#pragma pack(pop)

/** @brief Compile-time guard: build fails if struct is not exactly 20 bytes */
_Static_assert(sizeof(ISS_Payload_t) == 20, "ISS_Payload_t size mismatch — check packing!");

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
