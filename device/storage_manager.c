#include "storage_manager.h"
#include "app_log.h"
#include "ff.h"
#include "sdmmc.h"
#include "time_service.h"
#include <stdio.h>
#include <string.h>

#define STORAGE_FORMAT_WORK_BYTES 32768u
#define STORAGE_DEMO_DEFAULT_BYTES 4096u
#define STORAGE_DEMO_MAX_BYTES     65536u
#define STORAGE_WRITABLE_MIN_FREE_BYTES (64ull * 1024ull)

static storage_summary_t s_summary;
static uint8_t s_format_work[STORAGE_FORMAT_WORK_BYTES];
static uint8_t s_demo_work[512];

static storage_card_info_t *StorageManager_MutableCard(tf_slot_t slot)
{
  if ((slot < TF_SLOT_1) || (slot > TF_SLOT_4))
  {
    return 0;
  }

  return &s_summary.cards[(uint32_t)slot - 1u];
}

bool StorageManager_GetVolumePath(tf_slot_t slot, char *buffer, uint32_t buffer_len)
{
  if ((buffer == 0) || (buffer_len < 3u) || (slot < TF_SLOT_1) || (slot > TF_SLOT_4))
  {
    return false;
  }

  (void)snprintf(buffer, buffer_len, "%u:", (unsigned int)((uint32_t)slot - 1u));
  return true;
}

void StorageManager_Init(void)
{
  memset(&s_summary, 0, sizeof(s_summary));
  for (uint32_t i = 0; i < SYSTEM_STORAGE_MAX_CARDS; ++i)
  {
    s_summary.cards[i].slot = (tf_slot_t)(i + 1u);
  }
  APP_LOG_INFO("StorageManager init max_cards=%u max_total_mb=%u",
               (unsigned int)SYSTEM_STORAGE_MAX_CARDS,
               (unsigned int)(SYSTEM_MAX_TOTAL_STORAGE_BYTES / (1024u * 1024u)));
}

void StorageManager_ScanCards(void)
{
  HAL_SD_CardInfoTypeDef card_info;
  FATFS fs;
  FATFS *mounted_fs = 0;
  DWORD free_clusters = 0u;
  FRESULT result;
  uint64_t capacity_bytes;
  uint64_t free_bytes;
  char path[4];

  memset(&s_summary, 0, sizeof(s_summary));
  s_summary.scan_done = true;
  APP_LOG_INFO("SD self-check start slots=1..4");

  for (uint32_t i = 0; i < SYSTEM_STORAGE_MAX_CARDS; ++i)
  {
    tf_slot_t slot = (tf_slot_t)(i + 1u);
    storage_card_info_t *info = &s_summary.cards[i];
    SD_HandleTypeDef *sd = ((slot == TF_SLOT_1) || (slot == TF_SLOT_2)) ? &hsd1 : &hsd2;

    memset(info, 0, sizeof(*info));
    info->slot = slot;

    if (TFSlot_ProbeSelected(slot) != HAL_OK)
    {
      info->present = false;
      info->initialized = false;
      info->filesystem_ok = false;
      info->last_error = HAL_SD_GetError(sd);
      APP_LOG_INFO("SD%u absent_or_init_fail err=0x%08X",
                   (unsigned int)slot,
                   (unsigned int)info->last_error);
      continue;
    }

    memset(&card_info, 0, sizeof(card_info));
    if (HAL_SD_GetCardInfo(sd, &card_info) != HAL_OK)
    {
      info->present = false;
      info->initialized = false;
      info->filesystem_ok = false;
      info->last_error = HAL_SD_GetError(sd);
      APP_LOG_INFO("SD%u absent_or_capacity_fail err=0x%08X",
                   (unsigned int)slot,
                   (unsigned int)info->last_error);
      continue;
    }

    capacity_bytes = (uint64_t)card_info.LogBlockNbr * (uint64_t)card_info.LogBlockSize;
    if ((card_info.LogBlockNbr == 0u) || (card_info.LogBlockSize == 0u) || (capacity_bytes == 0u))
    {
      info->present = false;
      info->initialized = false;
      info->filesystem_ok = false;
      info->last_error = HAL_SD_GetError(sd);
      APP_LOG_INFO("SD%u zero_capacity block_nbr=%u block_size=%u",
                   (unsigned int)slot,
                   (unsigned int)card_info.LogBlockNbr,
                   (unsigned int)card_info.LogBlockSize);
      continue;
    }

    info->present = true;
    info->initialized = true;
    info->filesystem_ok = false;
    info->last_error = HAL_SD_ERROR_NONE;
    info->block_size = card_info.LogBlockSize;
    info->capacity_bytes = capacity_bytes;
    s_summary.present_count++;
    s_summary.total_capacity_bytes += info->capacity_bytes;

    if (!StorageManager_GetVolumePath(slot, path, sizeof(path)))
    {
      info->fs_error = FR_INVALID_DRIVE;
      APP_LOG_WARN("SD%u present but invalid volume path", (unsigned int)slot);
      continue;
    }

    memset(&fs, 0, sizeof(fs));
    mounted_fs = 0;
    free_clusters = 0u;
    free_bytes = 0u;
    result = f_mount(&fs, path, 1);
    if (result == FR_OK)
    {
      result = f_getfree(path, &free_clusters, &mounted_fs);
    }

    if ((result == FR_OK) && (mounted_fs != 0))
    {
      free_bytes = (uint64_t)free_clusters * (uint64_t)mounted_fs->csize * (uint64_t)card_info.LogBlockSize;
      info->filesystem_ok = true;
      info->fs_error = FR_OK;
      info->free_bytes = free_bytes;
      s_summary.total_free_bytes += info->free_bytes;

      if (info->free_bytes >= STORAGE_WRITABLE_MIN_FREE_BYTES)
      {
        s_summary.writable_count++;
      }

      APP_LOG_OK("SD%u ready fs=ok capacity_mb=%u free_mb=%u block_size=%u",
                 (unsigned int)slot,
                 (unsigned int)(info->capacity_bytes / (1024u * 1024u)),
                 (unsigned int)(info->free_bytes / (1024u * 1024u)),
                 (unsigned int)info->block_size);
    }
    else
    {
      info->fs_error = (uint32_t)result;
      info->free_bytes = 0u;
      APP_LOG_WARN("SD%u present fs=not_ready fr=%u capacity_mb=%u",
                   (unsigned int)slot,
                   (unsigned int)result,
                   (unsigned int)(info->capacity_bytes / (1024u * 1024u)));
    }

    (void)f_mount(0, path, 0);
  }

  TFSlot_AllOff();
  if (s_summary.present_count == 0u)
  {
    APP_LOG_WARN("SD self-check done: no card inserted");
  }
  else
  {
    APP_LOG_OK("SD self-check done present=%u writable=%u total_mb=%u",
               (unsigned int)s_summary.present_count,
               (unsigned int)s_summary.writable_count,
               (unsigned int)(s_summary.total_capacity_bytes / (1024u * 1024u)));
  }
}

const storage_summary_t *StorageManager_GetSummary(void)
{
  return &s_summary;
}

const storage_card_info_t *StorageManager_GetCard(tf_slot_t slot)
{
  return StorageManager_MutableCard(slot);
}

bool StorageManager_HasWritableCard(void)
{
  return (s_summary.scan_done && (s_summary.writable_count > 0u));
}

bool StorageManager_SelectWritableCard(tf_slot_t *slot)
{
  return StorageManager_SelectWritableCardAfter((tf_slot_t)0, slot);
}

bool StorageManager_SelectWritableCardAfter(tf_slot_t previous_slot, tf_slot_t *slot)
{
  if (!StorageManager_HasWritableCard())
  {
    APP_LOG_INFO("SD no writable card cached, rescan before select");
    StorageManager_ScanCards();
  }

  for (uint32_t i = (uint32_t)previous_slot; i < SYSTEM_STORAGE_MAX_CARDS; ++i)
  {
    if (s_summary.cards[i].present && s_summary.cards[i].initialized &&
        s_summary.cards[i].filesystem_ok &&
        (s_summary.cards[i].free_bytes >= STORAGE_WRITABLE_MIN_FREE_BYTES))
    {
      if (slot != 0)
      {
        *slot = s_summary.cards[i].slot;
      }
      return true;
    }
  }

  return false;
}

bool StorageManager_FormatCard(tf_slot_t slot, char *message, uint32_t message_len)
{
  HAL_SD_CardInfoTypeDef card_info;
  FATFS fs;
  FATFS *mounted_fs = 0;
  DWORD free_clusters = 0u;
  FRESULT result;
  char path[4];
  SD_HandleTypeDef *sd;
  uint64_t capacity_bytes = 0u;
  uint64_t free_bytes = 0u;

  if ((slot < TF_SLOT_1) || (slot > TF_SLOT_4))
  {
    if ((message != 0) && (message_len > 0u))
    {
      (void)snprintf(message, message_len, "SD_FORMAT_INVALID_SLOT");
    }
    return false;
  }

  sd = ((slot == TF_SLOT_1) || (slot == TF_SLOT_2)) ? &hsd1 : &hsd2;
  (void)StorageManager_GetVolumePath(slot, path, sizeof(path));

  APP_LOG_WARN("SD format start slot=%u fs=EXFAT", (unsigned int)slot);

  if (TFSlot_InitSelected(slot) != HAL_OK)
  {
    if ((message != 0) && (message_len > 0u))
    {
      (void)snprintf(message,
                     message_len,
                     "SD_FORMAT_FAIL slot=%u init_err=0x%08X",
                     (unsigned int)slot,
                     (unsigned int)HAL_SD_GetError(sd));
    }
    APP_LOG_ERR("SD format init failed slot=%u err=0x%08X",
                (unsigned int)slot,
                (unsigned int)HAL_SD_GetError(sd));
    TFSlot_AllOff();
    return false;
  }

  memset(&card_info, 0, sizeof(card_info));
  if (HAL_SD_GetCardInfo(sd, &card_info) == HAL_OK)
  {
    capacity_bytes = (uint64_t)card_info.LogBlockNbr * (uint64_t)card_info.LogBlockSize;
    APP_LOG_INFO("SD format card slot=%u capacity_mb=%u block_size=%u",
                 (unsigned int)slot,
                 (unsigned int)(capacity_bytes / (1024u * 1024u)),
                 (unsigned int)card_info.LogBlockSize);
  }

  (void)f_mount(0, path, 0);
  result = f_mkfs(path, FM_EXFAT, 0u, s_format_work, sizeof(s_format_work));
  if (result != FR_OK)
  {
    if ((message != 0) && (message_len > 0u))
    {
      (void)snprintf(message,
                     message_len,
                     "SD_FORMAT_FAIL slot=%u fs=%s fr=%u",
                     (unsigned int)slot,
                     "EXFAT",
                     (unsigned int)result);
    }
    APP_LOG_ERR("SD format failed slot=%u fs=%s fr=%u",
                (unsigned int)slot,
                "EXFAT",
                (unsigned int)result);
    TFSlot_AllOff();
    StorageManager_ScanCards();
    return false;
  }

  result = f_mount(&fs, path, 1);
  if (result == FR_OK)
  {
    result = f_getfree(path, &free_clusters, &mounted_fs);
    if ((result == FR_OK) && (mounted_fs != 0))
    {
      free_bytes = (uint64_t)free_clusters * (uint64_t)mounted_fs->csize * (uint64_t)card_info.LogBlockSize;
    }
    (void)f_mount(0, path, 0);
  }

  APP_LOG_OK("SD format done slot=%u fs=%s free_mb=%u",
             (unsigned int)slot,
             "EXFAT",
             (unsigned int)(free_bytes / (1024u * 1024u)));

  if ((message != 0) && (message_len > 0u))
  {
    (void)snprintf(message,
                   message_len,
                   "SD_FORMAT_OK slot=%u fs=%s capacity_mb=%u free_mb=%u",
                   (unsigned int)slot,
                   "EXFAT",
                   (unsigned int)(capacity_bytes / (1024u * 1024u)),
                   (unsigned int)(free_bytes / (1024u * 1024u)));
  }

  TFSlot_AllOff();
  StorageManager_ScanCards();
  return true;
}

bool StorageManager_FormatAllCards(char *message, uint32_t message_len)
{
  char card_message[128];
  uint8_t present_mask = 0u;
  uint8_t failed_mask = 0u;
  uint32_t formatted = 0u;
  uint32_t skipped = 0u;
  uint32_t failed = 0u;

  APP_LOG_WARN("SD format all requested: all inserted-card data will be erased");
  StorageManager_ScanCards();
  for (uint32_t i = 0u; i < SYSTEM_STORAGE_MAX_CARDS; ++i)
  {
    if (s_summary.cards[i].present && s_summary.cards[i].initialized)
    {
      present_mask |= (uint8_t)(1u << i);
    }
    else
    {
      skipped++;
    }
  }

  if (present_mask == 0u)
  {
    if ((message != 0) && (message_len > 0u))
    {
      (void)snprintf(message, message_len, "SD_FORMAT_ALL_NO_CARD formatted=0 skipped=4 failed=0");
    }
    return false;
  }

  for (uint32_t i = 0u; i < SYSTEM_STORAGE_MAX_CARDS; ++i)
  {
    tf_slot_t slot = (tf_slot_t)(i + 1u);

    if ((present_mask & (uint8_t)(1u << i)) == 0u)
    {
      continue;
    }

    memset(card_message, 0, sizeof(card_message));
    if (StorageManager_FormatCard(slot, card_message, sizeof(card_message)))
    {
      formatted++;
    }
    else
    {
      failed++;
      failed_mask |= (uint8_t)(1u << i);
      APP_LOG_ERR("SD format all slot=%u failed detail=%s",
                  (unsigned int)slot,
                  card_message);
    }
  }

  StorageManager_ScanCards();
  if ((message != 0) && (message_len > 0u))
  {
    if (failed == 0u)
    {
      (void)snprintf(message,
                     message_len,
                     "SD_FORMAT_ALL_OK formatted=%u skipped=%u failed=0 mask=0x%02X",
                     (unsigned int)formatted,
                     (unsigned int)skipped,
                     (unsigned int)present_mask);
    }
    else
    {
      (void)snprintf(message,
                     message_len,
                     "SD_FORMAT_ALL_PARTIAL formatted=%u skipped=%u failed=%u failed_mask=0x%02X",
                     (unsigned int)formatted,
                     (unsigned int)skipped,
                     (unsigned int)failed,
                     (unsigned int)failed_mask);
    }
  }

  if (failed == 0u)
  {
    APP_LOG_OK("SD format all done formatted=%u skipped=%u mask=0x%02X",
               (unsigned int)formatted,
               (unsigned int)skipped,
               (unsigned int)present_mask);
    return true;
  }

  APP_LOG_ERR("SD format all incomplete formatted=%u skipped=%u failed=%u failed_mask=0x%02X",
              (unsigned int)formatted,
              (unsigned int)skipped,
              (unsigned int)failed,
              (unsigned int)failed_mask);
  return false;
}

static bool StorageManager_MkdirPath(const char *path)
{
  FRESULT result = f_mkdir(path);

  if ((result == FR_OK) || (result == FR_EXIST))
  {
    return true;
  }

  APP_LOG_ERR("SD demo mkdir failed fr=%u path=%s", (unsigned int)result, path);
  return false;
}

static bool StorageManager_WriteDemoDataToSlot(tf_slot_t slot, uint32_t bytes)
{
  FATFS fs;
  FIL file;
  FRESULT result;
  UINT written = 0u;
  uint32_t remain;
  uint32_t offset = 0u;
  char volume[4];
  char path_root[32];
  char path_test[48];
  char path_file[96];
  char header[160];
  int header_len;

  if (!StorageManager_GetVolumePath(slot, volume, sizeof(volume)))
  {
    return false;
  }

  memset(&fs, 0, sizeof(fs));
  result = f_mount(&fs, volume, 1);
  if (result != FR_OK)
  {
    APP_LOG_ERR("SD demo mount failed slot=%u fr=%u", (unsigned int)slot, (unsigned int)result);
    TFSlot_AllOff();
    return false;
  }

  (void)snprintf(path_root, sizeof(path_root), "%s/HYDROPHONE", volume);
  (void)snprintf(path_test, sizeof(path_test), "%s/HYDROPHONE/TEST", volume);
  if (!StorageManager_MkdirPath(path_root) || !StorageManager_MkdirPath(path_test))
  {
    (void)f_mount(0, volume, 0);
    TFSlot_AllOff();
    return false;
  }

  (void)snprintf(path_file,
                 sizeof(path_file),
                 "%s/HYDROPHONE/TEST/SD%u_DEMO_%08X.bin",
                 volume,
                 (unsigned int)slot,
                 (unsigned int)HAL_GetTick());

  result = f_open(&file, path_file, FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK)
  {
    APP_LOG_ERR("SD demo open failed slot=%u fr=%u path=%s",
                (unsigned int)slot,
                (unsigned int)result,
                path_file);
    (void)f_mount(0, volume, 0);
    TFSlot_AllOff();
    return false;
  }

  header_len = snprintf(header,
                        sizeof(header),
                        "single_channel_device SD demo\r\nslot=%u\r\nbytes=%u\r\ntick_ms=%u\r\npayload=incremental_pattern\r\n\r\n",
                        (unsigned int)slot,
                        (unsigned int)bytes,
                        (unsigned int)HAL_GetTick());
  if (header_len <= 0)
  {
    (void)f_close(&file);
    (void)f_mount(0, volume, 0);
    TFSlot_AllOff();
    return false;
  }

  result = f_write(&file, header, (UINT)header_len, &written);
  if ((result != FR_OK) || (written != (UINT)header_len))
  {
    APP_LOG_ERR("SD demo header write failed slot=%u fr=%u", (unsigned int)slot, (unsigned int)result);
    (void)f_close(&file);
    (void)f_mount(0, volume, 0);
    TFSlot_AllOff();
    return false;
  }

  remain = bytes;
  while (remain > 0u)
  {
    UINT chunk = (remain > sizeof(s_demo_work)) ? (UINT)sizeof(s_demo_work) : (UINT)remain;

    for (UINT i = 0u; i < chunk; ++i)
    {
      s_demo_work[i] = (uint8_t)((offset + i + ((uint32_t)slot * 17u)) & 0xFFu);
    }

    written = 0u;
    result = f_write(&file, s_demo_work, chunk, &written);
    if ((result != FR_OK) || (written != chunk))
    {
      APP_LOG_ERR("SD demo payload write failed slot=%u fr=%u written=%u chunk=%u",
                  (unsigned int)slot,
                  (unsigned int)result,
                  (unsigned int)written,
                  (unsigned int)chunk);
      (void)f_close(&file);
      (void)f_mount(0, volume, 0);
      TFSlot_AllOff();
      return false;
    }

    offset += written;
    remain -= written;
  }

  result = f_sync(&file);
  if (result == FR_OK)
  {
    result = f_close(&file);
  }
  else
  {
    (void)f_close(&file);
  }
  (void)f_mount(0, volume, 0);
  TFSlot_AllOff();

  if (result != FR_OK)
  {
    APP_LOG_ERR("SD demo close failed slot=%u fr=%u", (unsigned int)slot, (unsigned int)result);
    return false;
  }

  APP_LOG_OK("SD demo wrote slot=%u bytes=%u file=%s",
             (unsigned int)slot,
             (unsigned int)bytes,
             path_file);
  return true;
}

bool StorageManager_WriteDemoData(uint8_t slot_mask, uint32_t bytes, char *message, uint32_t message_len)
{
  uint32_t tried = 0u;
  uint32_t ok = 0u;
  uint32_t skipped = 0u;
  uint32_t failed = 0u;

  if (slot_mask == 0u)
  {
    slot_mask = 0x0Fu;
  }
  bytes = (bytes == 0u) ? STORAGE_DEMO_DEFAULT_BYTES : bytes;
  if (bytes > STORAGE_DEMO_MAX_BYTES)
  {
    bytes = STORAGE_DEMO_MAX_BYTES;
  }

  APP_LOG_INFO("SD demo start mask=0x%02X bytes=%u", (unsigned int)slot_mask, (unsigned int)bytes);
  StorageManager_ScanCards();

  for (uint32_t i = 0u; i < SYSTEM_STORAGE_MAX_CARDS; ++i)
  {
    tf_slot_t slot = (tf_slot_t)(i + 1u);
    const storage_card_info_t *card = &s_summary.cards[i];

    if ((slot_mask & (1u << i)) == 0u)
    {
      continue;
    }

    if (!card->present || !card->filesystem_ok)
    {
      skipped++;
      APP_LOG_INFO("SD demo skip slot=%u present=%u fs=%u",
                   (unsigned int)slot,
                   card->present ? 1u : 0u,
                   card->filesystem_ok ? 1u : 0u);
      continue;
    }

    tried++;
    if (StorageManager_WriteDemoDataToSlot(slot, bytes))
    {
      ok++;
    }
    else
    {
      failed++;
    }
  }

  StorageManager_ScanCards();
  if ((message != 0) && (message_len > 0u))
  {
    (void)snprintf(message,
                   message_len,
                   "SD_DEMO tried=%u ok=%u fail=%u skip=%u bytes=%u",
                   (unsigned int)tried,
                   (unsigned int)ok,
                   (unsigned int)failed,
                   (unsigned int)skipped,
                   (unsigned int)bytes);
  }

  APP_LOG_OK("SD demo done tried=%u ok=%u fail=%u skip=%u",
             (unsigned int)tried,
             (unsigned int)ok,
             (unsigned int)failed,
             (unsigned int)skipped);

  return (tried > 0u) && (failed == 0u) && (ok > 0u);
}

void StorageManager_FormatSummary(char *buffer, uint32_t buffer_len)
{
  char slots[80];
  size_t used = 0u;

  if ((buffer == 0) || (buffer_len == 0u))
  {
    return;
  }

  if (!s_summary.scan_done)
  {
    (void)snprintf(buffer, buffer_len, "SD scan=0 present=0 writable=0 S1=? S2=? S3=? S4=?");
    return;
  }

  slots[0] = '\0';
  for (uint32_t i = 0; i < SYSTEM_STORAGE_MAX_CARDS; ++i)
  {
    const storage_card_info_t *card = &s_summary.cards[i];
    char state = '-';
    int len;

    if (card->present && card->filesystem_ok)
    {
      state = 'R';
    }
    else if (card->present)
    {
      state = 'U';
    }

    if (state == 'R')
    {
      len = snprintf(&slots[used],
                     sizeof(slots) - used,
                     " S%u=R:%uMB",
                     (unsigned int)card->slot,
                     (unsigned int)(card->free_bytes / (1024u * 1024u)));
    }
    else
    {
      len = snprintf(&slots[used],
                     sizeof(slots) - used,
                     " S%u=%c",
                     (unsigned int)card->slot,
                     state);
    }

    if (len < 0)
    {
      break;
    }
    if ((size_t)len >= (sizeof(slots) - used))
    {
      used = sizeof(slots) - 1u;
      break;
    }
    used += (size_t)len;
  }

  (void)snprintf(buffer,
                 buffer_len,
                 "SD scan=1 present=%u writable=%u total_mb=%u%s",
                 (unsigned int)s_summary.present_count,
                 (unsigned int)s_summary.writable_count,
                 (unsigned int)(s_summary.total_capacity_bytes / (1024u * 1024u)),
                 slots);
}

void StorageManager_BuildRunPath(char *buffer,
                                 uint32_t buffer_len,
                                 uint64_t start_time_ms,
                                 uint32_t sample_rate_hz,
                                 uint32_t run_id,
                                 const char *source_tag)
{
  const char *tag = (source_tag != 0) ? source_tag : "UNKNOWN";
  time_calendar_t calendar;

  if ((buffer == 0) || (buffer_len == 0u))
  {
    return;
  }

  if (TimeService_IsValid() && TimeService_UnixMsToLocalCalendar(start_time_ms, &calendar))
  {
    (void)snprintf(buffer,
                   buffer_len,
                   "/HYDROPHONE/DATA/RUN_%04u%02u%02u_%02u%02u%02u_%03u_%uHz_%s_%06u",
                   (unsigned int)calendar.year,
                   (unsigned int)calendar.month,
                   (unsigned int)calendar.day,
                   (unsigned int)calendar.hour,
                   (unsigned int)calendar.minute,
                   (unsigned int)calendar.second,
                   (unsigned int)(start_time_ms % 1000u),
                   (unsigned int)sample_rate_hz,
                   tag,
                   (unsigned int)(run_id % 1000000u));
  }
  else
  {
    (void)snprintf(buffer,
                   buffer_len,
                   "/HYDROPHONE/DATA/RUN_UNSYNC_%08X_%uHz_%s_%06u",
                   (unsigned int)(start_time_ms & 0xFFFFFFFFu),
                   (unsigned int)sample_rate_hz,
                   tag,
                   (unsigned int)(run_id % 1000000u));
  }
}
