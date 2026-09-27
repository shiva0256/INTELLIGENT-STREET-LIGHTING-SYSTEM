/* lora_ra02.c --- LoRa Ra-02 (SX1278) SPI Driver (TX-only)
 * v2.5 fixes:
 *   - PA_DAC 0x87 → 0x84 (normal mode, not boost)
 *     0x87 boost mode caused malformed TX after first packet
 *   - 20ms PA power-down settle after TX_DONE
 *   - 10ms Standby settle after TX_DONE
 *   - TX power set to 2dBm for bench testing <3m
 *     Restore LoRa_SetTxPower(17) + PA_DAC=0x84 for deployment
 */

#include "lora_ra02.h"
#include <string.h>
#include <stdio.h>

static SPI_HandleTypeDef *_hspi;

#define NSS_LOW()  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET)
#define NSS_HIGH() HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET)
#define RST_LOW()  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET)
#define RST_HIGH() HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET)

/* =====================================================================
 *  SPI Helpers
 * ===================================================================== */
static void WriteReg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg | 0x80, val };
    NSS_LOW();
    HAL_SPI_Transmit(_hspi, buf, 2, 100);
    NSS_HIGH();
}

static uint8_t ReadReg(uint8_t reg) {
    uint8_t tx[2] = { reg & 0x7F, 0x00 };
    uint8_t rx[2] = { 0, 0 };
    NSS_LOW();
    HAL_SPI_TransmitReceive(_hspi, tx, rx, 2, 100);
    NSS_HIGH();
    return rx[1];
}

static void WriteFIFO(uint8_t *data, uint8_t len) {
    uint8_t cmd = SX1278_REG_FIFO | 0x80;
    NSS_LOW();
    HAL_SPI_Transmit(_hspi, &cmd, 1, 100);
    HAL_SPI_Transmit(_hspi, data, len, 100);
    NSS_HIGH();
}

static void HardReset(void) {
    NSS_HIGH();
    HAL_Delay(5);
    RST_LOW();
    HAL_Delay(20);
    RST_HIGH();
    HAL_Delay(500);
}

uint8_t LoRa_ReadRawReg(uint8_t reg) {
    return ReadReg(reg);
}

/* =====================================================================
 *  LoRa_Init
 * ===================================================================== */
bool LoRa_Init(SPI_HandleTypeDef *hspi) {
    _hspi = hspi;
    NSS_HIGH();
    HardReset();

    uint8_t ver = 0;
    for (int i = 0; i < 10; i++) {
        HAL_Delay(50);
        ver = ReadReg(SX1278_REG_VERSION);
        printf("[INIT] VER attempt %d = 0x%02X\r\n", i+1, ver);
        if (ver == 0x12) break;
    }
    if (ver != 0x12) {
        printf("[INIT] FAIL: version check failed\r\n");
        return false;
    }

    uint8_t om;

    // Step 1: FSK Sleep
    WriteReg(SX1278_REG_OP_MODE, 0x00);
    HAL_Delay(50);
    om = ReadReg(SX1278_REG_OP_MODE);
    printf("[INIT] After FSK Sleep OP_MODE = 0x%02X (want 0x00)\r\n", om);
    if (om != 0x00) {
        printf("[INIT] FAIL: FSK Sleep\r\n");
        return false;
    }

    // Step 2: LoRa Sleep
    WriteReg(SX1278_REG_OP_MODE, 0x80);
    HAL_Delay(50);
    om = ReadReg(SX1278_REG_OP_MODE);
    printf("[INIT] After LoRa Sleep OP_MODE = 0x%02X (want 0x80)\r\n", om);
    if (om != 0x80) {
        printf("[INIT] FAIL: LoRa mode bit\r\n");
        return false;
    }

    // Step 3: Configure registers
    LoRa_SetFrequency(433000000UL);
    WriteReg(SX1278_REG_FIFO_TX_BASE_ADDR, 0x00);
    WriteReg(SX1278_REG_FIFO_RX_BASE_ADDR, 0x00);
    WriteReg(SX1278_REG_LNA,               0x23);  // Max LNA gain
    WriteReg(SX1278_REG_MODEM_CONFIG1,     0x72);  // BW125 CR4/5 Explicit header
    WriteReg(SX1278_REG_MODEM_CONFIG2,     0x74);  // SF7 CRC on
    WriteReg(SX1278_REG_MODEM_CONFIG3,     0x04);  // LNA auto gain
    WriteReg(SX1278_REG_PREAMBLE_MSB,      0x00);
    WriteReg(SX1278_REG_PREAMBLE_LSB,      0x08);
    WriteReg(SX1278_REG_SYNC_WORD,         0x12);
    WriteReg(SX1278_REG_DIO_MAPPING1,      0x40);  // DIO0 = TxDone

    // BENCH TEST: 2dBm — prevents RX saturation at <3m
    // DEPLOYMENT: restore LoRa_SetTxPower(17)
    LoRa_SetTxPower(2);

    // Step 4: LoRa Standby
    WriteReg(SX1278_REG_OP_MODE, SX1278_MODE_LORA | SX1278_MODE_STDBY);
    HAL_Delay(50);
    om = ReadReg(SX1278_REG_OP_MODE);
    printf("[INIT] After LoRa Standby OP_MODE = 0x%02X (want 0x81)\r\n", om);
    if (om != 0x81) {
        printf("[INIT] FAIL: LoRa Standby\r\n");
        return false;
    }

    printf("[INIT] LoRa Ra-02 OK | ver=0x%02X | 433MHz SF7 BW125 CR4/5 | TX=2dBm\r\n", ver);
    return true;
}

/* =====================================================================
 *  LoRa_SetFrequency
 * ===================================================================== */
void LoRa_SetFrequency(uint32_t freq_hz) {
    uint64_t frf = ((uint64_t)freq_hz << 19) / 32000000UL;
    WriteReg(SX1278_REG_FRF_MSB, (uint8_t)(frf >> 16));
    WriteReg(SX1278_REG_FRF_MID, (uint8_t)(frf >>  8));
    WriteReg(SX1278_REG_FRF_LSB, (uint8_t)(frf      ));
}

/* =====================================================================
 *  LoRa_SetTxPower
 *  FIX: PA_DAC = 0x84 (normal mode)
 *       0x87 = boost mode, caused malformed TX after first packet
 * ===================================================================== */
void LoRa_SetTxPower(uint8_t level) {
    if (level > 17) level = 17;
    if (level < 2)  level = 2;
    WriteReg(SX1278_REG_PA_CONFIG, 0x80 | 0x70 | (uint8_t)(level - 2));
    WriteReg(SX1278_REG_PA_DAC,    0x84);  // FIX: normal mode, NOT 0x87 boost
}

/* =====================================================================
 *  LoRa_Transmit
 *  FIX: 20ms PA settle + 10ms Standby settle after TX_DONE
 * ===================================================================== */
bool LoRa_Transmit(uint8_t *data, uint8_t len) {

    // Standby
    WriteReg(SX1278_REG_OP_MODE, SX1278_MODE_LORA | SX1278_MODE_STDBY);
    HAL_Delay(50);

    // Clear IRQ with retry loop
    for (int retry = 0; retry < 5; retry++) {
        WriteReg(SX1278_REG_IRQ_FLAGS, 0xFF);
        HAL_Delay(10);
        uint8_t irq = ReadReg(SX1278_REG_IRQ_FLAGS);
        printf("[TX] IRQ clear attempt %d: 0x%02X\r\n", retry+1, irq);
        if (irq == 0x00) break;

        // ── PASTE HERE — exactly at retry == 4 ──────────────────
        if (retry == 4) {
            printf("[TX] FAIL: cannot clear IRQ — re-initialising\r\n");
            LoRa_Init(_hspi);
            HAL_Delay(100);
            // Force FIFO pointer to 0 after re-init
            WriteReg(SX1278_REG_FIFO_TX_BASE_ADDR, 0x00);
            WriteReg(SX1278_REG_FIFO_ADDR_PTR,     0x00);
            WriteReg(SX1278_REG_IRQ_FLAGS,          0xFF);
            HAL_Delay(10);
        }
        // ── END OF PASTE ─────────────────────────────────────────
    }

    // Reset FIFO pointers
    WriteReg(SX1278_REG_FIFO_TX_BASE_ADDR, 0x00);
    WriteReg(SX1278_REG_FIFO_ADDR_PTR,     0x00);
    HAL_Delay(5);

    // Load payload
    WriteFIFO(data, len);
    WriteReg(SX1278_REG_PAYLOAD_LEN, len);

    printf("[TX] PTR=0x%02X LEN=%d IRQ=0x%02X\r\n",
           ReadReg(SX1278_REG_FIFO_ADDR_PTR),
           len,
           ReadReg(SX1278_REG_IRQ_FLAGS));

    // TX
    WriteReg(SX1278_REG_OP_MODE, SX1278_MODE_LORA | SX1278_MODE_TX);
    HAL_Delay(10);

    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < 3000) {
        uint8_t irq = ReadReg(SX1278_REG_IRQ_FLAGS);
        if (irq & SX1278_IRQ_TX_DONE) {
            WriteReg(SX1278_REG_IRQ_FLAGS, 0xFF);
            HAL_Delay(20);
            WriteReg(SX1278_REG_OP_MODE,
                     SX1278_MODE_LORA | SX1278_MODE_STDBY);
            HAL_Delay(10);
            printf("[TX] Done OK\r\n");
            return true;
        }
        HAL_Delay(5);
    }

    printf("[TX] TIMEOUT\r\n");
    return false;
}
