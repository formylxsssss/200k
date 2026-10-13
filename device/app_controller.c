#include "app_controller.h"
#include "acquisition.h"
#include "app_log.h"
#include "command_protocol.h"
#include "file_service.h"
#include "format_utils.h"
#include "main.h"
#include "range_ctrl.h"
#include "storage_manager.h"
#include "storage_recorder.h"
#include "stream_service.h"
#include "task_scheduler.h"
#include "tcp_service.h"
#include "time_service.h"
#include <stdio.h>
#include <string.h>

#define STORAGE_BATCH_SAMPLES         4096u
#define STORAGE_MAX_BATCHES_PER_TASK  8u
#define TRIGGER_DEBOUNCE_MS           10u

static system_mode_t s_mode;
static acquisition_settings_t s_current_settings;
static acquisition_settings_t s_trigger_settings;
static uint64_t s_trigger_duration_ms;
static trigger_output_t s_trigger_output;
static bool s_trigger_continuous;
static bool s_trigger_wait_release;
static uint32_t s_trigger_capture_count;
static uint32_t s_trigger_low_start_tick_ms;
static uint32_t s_trigger_capture_start_tick_ms;
static bool s_trigger_low_candidate;
static int32_t s_storage_batch[STORAGE_BATCH_SAMPLES];

static bool AppController_DrainStorageRoute(bool flush_all);

static void AppController_SetMode(system_mode_t mode)
{
  if (s_mode != mode)
  {
    APP_LOG_INFO("mode %s -> %s", SystemMode_ToString(s_mode), SystemMode_ToString(mode));
    s_mode = mode;
  }
}

static void AppController_SendSimpleResponse(uint32_t sequence, int32_t status, const char *message)
{
  command_response_t response;

  memset(&response, 0, sizeof(response));
  response.sequence = sequence;
  response.status = status;
  (void)snprintf(response.message, sizeof(response.message), "%s", message);
  (void)TcpService_SendResponse(&response);
}

static const char *AppController_TriggerOutputToString(trigger_output_t output)
{
  switch (output)
  {
    case TRIGGER_OUTPUT_REALTIME:
      return "REALTIME";
    case TRIGGER_OUTPUT_STORAGE:
    default:
      return "STORAGE";
  }
}

static const char *AppController_TriggerRepeatToString(void)
{
  return s_trigger_continuous ? "CONTINUOUS" : "ONCE";
}

static bool AppController_StopActiveCapture(void)
{
  bool storage_ok = true;

  if ((s_mode == SYSTEM_MODE_REALTIME_STREAM) || (s_mode == SYSTEM_MODE_REALTIME_TRIGGERED))
  {
    StreamService_Stop();
    Acquisition_Stop();
    Acquisition_DiscardBufferedSamples();
  }
  else if ((s_mode == SYSTEM_MODE_STORAGE_IMMEDIATE) ||
           (s_mode == SYSTEM_MODE_STORAGE_SCHEDULED) ||
           (s_mode == SYSTEM_MODE_STORAGE_TRIGGERED))
  {
    Acquisition_Stop();
    storage_ok = AppController_DrainStorageRoute(true);
    StorageRecorder_Stop(TimeService_NowMs());
  }

  AppController_SetMode(storage_ok ? SYSTEM_MODE_IDLE : SYSTEM_MODE_ERROR);
  return storage_ok;
}

static bool AppController_StartRealtimeMode(const acquisition_settings_t *settings, system_mode_t mode)
{
  if (!SystemConfig_ValidateRealtimeAcquisition(settings))
  {
    APP_LOG_WARN("realtime start blocked: unsupported rate=%u max=%u",
                 (unsigned int)((settings != 0) ? settings->sample_rate_hz : 0u),
                 (unsigned int)SYSTEM_REALTIME_RATE_MAX_HZ);
    return false;
  }

  if (!AppController_StopActiveCapture())
  {
    return false;
  }

  if (!TcpService_IsDataConnected())
  {
    APP_LOG_WARN("realtime start blocked: data client not connected");
    return false;
  }

  if (!Acquisition_Configure(settings))
  {
    return false;
  }

  if (!StreamService_Start(settings))
  {
    return false;
  }

  Acquisition_Start();
  AppController_SetMode(mode);
  return true;
}

static bool AppController_StartRealtime(const acquisition_settings_t *settings)
{
  return AppController_StartRealtimeMode(settings, SYSTEM_MODE_REALTIME_STREAM);
}

static bool AppController_StartStorage(const acquisition_settings_t *settings, recorder_source_t source, uint32_t task_id)
{
  if (!AppController_StopActiveCapture())
  {
    return false;
  }

  if (!Acquisition_Configure(settings))
  {
    return false;
  }

  if (!StorageRecorder_Start(settings, source, task_id))
  {
    return false;
  }

  Acquisition_Start();
  if (source == RECORDER_SOURCE_TASK_TABLE)
  {
    AppController_SetMode(SYSTEM_MODE_STORAGE_SCHEDULED);
  }
  else if (source == RECORDER_SOURCE_TRIGGER)
  {
    AppController_SetMode(SYSTEM_MODE_STORAGE_TRIGGERED);
  }
  else
  {
    AppController_SetMode(SYSTEM_MODE_STORAGE_IMMEDIATE);
  }
  return true;
}

static void AppController_TaskStartCallback(const acquisition_task_item_t *task)
{
  if (task == 0)
  {
    return;
  }

  if (!TimeService_IsValid())
  {
    APP_LOG_ERR("scheduled task blocked: time invalid");
    return;
  }

  (void)AppController_StartStorage(&task->settings, RECORDER_SOURCE_TASK_TABLE, task->task_id);
}

static void AppController_TaskStopCallback(const acquisition_task_item_t *task)
{
  (void)task;

  if (s_mode == SYSTEM_MODE_STORAGE_SCHEDULED)
  {
    AppController_StopActiveCapture();
  }
}

static bool AppController_DrainStorageRoute(bool flush_all)
{
  uint32_t batches_written = 0u;

  if ((s_mode != SYSTEM_MODE_STORAGE_IMMEDIATE) &&
      (s_mode != SYSTEM_MODE_STORAGE_SCHEDULED) &&
      (s_mode != SYSTEM_MODE_STORAGE_TRIGGERED))
  {
    return true;
  }

  while (flush_all || (batches_written < STORAGE_MAX_BATCHES_PER_TASK))
  {
    size_t available = Acquisition_GetBufferedSamples();
    size_t target_count;
    size_t count = 0u;
    int32_t sample;

    if (available == 0u)
    {
      break;
    }
    if (!flush_all && (available < STORAGE_BATCH_SAMPLES))
    {
      break;
    }

    target_count = (available < STORAGE_BATCH_SAMPLES) ? available : STORAGE_BATCH_SAMPLES;
    while ((count < target_count) && Acquisition_ReadSample(&sample))
    {
      s_storage_batch[count++] = sample;
    }

    if ((count > 0u) && !StorageRecorder_WriteSamples(s_storage_batch, count))
    {
      APP_LOG_ERR("storage write failed count=%u buffered=%u",
                  (unsigned int)count,
                  (unsigned int)Acquisition_GetBufferedSamples());
      return false;
    }
    batches_written++;
  }

  return true;
}

static void AppController_HandleStorageRoute(void)
{
  acquisition_stats_t stats;

  if ((s_mode != SYSTEM_MODE_STORAGE_IMMEDIATE) &&
      (s_mode != SYSTEM_MODE_STORAGE_SCHEDULED) &&
      (s_mode != SYSTEM_MODE_STORAGE_TRIGGERED))
  {
    return;
  }

  if (!AppController_DrainStorageRoute(false))
  {
    Acquisition_Stop();
    StorageRecorder_Stop(TimeService_NowMs());
    AppController_SetMode(SYSTEM_MODE_ERROR);
    return;
  }

  Acquisition_GetStats(&stats);
  if (stats.ring_dropped > 0u)
  {
    APP_LOG_ERR("storage stopped: buffer overflow dropped_low=%u buffered=%u",
                (unsigned int)(stats.ring_dropped & 0xFFFFFFFFu),
                (unsigned int)stats.buffered_samples);
    Acquisition_Stop();
    StorageRecorder_Stop(TimeService_NowMs());
    AppController_SetMode(SYSTEM_MODE_ERROR);
  }
}

static void AppController_HandleRealtimeHealth(void)
{
  acquisition_stats_t stats;

  if ((s_mode != SYSTEM_MODE_REALTIME_STREAM) &&
      (s_mode != SYSTEM_MODE_REALTIME_TRIGGERED))
  {
    return;
  }

  if (!TcpService_IsDataConnected())
  {
    APP_LOG_WARN("realtime stopped: data client disconnected");
    (void)AppController_StopActiveCapture();
    return;
  }

  Acquisition_GetStats(&stats);
  if (stats.ring_dropped > 0u)
  {
    APP_LOG_ERR("realtime stopped: buffer overflow dropped_low=%u",
                (unsigned int)(stats.ring_dropped & 0xFFFFFFFFu));
    (void)AppController_StopActiveCapture();
  }
}

static bool AppController_TriggerInputIsLow(void)
{
  return HAL_GPIO_ReadPin(EXT_TRIGGER_GPIO_Port, EXT_TRIGGER_Pin) == GPIO_PIN_RESET;
}

static void AppController_ResetTriggerDebounce(void)
{
  s_trigger_low_candidate = false;
  s_trigger_low_start_tick_ms = 0u;
}

static bool AppController_ArmTrigger(void)
{
  char duration_text[FORMAT_U64_DECIMAL_SIZE];

  if (s_mode != SYSTEM_MODE_IDLE)
  {
    APP_LOG_WARN("trigger arm blocked mode=%s", SystemMode_ToString(s_mode));
    return false;
  }

  if ((s_trigger_output == TRIGGER_OUTPUT_STORAGE) && (StorageManager_GetSummary()->writable_count == 0u))
  {
    APP_LOG_WARN("trigger arm blocked: no writable SD card");
    return false;
  }
  if ((s_trigger_output == TRIGGER_OUTPUT_REALTIME) && !TcpService_IsDataConnected())
  {
    APP_LOG_WARN("trigger arm blocked: data client not connected");
    return false;
  }

  AppController_ResetTriggerDebounce();
  s_trigger_wait_release = false;
  s_trigger_capture_count = 0u;
  AppController_SetMode(SYSTEM_MODE_TRIGGER_ARMED);
  (void)Format_U64Decimal(s_trigger_duration_ms, duration_text, sizeof(duration_text));
  APP_LOG_OK("trigger armed level=LOW output=%s repeat=%s duration_ms=%s pin=%u",
             AppController_TriggerOutputToString(s_trigger_output),
             AppController_TriggerRepeatToString(),
             duration_text,
             AppController_TriggerInputIsLow() ? 0u : 1u);
  return true;
}

static bool AppController_RearmContinuousTrigger(void)
{
  if ((s_trigger_output == TRIGGER_OUTPUT_STORAGE) &&
      (StorageManager_GetSummary()->writable_count == 0u))
  {
    APP_LOG_WARN("trigger continuous rearm stopped: no writable SD card");
    return false;
  }
  if ((s_trigger_output == TRIGGER_OUTPUT_REALTIME) && !TcpService_IsDataConnected())
  {
    APP_LOG_WARN("trigger continuous rearm stopped: data client not connected");
    return false;
  }

  AppController_ResetTriggerDebounce();
  s_trigger_wait_release = AppController_TriggerInputIsLow();
  AppController_SetMode(SYSTEM_MODE_TRIGGER_ARMED);
  APP_LOG_OK("trigger continuous rearmed count=%u wait_release=%u pin=%u",
             (unsigned int)s_trigger_capture_count,
             s_trigger_wait_release ? 1u : 0u,
             AppController_TriggerInputIsLow() ? 0u : 1u);
  return true;
}

static void AppController_HandleTrigger(void)
{
  uint32_t now_tick = HAL_GetTick();
  char duration_text[FORMAT_U64_DECIMAL_SIZE];

  if (s_mode == SYSTEM_MODE_TRIGGER_ARMED)
  {
    if (s_trigger_wait_release)
    {
      if (!AppController_TriggerInputIsLow())
      {
        s_trigger_wait_release = false;
        AppController_ResetTriggerDebounce();
        APP_LOG_OK("trigger input released, ready for next low level");
      }
      return;
    }

    if (AppController_TriggerInputIsLow())
    {
      if (!s_trigger_low_candidate)
      {
        s_trigger_low_candidate = true;
        s_trigger_low_start_tick_ms = now_tick;
        APP_LOG_INFO("trigger low detected, debounce start");
      }
      else if ((now_tick - s_trigger_low_start_tick_ms) >= TRIGGER_DEBOUNCE_MS)
      {
        APP_LOG_OK("trigger accepted, output=%s capture start",
                   AppController_TriggerOutputToString(s_trigger_output));
        AppController_ResetTriggerDebounce();
        if (((s_trigger_output == TRIGGER_OUTPUT_STORAGE) &&
             AppController_StartStorage(&s_trigger_settings, RECORDER_SOURCE_TRIGGER, 0u)) ||
            ((s_trigger_output == TRIGGER_OUTPUT_REALTIME) &&
             AppController_StartRealtimeMode(&s_trigger_settings, SYSTEM_MODE_REALTIME_TRIGGERED)))
        {
          s_trigger_capture_start_tick_ms = HAL_GetTick();
          s_trigger_capture_count++;
          (void)Format_U64Decimal(s_trigger_duration_ms, duration_text, sizeof(duration_text));
          APP_LOG_OK("trigger capture running count=%u output=%s duration_ms=%s",
                     (unsigned int)s_trigger_capture_count,
                     AppController_TriggerOutputToString(s_trigger_output),
                     duration_text);
        }
        else
        {
          APP_LOG_ERR("trigger capture start failed");
          AppController_SetMode(SYSTEM_MODE_IDLE);
        }
      }
    }
    else if (s_trigger_low_candidate)
    {
      APP_LOG_INFO("trigger debounce cancelled");
      AppController_ResetTriggerDebounce();
    }
  }
  else if ((s_mode == SYSTEM_MODE_STORAGE_TRIGGERED) || (s_mode == SYSTEM_MODE_REALTIME_TRIGGERED))
  {
    if ((uint32_t)(now_tick - s_trigger_capture_start_tick_ms) >= (uint32_t)s_trigger_duration_ms)
    {
      bool stop_ok;

      APP_LOG_OK("trigger capture duration reached, stopping");
      stop_ok = AppController_StopActiveCapture();
      if (stop_ok && s_trigger_continuous)
      {
        (void)AppController_RearmContinuousTrigger();
      }
    }
  }
}

static void AppController_HandleCommand(const command_request_t *request)
{
  char message[256];
  char u64_text[4][FORMAT_U64_DECIMAL_SIZE];
  int32_t nominal_gain_db = 0;
  int32_t hardware_gain_db = 0;
  int32_t software_gain_db = 0;

  if (request == 0)
  {
    return;
  }

  APP_LOG_INFO("command type=%u seq=%u", (unsigned int)request->type, (unsigned int)request->sequence);

  switch (request->type)
  {
    case COMMAND_QUERY_STATUS:
    {
      acquisition_stats_t acquisition;
      const recorder_status_t *recorder = StorageRecorder_GetStatus();

      Acquisition_GetStats(&acquisition);
      (void)snprintf(message,
                     sizeof(message),
                     "%s CTRL=%u DATA=%u SDW=%u REC=%u SLOT=%u RUN=%u SAMPLES=%u TRIG_PIN=%u TOUT=%s TDUR=%u BUF=%u DROP=%u MISS=%u SW=%u",
                     SystemMode_ToString(s_mode),
                     TcpService_IsControlConnected() ? 1u : 0u,
                     TcpService_IsDataConnected() ? 1u : 0u,
                     (unsigned int)StorageManager_GetSummary()->writable_count,
                     recorder->active ? 1u : 0u,
                     (unsigned int)recorder->slot,
                     (unsigned int)recorder->run_id,
                     (unsigned int)(recorder->samples_written & 0xFFFFFFFFu),
                     AppController_TriggerInputIsLow() ? 0u : 1u,
                     AppController_TriggerOutputToString(s_trigger_output),
                     (unsigned int)s_trigger_duration_ms,
                     (unsigned int)acquisition.buffered_samples,
                     (unsigned int)(acquisition.ring_dropped & 0xFFFFFFFFu),
                     (unsigned int)(acquisition.estimated_missed & 0xFFFFFFFFu),
                     (unsigned int)recorder->switch_count);
      AppController_SendSimpleResponse(request->sequence, 0, message);
      break;
    }

    case COMMAND_SYNC_TIME:
      TimeService_SetUnixTimeMs(request->data.unix_time_ms);
      AppController_SendSimpleResponse(request->sequence, 0, "TIME_OK");
      break;

    case COMMAND_SET_ACQUISITION:
      if (SystemConfig_ValidateAcquisition(&request->data.acquisition))
      {
        bool applied_now = (s_mode == SYSTEM_MODE_IDLE);

        s_current_settings = request->data.acquisition;
        if (applied_now)
        {
          RangeCtrl_SetSwitchBits(s_current_settings.range_switch_bits);
        }
        if (SystemConfig_GetNominalGainDb(&s_current_settings, &nominal_gain_db))
        {
          (void)SystemConfig_GetHardwareGainDb(&s_current_settings, &hardware_gain_db);
          software_gain_db = SystemConfig_GetSoftwareGainDb(&s_current_settings);
          (void)snprintf(message,
                         sizeof(message),
                         "CONFIG_OK rate=%u prec=%s gain=%+dDB hw=%+dDB sw=%+dDB branch=0x%02X applied=%u pb=0x%X",
                         (unsigned int)s_current_settings.sample_rate_hz,
                         SamplePrecision_ToString(s_current_settings.precision),
                         (int)nominal_gain_db,
                         (int)hardware_gain_db,
                         (int)software_gain_db,
                         (unsigned int)(s_current_settings.range_switch_bits & 0x0Fu),
                         applied_now ? 1u : 0u,
                         (unsigned int)RangeCtrl_GetGpioLevels());
        }
        else
        {
          (void)snprintf(message,
                         sizeof(message),
                         "CONFIG_OK rate=%u prec=%s gain=INVALID branch=0x%02X",
                         (unsigned int)s_current_settings.sample_rate_hz,
                         SamplePrecision_ToString(s_current_settings.precision),
                         (unsigned int)(s_current_settings.range_switch_bits & 0x0Fu));
        }
        AppController_SendSimpleResponse(request->sequence, 0, message);
      }
      else
      {
        AppController_SendSimpleResponse(request->sequence, -1, "CONFIG_INVALID");
      }
      break;

    case COMMAND_START_REALTIME:
      if (AppController_StartRealtime(&s_current_settings))
      {
        AppController_SendSimpleResponse(request->sequence, 0, "START_REALTIME");
      }
      else
      {
        AppController_SendSimpleResponse(request->sequence, -1, "START_REALTIME_FAIL");
      }
      break;

    case COMMAND_STOP_REALTIME:
      AppController_SendSimpleResponse(request->sequence,
                                       AppController_StopActiveCapture() ? 0 : -1,
                                       "STOP_REALTIME");
      break;

    case COMMAND_START_STORAGE:
      if (AppController_StartStorage(&s_current_settings, RECORDER_SOURCE_MANUAL, 0u))
      {
        AppController_SendSimpleResponse(request->sequence, 0, "START_STORAGE");
      }
      else
      {
        StorageManager_FormatSummary(message, sizeof(message));
        AppController_SendSimpleResponse(request->sequence, -1, message);
      }
      break;

    case COMMAND_STOP_STORAGE:
      AppController_SendSimpleResponse(request->sequence,
                                       AppController_StopActiveCapture() ? 0 : -1,
                                       "STOP_STORAGE");
      break;

    case COMMAND_SD_SCAN:
      if (s_mode != SYSTEM_MODE_IDLE)
      {
        AppController_SendSimpleResponse(request->sequence, -1, "SD_SCAN_BUSY");
      }
      else
      {
        StorageManager_ScanCards();
        StorageManager_FormatSummary(message, sizeof(message));
        AppController_SendSimpleResponse(request->sequence, 0, message);
      }
      break;

    case COMMAND_QUERY_SD:
      StorageManager_FormatSummary(message, sizeof(message));
      AppController_SendSimpleResponse(request->sequence, 0, message);
      break;

    case COMMAND_SD_FORMAT:
      if (s_mode != SYSTEM_MODE_IDLE)
      {
        AppController_SendSimpleResponse(request->sequence, -1, "SD_FORMAT_BUSY");
      }
      else if (!request->data.sd_format.fs_is_exfat)
      {
        AppController_SendSimpleResponse(request->sequence, -1, "SD_FORMAT_EXFAT_ONLY");
      }
      else if (request->data.sd_format.all_slots &&
               !request->data.sd_format.confirmed_all)
      {
        AppController_SendSimpleResponse(request->sequence, -1, "SD_FORMAT_ALL_NEED_CONFIRM");
      }
      else if (!request->data.sd_format.confirmed)
      {
        AppController_SendSimpleResponse(request->sequence, -1, "SD_FORMAT_NEED_CONFIRM");
      }
      else
      {
        bool ok;

        if (request->data.sd_format.all_slots)
        {
          ok = StorageManager_FormatAllCards(message, sizeof(message));
        }
        else
        {
          ok = StorageManager_FormatCard(request->data.sd_format.slot,
                                         message,
                                         sizeof(message));
        }
        AppController_SendSimpleResponse(request->sequence, ok ? 0 : -1, message);
      }
      break;

    case COMMAND_SD_DEMO:
      if (s_mode != SYSTEM_MODE_IDLE)
      {
        AppController_SendSimpleResponse(request->sequence, -1, "SD_DEMO_BUSY");
      }
      else
      {
        bool ok = StorageManager_WriteDemoData(request->data.sd_demo.slot_mask,
                                               request->data.sd_demo.bytes,
                                               message,
                                               sizeof(message));
        AppController_SendSimpleResponse(request->sequence, ok ? 0 : -1, message);
      }
      break;

    case COMMAND_TASK_ADD:
      if (!TimeService_IsValid())
      {
        AppController_SendSimpleResponse(request->sequence, -1, "TASK_ADD_TIME_INVALID");
      }
      else
      {
        AppController_SendSimpleResponse(request->sequence,
                                         TaskScheduler_Add(&request->data.task) ? 0 : -1,
                                         "TASK_ADD");
      }
      break;

    case COMMAND_TASK_CLEAR:
      TaskScheduler_Clear();
      AppController_SendSimpleResponse(request->sequence, 0, "TASK_CLEAR");
      break;

    case COMMAND_TASK_LIST:
      (void)snprintf(message,
                     sizeof(message),
                     "TASK_LIST COUNT=%u MAX=%u TIME=%u",
                     (unsigned int)TaskScheduler_Count(),
                     (unsigned int)TaskScheduler_MaxCount(),
                     TimeService_IsValid() ? 1u : 0u);
      AppController_SendSimpleResponse(request->sequence, 0, message);
      break;

    case COMMAND_TASK_GET:
    {
      task_scheduler_entry_t entry;
      if (!TaskScheduler_GetEntry(request->data.task_index, &entry))
      {
        AppController_SendSimpleResponse(request->sequence, -1, "TASK_GET_INVALID");
      }
      else if (!entry.item.enabled)
      {
        (void)snprintf(message,
                       sizeof(message),
                       "TASK_EMPTY I=%u",
                       (unsigned int)request->data.task_index);
        AppController_SendSimpleResponse(request->sequence, 0, message);
      }
      else
      {
        int32_t task_gain_db = 0;
        if (!SystemConfig_GetNominalGainDb(&entry.item.settings, &task_gain_db))
        {
          task_gain_db = 999;
        }
        (void)Format_U64Decimal(entry.item.start_time_ms, u64_text[0], sizeof(u64_text[0]));
        (void)Format_U64Decimal(entry.item.start_time_ms + entry.item.duration_ms,
                                u64_text[1], sizeof(u64_text[1]));
        (void)Format_U64Decimal(entry.item.duration_ms, u64_text[2], sizeof(u64_text[2]));
        (void)Format_U64Decimal(entry.item.interval_ms, u64_text[3], sizeof(u64_text[3]));
        (void)snprintf(message,
                       sizeof(message),
                       "TASK I=%u RUN=%u CMP=%u ID=%u ST=%s END=%s DUR=%s INT=%s REP=%u EXE=%u RATE=%u PREC=%u GAIN=%d",
                       (unsigned int)entry.table_index,
                       entry.running ? 1u : 0u,
                       entry.completed ? 1u : 0u,
                       (unsigned int)entry.item.task_id,
                       u64_text[0],
                       u64_text[1],
                       u64_text[2],
                       u64_text[3],
                       (unsigned int)entry.item.repeat_count,
                       (unsigned int)entry.executed_count,
                       (unsigned int)entry.item.settings.sample_rate_hz,
                       (unsigned int)entry.item.settings.precision,
                       (int)task_gain_db);
        AppController_SendSimpleResponse(request->sequence, 0, message);
      }
      break;
    }

    case COMMAND_TASK_DELETE:
      (void)snprintf(message,
                     sizeof(message),
                     "TASK_DELETE INDEX=%u",
                     (unsigned int)request->data.task_index);
      AppController_SendSimpleResponse(request->sequence,
                                       TaskScheduler_Delete(request->data.task_index) ? 0 : -1,
                                       message);
      break;

    case COMMAND_TRIGGER_CONFIG:
      if (s_mode != SYSTEM_MODE_IDLE)
      {
        AppController_SendSimpleResponse(request->sequence, -1, "TRIG_CONFIG_BUSY");
        break;
      }
      if ((request->data.trigger.output == TRIGGER_OUTPUT_REALTIME) &&
          !SystemConfig_ValidateRealtimeAcquisition(&request->data.trigger.settings))
      {
        AppController_SendSimpleResponse(request->sequence, -1, "TRIG_CONFIG_REALTIME_RATE_INVALID");
        break;
      }
      s_trigger_settings = request->data.trigger.settings;
      s_trigger_duration_ms = request->data.trigger.duration_ms;
      s_trigger_output = request->data.trigger.output;
      s_trigger_continuous = request->data.trigger.continuous;
      s_trigger_capture_count = 0u;
      s_trigger_wait_release = false;
      (void)Format_U64Decimal(s_trigger_duration_ms, u64_text[0], sizeof(u64_text[0]));
      if (SystemConfig_GetNominalGainDb(&s_trigger_settings, &nominal_gain_db))
      {
        (void)SystemConfig_GetHardwareGainDb(&s_trigger_settings, &hardware_gain_db);
        software_gain_db = SystemConfig_GetSoftwareGainDb(&s_trigger_settings);
        (void)snprintf(message,
                       sizeof(message),
                       "TRIG_CONFIG_OK level=LOW output=%s repeat=%s rate=%u prec=%s gain=%+dDB hw=%+dDB sw=%+dDB duration_ms=%s",
                       AppController_TriggerOutputToString(s_trigger_output),
                       AppController_TriggerRepeatToString(),
                       (unsigned int)s_trigger_settings.sample_rate_hz,
                       SamplePrecision_ToString(s_trigger_settings.precision),
                       (int)nominal_gain_db,
                       (int)hardware_gain_db,
                       (int)software_gain_db,
                       u64_text[0]);
      }
      else
      {
        (void)snprintf(message,
                       sizeof(message),
                       "TRIG_CONFIG_OK level=LOW output=%s repeat=%s rate=%u prec=%s gain=CUSTOM duration_ms=%s",
                       AppController_TriggerOutputToString(s_trigger_output),
                       AppController_TriggerRepeatToString(),
                       (unsigned int)s_trigger_settings.sample_rate_hz,
                       SamplePrecision_ToString(s_trigger_settings.precision),
                       u64_text[0]);
      }
      AppController_SendSimpleResponse(request->sequence, 0, message);
      break;

    case COMMAND_TRIGGER_ARM:
    {
      bool armed = AppController_ArmTrigger();

      (void)snprintf(message,
                     sizeof(message),
                     "TRIG_ARM level=LOW repeat=%s",
                     AppController_TriggerRepeatToString());
      AppController_SendSimpleResponse(request->sequence, armed ? 0 : -1, message);
      break;
    }

    case COMMAND_TRIGGER_STOP:
    {
      bool stop_ok = true;

      if ((s_mode == SYSTEM_MODE_TRIGGER_ARMED) ||
          (s_mode == SYSTEM_MODE_STORAGE_TRIGGERED) ||
          (s_mode == SYSTEM_MODE_REALTIME_TRIGGERED))
      {
        stop_ok = AppController_StopActiveCapture();
      }
      AppController_ResetTriggerDebounce();
      s_trigger_wait_release = false;
      AppController_SendSimpleResponse(request->sequence, stop_ok ? 0 : -1, "TRIG_STOP");
      break;
    }

    case COMMAND_TRIGGER_STATUS:
      (void)Format_U64Decimal(s_trigger_duration_ms, u64_text[0], sizeof(u64_text[0]));
      if (SystemConfig_GetNominalGainDb(&s_trigger_settings, &nominal_gain_db))
      {
        (void)SystemConfig_GetHardwareGainDb(&s_trigger_settings, &hardware_gain_db);
        software_gain_db = SystemConfig_GetSoftwareGainDb(&s_trigger_settings);
        (void)snprintf(message,
                       sizeof(message),
                       "TRIG_STATUS mode=%s level=LOW pin=%u output=%s repeat=%s count=%u wait=%u rate=%u prec=%s gain=%+dDB hw=%+dDB sw=%+dDB duration_ms=%s",
                       SystemMode_ToString(s_mode),
                       AppController_TriggerInputIsLow() ? 0u : 1u,
                       AppController_TriggerOutputToString(s_trigger_output),
                       AppController_TriggerRepeatToString(),
                       (unsigned int)s_trigger_capture_count,
                       s_trigger_wait_release ? 1u : 0u,
                       (unsigned int)s_trigger_settings.sample_rate_hz,
                       SamplePrecision_ToString(s_trigger_settings.precision),
                       (int)nominal_gain_db,
                       (int)hardware_gain_db,
                       (int)software_gain_db,
                       u64_text[0]);
      }
      else
      {
        (void)snprintf(message,
                       sizeof(message),
                       "TRIG_STATUS mode=%s level=LOW pin=%u output=%s repeat=%s count=%u wait=%u rate=%u prec=%s gain=CUSTOM duration_ms=%s",
                       SystemMode_ToString(s_mode),
                       AppController_TriggerInputIsLow() ? 0u : 1u,
                       AppController_TriggerOutputToString(s_trigger_output),
                       AppController_TriggerRepeatToString(),
                       (unsigned int)s_trigger_capture_count,
                       s_trigger_wait_release ? 1u : 0u,
                       (unsigned int)s_trigger_settings.sample_rate_hz,
                       SamplePrecision_ToString(s_trigger_settings.precision),
                       u64_text[0]);
      }
      AppController_SendSimpleResponse(request->sequence, 0, message);
      break;

    default:
      AppController_SendSimpleResponse(request->sequence, -1, "UNSUPPORTED");
      break;
  }
}

void AppController_Init(void)
{
  s_mode = SYSTEM_MODE_IDLE;
  s_current_settings = *SystemConfig_GetDefaultAcquisition();
  s_trigger_settings = s_current_settings;
  s_trigger_duration_ms = SYSTEM_TRIGGER_DURATION_DEFAULT_MS;
  s_trigger_output = TRIGGER_OUTPUT_STORAGE;
  s_trigger_continuous = false;
  s_trigger_wait_release = false;
  s_trigger_capture_count = 0u;
  AppController_ResetTriggerDebounce();
  s_trigger_capture_start_tick_ms = 0u;

  CommandProtocol_Init();
  TimeService_Init();
  TcpService_Init();
  StorageManager_Init();
  StorageRecorder_Init();
  FileService_Init();
  StreamService_Init();
  TaskScheduler_Init();
  TaskScheduler_SetCallbacks(AppController_TaskStartCallback, AppController_TaskStopCallback);

  StorageManager_ScanCards();
  APP_LOG_OK("AppController init done");
}

void AppController_Task(void)
{
  command_request_t request;

  TcpService_Task();
  if (TcpService_PollCommand(&request))
  {
    AppController_HandleCommand(&request);
  }
  FileService_Task(s_mode == SYSTEM_MODE_IDLE);

  AppController_HandleTrigger();
  if ((s_mode != SYSTEM_MODE_TRIGGER_ARMED) &&
      (s_mode != SYSTEM_MODE_STORAGE_TRIGGERED) &&
      (s_mode != SYSTEM_MODE_REALTIME_TRIGGERED))
  {
    TaskScheduler_Task(TimeService_NowMs(), TimeService_IsValid());
  }
  Acquisition_Task();
  if (Acquisition_IsOverloaded() &&
      ((s_mode == SYSTEM_MODE_REALTIME_STREAM) ||
       (s_mode == SYSTEM_MODE_REALTIME_TRIGGERED) ||
       (s_mode == SYSTEM_MODE_STORAGE_IMMEDIATE) ||
       (s_mode == SYSTEM_MODE_STORAGE_SCHEDULED) ||
       (s_mode == SYSTEM_MODE_STORAGE_TRIGGERED)))
  {
    acquisition_stats_t stats;

    Acquisition_GetStats(&stats);
    APP_LOG_ERR("acquisition stopped reason=%s: max=%u safe=%u budget=%u cycles drdy_low=%u errors_low=%u",
                Acquisition_IsMainStalled() ? "main_stall" : "isr_budget",
                (unsigned int)stats.isr_max_cycles,
                (unsigned int)((stats.isr_cycle_budget * 4u) / 5u),
                (unsigned int)stats.isr_cycle_budget,
                (unsigned int)(stats.drdy_events & 0xFFFFFFFFu),
                (unsigned int)(stats.read_errors & 0xFFFFFFFFu));
    (void)AppController_StopActiveCapture();
    AppController_SetMode(SYSTEM_MODE_ERROR);
    return;
  }
  StreamService_Task();
  AppController_HandleRealtimeHealth();
  AppController_HandleStorageRoute();
}

system_mode_t AppController_GetMode(void)
{
  return s_mode;
}
