/******************************************************************************
 *  File        : sd_spi.h (SDSC/SDHC support)
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

#ifndef __SD_SPI_H__
#define __SD_SPI_H__

#include "main.h"  // or your specific STM32 family header
#include <stdint.h>
#include "integer.h"
#include "diskio.h"

#define CMD0  	(0)
#define CMD8  	(8)
#define CMD9	(9)			/* SEND_CSD */
#define CMD12	(12)		/* STOP_TRANSMISSION */
#define CMD13	(13)		/* STSTUS */
#define CMD17 	(17)
#define CMD18	(18)		/* READ_MULTIPLE_BLOCK */
#define CMD24 	(24)
#define CMD25	(25)		/* WRITE_MULTIPLE_BLOCK */
#define CMD32	(32)		/* ERASE_ER_BLK_START */
#define CMD33	(33)		/* ERASE_ER_BLK_END */
#define CMD38	(38)		/* ERASE */
#define CMD55 	(55)
#define CMD58 	(58)
#define ACMD41 	(41)
#define ACMD13	(0x80+13)	/* SD_STATUS (SDC) */

#define FCLK_SLOW() { MODIFY_REG(SD_SPI_HANDLE.Instance->CR1, SPI_BAUDRATEPRESCALER_256, SPI_BAUDRATEPRESCALER_256); }	/* Set SCLK = slow, approx 280 KBits/s*/
#define FCLK_FAST() { MODIFY_REG(SD_SPI_HANDLE.Instance->CR1, SPI_BAUDRATEPRESCALER_256, SPI_BAUDRATEPRESCALER_8); }	/* Set SCLK = fast, approx 4.5 MBits/s */

/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC		0x01		/* MMC ver 3 */
#define CT_SD1		0x02		/* SD ver 1 */
#define CT_SD2		0x04		/* SD ver 2 */
#define CT_SDC		(CT_SD1|CT_SD2)	/* SD */
#define CT_BLOCK	0x08		/* Block addressing */

DRESULT SD_SPI_Init(BYTE pdrv);
DRESULT SD_ReadBlocks(BYTE pdrv, uint8_t *buff, uint32_t sector, uint32_t count);
DRESULT SD_WriteBlocks(BYTE pdrv, const uint8_t *buff, uint32_t sector, uint32_t count);
DRESULT SD_ioctl(BYTE drv, BYTE cmd, void *buff);
DSTATUS SD_status (BYTE drv);

//DSTATUS USER_Read_Spi(BYTE drv, uint8_t *buff, uint32_t sector, uint32_t count);
//DSTATUS USER_Write_Spi(BYTE drv, const uint8_t *buff, uint32_t sector, uint32_t count);
//DSTATUS USER_Ioctl_Spi(BYTE drv, BYTE cmd, void *buff);

#endif // __SD_SPI_H__
