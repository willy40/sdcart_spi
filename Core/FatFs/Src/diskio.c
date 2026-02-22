/*-----------------------------------------------------------------------*/
/* Low level disk I/O module for FatFs     (C)ChaN, 2019                */
/*                                                                       */
/*   Portions COPYRIGHT 2017 STMicroelectronics                          */
/*   Portions Copyright (C) 2017, ChaN, all right reserved               */
/*   Modified to directly call SD card functions - proxy layer removed   */
/*-----------------------------------------------------------------------*/

/* Includes ------------------------------------------------------------------*/
#include "diskio.h"
#include "sd_spi.h"

/* Private variables ---------------------------------------------------------*/
/* Note: This is a single-threaded embedded system design with single SD card.
 * The is_initialized flag tracks initialization state and is only accessed from
 * main thread context (not from ISRs). The SD card driver handles its own
 * interrupt safety with volatile variables where needed. */
static BYTE is_initialized = 0;

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  Gets Disk Status
 * @param  pdrv: Physical drive number (0..)
 * @retval DSTATUS: Operation status
 */
DSTATUS disk_status(BYTE pdrv /* Physical drive number to identify the drive */
) {
	DSTATUS stat = SD_status(pdrv);

	is_initialized = (stat == RES_OK);
	return stat;
}

/**
 * @brief  Initializes a Drive
 * @param  pdrv: Physical drive number (0..)
 * @retval DSTATUS: Operation status
 */
DSTATUS disk_initialize(BYTE pdrv /* Physical drive nmuber to identify the drive */
) {
	DSTATUS stat = SD_status(pdrv);
	if (!is_initialized || stat != RES_OK) {
		stat = SD_SPI_Init(pdrv);
		is_initialized = stat == RES_OK;
	}

	return stat;
}

/**
 * @brief  Reads Sector(s)
 * @param  pdrv: Physical drive number (0..)
 * @param  *buff: Data buffer to store read data
 * @param  sector: Sector address (LBA)
 * @param  count: Number of sectors to read (1..128)
 * @retval DRESULT: Operation result
 */
DRESULT disk_read(BYTE pdrv, /* Physical drive nmuber to identify the drive */
BYTE *buff, /* Data buffer to store read data */
LBA_t sector, /* Sector address in LBA */
UINT count /* Number of sectors to read */
) {
	return SD_ReadBlocks(pdrv, buff, sector, count);
}

/**
 * @brief  Writes Sector(s)
 * @param  pdrv: Physical drive number (0..)
 * @param  *buff: Data to be written
 * @param  sector: Sector address (LBA)
 * @param  count: Number of sectors to write (1..128)
 * @retval DRESULT: Operation result
 */
DRESULT disk_write(BYTE pdrv, /* Physical drive nmuber to identify the drive */
const BYTE *buff, /* Data to be written */
LBA_t sector, /* Sector address in LBA */
UINT count /* Number of sectors to write */
) {
	return SD_WriteBlocks(pdrv, buff, sector, count);
}

/**
 * @brief  I/O control operation
 * @param  pdrv: Physical drive number (0..)
 * @param  cmd: Control code
 * @param  *buff: Buffer to send/receive control data
 * @retval DRESULT: Operation result
 */
DRESULT disk_ioctl(BYTE pdrv, /* Physical drive nmuber (0..) */
BYTE cmd, /* Control code */
void *buff /* Buffer to send/receive control data */
) {
	return SD_ioctl(pdrv, cmd, buff);
}
