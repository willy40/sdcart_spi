/*-----------------------------------------------------------------------*/
/* Low level disk I/O module for FatFs     (C)ChaN, 2017                */
/*                                                                       */
/*   Portions COPYRIGHT 2017 STMicroelectronics                          */
/*   Portions Copyright (C) 2017, ChaN, all right reserved               */
/*   Modified to directly call SD card functions - proxy layer removed   */
/*-----------------------------------------------------------------------*/

/* Includes ------------------------------------------------------------------*/
#include "diskio.h"

/* Forward declarations of SD card functions */
extern DRESULT SD_SPI_Init(BYTE pdrv);
extern DSTATUS SD_status(BYTE drv);
extern DRESULT SD_ReadBlocks(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
extern DRESULT SD_WriteBlocks(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
extern DRESULT SD_ioctl(BYTE drv, BYTE cmd, void *buff);

#if defined ( __GNUC__ )
#ifndef __weak
#define __weak __attribute__((weak))
#endif
#endif

/* Private variables ---------------------------------------------------------*/
static volatile BYTE is_initialized = 0;

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS disk_status (
	BYTE pdrv		/* Physical drive number to identify the drive */
)
{
  DSTATUS stat;

  /* Directly call SD card status function */
  stat = SD_status(pdrv);

  /* If the driver reports that the disk is not initialized (e.g., card removed),
   * clear the is_initialized flag so that the next mount will properly reinitialize */
  if (stat & STA_NOINIT) {
    is_initialized = 0;
  }

  return stat;
}

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
  DSTATUS stat = RES_OK;

  if(is_initialized == 0)
  {
    /* Directly call SD card initialization function */
    stat = SD_SPI_Init(pdrv);
    if(stat == RES_OK)
    {
      is_initialized = 1;
    }
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
DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	DWORD sector,	        /* Sector address in LBA */
	UINT count		/* Number of sectors to read */
)
{
  /* Directly call SD card read function */
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
#if _USE_WRITE == 1
DRESULT disk_write (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	DWORD sector,		/* Sector address in LBA */
	UINT count        	/* Number of sectors to write */
)
{
  /* Directly call SD card write function */
  return SD_WriteBlocks(pdrv, buff, sector, count);
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
  /* Directly call SD card ioctl function */
  return SD_ioctl(pdrv, cmd, buff);
}
#endif /* _USE_IOCTL == 1 */

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
__weak DWORD get_fattime (void)
{
  return 0;
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

