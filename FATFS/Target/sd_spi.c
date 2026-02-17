/******************************************************************************
 *  File        : sd_spi.c (SDSC/SDHC support)
 *  Author      : ControllersTech
 *  Website     : https://controllerstech.com
 *  Date        : June 26, 2025
 *
 *  Description :
 *    This file is part of a custom STM32/Embedded tutorial series.
 *    For documentation, updates, and more examples, visit the website above.
 *
 *  Note :
 *    This code is written and maintained by ControllersTech.
 *    You are free to use and modify it for learning and development.
 ******************************************************************************/

#include "sd_spi.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include "diskio.h"

/***************************************************************
 * 🔧 USER-MODIFIABLE SECTION
 * You are free to edit anything below this line
 ***************************************************************/

#define USE_DMA 1

extern SPI_HandleTypeDef hspi1;
#define SD_SPI_HANDLE hspi1

#define SD_CS_LOW()     HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET)
#define SD_CS_HIGH()    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET)

/***************************************************************
 * 🚫 DO NOT MODIFY BELOW THIS LINE
 * Auto-generated/system-managed code. Changes may be lost.
 ***************************************************************/
static volatile DSTATUS Stat = STA_NOINIT;	/* Physical drive status */
BYTE CardType;								/* Card type flags */
static uint8_t sdhc = 0;

#if USE_DMA
volatile int dma_tx_done = 0;
volatile int dma_rx_done = 0;

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
	if (hspi == &SD_SPI_HANDLE) dma_tx_done = 1;
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
	if (hspi == &hspi1) dma_rx_done = 1;
}
#endif

static void SD_TransmitByte(uint8_t data) {
    HAL_SPI_Transmit(&SD_SPI_HANDLE, &data, 1, HAL_MAX_DELAY);
}

static uint8_t SD_ReceiveByte(void) {
    uint8_t dummy = 0xFF, data = 0;
    HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &dummy, &data, 1, HAL_MAX_DELAY);
    return data;
}

static uint8_t SD_TransmitBuffer(const uint8_t *buffer, uint16_t len) {
	HAL_StatusTypeDef res = HAL_ERROR;

#if USE_DMA
    dma_tx_done = 0;
    res = HAL_SPI_Transmit_DMA(&SD_SPI_HANDLE, (uint8_t *)buffer, len);
    while (!dma_tx_done);
#else
    res = HAL_SPI_Transmit(&SD_SPI_HANDLE, (uint8_t *)buffer, len, HAL_MAX_DELAY);
#endif
    return res != HAL_OK ? 1:0;
}

static uint8_t SD_ReceiveBuffer(uint8_t *buffer, uint16_t len) {
	HAL_StatusTypeDef res = HAL_OK;
#if USE_DMA
	static uint8_t tx_dummy[512];
    for (int i = 0; i < len; i++) tx_dummy[i] = 0xFF;  // Fill with 0xFF
    dma_rx_done = 0;
    res = HAL_SPI_TransmitReceive_DMA(&hspi1, tx_dummy, buffer, len);
    while (!dma_rx_done);
#else
    for (uint16_t i = 0; i < len; i++) {
        buffer[i] = SD_ReceiveByte();
    }
#endif
    return res != HAL_OK ? 1:0;
}

static DRESULT SD_WaitReady(uint32_t delay) {
    uint32_t timeout = HAL_GetTick() + delay;
    uint8_t resp;
    do {
        resp = SD_ReceiveByte();
        if (resp == 0xFF) return RES_OK;
    } while (HAL_GetTick() < timeout);
    return RES_ERROR;
}

static uint8_t SD_SendCommand(uint8_t cmd, uint32_t arg, uint8_t crc) {
    uint8_t response, retry = 0xFF;

    SD_WaitReady(500);
    SD_TransmitByte(0x40 | cmd);
    SD_TransmitByte(arg >> 24);
    SD_TransmitByte(arg >> 16);
    SD_TransmitByte(arg >> 8);
    SD_TransmitByte(arg);
    SD_TransmitByte(crc);

    do {
        response = SD_ReceiveByte();
    } while ((response & 0x80) && --retry);

    return response;
}

DRESULT SD_SPI_Init(BYTE pdrv) {
    uint8_t i, response;
    uint8_t r7[4];
    uint32_t retry;

    SD_CS_HIGH();
    for (i = 0; i < 10; i++) SD_TransmitByte(0xFF);

    SD_CS_LOW();
    response = SD_SendCommand(CMD0, 0, 0x95);
    SD_CS_HIGH();
    SD_TransmitByte(0xFF);
    if (response != 0x01) return RES_ERROR;

    SD_CS_LOW();
    response = SD_SendCommand(CMD8, 0x000001AA, 0x87);
    for (i = 0; i < 4; i++) r7[i] = SD_ReceiveByte();
    SD_CS_HIGH();
    SD_TransmitByte(0xFF);

    sdhc = 0;
    retry = HAL_GetTick() + 1000;
    if (response == 0x01 && r7[2] == 0x01 && r7[3] == 0xAA) {
        do {
            SD_CS_LOW();
            SD_SendCommand(CMD55, 0, 0xFF);
            response = SD_SendCommand(ACMD41, 0x40000000, 0xFF);
            SD_CS_HIGH();
            SD_TransmitByte(0xFF);
        } while (response != 0x00 && HAL_GetTick() < retry);

        if (response != 0x00) return RES_ERROR;

        SD_CS_LOW();
        response = SD_SendCommand(CMD58, 0, 0xFF);
        uint8_t ocr[4];
        for (i = 0; i < 4; i++) ocr[i] = SD_ReceiveByte();
        SD_CS_HIGH();
        if (ocr[0] & 0x40) sdhc = 1;
    } else {
        do {
            SD_CS_LOW();
            SD_SendCommand(CMD55, 0, 0xFF);
            response = SD_SendCommand(ACMD41, 0, 0xFF);
            SD_CS_HIGH();
            SD_TransmitByte(0xFF);
        } while (response != 0x00 && HAL_GetTick() < retry);
        if (response != 0x00) return RES_ERROR;
    }

    FCLK_FAST();

    Stat &= ~STA_NOINIT;	/* Clear STA_NOINIT flag */
    return RES_OK;
}

DRESULT SD_WriteBlocks(BYTE pdrv, const uint8_t *buff, uint32_t sector, uint32_t count) {
    if (!count) return RES_ERROR;

    if (!sdhc) sector *= 512;

    SD_CS_LOW();

    if (count == 1) {
        // Single block write
        if (SD_SendCommand(CMD24, sector, 0xFF) != 0x00) {
            SD_CS_HIGH();
            return RES_ERROR;
        }

        SD_TransmitByte(0xFE);  // Start single block token
        SD_TransmitBuffer(buff, 512);
        SD_TransmitByte(0xFF);  // dummy CRC
        SD_TransmitByte(0xFF);

        uint8_t resp = SD_ReceiveByte();
        if ((resp & 0x1F) != 0x05) {
            SD_CS_HIGH();
            return RES_ERROR;
        }

        while (SD_ReceiveByte() == 0);  // busy wait

    } else {
        // Multiple blocks write
        if (SD_SendCommand(CMD25, sector, 0xFF) != 0x00) {
            SD_CS_HIGH();
            return RES_ERROR;
        }

        while (count--) {
            SD_TransmitByte(0xFC);  // Start multi-block write token

            SD_TransmitBuffer((uint8_t *)buff, 512);
            SD_TransmitByte(0xFF);  // dummy CRC
            SD_TransmitByte(0xFF);

            uint8_t resp = SD_ReceiveByte();
            if ((resp & 0x1F) != 0x05) {
                SD_CS_HIGH();
                return RES_ERROR;
            }

            while (SD_ReceiveByte() == 0);  // busy wait
            buff += 512;
        }

        SD_TransmitByte(0xFD);  // STOP_TRAN token
        while (SD_ReceiveByte() == 0);  // busy wait
    }

    SD_CS_HIGH();
    SD_TransmitByte(0xFF);

    return RES_OK;
}

DRESULT SD_ReadBlocks(BYTE pdrv, uint8_t *buff, uint32_t sector, uint32_t count) {
    if (!count) return RES_ERROR;

    if (!sdhc) sector *= 512;

    SD_CS_LOW();

    if (count == 1) {
        // Single block read
        if (SD_SendCommand(CMD17, sector, 0xFF) != 0x00) {
            SD_CS_HIGH();
            return RES_ERROR;
        }

        uint8_t token;
        uint32_t timeout = HAL_GetTick() + 200;
        do {
            token = SD_ReceiveByte();
            if (token == 0xFE) break;
        } while (HAL_GetTick() < timeout);

        if (token != 0xFE) {
            SD_CS_HIGH();
            return RES_ERROR;
        }

        SD_ReceiveBuffer(buff, 512);
        SD_ReceiveByte();  // CRC
        SD_ReceiveByte();

    } else {
        // Multiple blocks read
        if (SD_SendCommand(CMD18, sector, 0xFF) != 0x00) {
            SD_CS_HIGH();
            return RES_ERROR;
        }

        while (count--) {
            uint8_t token;
            uint32_t timeout = HAL_GetTick() + 200;

            do {
                token = SD_ReceiveByte();
                if (token == 0xFE) break;
            } while (HAL_GetTick() < timeout);

            if (token != 0xFE) {
                SD_CS_HIGH();
                return RES_ERROR;
            }

            SD_ReceiveBuffer(buff, 512);
            SD_ReceiveByte();  // discard CRC
            SD_ReceiveByte();

            buff += 512;
        }

        SD_SendCommand(CMD12, 0, 0xFF);  // STOP_TRANSMISSION
    }

    SD_CS_HIGH();
    SD_TransmitByte(0xFF);  // Extra 8 clocks

    return RES_OK;
}

static DRESULT SD_GetSectorCount(void *buff)
{
    BYTE n, csd[16];
    DWORD csize;

    if ((SD_SendCommand(CMD9, 0, 0xFF) != 0) || !SD_ReceiveBuffer(csd, 16)) {
        return RES_ERROR;
    }

    if ((csd[0] >> 6) == 1) {   /* SDC ver 2.00 */
        csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
        *(DWORD*)buff = csize << 10;
    } else {                    /* SDC ver 1.XX or MMC ver 3 */
        n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
        csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
        *(DWORD*)buff = csize << (n - 9);
    }

    return RES_OK;
}

static DRESULT SD_GetBlockSize(void *buff)
{
    BYTE n, csd[16];

    if (CardType & CT_SD2) {    /* SDC ver 2.00 */
        if (SD_SendCommand(ACMD13, 0, 0xFF) != 0) {
            return RES_ERROR;
        }

        SD_TransmitByte(0xFF);
        if (!SD_ReceiveBuffer(csd, 16)) {
            return RES_ERROR;
        }

        for (n = 64 - 16; n; n--) {
            SD_TransmitByte(0xFF);   /* Purge trailing data */
        }
        *(DWORD*)buff = 16UL << (csd[10] >> 4);

    } else {                    /* SDC ver 1.XX or MMC */
        if ((SD_SendCommand(CMD9, 0, 0xFF) != 0) || !SD_ReceiveBuffer(csd, 16)) {
            return RES_ERROR;
        }

        if (CardType & CT_SD1) {    /* SDC ver 1.XX */
            *(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
        } else {                    /* MMC */
            *(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
        }
    }

    return RES_OK;
}

static DRESULT SD_TrimSectors(BYTE drv, void *buff)
{
    BYTE csd[16];
    DWORD *dp, st, ed;

    if (!(CardType & CT_SDC)) {
        return RES_ERROR;                           /* Check if the card is SDC */
    }

    if (SD_ioctl(drv, MMC_GET_CSD, csd)) {
        return RES_ERROR;                           /* Get CSD */
    }

    if (!(csd[0] >> 6) && !(csd[10] & 0x40)) {
        return RES_ERROR;                           /* Check if sector erase can be applied */
    }

    dp = buff;
    st = dp[0];
    ed = dp[1];

    if (!(CardType & CT_BLOCK)) {
        st *= 512;
        ed *= 512;
    }

    if (SD_SendCommand(CMD32, st, 0xFF) == 0 &&
        SD_SendCommand(CMD33, ed, 0xFF) == 0 &&
        SD_SendCommand(CMD38, 0, 0xFF) == 0 &&
        SD_WaitReady(30000)) {
        return RES_OK;
    }

    return RES_ERROR;
}

DRESULT SD_ioctl(BYTE drv, BYTE cmd, void *buff)
{
    DRESULT res = RES_ERROR;

    if (drv) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    SD_CS_LOW();

    switch (cmd)
    {
    case CTRL_SYNC:
        res = RES_OK;
        break;

    case GET_SECTOR_COUNT:
        res = SD_GetSectorCount(buff);
        break;

    case GET_BLOCK_SIZE:
        res = SD_GetBlockSize(buff);
        break;

    case GET_SECTOR_SIZE:
        *(WORD*)buff = 512;
        res = RES_OK;
        break;

    case CTRL_TRIM:
        res = SD_TrimSectors(drv, buff);
        break;

    default:
        res = RES_PARERR;
        break;
    }

    SD_CS_HIGH();

    return res;
}

inline DSTATUS SD_status (BYTE drv)
{
	if (drv) return STA_NOINIT;		/* Supports only drive 0 */

	return Stat;	/* Return disk status */
}

//SD_Status SD_ReadBlocks(uint8_t *buff, uint32_t sector, uint32_t count) {
//    if (!count) return RES_ERROR;
//
//    if (count == 1) {
//    	if (!sdhc) sector *= 512;
//        SD_CS_LOW();
//        if (SD_SendCommand(CMD17, sector, 0xFF) != 0x00) {
//            SD_CS_HIGH();
//            return RES_ERROR;
//        }
//
//        uint8_t token;
//        uint32_t timeout = HAL_GetTick() + 200;
//        do {
//            token = SD_ReceiveByte();
//            if (token == 0xFE) break;
//        } while (HAL_GetTick() < timeout);
//        if (token != 0xFE) {
//            SD_CS_HIGH();
//            return RES_ERROR;
//        }
//
//        SD_ReceiveBuffer(buff, 512);
//        SD_ReceiveByte();  // CRC
//        SD_ReceiveByte();
//        SD_CS_HIGH();
//        SD_TransmitByte(0xFF);
//        return RES_OK;
//    } else {
//        return SD_ReadMultiBlocks(buff, sector, count);
//    }
//}
//
//SD_Status SD_ReadMultiBlocks(uint8_t *buff, uint32_t sector, uint32_t count) {
//    if (!count) return RES_ERROR;
//    if (!sdhc) sector *= 512;
//
//    SD_CS_LOW();
//    if (SD_SendCommand(18, sector, 0xFF) != 0x00) {
//        SD_CS_HIGH();
//        return RES_ERROR;
//    }
//
//    while (count--) {
//        uint8_t token;
//        uint32_t timeout = HAL_GetTick() + 200;
//
//        do {
//            token = SD_ReceiveByte();
//            if (token == 0xFE) break;
//        } while (HAL_GetTick() < timeout);
//
//        if (token != 0xFE) {
//            SD_CS_HIGH();
//            return RES_ERROR;
//        }
//
//        SD_ReceiveBuffer(buff, 512);
//        SD_ReceiveByte();  // discard CRC
//        SD_ReceiveByte();
//
//        buff += 512;
//    }
//
//    SD_SendCommand(12, 0, 0xFF);  // STOP_TRANSMISSION
//    SD_CS_HIGH();
//    SD_TransmitByte(0xFF); // Extra 8 clocks
//
//    return RES_OK;
//}

//SD_Status SD_WriteBlocks(const uint8_t *buff, uint32_t sector, uint32_t count) {
//    if (!count) return RES_ERROR;
//
//    if (count == 1) {
//    	if (!sdhc) sector *= 512;
//        SD_CS_LOW();
//        if (SD_SendCommand(CMD24, sector, 0xFF) != 0x00) {
//            SD_CS_HIGH();
//            return RES_ERROR;
//        }
//
//        SD_TransmitByte(0xFE);
//        SD_TransmitBuffer(buff, 512);
//        SD_TransmitByte(0xFF);
//        SD_TransmitByte(0xFF);
//
//        uint8_t resp = SD_ReceiveByte();
//        if ((resp & 0x1F) != 0x05) {
//            SD_CS_HIGH();
//            return RES_ERROR;
//        }
//
//        while (SD_ReceiveByte() == 0);
//        SD_CS_HIGH();
//        SD_TransmitByte(0xFF);
//
//        return RES_OK;
//    } else {
//        return SD_WriteMultiBlocks(buff, sector, count);
//    }
//}
//
//SD_Status SD_WriteMultiBlocks(const uint8_t *buff, uint32_t sector, uint32_t count) {
//    if (!count) return RES_ERROR;
//    if (!sdhc) sector *= 512;
//
//    SD_CS_LOW();
//    if (SD_SendCommand(25, sector, 0xFF) != 0x00) {
//        SD_CS_HIGH();
//        return RES_ERROR;
//    }
//
//    while (count--) {
//        SD_TransmitByte(0xFC);  // Start multi-block write token
//
//        SD_TransmitBuffer((uint8_t *)buff, 512);
//        SD_TransmitByte(0xFF);  // dummy CRC
//        SD_TransmitByte(0xFF);
//
//        uint8_t resp = SD_ReceiveByte();
//        if ((resp & 0x1F) != 0x05) {
//            SD_CS_HIGH();
//            return RES_ERROR;
//        }
//
//        while (SD_ReceiveByte() == 0);  // busy wait
//        buff += 512;
//    }
//
//    SD_TransmitByte(0xFD);  // STOP_TRAN token
//    while (SD_ReceiveByte() == 0);  // busy wait
//
//    SD_CS_HIGH();
//    SD_TransmitByte(0xFF);
//
//    return RES_OK;
//}
