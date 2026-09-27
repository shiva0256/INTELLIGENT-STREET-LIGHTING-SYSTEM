/**
 * @file    main.c
 * @brief   ISS Node — Unified Firmware v2.5
 *
 * @changes v2.5
 *   - REMOVED: SCT013 current sensor (ADC_ReadCurrentRMS eliminated)
 *   - ADDED: Hardcoded current logic — when voltage_rms > 225V,
 *            current_rms = 0.043A (10W / 230V nominal Indian mains)
 *            power_w = voltage_rms * 0.043
 *            Below 225V → current=0, power=0 (fault/outage condition)
 *   - CHANGED: LUX_NIGHT_THRESHOLD 50.0 → 5.0 lux
 *   - CHANGED: LCD format — meaningful labels with units
 *              Row 0: N1 NIGHT 3.2lx P:ON
 *              Row 1: 230V 9.9W R:ON TX:OK
 *   - REMOVED: ADC_CHANNEL_5 (PA5) init and sampling
 *   - KEPT: All v2.4 fixes (500 samples, 200us spacing, PIR lag fix)
 */

#include "main.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "bh1750.h"
#include "lcd_i2c.h"
#include "lora_ra02.h"

/* =====================================================================
 *  TIMING CONSTANTS
 * ===================================================================== */
#define TX_INTERVAL_MS        15000UL
#define PIR_POLL_MS             500UL
#define RELAY_HOLD_MS         60000UL
#define LCD_UPDATE_MS          3000UL

/* =====================================================================
 *  ADC / SENSOR CONSTANTS
 *
 *  500 samples x 200us = 100ms = 5 full 50Hz cycles.
 *  Only voltage (PA4 / ADC_CHANNEL_4) is sampled — current sensor removed.
 *
 *  CURRENT_HARDCODE_A:
 *    Assumed load = 10W streetlight demo bulb.
 *    I = P / V = 10 / 230 = 0.043A (Indian nominal mains).
 *    Applied only when voltage_rms > VOLTAGE_ACTIVE_THRESHOLD.
 *    Below threshold → current = 0, power = 0 (no supply / fault).
 *
 *  VOLTAGE_CAL_FACTOR:
 *    Still requires empirical calibration. See procedure in v2.4 comments.
 *    Default 595.27 is a starting estimate — reflash after calibration.
 * ===================================================================== */
#define ADC_SAMPLES                500
#define ADC_SAMPLE_DELAY_TICKS     19200UL
#define ADC_VREF                   3.3f
#define ADC_RESOLUTION             4095.0f
#define VOLTAGE_CAL_FACTOR         595.27f

#define LUX_NIGHT_THRESHOLD        5.0f
#define VOLTAGE_ACTIVE_THRESHOLD   225.0f
#define CURRENT_HARDCODE_A         0.043f    /* 10W / 230V */

/* =====================================================================
 *  NODE IDENTITY
 * ===================================================================== */
#define NODE_ID   0x01

/* =====================================================================
 *  GPIO MACROS
 * ===================================================================== */
#define PIR_GPIO_Port   GPIOB
#define PIR_Pin         GPIO_PIN_1

#define RELAY_ON()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET)
#define RELAY_OFF()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET)
#define LED_TOGGLE() HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13)

/* =====================================================================
 *  BUSY-WAIT DELAY — ~200us between ADC samples at 96MHz
 * ===================================================================== */
static inline void delay_ticks(uint32_t ticks) {
    for (volatile uint32_t i = 0; i < ticks; i++);
}

/* =====================================================================
 *  PERIPHERAL HANDLES
 * ===================================================================== */
ADC_HandleTypeDef  hadc1;
I2C_HandleTypeDef  hi2c1;
SPI_HandleTypeDef  hspi1;
UART_HandleTypeDef huart2;

/* =====================================================================
 *  PROTOTYPES
 * ===================================================================== */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static uint16_t ADC_ReadChannel(uint32_t channel);
static float    ADC_ReadVoltageRMS(float *raw_rms_out);
static void     LCD_UpdateDisplay(ISS_Payload_t *p, bool tx_ok, bool tx_done_once);
static void     LoRa_DiagnoseInitFail(void);

/* =====================================================================
 *  PRINTF → UART2
 * ===================================================================== */
int __io_putchar(int ch) {
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* =====================================================================
 *  ADC HELPERS
 * ===================================================================== */
static uint16_t ADC_ReadChannel(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = channel;
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return val;
}

/**
 * @brief  Compute AC mains voltage RMS via ZMPT101B on PA4 (ADC_CHANNEL_4).
 *         500 samples x 200us = 100ms = 5 complete 50Hz cycles.
 *         raw_rms_out: receives raw ADC RMS count for calibration printout.
 */
static float ADC_ReadVoltageRMS(float *raw_rms_out) {
    float buf[ADC_SAMPLES];
    float dc_sum = 0.0f;

    for (int i = 0; i < ADC_SAMPLES; i++) {
        buf[i]  = (float)ADC_ReadChannel(ADC_CHANNEL_4);
        dc_sum += buf[i];
        delay_ticks(ADC_SAMPLE_DELAY_TICKS);
    }

    float dc     = dc_sum / ADC_SAMPLES;
    float sq_sum = 0.0f;

    for (int i = 0; i < ADC_SAMPLES; i++) {
        float s = buf[i] - dc;
        sq_sum += s * s;
    }

    float raw_rms = sqrtf(sq_sum / ADC_SAMPLES);

    if (raw_rms_out != NULL) *raw_rms_out = raw_rms;

    return (raw_rms / ADC_RESOLUTION) * ADC_VREF * VOLTAGE_CAL_FACTOR;
}

/* =====================================================================
 *  LCD DISPLAY UPDATE
 *
 *  Row 0 (16 chars): N<id> DAY/NGT <lux>lx P:ON/OFF
 *  Row 1 (16 chars): <V>V <W>W R:ON/OFF TX:OK/FL
 *
 *  Examples:
 *    Row 0: "N1 NGT 3.2lx P:ON "
 *    Row 1: "230V 9.9W R:ON OK  "
 *
 *    Row 0: "N1 DAY 42.1lx P:OFF"
 *    Row 1: "V:--- W:--- R:OFF  "   ← before first TX
 * ===================================================================== */
static void LCD_UpdateDisplay(ISS_Payload_t *p, bool tx_ok, bool tx_done_once) {
    char row[17];
    LCD_Clear(&hi2c1);

    /* ── Row 0: Node | Mode | Lux | PIR ── */
    LCD_SetCursor(&hi2c1, 0, 0);
    snprintf(row, sizeof(row), "N%d %s", p->node_id,
             p->night_mode ? "NGT" : "DAY");
    LCD_Print(&hi2c1, row);

    /* Lux: show 1 decimal, max 4 digits before decimal */
    snprintf(row, sizeof(row), " %.1flux", p->lux);
    LCD_Print(&hi2c1, row);

    LCD_Print(&hi2c1, p->pir_state ? " P:ON" : " P:OF");

    /* ── Row 1: Voltage | Power | Relay | TX status ── */
    LCD_SetCursor(&hi2c1, 1, 0);
    if (tx_done_once) {
        snprintf(row, sizeof(row), "%.0fV %.1fW", p->voltage_rms, p->power_w);
        LCD_Print(&hi2c1, row);
        LCD_Print(&hi2c1, p->relay_state ? " R:ON" : " R:OF");
        LCD_Print(&hi2c1, tx_ok          ? " OK"   : " FL");
    } else {
        LCD_Print(&hi2c1, "V:--- W:--- R:--");
    }
}

/* =====================================================================
 *  LORA INIT FAILURE DIAGNOSIS
 *  Reads REG_VERSION + REG_OP_MODE, maps to LCD error screen + UART log.
 *  Never returns — LED blinks at 300ms.
 * ===================================================================== */
static void LoRa_DiagnoseInitFail(void) {
    uint8_t ver    = LoRa_ReadRawReg(SX1278_REG_VERSION);
    uint8_t opmode = LoRa_ReadRawReg(SX1278_REG_OP_MODE);

    char lcd0[17];
    char lcd1[17];

    if (ver == 0x00) {
        snprintf(lcd0, sizeof(lcd0), "VER:0x00 NoSPI  ");
        snprintf(lcd1, sizeof(lcd1), "Check MISO/NSS  ");
        printf("[DIAG] VER=0x00 — MISO all zeros. SPI not responding.\r\n");
        printf("       Check: PA6<-MISO  PA7->MOSI  PB3->SCK  PA8->NSS\r\n");
    }
    else if (ver == 0xFF) {
        snprintf(lcd0, sizeof(lcd0), "VER:0xFF Pullup ");
        snprintf(lcd1, sizeof(lcd1), "MISO 5V? or NC  ");
        printf("[DIAG] VER=0xFF — MISO stuck HIGH.\r\n");
    }
    else if (ver == 0x12 && !(opmode & 0x80)) {
        snprintf(lcd0, sizeof(lcd0), "OM:%02X FSK mode ", opmode);
        snprintf(lcd1, sizeof(lcd1), "RST too short?  ");
        printf("[DIAG] VER=0x12 OK. OpMode=0x%02X — FSK, LoRa bit not set.\r\n", opmode);
    }
    else if (ver == 0x12 && opmode == 0x80) {
        snprintf(lcd0, sizeof(lcd0), "OM:80 Sleep stk ");
        snprintf(lcd1, sizeof(lcd1), "Weak 3.3V supply");
        printf("[DIAG] VER=0x12 OK. OpMode=0x80 — stuck in LoRa Sleep.\r\n");
    }
    else {
        snprintf(lcd0, sizeof(lcd0), "VER:%02X BadChip  ", ver);
        if (ver == 0x11) {
            snprintf(lcd1, sizeof(lcd1), "SX1276 not 1278 ");
            printf("[DIAG] VER=0x11 — SX1276 found, not SX1278.\r\n");
        } else {
            snprintf(lcd1, sizeof(lcd1), "SPI noise/wrong ");
            printf("[DIAG] VER=0x%02X — unexpected.\r\n", ver);
        }
    }

    LCD_Clear(&hi2c1);
    LCD_SetCursor(&hi2c1, 0, 0);
    LCD_Print(&hi2c1, lcd0);
    LCD_SetCursor(&hi2c1, 1, 0);
    LCD_Print(&hi2c1, lcd1);

    while (1) { LED_TOGGLE(); HAL_Delay(300); }
}

/* =====================================================================
 *  MAIN
 * ===================================================================== */
int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_ADC1_Init();
    MX_I2C1_Init();
    MX_SPI1_Init();

    printf("ISS Node v2.5 booting...\r\n");
    printf("Payload size : %u bytes (must be 20)\r\n", (unsigned)sizeof(ISS_Payload_t));
    printf("ADC          : %d samples x ~200us = ~100ms (5x 50Hz cycles)\r\n", ADC_SAMPLES);
    printf("Voltage CAL  : %.2f  (update after empirical calibration)\r\n", VOLTAGE_CAL_FACTOR);
    printf("Load assumed : 10W bulb | I=%.3fA | threshold=%.0fV\r\n",
           CURRENT_HARDCODE_A, VOLTAGE_ACTIVE_THRESHOLD);
    printf("Night mode   : lux < %.1f\r\n\r\n", LUX_NIGHT_THRESHOLD);

    BH1750_Init(&hi2c1);
    LCD_Init(&hi2c1);

    LCD_Clear(&hi2c1);
    LCD_SetCursor(&hi2c1, 0, 0);
    LCD_Print(&hi2c1, "ISS Node v2.5");
    LCD_SetCursor(&hi2c1, 1, 0);
    LCD_Print(&hi2c1, "LoRa init...");

    if (!LoRa_Init(&hspi1)) {
        printf("ERROR: LoRa_Init() failed. Running diagnosis...\r\n");
        LoRa_DiagnoseInitFail();
    }

    LCD_Clear(&hi2c1);
    LCD_SetCursor(&hi2c1, 0, 0);
    LCD_Print(&hi2c1, "LoRa OK!");
    LCD_SetCursor(&hi2c1, 1, 0);
    LCD_Print(&hi2c1, "433MHz SF7 BW125");
    HAL_Delay(2000);

    printf("ISS Node Online | LoRa 433MHz SF7 BW125 CR4/5 | TX every 15s\r\n");
    printf("Night threshold : lux < %.1f\r\n", LUX_NIGHT_THRESHOLD);
    printf("Current logic   : V>%.0fV → I=%.3fA fixed | V<=%.0fV → I=0 (fault)\r\n\r\n",
           VOLTAGE_ACTIVE_THRESHOLD, CURRENT_HARDCODE_A, VOLTAGE_ACTIVE_THRESHOLD);

    RELAY_OFF();

    uint32_t last_tx_ms       = 0;
    uint32_t last_pir_poll_ms = 0;
    uint32_t last_lcd_ms      = 0;
    bool     last_tx_ok       = false;
    bool     tx_done_once     = false;
    uint8_t  prev_pir_state   = 0xFF;

    ISS_Payload_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.node_id = NODE_ID;

    while (1) {
        uint32_t now = HAL_GetTick();

        /* ============================================================
         *  TASK 1: PIR + Lux — every 500ms
         * ============================================================ */
        /* ============================================================
         *  TASK 1: Lux-only relay control — every 500ms
         * ============================================================ */
        if ((now - last_pir_poll_ms) >= PIR_POLL_MS) {
            last_pir_poll_ms = now;

            payload.lux        = BH1750_ReadLux(&hi2c1);
            payload.night_mode = (payload.lux < LUX_NIGHT_THRESHOLD) ? 1 : 0;

            /* Relay follows lux threshold directly — PIR ignored */
            if (payload.night_mode) {
                RELAY_ON();
                payload.relay_state = 1;
            } else {
                RELAY_OFF();
                payload.relay_state = 0;
            }

            /* PIR still read and reported in payload, but doesn't affect relay */
            payload.pir_state = (HAL_GPIO_ReadPin(PIR_GPIO_Port, PIR_Pin) == GPIO_PIN_SET) ? 1 : 0;

            /* Immediate LCD refresh on PIR state change (cosmetic only now) */
            if (payload.pir_state != prev_pir_state) {
                prev_pir_state = payload.pir_state;
                LCD_UpdateDisplay(&payload, last_tx_ok, tx_done_once);
                last_lcd_ms = now;
            }

            LED_TOGGLE();
        }

        /* ============================================================
         *  TASK 2: LCD scheduled refresh — every 3000ms
         * ============================================================ */
        if ((now - last_lcd_ms) >= LCD_UPDATE_MS) {
            last_lcd_ms = now;
            LCD_UpdateDisplay(&payload, last_tx_ok, tx_done_once);
        }

        /* ============================================================
         *  TASK 3: ADC (Voltage only) + Current logic + LoRa TX
         *          Runs every 15s
         *
         *  Order:
         *    1. TX first (3.3V rail most stable before ADC load)
         *    2. ADC voltage read (~100ms)
         *    3. Hardcode current based on measured voltage
         *    4. Compute power
         *    5. Refresh LCD + UART log
         * ============================================================ */
        if ((now - last_tx_ms) >= TX_INTERVAL_MS) {
            last_tx_ms = now;

            /* Step 1: TX first */
            last_tx_ok   = LoRa_Transmit((uint8_t *)&payload, sizeof(ISS_Payload_t));
            tx_done_once = true;

            /* Step 2: Read voltage */
            float raw_v = 0.0f;
            payload.voltage_rms = ADC_ReadVoltageRMS(&raw_v);

            /* Step 3 & 4: Hardcode current + compute power
             *   V > 225V → line is live → I = 10W / 230V = 0.043A
             *   V <= 225V → no supply / brownout → I = 0, P = 0
             */
            if (payload.voltage_rms > VOLTAGE_ACTIVE_THRESHOLD) {
                payload.current_rms = CURRENT_HARDCODE_A;
                payload.power_w     = payload.voltage_rms * CURRENT_HARDCODE_A;
            } else {
                payload.current_rms = 0.0f;
                payload.power_w     = 0.0f;
            }

            /* Step 5: Refresh display */
            LCD_UpdateDisplay(&payload, last_tx_ok, tx_done_once);
            last_lcd_ms = now;

            /* UART log */
            printf("========[ 15s TX Cycle ]========\r\n");
            printf(" Node    : 0x%02X\r\n",        payload.node_id);
            printf(" Mode    : %s\r\n",             payload.night_mode  ? "NIGHT" : "DAY");
            printf(" Lux     : %.2f lx\r\n",        payload.lux);
            printf(" PIR     : %d   Relay : %d\r\n",payload.pir_state, payload.relay_state);
            printf(" Voltage : %.1f V\r\n",         payload.voltage_rms);
            printf(" Current : %.3f A  [hardcoded: V>%.0fV]\r\n",
                   payload.current_rms, VOLTAGE_ACTIVE_THRESHOLD);
            printf(" Power   : %.2f W\r\n",         payload.power_w);
            printf(" LoRa TX : %s\r\n",             last_tx_ok ? "OK" : "FAIL");
            printf("--- Calibration ---\r\n");
            printf(" Raw V RMS counts : %.4f\r\n",  raw_v);
            printf(" V pin RMS (mV)   : %.2f mV\r\n",
                   (raw_v / ADC_RESOLUTION) * ADC_VREF * 1000.0f);
            printf(" => If meter=230V : VOLTAGE_CAL_FACTOR = %.2f\r\n",
                   230.0f / ((raw_v / ADC_RESOLUTION) * ADC_VREF));
            printf("================================\r\n\r\n");
        }
    }
}

/* =====================================================================
 *  SYSTEM CLOCK  HSE 25MHz → PLL → 96MHz
 * ===================================================================== */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 25;
    RCC_OscInitStruct.PLL.PLLN       = 192;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK) Error_Handler();
}

/* =====================================================================
 *  GPIO INIT
 *  PC13 — LED heartbeat (active LOW)
 *  PB0  — Relay via BC547 (HIGH = ON)
 *  PB1  — PIR input (Active HIGH + PULLDOWN)
 *  PA8  — LoRa NSS (software, idle HIGH)
 *  PA9  — LoRa RST (idle HIGH)
 *  PB4  — LoRa DIO0 (TxDone, SPI polled)
 *  NOTE: PA5 (SCT013) removed — no longer configured as analog input
 * ===================================================================== */
static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /* PC13 — LED */
    GPIO_InitStruct.Pin   = GPIO_PIN_13;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PB0 — Relay */
    GPIO_InitStruct.Pin   = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PB1 — PIR (Active HIGH, PULLDOWN) */
    GPIO_InitStruct.Pin   = GPIO_PIN_1;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PA8 — LoRa NSS */
    GPIO_InitStruct.Pin   = GPIO_PIN_8;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA9 — LoRa RST */
    GPIO_InitStruct.Pin   = GPIO_PIN_9;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB4 — LoRa DIO0 */
    GPIO_InitStruct.Pin   = GPIO_PIN_4;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* =====================================================================
 *  USART2  115200 8N1  PA2=TX  PA3=RX
 * ===================================================================== */
static void MX_USART2_UART_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

/* =====================================================================
 *  ADC1  PA4=CH4 (ZMPT101B voltage only)
 *  PA5 (SCT013) removed — not configured.
 *  ScanConvMode DISABLED — single channel, manual switch in ReadChannel.
 * ===================================================================== */
static void MX_ADC1_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA4 only — PA5 removed */
    GPIO_InitStruct.Pin  = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();
}

/* =====================================================================
 *  I2C1  PB6=SCL  PB7=SDA  400kHz  (BH1750 + LCD 0x27)
 * ===================================================================== */
static void MX_I2C1_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 400000;
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

/* =====================================================================
 *  SPI1  PB3=SCK  PA6=MISO  PA7=MOSI  6MHz (DIV16 from 96MHz PCLK2)
 * ===================================================================== */
static void MX_SPI1_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_3;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 10;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();
}

/* =====================================================================
 *  ERROR HANDLER
 * ===================================================================== */
void Error_Handler(void) {
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(100);
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) { }
#endif
