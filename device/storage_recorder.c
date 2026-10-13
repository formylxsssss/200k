#include "storage_recorder.h"
#include "acquisition.h"
#include "app_log.h"
#include "ff.h"
#include "format_utils.h"
#include "storage_manager.h"
#include "time_service.h"
#include <stdio.h>
#include <string.h>

#define RECORDER_WRITE_BUFFER_BYTES    (30u * 1024u)
#define RECORDER_SYNC_BYTES            (4u * 1024u * 1024u)
#define RECORDER_CARD_RESERVE_BYTES    (16ull * 1024ull * 1024ull)
#define RECORDER_MIN_CARD_FREE_BYTES   (64ull * 1024ull)

static recorder_status_t s_recorder;
static uint32_t s_next_run_id = 1u;
static FATFS s_fs;
static FIL s_data_file;
static bool s_fs_mounted;
static bool s_data_file_open;
static char s_volume_path[4];
static uint32_t s_bytes_since_sync;
static uint64_t s_segment_start_samples;
static uint64_t s_segment_start_bytes;
static uint32_t s_segment_trailing_bytes;
static uint64_t s_slot_free_at_open;
static tf_slot_t s_card_queue[SYSTEM_STORAGE_MAX_CARDS];
static uint8_t s_card_queue_count;
static uint8_t s_card_queue_position;
static uint8_t s_prepared_slot_mask;
static uint8_t s_write_buffer[RECORDER_WRITE_BUFFER_BYTES] __attribute__((aligned(4)));
static size_t s_write_buffer_fill;

static size_t StorageRecorder_BytesPerSample(sample_precision_t precision)
{
  switch (precision)
  {
    case SAMPLE_PRECISION_16BIT:
      return 2u;
    case SAMPLE_PRECISION_32BIT:
      return 4u;
    case SAMPLE_PRECISION_24BIT:
    default:
      return 3u;
  }
}

static void StorageRecorder_BuildCardQueue(void)
{
  const storage_summary_t *summary = StorageManager_GetSummary();

  s_card_queue_count = 0u;
  s_card_queue_position = 0u;
  if ((summary == 0) || !summary->scan_done)
  {
    return;
  }

  for (uint32_t i = 0u; i < SYSTEM_STORAGE_MAX_CARDS; ++i)
  {
    const storage_card_info_t *card = &summary->cards[i];

    if (card->present && card->initialized && card->filesystem_ok &&
        (card->free_bytes >= RECORDER_MIN_CARD_FREE_BYTES))
    {
      s_card_queue[s_card_queue_count++] = card->slot;
    }
  }
}

static bool StorageRecorder_HasNextCard(void)
{
  return (uint32_t)s_card_queue_position + 1u < (uint32_t)s_card_queue_count;
}

static bool StorageRecorder_ShouldSwitchBeforeWrite(uint32_t next_write_bytes)
{
  uint64_t segment_bytes;
  uint64_t required_bytes;

  if (!StorageRecorder_HasNextCard() || (s_slot_free_at_open == 0u))
  {
    return false;
  }

  segment_bytes = s_recorder.bytes_written - s_segment_start_bytes;
  required_bytes = segment_bytes + (uint64_t)next_write_bytes + RECORDER_CARD_RESERVE_BYTES;
  return required_bytes >= s_slot_free_at_open;
}

static size_t StorageRecorder_PackSample(uint8_t *buffer, int32_t sample, sample_precision_t precision)
{
  uint32_t raw = (uint32_t)sample;

  switch (precision)
  {
    case SAMPLE_PRECISION_16BIT:
      raw = (uint32_t)((int16_t)(sample >> 8));
      buffer[0] = (uint8_t)(raw & 0xFFu);
      buffer[1] = (uint8_t)((raw >> 8) & 0xFFu);
      return 2u;

    case SAMPLE_PRECISION_32BIT:
      buffer[0] = (uint8_t)(raw & 0xFFu);
      buffer[1] = (uint8_t)((raw >> 8) & 0xFFu);
      buffer[2] = (uint8_t)((raw >> 16) & 0xFFu);
      buffer[3] = (uint8_t)((raw >> 24) & 0xFFu);
      return 4u;

    case SAMPLE_PRECISION_24BIT:
    default:
      buffer[0] = (uint8_t)(raw & 0xFFu);
      buffer[1] = (uint8_t)((raw >> 8) & 0xFFu);
      buffer[2] = (uint8_t)((raw >> 16) & 0xFFu);
      return 3u;
  }
}

static bool StorageRecorder_WriteText(const char *path, const char *text, BYTE mode)
{
  FIL file;
  FRESULT result;
  FRESULT close_result;
  UINT written = 0u;
  UINT len;

  if ((path == 0) || (text == 0))
  {
    return false;
  }

  result = f_open(&file, path, mode | FA_WRITE);
  if (result != FR_OK)
  {
    APP_LOG_ERR("recorder text open failed fr=%u path=%s", (unsigned int)result, path);
    return false;
  }

  len = (UINT)strlen(text);
  result = f_write(&file, text, len, &written);
  close_result = f_close(&file);

  if ((result != FR_OK) || (written != len))
  {
    APP_LOG_ERR("recorder text write failed fr=%u written=%u len=%u",
                (unsigned int)result,
                (unsigned int)written,
                (unsigned int)len);
    return false;
  }
  if (close_result != FR_OK)
  {
    APP_LOG_ERR("recorder text close failed fr=%u path=%s",
                (unsigned int)close_result,
                path);
    return false;
  }

  return true;
}

static bool StorageRecorder_Mkdir(const char *path)
{
  FRESULT result;

  result = f_mkdir(path);
  if ((result == FR_OK) || (result == FR_EXIST))
  {
    return true;
  }

  APP_LOG_ERR("recorder mkdir failed fr=%u path=%s", (unsigned int)result, path);
  return false;
}

static bool StorageRecorder_PrepareSlotDirectory(tf_slot_t slot)
{
  char path_root[32];
  char path_data[48];
  char path_run[160];
  FRESULT result;
  bool prepared;
  uint32_t start_ms = HAL_GetTick();

  if (!StorageManager_GetVolumePath(slot, s_volume_path, sizeof(s_volume_path)))
  {
    return false;
  }

  memset(&s_fs, 0, sizeof(s_fs));
  result = f_mount(&s_fs, s_volume_path, 1);
  if (result != FR_OK)
  {
    APP_LOG_WARN("recorder prepare mount failed slot=%u fr=%u",
                 (unsigned int)slot,
                 (unsigned int)result);
    TFSlot_AllOff();
    s_volume_path[0] = '\0';
    return false;
  }

  (void)snprintf(path_root, sizeof(path_root), "%s/HYDROPHONE", s_volume_path);
  (void)snprintf(path_data, sizeof(path_data), "%s/HYDROPHONE/DATA", s_volume_path);
  (void)snprintf(path_run, sizeof(path_run), "%s%s", s_volume_path, s_recorder.run_path);
  prepared = StorageRecorder_Mkdir(path_root) &&
             StorageRecorder_Mkdir(path_data) &&
             StorageRecorder_Mkdir(path_run);

  (void)f_mount(0, s_volume_path, 0);
  TFSlot_AllOff();
  s_volume_path[0] = '\0';
  if (prepared)
  {
    s_prepared_slot_mask |= (uint8_t)(1u << ((uint32_t)slot - 1u));
    APP_LOG_INFO("recorder prepared slot=%u duration_ms=%u",
                 (unsigned int)slot,
                 (unsigned int)(HAL_GetTick() - start_ms));
  }
  return prepared;
}

static void StorageRecorder_PrepareCardDirectories(void)
{
  s_prepared_slot_mask = 0u;
  for (uint32_t i = 0u; i < s_card_queue_count; ++i)
  {
    (void)StorageRecorder_PrepareSlotDirectory(s_card_queue[i]);
  }
}

static bool StorageRecorder_CloseDataFile(void)
{
  bool close_ok = true;

  if (s_data_file_open)
  {
    FRESULT result = f_truncate(&s_data_file);
    if (result != FR_OK)
    {
      APP_LOG_WARN("recorder truncate failed slot=%u fr=%u",
                   (unsigned int)s_recorder.slot,
                   (unsigned int)result);
      close_ok = false;
    }

    result = f_sync(&s_data_file);
    if (result != FR_OK)
    {
      APP_LOG_WARN("recorder final sync failed slot=%u fr=%u",
                   (unsigned int)s_recorder.slot,
                   (unsigned int)result);
      close_ok = false;
    }
    else
    {
      result = f_close(&s_data_file);
      if (result != FR_OK)
      {
        APP_LOG_WARN("recorder close failed slot=%u fr=%u",
                     (unsigned int)s_recorder.slot,
                     (unsigned int)result);
        close_ok = false;
      }
    }
    s_data_file_open = false;
  }

  return close_ok;
}

static bool StorageRecorder_CloseCurrentCard(void)
{
  bool close_ok = StorageRecorder_CloseDataFile();

  if (s_fs_mounted)
  {
    (void)f_mount(0, s_volume_path, 0);
    s_fs_mounted = false;
  }

  TFSlot_AllOff();
  s_volume_path[0] = '\0';
  s_slot_free_at_open = 0u;
  return close_ok;
}

static void StorageRecorder_AbandonCurrentCard(void)
{
  s_data_file_open = false;
  if (s_volume_path[0] != '\0')
  {
    (void)f_mount(0, s_volume_path, 0);
  }
  s_fs_mounted = false;
  TFSlot_AllOff();
  s_volume_path[0] = '\0';
  s_slot_free_at_open = 0u;
}

static bool StorageRecorder_BuildPath(char *buffer, uint32_t buffer_len, const char *suffix)
{
  if ((buffer == 0) || (buffer_len == 0u) || (suffix == 0))
  {
    return false;
  }

  return snprintf(buffer, buffer_len, "%s%s%s", s_volume_path, s_recorder.run_path, suffix) > 0;
}

static const char *StorageRecorder_SourceToString(recorder_source_t source)
{
  switch (source)
  {
    case RECORDER_SOURCE_TASK_TABLE:
      return "TASK_TABLE";
    case RECORDER_SOURCE_TRIGGER:
      return "TRIGGER";
    case RECORDER_SOURCE_MANUAL:
    default:
      return "MANUAL";
  }
}

static const char *StorageRecorder_SourceToPathTag(recorder_source_t source)
{
  switch (source)
  {
    case RECORDER_SOURCE_TASK_TABLE:
      return "TASK";
    case RECORDER_SOURCE_TRIGGER:
      return "TRIG";
    case RECORDER_SOURCE_MANUAL:
    default:
      return "MANUAL";
  }
}

static bool StorageRecorder_AppendSegmentEnd(const char *reason, uint64_t time_ms)
{
  acquisition_stats_t stats;
  char path_meta[176];
  char text[640];
  char values[7][FORMAT_U64_DECIMAL_SIZE];
  int text_len;

  if (!s_fs_mounted)
  {
    return false;
  }

  Acquisition_GetStats(&stats);
  (void)Format_U64Decimal(time_ms, values[0], sizeof(values[0]));
  (void)Format_U64Decimal(s_recorder.samples_written - s_segment_start_samples,
                          values[1], sizeof(values[1]));
  (void)Format_U64Decimal(s_recorder.bytes_written - s_segment_start_bytes,
                          values[2], sizeof(values[2]));
  (void)Format_U64Decimal(stats.samples_captured, values[3], sizeof(values[3]));
  (void)Format_U64Decimal(stats.ring_dropped, values[4], sizeof(values[4]));
  (void)Format_U64Decimal(stats.estimated_missed, values[5], sizeof(values[5]));
  (void)Format_U64Decimal(stats.read_errors, values[6], sizeof(values[6]));
  text_len = snprintf(text,
                      sizeof(text),
                      "segment_end_reason=%s\r\nsegment_stop_time_ms=%s\r\nsegment_samples=%s\r\nsegment_bytes=%s\r\nsegment_trailing_bytes=%u\r\nacq_samples_captured=%s\r\nacq_ring_dropped=%s\r\nacq_estimated_missed=%s\r\nacq_read_errors=%s\r\nacq_isr_max_cycles=%u\r\nacq_isr_cycle_budget=%u\r\nacq_buffered_at_end=%u\r\n",
                      (reason != 0) ? reason : "UNKNOWN",
                      values[0],
                      values[1],
                      values[2],
                      (unsigned int)s_segment_trailing_bytes,
                      values[3],
                      values[4],
                      values[5],
                      values[6],
                      (unsigned int)stats.isr_max_cycles,
                      (unsigned int)stats.isr_cycle_budget,
                      (unsigned int)stats.buffered_samples);

  if (!StorageRecorder_BuildPath(path_meta, sizeof(path_meta), "/segment_end.txt") ||
      (text_len <= 0) || ((size_t)text_len >= sizeof(text)))
  {
    return false;
  }

  return StorageRecorder_WriteText(path_meta, text, FA_CREATE_ALWAYS);
}

static bool StorageRecorder_OpenSlot(tf_slot_t slot, bool continuation)
{
  const storage_card_info_t *card_info;
  char path_root[32];
  char path_data[48];
  char path_run[160];
  char path_meta[176];
  char path_bin[176];
  char gain_text[16];
  char meta[768];
  char values[3][FORMAT_U64_DECIMAL_SIZE];
  uint32_t candidate_segment = s_recorder.segment_index + 1u;
  int32_t nominal_gain_db = 0;
  int32_t hardware_gain_db = 0;
  int32_t software_gain_db = 0;
  int meta_len;
  FRESULT result;
  uint32_t open_start_ms = HAL_GetTick();
  uint32_t mount_done_ms;
  uint32_t directories_done_ms;
  uint32_t metadata_done_ms;

  if (s_data_file_open || s_fs_mounted)
  {
    StorageRecorder_CloseCurrentCard();
  }

  if (!StorageManager_GetVolumePath(slot, s_volume_path, sizeof(s_volume_path)))
  {
    APP_LOG_ERR("recorder open invalid slot=%u", (unsigned int)slot);
    return false;
  }

  memset(&s_fs, 0, sizeof(s_fs));
  result = f_mount(&s_fs, s_volume_path, 1);
  mount_done_ms = HAL_GetTick();
  if (result != FR_OK)
  {
    APP_LOG_ERR("recorder mount failed slot=%u fr=%u", (unsigned int)slot, (unsigned int)result);
    StorageRecorder_AbandonCurrentCard();
    return false;
  }
  s_fs_mounted = true;

  (void)snprintf(path_root, sizeof(path_root), "%s/HYDROPHONE", s_volume_path);
  (void)snprintf(path_data, sizeof(path_data), "%s/HYDROPHONE/DATA", s_volume_path);
  (void)snprintf(path_run, sizeof(path_run), "%s%s", s_volume_path, s_recorder.run_path);
  if (((s_prepared_slot_mask & (uint8_t)(1u << ((uint32_t)slot - 1u))) == 0u) &&
      (!StorageRecorder_Mkdir(path_root) ||
       !StorageRecorder_Mkdir(path_data) ||
       !StorageRecorder_Mkdir(path_run)))
  {
    StorageRecorder_CloseCurrentCard();
    return false;
  }
  directories_done_ms = HAL_GetTick();

  if (!StorageRecorder_BuildPath(path_meta, sizeof(path_meta), "/meta.txt") ||
      !StorageRecorder_BuildPath(path_bin, sizeof(path_bin), "/data.bin"))
  {
    StorageRecorder_CloseCurrentCard();
    return false;
  }

  if (SystemConfig_GetNominalGainDb(&s_recorder.settings, &nominal_gain_db))
  {
    (void)snprintf(gain_text, sizeof(gain_text), "%d", (int)nominal_gain_db);
    (void)SystemConfig_GetHardwareGainDb(&s_recorder.settings, &hardware_gain_db);
    software_gain_db = SystemConfig_GetSoftwareGainDb(&s_recorder.settings);
  }
  else
  {
    (void)snprintf(gain_text, sizeof(gain_text), "CUSTOM");
  }

  card_info = StorageManager_GetCard(slot);
  s_slot_free_at_open = (card_info != 0) ? card_info->free_bytes : 0u;
  (void)Format_U64Decimal(s_recorder.start_time_ms, values[0], sizeof(values[0]));
  (void)Format_U64Decimal(s_recorder.samples_written, values[1], sizeof(values[1]));
  (void)Format_U64Decimal((card_info != 0) ? card_info->free_bytes : 0u,
                          values[2], sizeof(values[2]));
  meta_len = snprintf(meta,
                      sizeof(meta),
                      "run_id=%u\r\nsegment=%u\r\nslot=%u\r\nsource=%s\r\ntask_id=%u\r\ncontinued=%u\r\nstart_time_ms=%s\r\nsegment_first_sample_index=%s\r\nsample_rate_hz=%u\r\nactual_sample_rate_hz=%u\r\nprecision=%s\r\nbytes_per_sample=%u\r\ngain_db=%s\r\nhardware_gain_db=%d\r\nsoftware_gain_db=%d\r\nrange_switch_bits=0x%02X\r\nbuffer_capacity_samples=%u\r\ncard_free_at_open_bytes=%s\r\nfile=data.bin\r\n",
                      (unsigned int)s_recorder.run_id,
                      (unsigned int)candidate_segment,
                      (unsigned int)slot,
                      StorageRecorder_SourceToString(s_recorder.source),
                      (unsigned int)s_recorder.task_id,
                      continuation ? 1u : 0u,
                      values[0],
                      values[1],
                      (unsigned int)s_recorder.settings.sample_rate_hz,
                      (unsigned int)Acquisition_GetActualSampleRate(),
                      SamplePrecision_ToString(s_recorder.settings.precision),
                      (unsigned int)StorageRecorder_BytesPerSample(s_recorder.settings.precision),
                      gain_text,
                      (int)hardware_gain_db,
                      (int)software_gain_db,
                      (unsigned int)(s_recorder.settings.range_switch_bits & 0x0Fu),
                      (unsigned int)Acquisition_GetBufferCapacitySamples(),
                      values[2]);

  if ((meta_len <= 0) || ((size_t)meta_len >= sizeof(meta)) ||
      !StorageRecorder_WriteText(path_meta, meta, FA_CREATE_ALWAYS))
  {
    StorageRecorder_CloseCurrentCard();
    return false;
  }
  metadata_done_ms = HAL_GetTick();

  result = f_open(&s_data_file, path_bin, FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK)
  {
    APP_LOG_ERR("recorder data open failed slot=%u fr=%u", (unsigned int)slot, (unsigned int)result);
    StorageRecorder_CloseCurrentCard();
    return false;
  }

  s_data_file_open = true;
  s_recorder.slot = slot;
  s_recorder.segment_index = candidate_segment;
  s_bytes_since_sync = 0u;
  s_segment_start_samples = s_recorder.samples_written;
  s_segment_start_bytes = s_recorder.bytes_written;
  s_segment_trailing_bytes = 0u;
  APP_LOG_OK("recorder segment open run=%u seg=%u slot=%u path=%s",
             (unsigned int)s_recorder.run_id,
             (unsigned int)s_recorder.segment_index,
             (unsigned int)slot,
             path_run);
  APP_LOG_INFO("recorder open timing slot=%u mount=%ums dirs=%ums meta=%ums data=%ums total=%ums",
               (unsigned int)slot,
               (unsigned int)(mount_done_ms - open_start_ms),
               (unsigned int)(directories_done_ms - mount_done_ms),
               (unsigned int)(metadata_done_ms - directories_done_ms),
               (unsigned int)(HAL_GetTick() - metadata_done_ms),
               (unsigned int)(HAL_GetTick() - open_start_ms));
  return true;
}

static void StorageRecorder_AppendSwitchInfo(tf_slot_t previous_slot,
                                             const char *reason,
                                             uint32_t duration_ms)
{
  char path_meta[176];
  char text[192];
  int text_len;

  text_len = snprintf(text,
                      sizeof(text),
                      "previous_slot=%u\r\nprevious_switch_reason=%s\r\nprevious_switch_duration_ms=%u\r\n",
                      (unsigned int)previous_slot,
                      (reason != 0) ? reason : "UNKNOWN",
                      (unsigned int)duration_ms);
  if (StorageRecorder_BuildPath(path_meta, sizeof(path_meta), "/switch_info.txt") &&
      (text_len > 0) && ((size_t)text_len < sizeof(text)))
  {
    (void)StorageRecorder_WriteText(path_meta, text, FA_CREATE_ALWAYS);
  }
}

static bool StorageRecorder_SwitchToNextCard(const char *reason, bool graceful_close)
{
  tf_slot_t current_slot = s_recorder.slot;
  uint32_t switch_start_ms = HAL_GetTick();
  const char *switch_reason = reason;
  bool data_close_ok = true;
  uint32_t data_close_done_ms;
  uint32_t footer_done_ms;
  uint32_t card_close_done_ms;

  APP_LOG_WARN("recorder switch start slot=%u reason=%s buffered=%u",
               (unsigned int)current_slot,
               (reason != 0) ? reason : "UNKNOWN",
               (unsigned int)Acquisition_GetBufferedSamples());
  if (graceful_close)
  {
    data_close_ok = StorageRecorder_CloseDataFile();
  }
  data_close_done_ms = HAL_GetTick();
  if (graceful_close && !StorageRecorder_AppendSegmentEnd(reason, TimeService_NowMs()))
  {
    APP_LOG_WARN("recorder segment footer unavailable slot=%u", (unsigned int)current_slot);
  }
  footer_done_ms = HAL_GetTick();
  if (graceful_close)
  {
    if (!StorageRecorder_CloseCurrentCard() || !data_close_ok)
    {
      s_recorder.write_error_count++;
      switch_reason = "CLOSE_FAIL";
    }
  }
  else
  {
    APP_LOG_WARN("recorder abandon failed slot=%u without retrying sync", (unsigned int)current_slot);
    StorageRecorder_AbandonCurrentCard();
  }
  card_close_done_ms = HAL_GetTick();
  APP_LOG_INFO("recorder switch close timing slot=%u data=%ums footer=%ums unmount=%ums total=%ums",
               (unsigned int)current_slot,
               (unsigned int)(data_close_done_ms - switch_start_ms),
               (unsigned int)(footer_done_ms - data_close_done_ms),
               (unsigned int)(card_close_done_ms - footer_done_ms),
               (unsigned int)(card_close_done_ms - switch_start_ms));

  while (StorageRecorder_HasNextCard())
  {
    tf_slot_t next_slot;
    uint32_t duration_ms;

    s_card_queue_position++;
    next_slot = s_card_queue[s_card_queue_position];
    if (!StorageRecorder_OpenSlot(next_slot, true))
    {
      APP_LOG_WARN("recorder skip unavailable cached slot=%u", (unsigned int)next_slot);
      continue;
    }

    duration_ms = HAL_GetTick() - switch_start_ms;
    s_recorder.switch_count++;
    s_recorder.last_switch_duration_ms = duration_ms;
    StorageRecorder_AppendSwitchInfo(current_slot, switch_reason, duration_ms);
    APP_LOG_OK("recorder switch done from=%u to=%u duration_ms=%u buffered=%u",
               (unsigned int)current_slot,
               (unsigned int)next_slot,
               (unsigned int)duration_ms,
               (unsigned int)Acquisition_GetBufferedSamples());
    return true;
  }

  APP_LOG_ERR("recorder no next cached card after slot=%u switch_ms=%u",
              (unsigned int)current_slot,
              (unsigned int)(HAL_GetTick() - switch_start_ms));
  return false;
}

static FRESULT StorageRecorder_WriteRaw(const uint8_t *buffer, UINT len, UINT *written)
{
  FRESULT result;

  if (written != 0)
  {
    *written = 0u;
  }
  if (!s_data_file_open || (buffer == 0) || (len == 0u) || (written == 0))
  {
    return FR_INVALID_OBJECT;
  }

  result = f_write(&s_data_file, buffer, len, written);
  return result;
}

static UINT StorageRecorder_KeepCompleteSamples(FSIZE_t write_start,
                                                UINT written,
                                                size_t bytes_per_sample)
{
  UINT aligned = (UINT)((written / bytes_per_sample) * bytes_per_sample);

  if (aligned == written)
  {
    return aligned;
  }

  s_recorder.partial_write_bytes += written - aligned;
  if ((f_lseek(&s_data_file, write_start + aligned) == FR_OK) &&
      (f_truncate(&s_data_file) == FR_OK))
  {
    APP_LOG_WARN("recorder trimmed partial sample slot=%u bytes=%u",
                 (unsigned int)s_recorder.slot,
                 (unsigned int)(written - aligned));
  }
  else
  {
    s_segment_trailing_bytes += written - aligned;
    APP_LOG_ERR("recorder partial sample trim failed slot=%u trailing=%u",
                (unsigned int)s_recorder.slot,
                (unsigned int)(written - aligned));
  }
  return aligned;
}

static bool StorageRecorder_SyncIfNeeded(void)
{
  FRESULT result;

  if (s_bytes_since_sync < RECORDER_SYNC_BYTES)
  {
    return true;
  }

  result = f_sync(&s_data_file);
  if (result != FR_OK)
  {
    APP_LOG_ERR("recorder sync failed slot=%u fr=%u",
                (unsigned int)s_recorder.slot,
                (unsigned int)result);
    return false;
  }

  s_bytes_since_sync = 0u;
  return true;
}

static bool StorageRecorder_AccountWrittenBytes(UINT accepted_bytes,
                                                size_t bytes_per_sample)
{
  size_t accepted_samples;

  if (accepted_bytes == 0u)
  {
    return true;
  }

  accepted_samples = accepted_bytes / bytes_per_sample;
  s_bytes_since_sync += accepted_bytes;
  s_recorder.bytes_written += accepted_bytes;
  s_recorder.samples_written += accepted_samples;

  if (!StorageRecorder_SyncIfNeeded())
  {
    s_recorder.write_error_count++;
    return false;
  }

  return true;
}

static bool StorageRecorder_FlushWriteBuffer(void)
{
  size_t bytes_per_sample = StorageRecorder_BytesPerSample(s_recorder.settings.precision);

  while (s_write_buffer_fill > 0u)
  {
    FSIZE_t write_start;
    FRESULT result;
    UINT requested_bytes;
    UINT written = 0u;
    UINT accepted_bytes;
    bool sync_ok;
    bool write_complete;

    if (StorageRecorder_ShouldSwitchBeforeWrite((uint32_t)s_write_buffer_fill))
    {
      if (!StorageRecorder_SwitchToNextCard("LOW_SPACE", true))
      {
        return false;
      }
    }

    requested_bytes = (UINT)s_write_buffer_fill;
    write_start = f_tell(&s_data_file);
    result = StorageRecorder_WriteRaw(s_write_buffer, requested_bytes, &written);
    accepted_bytes = StorageRecorder_KeepCompleteSamples(write_start, written, bytes_per_sample);
    sync_ok = StorageRecorder_AccountWrittenBytes(accepted_bytes, bytes_per_sample);

    if (accepted_bytes > 0u)
    {
      s_write_buffer_fill -= accepted_bytes;
      if (s_write_buffer_fill > 0u)
      {
        memmove(s_write_buffer, &s_write_buffer[accepted_bytes], s_write_buffer_fill);
      }
    }

    write_complete = (result == FR_OK) && (written == requested_bytes) &&
                     (accepted_bytes == requested_bytes);
    if (write_complete && sync_ok)
    {
      continue;
    }

    if (!write_complete)
    {
      s_recorder.write_error_count++;
      APP_LOG_ERR("recorder write interrupted slot=%u fr=%u written=%u accepted=%u requested=%u pending=%u",
                  (unsigned int)s_recorder.slot,
                  (unsigned int)result,
                  (unsigned int)written,
                  (unsigned int)accepted_bytes,
                  (unsigned int)requested_bytes,
                  (unsigned int)s_write_buffer_fill);
    }

    if (!StorageRecorder_SwitchToNextCard(sync_ok ? "WRITE_FAIL" : "SYNC_FAIL", false))
    {
      return false;
    }
  }

  return true;
}

void StorageRecorder_Init(void)
{
  memset(&s_recorder, 0, sizeof(s_recorder));
  memset(&s_fs, 0, sizeof(s_fs));
  memset(&s_data_file, 0, sizeof(s_data_file));
  s_fs_mounted = false;
  s_data_file_open = false;
  s_volume_path[0] = '\0';
  s_bytes_since_sync = 0u;
  s_segment_start_samples = 0u;
  s_segment_start_bytes = 0u;
  s_segment_trailing_bytes = 0u;
  s_slot_free_at_open = 0u;
  s_card_queue_count = 0u;
  s_card_queue_position = 0u;
  s_write_buffer_fill = 0u;
  s_prepared_slot_mask = 0u;
  APP_LOG_INFO("StorageRecorder init block=%uKB sync=%uMB reserve=%uMB",
               (unsigned int)(RECORDER_WRITE_BUFFER_BYTES / 1024u),
               (unsigned int)(RECORDER_SYNC_BYTES / (1024u * 1024u)),
               (unsigned int)(RECORDER_CARD_RESERVE_BYTES / (1024ull * 1024ull)));
}

bool StorageRecorder_Start(const acquisition_settings_t *settings, recorder_source_t source, uint32_t task_id)
{
  bool opened = false;
  int32_t nominal_gain_db = 0;
  int32_t hardware_gain_db = 0;
  int32_t software_gain_db = 0;
  uint64_t start_time_ms;

  if ((settings == 0) || !SystemConfig_ValidateAcquisition(settings))
  {
    APP_LOG_ERR("recorder start invalid settings");
    return false;
  }

  if (s_recorder.active)
  {
    APP_LOG_WARN("recorder already active run=%u", (unsigned int)s_recorder.run_id);
    return true;
  }

  if (!StorageManager_HasWritableCard())
  {
    StorageManager_ScanCards();
  }
  StorageRecorder_BuildCardQueue();
  if (s_card_queue_count == 0u)
  {
    APP_LOG_WARN("recorder start blocked: no card with recording space");
    return false;
  }

  start_time_ms = TimeService_NowMs();
  memset(&s_recorder, 0, sizeof(s_recorder));
  s_recorder.active = true;
  s_recorder.run_id = s_next_run_id++;
  s_recorder.task_id = task_id;
  s_recorder.source = source;
  s_recorder.start_time_ms = start_time_ms;
  s_recorder.settings = *settings;
  s_write_buffer_fill = 0u;
  StorageManager_BuildRunPath(s_recorder.run_path,
                              sizeof(s_recorder.run_path),
                              start_time_ms,
                              settings->sample_rate_hz,
                               s_recorder.run_id,
                               StorageRecorder_SourceToPathTag(source));
  StorageRecorder_PrepareCardDirectories();

  for (s_card_queue_position = 0u;
       s_card_queue_position < s_card_queue_count;
       ++s_card_queue_position)
  {
    if (StorageRecorder_OpenSlot(s_card_queue[s_card_queue_position], false))
    {
      opened = true;
      break;
    }
  }
  if (!opened)
  {
    memset(&s_recorder, 0, sizeof(s_recorder));
    return false;
  }

  APP_LOG_OK("recorder start run=%u slot=%u cards=%u reserve_mb=%u path=%s",
             (unsigned int)s_recorder.run_id,
             (unsigned int)s_recorder.slot,
             (unsigned int)s_card_queue_count,
             (unsigned int)(RECORDER_CARD_RESERVE_BYTES / (1024ull * 1024ull)),
             s_recorder.run_path);
  if (SystemConfig_GetNominalGainDb(settings, &nominal_gain_db))
  {
    (void)SystemConfig_GetHardwareGainDb(settings, &hardware_gain_db);
    software_gain_db = SystemConfig_GetSoftwareGainDb(settings);
    APP_LOG_INFO("recorder cfg rate=%u precision=%s gain=%+dDB hw=%+dDB sw=%+dDB branch=0x%02X",
                 (unsigned int)settings->sample_rate_hz,
                 SamplePrecision_ToString(settings->precision),
                 (int)nominal_gain_db,
                 (int)hardware_gain_db,
                 (int)software_gain_db,
                 (unsigned int)(settings->range_switch_bits & 0x0Fu));
  }
  else
  {
    APP_LOG_INFO("recorder cfg rate=%u precision=%s gain=INVALID branch=0x%02X",
                 (unsigned int)settings->sample_rate_hz,
                 SamplePrecision_ToString(settings->precision),
                 (unsigned int)(settings->range_switch_bits & 0x0Fu));
  }

  return true;
}

void StorageRecorder_Stop(uint64_t stop_time_ms)
{
  acquisition_stats_t stats;
  char path_meta[176];
  char end_text[640];
  char values[7][FORMAT_U64_DECIMAL_SIZE];
  int end_len;

  if (!s_recorder.active)
  {
    return;
  }

  if (!StorageRecorder_FlushWriteBuffer())
  {
    s_recorder.write_error_count++;
    APP_LOG_ERR("recorder final buffered write failed run=%u pending_bytes=%u",
                (unsigned int)s_recorder.run_id,
                (unsigned int)s_write_buffer_fill);
  }

  Acquisition_GetStats(&stats);
  if (!s_fs_mounted || !s_data_file_open)
  {
    APP_LOG_ERR("recorder abort run=%u samples_low=%u buffered=%u dropped_low=%u missed_low=%u",
                (unsigned int)s_recorder.run_id,
                (unsigned int)(s_recorder.samples_written & 0xFFFFFFFFu),
                (unsigned int)stats.buffered_samples,
                (unsigned int)(stats.ring_dropped & 0xFFFFFFFFu),
                (unsigned int)(stats.estimated_missed & 0xFFFFFFFFu));
    s_recorder.active = false;
    s_write_buffer_fill = 0u;
    return;
  }

  if (!StorageRecorder_CloseDataFile())
  {
    s_recorder.write_error_count++;
    APP_LOG_ERR("recorder data close failed run=%u", (unsigned int)s_recorder.run_id);
  }

  if (!StorageRecorder_AppendSegmentEnd("STOP", stop_time_ms))
  {
    APP_LOG_WARN("recorder final segment footer unavailable slot=%u",
                 (unsigned int)s_recorder.slot);
  }

  (void)Format_U64Decimal(stop_time_ms, values[0], sizeof(values[0]));
  (void)Format_U64Decimal(s_recorder.samples_written, values[1], sizeof(values[1]));
  (void)Format_U64Decimal(s_recorder.bytes_written, values[2], sizeof(values[2]));
  (void)Format_U64Decimal(stats.samples_captured, values[3], sizeof(values[3]));
  (void)Format_U64Decimal(stats.ring_dropped, values[4], sizeof(values[4]));
  (void)Format_U64Decimal(stats.estimated_missed, values[5], sizeof(values[5]));
  (void)Format_U64Decimal(stats.read_errors, values[6], sizeof(values[6]));
  end_len = snprintf(end_text,
                     sizeof(end_text),
                     "run_stop_time_ms=%s\r\nrun_samples_written=%s\r\nrun_bytes_written=%s\r\nrun_switch_count=%u\r\nlast_switch_duration_ms=%u\r\nwrite_error_count=%u\r\npartial_write_bytes=%u\r\nacq_samples_captured_final=%s\r\nacq_ring_dropped_final=%s\r\nacq_estimated_missed_final=%s\r\nacq_read_errors_final=%s\r\nacq_isr_max_cycles_final=%u\r\nacq_isr_cycle_budget=%u\r\n",
                     values[0],
                     values[1],
                     values[2],
                     (unsigned int)s_recorder.switch_count,
                     (unsigned int)s_recorder.last_switch_duration_ms,
                     (unsigned int)s_recorder.write_error_count,
                     (unsigned int)s_recorder.partial_write_bytes,
                     values[3],
                     values[4],
                     values[5],
                     values[6],
                     (unsigned int)stats.isr_max_cycles,
                     (unsigned int)stats.isr_cycle_budget);
  if (StorageRecorder_BuildPath(path_meta, sizeof(path_meta), "/run_end.txt") &&
      (end_len > 0) && ((size_t)end_len < sizeof(end_text)))
  {
    (void)StorageRecorder_WriteText(path_meta, end_text, FA_CREATE_ALWAYS);
  }

  APP_LOG_OK("recorder stop run=%u samples_low=%u switches=%u last_switch_ms=%u dropped_low=%u missed_low=%u",
             (unsigned int)s_recorder.run_id,
             (unsigned int)(s_recorder.samples_written & 0xFFFFFFFFu),
             (unsigned int)s_recorder.switch_count,
             (unsigned int)s_recorder.last_switch_duration_ms,
             (unsigned int)(stats.ring_dropped & 0xFFFFFFFFu),
             (unsigned int)(stats.estimated_missed & 0xFFFFFFFFu));

  if (!StorageRecorder_CloseCurrentCard())
  {
    s_recorder.write_error_count++;
    APP_LOG_ERR("recorder stop close failed run=%u", (unsigned int)s_recorder.run_id);
  }
  s_recorder.active = false;
  s_write_buffer_fill = 0u;
}

bool StorageRecorder_WriteSamples(const int32_t *samples, size_t count)
{
  size_t index = 0u;
  size_t bytes_per_sample;

  if (!s_recorder.active)
  {
    return false;
  }

  if ((samples == 0) || (count == 0u))
  {
    return false;
  }

  bytes_per_sample = StorageRecorder_BytesPerSample(s_recorder.settings.precision);
  while (index < count)
  {
    size_t available_samples = (sizeof(s_write_buffer) - s_write_buffer_fill) / bytes_per_sample;
    size_t pack_count = count - index;

    if (available_samples == 0u)
    {
      if (!StorageRecorder_FlushWriteBuffer())
      {
        return false;
      }
      continue;
    }
    if (pack_count > available_samples)
    {
      pack_count = available_samples;
    }

    for (size_t packed = 0u; packed < pack_count; ++packed)
    {
      s_write_buffer_fill += StorageRecorder_PackSample(&s_write_buffer[s_write_buffer_fill],
                                                        samples[index + packed],
                                                        s_recorder.settings.precision);
    }
    index += pack_count;

    if (s_write_buffer_fill == sizeof(s_write_buffer) &&
        !StorageRecorder_FlushWriteBuffer())
    {
      return false;
    }
  }

  return true;
}

const recorder_status_t *StorageRecorder_GetStatus(void)
{
  return &s_recorder;
}
