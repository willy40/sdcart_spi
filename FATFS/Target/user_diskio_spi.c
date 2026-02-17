/**
 ******************************************************************************
  * @file    user_diskio_spi.c
  * @brief   This file contains the implementation of the user_diskio_spi FatFs
  *          driver.
  ******************************************************************************
  * Portions copyright (C) 2014, ChaN, all rights reserved.
  * Portions copyright (C) 2017, kiwih, all rights reserved.
  *
  * This software is a free software and there is NO WARRANTY.
  * No restriction on use. You can use, modify and redistribute it for
  * personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
  * Redistributions of source code must retain the above copyright notice.
  *
  ******************************************************************************
  */

//This code was ported by kiwih from a copywrited (C) library written by ChaN
//available at http://elm-chan.org/fsw/ff/ffsample.zip
//(text at http://elm-chan.org/fsw/ff/00index_e.html)

//This file provides the FatFs driver functions and SPI code required to manage
//an SPI-connected MMC or compatible SD card with FAT

//It is designed to be wrapped by a cubemx generated user_diskio.c file.

#include "stm32f4xx_hal.h" /* Provide the low-level HAL functions */
#include "user_diskio_spi.h"
#include <string.h>

//Make sure you set #define SD_SPI_HANDLE as some hspix in main.h
//Make sure you set #define SD_CS_GPIO_Port as some GPIO port in main.h
//Make sure you set #define SD_CS_Pin as some GPIO pin in main.h
extern SPI_HandleTypeDef SD_SPI_HANDLE;

/* Function prototypes */

#if USE_DMA_TRANSFER
static volatile uint8_t dma_buffers_initialized=0;
/* Bufor dummy TX wypełniony 0xFF przy kompilacji (C99 designated initializer) */
/* Umieszczony w sekcji .dma_buffer, wyrównany do 32 bajtów dla cache line (F7) */

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t dma_tx_dummy[514] = {
    [0 ... 513] = 0xFF
};

/* Bufor tymczasowy RX dla 512 + 2 CRC */
__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t dma_rx_tmp[514];

volatile uint8_t spi_dma_done = 0;

/* HAL Callback wywoływany po zakończeniu DMA TxRx */
//void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
//{
//    if (hspi == &SD_SPI_HANDLE) {
//        spi_dma_done = 1;
//    }
//}
//
///* HAL Callback wywoływany po zakończeniu DMA Tx */
//void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
//{
//    if (hspi == &SD_SPI_HANDLE) {
//        spi_dma_done = 1;
//    }
//}
#endif

static volatile DSTATUS Stat = STA_NOINIT;	/* Physical drive status */

static
BYTE CardType;			/* Card type flags */

uint32_t spiTimerTickStart;
uint32_t spiTimerTickDelay;

void SPI_Timer_On(uint32_t waitTicks)
{
    spiTimerTickStart = HAL_GetTick();
    spiTimerTickDelay = waitTicks;
}

uint8_t SPI_Timer_Status()
{
    return ((HAL_GetTick() - spiTimerTickStart) < spiTimerTickDelay);
}

/*-----------------------------------------------------------------------*/
/* SPI controls (Platform dependent)                                     */
/*-----------------------------------------------------------------------*/

/* Exchange a byte */
static
BYTE xchg_spi (BYTE dat)
{
	BYTE rxDat;
    HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &dat, &rxDat, 1, 50);
    return rxDat;
}
#if _USE_WRITE && !USE_DMA_TRANSFER
/* Receive multiple byte */
static void rcvr_spi_multi (BYTE *buff, UINT size)
{
	for(UINT i=0; i<size; i++)
	{
		*(buff+i) = xchg_spi(0xFF);
	}
}

static void xmit_spi_multi (const BYTE *buff, UINT size)
{
	HAL_SPI_Transmit(&SD_SPI_HANDLE, buff, size, HAL_MAX_DELAY);
}
#endif

/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static int wait_ready (UINT wt)
{
	BYTE d;
	//wait_ready needs its own timer, unfortunately, so it can't use the
	//spi_timer functions
	uint32_t waitSpiTimerTickStart;
	uint32_t waitSpiTimerTickDelay;

	waitSpiTimerTickStart = HAL_GetTick();
	waitSpiTimerTickDelay = (uint32_t)wt;
	do {
		d = xchg_spi(0xFF);
		/* This loop takes a time. Insert rot_rdq() here for multitask envilonment. */
	} while (d != 0xFF && ((HAL_GetTick() - waitSpiTimerTickStart) < waitSpiTimerTickDelay));	/* Wait for card goes ready or timeout */

	return (d == 0xFF) ? 1 : 0;
}

/*-----------------------------------------------------------------------*/
/* Despiselect card and release SPI                                         */
/*-----------------------------------------------------------------------*/

static void despiselect (void)
{
	CS_HIGH();		/* Set CS# high */
	xchg_spi(0xFF);	/* Dummy clock (force DO hi-z for multiple slave SPI) */
}

/*-----------------------------------------------------------------------*/
/* Select card and wait for ready                                        */
/*-----------------------------------------------------------------------*/

static int spiselect (void)
{
	CS_LOW();		/* Set CS# low */
	xchg_spi(0xFF);	/* Dummy clock (force DO enabled) */
	if (wait_ready(500)) return 1;	/* Wait for card ready */

	despiselect();
	return 0;	/* Timeout */
}

/*-----------------------------------------------------------------------*/
/* Receive a data packet from the MMC                                    */
/*-----------------------------------------------------------------------*/

#if USE_DMA_TRANSFER
static void dma_buffers_init(void)
{
    volatile uint32_t *p32 = (volatile uint32_t*)dma_tx_dummy;

    /* 514 bajtów = 128 * 4 + 2 bajty */
    for (uint32_t i = 0; i < (514 / 4); i++) {  // 128 iteracji (512 bajtów)
        p32[i] = 0xFFFFFFFF;
    }

    /* Ostatnie 2 bajty */
    dma_tx_dummy[512] = 0xFF;
    dma_tx_dummy[513] = 0xFF;

    dma_buffers_initialized = 1;
}

static int rcvr_datablock_dma(BYTE *buff, UINT size)
{
	BYTE token;

	if (!dma_buffers_initialized) {
		dma_buffers_init();
	}

	SPI_Timer_On(200);
	do {
		token = xchg_spi(0xFF);
	} while ((token == 0xFF) && SPI_Timer_Status());

	if(token != 0xFE) return 0;

#if defined(STM32F7) || defined(STM32H7)
	/* F7/H7: Invalidate D-Cache przed odbiorem DMA */
	SCB_InvalidateDCache_by_Addr((uint32_t*)dma_rx_tmp, sizeof(dma_rx_tmp));
#endif

	/* DMA transfer: 512 + 2 CRC */
	spi_dma_done = 0;
	if (HAL_SPI_TransmitReceive_DMA(&SD_SPI_HANDLE, dma_tx_dummy, buff, size) != HAL_OK)
	{
		return 0;
	}

	uint32_t timeout=1000;
	while (__HAL_SPI_GET_FLAG(&hspi1, SPI_FLAG_BSY))
	{
	    if (timeout-- == 0)
	    {
	        // timeout error
			HAL_SPI_Abort(&SD_SPI_HANDLE);
			return 0;
	    }
	}

	xchg_spi(0xFF); xchg_spi(0xFF);

#if defined(STM32F7) || defined(STM32H7)
	/* F7/H7: Invalidate ponownie po DMA */
	SCB_InvalidateDCache_by_Addr((uint32_t*)dma_rx_tmp, sizeof(dma_rx_tmp));
#endif

	/* Skopiuj 512 bajtów danych do docelowego buff */
	//memcpy(buff, dma_rx_tmp, 512);

	return 1;
}

#if _USE_WRITE
static int xmit_datablock_dma (const BYTE *buff, BYTE token)
{
	BYTE resp;

	if (!wait_ready(500)) return 0;

	xchg_spi(token);

	if (token != 0xFD)
	{
#if defined(STM32F7) || defined(STM32H7)
		/* F7/H7: Clean D-Cache przed wysłaniem DMA */
		SCB_CleanDCache_by_Addr((uint32_t*)buff, 512);
#endif
		/* Wyślij 512 bajtów przez DMA */
		spi_dma_done = 0;
		if (HAL_SPI_Transmit_DMA(&SD_SPI_HANDLE, (uint8_t*)buff, 512) != HAL_OK)
		{
			return 0;
		}

		/* Czekaj na zakończenie DMA */
		uint32_t wait = 1000;
		while (!spi_dma_done && wait)
		{
			HAL_Delay(1);
			--wait;
		}

		if (!spi_dma_done)
		{
			HAL_SPI_Abort(&SD_SPI_HANDLE);
			return 0;
		}

		xchg_spi(0xFF);
		xchg_spi(0xFF);
		resp = xchg_spi(0xFF);

		if ((resp & 0x1F) != 0x05) return 0;
	}

	return 1;
}
#endif
#else
static int rcvr_datablock (BYTE *buff, UINT size)
{
	BYTE token;

	SPI_Timer_On(200);
	do {							/* Wait for DataStart token in timeout of 200ms */
		token = xchg_spi(0xFF);
		/* This loop will take a time. Insert rot_rdq() here for multitask envilonment. */
	} while ((token == 0xFF) && SPI_Timer_Status());
	if(token != 0xFE) return 0;		/* Function fails if invalid DataStart token or timeout */

	rcvr_spi_multi(buff, size);		/* Store trailing data to the buffer */
	xchg_spi(0xFF); xchg_spi(0xFF);			/* Discard CRC */

	return 1;						/* Function succeeded */
}

#if _USE_WRITE
static int xmit_datablock (const BYTE *buff, BYTE token)
{
	BYTE resp;

	if (!wait_ready(500)) return 0;		/* Wait for card ready */

	xchg_spi(token);					/* Send token */
	if (token != 0xFD)
	{				/* Send data if token is other than StopTran */
		xmit_spi_multi(buff, 512);		/* Data */
		xchg_spi(0xFF); xchg_spi(0xFF);	/* Dummy CRC */

		resp = xchg_spi(0xFF);				/* Receive data resp */
		if ((resp & 0x1F) != 0x05) return 0;	/* Function fails if the data packet was not accepted */
	}
	return 1;
}
#endif
#endif


/*-----------------------------------------------------------------------*/
/* Send a command packet to the MMC                                      */
/*-----------------------------------------------------------------------*/

static
BYTE send_cmd (BYTE cmd, DWORD arg)
{
	BYTE n, res;

	if (cmd & 0x80)
	{	/* Send a CMD55 prior to ACMD<n> */
		cmd &= 0x7F;
		res = send_cmd(CMD55, 0);
		if (res > 1) return res;
	}

	/* Select the card and wait for ready except to stop multiple block read */
	if (cmd != CMD12)
	{
		despiselect();
		if (!spiselect()) return 0xFF;
	}

	/* Send command packet */
	xchg_spi(0x40 | cmd);				/* Start + command index */
	xchg_spi((BYTE)(arg >> 24));		/* Argument[31..24] */
	xchg_spi((BYTE)(arg >> 16));		/* Argument[23..16] */
	xchg_spi((BYTE)(arg >> 8));			/* Argument[15..8] */
	xchg_spi((BYTE)arg);				/* Argument[7..0] */
	n = 0x01;							/* Dummy CRC + Stop */
	if (cmd == CMD0) n = 0x95;			/* Valid CRC for CMD0(0) */
	if (cmd == CMD8) n = 0x87;			/* Valid CRC for CMD8(0x1AA) */
	xchg_spi(n);

	/* Receive command resp */
	if (cmd == CMD12) xchg_spi(0xFF);	/* Diacard following one byte when CMD12 */
	n = 10;								/* Wait for response (10 bytes max) */
	do {
		res = xchg_spi(0xFF);
	} while ((res & 0x80) && --n);

	return res;							/* Return received response */
}

/*--------------------------------------------------------------------------

   Public FatFs Functions (wrapped in user_diskio.c)

---------------------------------------------------------------------------*/

//The following functions are defined as inline because they aren't the functions that
//are passed to FatFs - they are wrapped by autogenerated (non-inline) cubemx template
//code.
//If you do not wish to use cubemx, remove the "inline" from these functions here
//and in the associated .h

/*-----------------------------------------------------------------------*/
/* Initialize disk drive                                                 */
/*-----------------------------------------------------------------------*/

inline DSTATUS USER_SPI_initialize (BYTE drv)
{
	BYTE n, cmd, ty, ocr[4];

	if (drv != 0) return STA_NOINIT;		/* Supports only drive 0 */
	//assume SPI already init init_spi();	/* Initialize SPI */

	if (Stat & STA_NODISK) return Stat;	/* Is card existing in the soket? */
	FCLK_SLOW();

	for (n = 10; n; n--) xchg_spi(0xFF);	/* Send 80 dummy clocks */

	ty = 0;
	if (send_cmd(CMD0, 0) == 1)
	{			/* Put the card SPI/Idle state */
		SPI_Timer_On(1000);												/* Initialization timeout = 1 sec */
		if (send_cmd(CMD8, 0x1AA) == 1)
		{	/* SDv2? */
			for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);			/* Get 32 bit return value of R7 resp */
			if (ocr[2] == 0x01 && ocr[3] == 0xAA) {						/* Is the card supports vcc of 2.7-3.6V? */
				while (SPI_Timer_Status() && send_cmd(ACMD41, 1UL << 30)) ;	/* Wait for end of initialization with ACMD41(HCS) */
				if (SPI_Timer_Status() && send_cmd(CMD58, 0) == 0)
				{														/* Check CCS bit in the OCR */
					for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
					ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;	/* Card id SDv2 */
				}
			}
		}
		else
		{	/* Not SDv2 card */
			if (send_cmd(ACMD41, 0) <= 1)
			{								/* SDv1 or MMC? */
				ty = CT_SD1; cmd = ACMD41;	/* SDv1 (ACMD41(0)) */
			} else {
				ty = CT_MMC; cmd = CMD1;	/* MMCv3 (CMD1(0)) */
			}
			while (SPI_Timer_Status() && send_cmd(cmd, 0));		/* Wait for end of initialization */
			if (!SPI_Timer_Status() || send_cmd(CMD16, 512) != 0)	/* Set block length: 512 */
				ty = 0;
		}
	}

	CardType = ty;	/* Card type */
	despiselect();

	if (ty)
	{						/* OK */
		FCLK_FAST();
		HAL_Delay(1);		/* Set fast clock */
		Stat &= ~STA_NOINIT;	/* Clear STA_NOINIT flag */
	}
	else
	{						/* Failed */
		Stat = STA_NOINIT;
	}

	return Stat;
}

/*-----------------------------------------------------------------------*/
/* Get disk status                                                       */
/*-----------------------------------------------------------------------*/

inline DSTATUS USER_SPI_status (BYTE drv)
{
	if (drv) return STA_NOINIT;		/* Supports only drive 0 */

	return Stat;	/* Return disk status */
}

/*-----------------------------------------------------------------------*/
/* Read sector(s)                                                        */
/*-----------------------------------------------------------------------*/

inline DRESULT USER_SPI_read (BYTE drv, BYTE *buff, DWORD sector, UINT count)
{
	if (drv || !count) return RES_PARERR;		/* Check parameter */
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check if drive is ready */

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* LBA ot BA conversion (byte addressing cards) */

	if (count == 1)
	{											/* Single sector read */
		if ((send_cmd(CMD17, sector) == 0) && RCVR_DATABLOCK(buff, 512)) /* READ_SINGLE_BLOCK */
		{
			count = 0;
		}
	}
	else
	{											/* Multiple sector read */
		if (send_cmd(CMD18, sector) == 0)
		{										/* READ_MULTIPLE_BLOCK */
			do
			{
				if (!RCVR_DATABLOCK(buff, 512)) break;
				buff += 512;
			} while (--count);
			send_cmd(CMD12, 0);				/* STOP_TRANSMISSION */
		}
	}

	despiselect();

	return count ? RES_ERROR : RES_OK;	/* Return result */
}

/*-----------------------------------------------------------------------*/
/* Write sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if _USE_WRITE
inline DRESULT USER_SPI_write (BYTE drv, const BYTE *buff, DWORD sector, UINT count)
{
	if (drv || !count) return RES_PARERR;		/* Check parameter */
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check drive status */
	if (Stat & STA_PROTECT) return RES_WRPRT;	/* Check write protect */

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* LBA ==> BA conversion (byte addressing cards) */

	if (count == 1)
	{											/* Single sector write */
		if ((send_cmd(CMD24, sector) == 0)		/* WRITE_BLOCK */
			&& XMIT_DATABLOCK(buff, 0xFE))
		{
			count = 0;
		}
	}
	else
	{											/* Multiple sector write */
		if (CardType & CT_SDC) send_cmd(ACMD23, count);	/* Predefine number of sectors */

		if (send_cmd(CMD25, sector) == 0)
		{										/* WRITE_MULTIPLE_BLOCK */
			do
			{
				if (!XMIT_DATABLOCK(buff, 0xFC)) break;
				buff += 512;
			} while (--count);

			if (!XMIT_DATABLOCK(0, 0xFD)) count = 1;	/* STOP_TRAN token */
		}
	}

	despiselect();

	return count ? RES_ERROR : RES_OK;	/* Return result */
}
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous drive controls other than data read/write               */
/*-----------------------------------------------------------------------*/

#if _USE_IOCTL
inline DRESULT USER_SPI_ioctl (BYTE drv, BYTE cmd, void *buff) {
	DRESULT res;
	BYTE n, csd[16];
	DWORD *dp, st, ed, csize;

	if (drv) return RES_PARERR;					/* Check parameter */
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check if drive is ready */

	res = RES_ERROR;

	switch (cmd)
	{
	case CTRL_SYNC :		/* Wait for end of internal write process of the drive */
		if (spiselect()) res = RES_OK;
		break;

	case GET_SECTOR_COUNT :	/* Get drive capacity in unit of sector (DWORD) */
		if ((send_cmd(CMD9, 0) == 0) && RCVR_DATABLOCK(csd, 16)) {
			if ((csd[0] >> 6) == 1) {	/* SDC ver 2.00 */
				csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
				*(DWORD*)buff = csize << 10;
			} else {					/* SDC ver 1.XX or MMC ver 3 */
				n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
				csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
				*(DWORD*)buff = csize << (n - 9);
			}
			res = RES_OK;
		}
		break;

	case GET_BLOCK_SIZE :	/* Get erase block size in unit of sector (DWORD) */
		if (CardType & CT_SD2) {	/* SDC ver 2.00 */
			if (send_cmd(ACMD13, 0) == 0) {	/* Read SD status */
				xchg_spi(0xFF);
				if (RCVR_DATABLOCK(csd, 16)) {				/* Read partial block */
					for (n = 64 - 16; n; n--) xchg_spi(0xFF);	/* Purge trailing data */
					*(DWORD*)buff = 16UL << (csd[10] >> 4);
					res = RES_OK;
				}
			}
		} else {					/* SDC ver 1.XX or MMC */
			if ((send_cmd(CMD9, 0) == 0) && RCVR_DATABLOCK(csd, 16)) {	/* Read CSD */
				if (CardType & CT_SD1) {	/* SDC ver 1.XX */
					*(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
				} else {					/* MMC */
					*(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
				}
				res = RES_OK;
			}
		}
		break;
	case GET_SECTOR_SIZE:
		*(WORD*) buff = 512;
		res = RES_OK;
		break;

	case CTRL_TRIM :	/* Erase a block of sectors (used when _USE_ERASE == 1) */
		if (!(CardType & CT_SDC)) break;				/* Check if the card is SDC */
		if (USER_SPI_ioctl(drv, MMC_GET_CSD, csd)) break;	/* Get CSD */
		if (!(csd[0] >> 6) && !(csd[10] & 0x40)) break;	/* Check if sector erase can be applied to the card */
		dp = buff; st = dp[0]; ed = dp[1];				/* Load sector block */
		if (!(CardType & CT_BLOCK))
		{
			st *= 512; ed *= 512;
		}

		if (send_cmd(CMD32, st) == 0 && send_cmd(CMD33, ed) == 0 && send_cmd(CMD38, 0) == 0 && wait_ready(30000))
		{	/* Erase sector block */
			res = RES_OK;	/* FatFs does not check result of this command */
		}
		break;

	default:
		res = RES_PARERR;
	}

	despiselect();

	return res;
}
#endif
