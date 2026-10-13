/* FatFs disk I/O bridge for the four TF slots. */

#include "diskio.h"
#include "app_log.h"
#include "sdmmc.h"
#include "tf_slots.h"

#include <string.h>

#define DISKIO_TRANSFER_TIMEOUT_MS 1000u
#define DISKIO_SECTOR_SIZE          512u

static BYTE s_unaligned_buffer[DISKIO_SECTOR_SIZE] __attribute__((aligned(4)));
static uint32_t s_write_rate_tick_ms;
static uint32_t s_write_rate_bytes;
static uint32_t s_write_rate_busy_ms;
static uint32_t s_write_rate_calls;

static SD_HandleTypeDef *DiskIo_SdHandle(BYTE pdrv)
{
  tf_slot_t slot = (tf_slot_t)(pdrv + 1u);

  return ((slot == TF_SLOT_1) || (slot == TF_SLOT_2)) ? &hsd1 : &hsd2;
}

static int DiskIo_IsValidDrive(BYTE pdrv)
{
  return pdrv < 4u;
}

static DRESULT DiskIo_WaitDma(SD_HandleTypeDef *sd, BYTE pdrv, const char *operation)
{
  uint32_t start = HAL_GetTick();

  while (HAL_SD_GetState(sd) != HAL_SD_STATE_READY)
  {
    if ((HAL_GetTick() - start) > DISKIO_TRANSFER_TIMEOUT_MS)
    {
      APP_LOG_ERR("SD%u %s DMA timeout state=%u err=0x%08X",
                  (unsigned int)pdrv + 1u,
                  operation,
                  (unsigned int)HAL_SD_GetState(sd),
                  (unsigned int)sd->ErrorCode);
      (void)HAL_SD_Abort(sd);
      return RES_ERROR;
    }
  }

  if (sd->ErrorCode != HAL_SD_ERROR_NONE)
  {
    APP_LOG_ERR("SD%u %s DMA failed err=0x%08X",
                (unsigned int)pdrv + 1u,
                operation,
                (unsigned int)sd->ErrorCode);
    return RES_ERROR;
  }

  start = HAL_GetTick();
  while (HAL_SD_GetCardState(sd) != HAL_SD_CARD_TRANSFER)
  {
    if ((HAL_GetTick() - start) > DISKIO_TRANSFER_TIMEOUT_MS)
    {
      APP_LOG_ERR("SD%u %s card busy timeout err=0x%08X",
                  (unsigned int)pdrv + 1u,
                  operation,
                  (unsigned int)sd->ErrorCode);
      return RES_ERROR;
    }
  }

  return RES_OK;
}

static DRESULT DiskIo_ReadDma(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  SD_HandleTypeDef *sd = DiskIo_SdHandle(pdrv);

  if (HAL_SD_ReadBlocks_DMA(sd, buff, sector, count) != HAL_OK)
  {
    APP_LOG_ERR("SD%u read DMA start failed err=0x%08X",
                (unsigned int)pdrv + 1u,
                (unsigned int)sd->ErrorCode);
    return RES_ERROR;
  }

  return DiskIo_WaitDma(sd, pdrv, "read");
}

static DRESULT DiskIo_WriteDma(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
  SD_HandleTypeDef *sd = DiskIo_SdHandle(pdrv);
  DRESULT result;
  uint32_t start_ms = HAL_GetTick();
  uint32_t now_ms;
  uint32_t elapsed_ms;

  if ((s_write_rate_tick_ms == 0u) ||
      ((start_ms - s_write_rate_tick_ms) > (DISKIO_TRANSFER_TIMEOUT_MS * 5u)))
  {
    s_write_rate_tick_ms = start_ms;
    s_write_rate_bytes = 0u;
    s_write_rate_busy_ms = 0u;
    s_write_rate_calls = 0u;
  }

  if (HAL_SD_WriteBlocks_DMA(sd, buff, sector, count) != HAL_OK)
  {
    APP_LOG_ERR("SD%u write DMA start failed err=0x%08X",
                (unsigned int)pdrv + 1u,
                (unsigned int)sd->ErrorCode);
    return RES_ERROR;
  }

  result = DiskIo_WaitDma(sd, pdrv, "write");
  now_ms = HAL_GetTick();
  s_write_rate_busy_ms += now_ms - start_ms;
  s_write_rate_calls++;
  if (result == RES_OK)
  {
    s_write_rate_bytes += count * DISKIO_SECTOR_SIZE;
  }

  elapsed_ms = now_ms - s_write_rate_tick_ms;
  if (elapsed_ms >= 1000u)
  {
    APP_LOG_INFO("SD%u write rate=%u kB/s busy=%u/%u ms calls=%u",
                 (unsigned int)pdrv + 1u,
                 (unsigned int)(((uint64_t)s_write_rate_bytes * 1000u) /
                                ((uint64_t)elapsed_ms * 1024u)),
                 (unsigned int)s_write_rate_busy_ms,
                 (unsigned int)elapsed_ms,
                 (unsigned int)s_write_rate_calls);
    s_write_rate_tick_ms = now_ms;
    s_write_rate_bytes = 0u;
    s_write_rate_busy_ms = 0u;
    s_write_rate_calls = 0u;
  }

  return result;
}

DSTATUS disk_initialize(BYTE pdrv)
{
  if (!DiskIo_IsValidDrive(pdrv))
  {
    return STA_NOINIT | STA_NODISK;
  }

  if (TFSlot_ProbeSelected((tf_slot_t)(pdrv + 1u)) != HAL_OK)
  {
    return STA_NOINIT | STA_NODISK;
  }

  return 0;
}

DSTATUS disk_status(BYTE pdrv)
{
  SD_HandleTypeDef *sd;

  if (!DiskIo_IsValidDrive(pdrv))
  {
    return STA_NOINIT | STA_NODISK;
  }

  sd = DiskIo_SdHandle(pdrv);
  if (HAL_SD_GetCardState(sd) == HAL_SD_CARD_TRANSFER)
  {
    return 0;
  }

  return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  if (!DiskIo_IsValidDrive(pdrv) || (buff == 0) || (count == 0u))
  {
    return RES_PARERR;
  }

  if ((((uintptr_t)buff) & 0x3u) == 0u)
  {
    return DiskIo_ReadDma(pdrv, buff, sector, count);
  }

  while (count > 0u)
  {
    if (DiskIo_ReadDma(pdrv, s_unaligned_buffer, sector, 1u) != RES_OK)
    {
      return RES_ERROR;
    }
    memcpy(buff, s_unaligned_buffer, DISKIO_SECTOR_SIZE);
    buff += DISKIO_SECTOR_SIZE;
    sector++;
    count--;
  }

  return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
  if (!DiskIo_IsValidDrive(pdrv) || (buff == 0) || (count == 0u))
  {
    return RES_PARERR;
  }

  if ((((uintptr_t)buff) & 0x3u) == 0u)
  {
    return DiskIo_WriteDma(pdrv, buff, sector, count);
  }

  while (count > 0u)
  {
    memcpy(s_unaligned_buffer, buff, DISKIO_SECTOR_SIZE);
    if (DiskIo_WriteDma(pdrv, s_unaligned_buffer, sector, 1u) != RES_OK)
    {
      return RES_ERROR;
    }
    buff += DISKIO_SECTOR_SIZE;
    sector++;
    count--;
  }

  return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  HAL_SD_CardInfoTypeDef card_info;
  SD_HandleTypeDef *sd;

  if (!DiskIo_IsValidDrive(pdrv))
  {
    return RES_PARERR;
  }

  sd = DiskIo_SdHandle(pdrv);

  switch (cmd)
  {
    case CTRL_SYNC:
      return (HAL_SD_GetCardState(sd) == HAL_SD_CARD_TRANSFER) ? RES_OK : RES_ERROR;

    case GET_SECTOR_COUNT:
      if ((buff == 0) || (HAL_SD_GetCardInfo(sd, &card_info) != HAL_OK))
      {
        return RES_ERROR;
      }
      *(DWORD *)buff = card_info.LogBlockNbr;
      return RES_OK;

    case GET_SECTOR_SIZE:
      if ((buff == 0) || (HAL_SD_GetCardInfo(sd, &card_info) != HAL_OK))
      {
        return RES_ERROR;
      }
      *(WORD *)buff = (WORD)card_info.LogBlockSize;
      return RES_OK;

    case GET_BLOCK_SIZE:
      if (buff == 0)
      {
        return RES_ERROR;
      }
      *(DWORD *)buff = 128u;
      return RES_OK;

    case CTRL_TRIM:
      return RES_OK;

    default:
      return RES_PARERR;
  }
}
