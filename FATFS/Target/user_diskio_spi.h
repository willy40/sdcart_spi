/**
 ******************************************************************************
  * @file    user_diskio_spi.h
  * @brief   This file contains the common defines and functions prototypes for
  *          the user_diskio_spi driver implementation with DMA support
  ******************************************************************************
  * Portions copyright (C) 2014, ChaN, all rights reserved.
  * Portions copyright (C) 2017, kiwih, all rights reserved.
  *
  * This software is a free software and there is NO WARRANTY.
  * No restriction on use. You can use, modify and redistribute it for
  * personal, non-profit or commercial products68 1050 1504 1000 0092 5622 3752 UNDER YOUR RESPONSIBILITY.
  * Redistributions of source code must retain the above copyright notice.
  *
  ******************************************************************************
  */

#ifndef _USER_DISKIO_SPI_H
#define _USER_DISKIO_SPI_H

#include "integer.h"
#include "diskio.h"
#include "ff_gen_drv.h"

/* ===== DMA Support ===== */
/* Declare DMA flag as extern if other modules need to check DMA status */
#ifndef USE_DMA_TRANSFER
	#define USE_DMA_TRANSFER 1  /* Default: use DMA (can be overridden in user_diskio_spi.c) */
#endif

#if USE_DMA_TRANSFER
    #define RCVR_DATABLOCK(buff, size)   rcvr_datablock_dma((buff), (size))
	#define XMIT_DATABLOCK(buff, size)   xmit_datablock_dma((buff), (size))
#else
    #define RCVR_DATABLOCK(buff, size)   rcvr_datablock((buff), (size))
	#define XMIT_DATABLOCK(buff, size)   xmit_datablock((buff), (size))
#endif

//(Note that the _256 is used as a mask to clear the prescalar bits as it provides binary 111 in the correct position)
#define FCLK_SLOW() { MODIFY_REG(SD_SPI_HANDLE.Instance->CR1, SPI_BAUDRATEPRESCALER_256, SPI_BAUDRATEPRESCALER_256); }	/* Set SCLK = slow, approx 280 KBits/s*/
#define FCLK_FAST() { MODIFY_REG(SD_SPI_HANDLE.Instance->CR1, SPI_BAUDRATEPRESCALER_256, SPI_BAUDRATEPRESCALER_16); }	/* Set SCLK = fast, approx 4.5 MBits/s */

#define CS_HIGH()	{HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);}
#define CS_LOW()	{HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);}

/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/
/* MMC/SD command */
#define CMD0	(0)			/* GO_IDLE_STATE */
#define CMD1	(1)			/* SEND_OP_COND (MMC) */
#define	ACMD41	(0x80+41)	/* SEND_OP_COND (SDC) */
#define CMD8	(8)			/* SEND_IF_COND */
#define CMD9	(9)			/* SEND_CSD */
#define CMD10	(10)		/* SEND_CID */
#define CMD12	(12)		/* STOP_TRANSMISSION */
#define ACMD13	(0x80+13)	/* SD_STATUS (SDC) */
#define CMD16	(16)		/* SET_BLOCKLEN */
#define CMD17	(17)		/* READ_SINGLE_BLOCK */
#define CMD18	(18)		/* READ_MULTIPLE_BLOCK */
#define CMD23	(23)		/* SET_BLOCK_COUNT (MMC) */
#define	ACMD23	(0x80+23)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24	(24)		/* WRITE_BLOCK */
#define CMD25	(25)		/* WRITE_MULTIPLE_BLOCK */
#define CMD32	(32)		/* ERASE_ER_BLK_START */
#define CMD33	(33)		/* ERASE_ER_BLK_END */
#define CMD38	(38)		/* ERASE */
#define CMD55	(55)		/* APP_CMD */
#define CMD58	(58)		/* READ_OCR */

/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC		0x01		/* MMC ver 3 */
#define CT_SD1		0x02		/* SD ver 1 */
#define CT_SD2		0x04		/* SD ver 2 */
#define CT_SDC		(CT_SD1|CT_SD2)	/* SD */
#define CT_BLOCK	0x08		/* Block addressing */

extern DSTATUS USER_SPI_initialize (BYTE pdrv);
extern DSTATUS USER_SPI_status (BYTE pdrv);
extern DRESULT USER_SPI_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);

#if _USE_WRITE == 1
extern DRESULT USER_SPI_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif

#if _USE_IOCTL == 1
extern DRESULT USER_SPI_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif

#if USE_DMA_TRANSFER
  /* Exported DMA flag for external modules (if needed for debugging/monitoring) */
extern volatile uint8_t spi_dma_done;

  /* HAL SPI DMA Callbacks - declared here so they can be placed in stm32f4xx_it.c or main.c if needed */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi);
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi);
#endif

#endif /* _USER_DISKIO_SPI_H */
