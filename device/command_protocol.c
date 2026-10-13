#include "command_protocol.h"
#include "app_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t s_sequence;

static bool CommandProtocol_StartsWith(const uint8_t *data, size_t len, const char *text)
{
  size_t text_len = strlen(text);

  return (len >= text_len) && (memcmp(data, text, text_len) == 0);
}

static uint64_t CommandProtocol_ParseU64(const uint8_t *data, size_t len)
{
  uint64_t value = 0u;

  for (size_t i = 0; i < len; ++i)
  {
    if ((data[i] < (uint8_t)'0') || (data[i] > (uint8_t)'9'))
    {
      break;
    }
    value = (value * 10u) + (uint64_t)(data[i] - (uint8_t)'0');
  }

  return value;
}

static bool CommandProtocol_TokenStartsWith(const char *token, const char *prefix)
{
  return strncmp(token, prefix, strlen(prefix)) == 0;
}

static bool CommandProtocol_ParseConfigLine(const uint8_t *data, size_t len, acquisition_settings_t *settings)
{
  char line[128];
  char *token;
  char *value;
  uint32_t positional = 0u;

  if ((data == 0) || (settings == 0) || (len >= sizeof(line)))
  {
    return false;
  }

  memcpy(line, data, len);
  line[len] = '\0';
  *settings = *SystemConfig_GetDefaultAcquisition();

  token = strtok(line, " ,\r\n");
  while (token != 0)
  {
    if ((strcmp(token, "CONFIG") == 0) || (strcmp(token, "SET_ACQ") == 0))
    {
      token = strtok(0, " ,\r\n");
      continue;
    }

    value = strchr(token, '=');
    if (value != 0)
    {
      *value = '\0';
      value++;

      if ((strcmp(token, "RATE") == 0) || (strcmp(token, "SR") == 0))
      {
        settings->sample_rate_hz = (uint32_t)strtoul(value, 0, 0);
      }
      else if ((strcmp(token, "PREC") == 0) || (strcmp(token, "BITS") == 0))
      {
        settings->precision = (sample_precision_t)strtoul(value, 0, 0);
      }
      else if (strcmp(token, "GAIN") == 0)
      {
        if (!SystemConfig_ApplyGainDb((int32_t)strtol(value, 0, 0), settings))
        {
          return false;
        }
      }
      else
      {
        return false;
      }
    }
    else if (!CommandProtocol_TokenStartsWith(token, "#"))
    {
      switch (positional)
      {
        case 0:
          settings->sample_rate_hz = (uint32_t)strtoul(token, 0, 0);
          break;
        case 1:
          settings->precision = (sample_precision_t)strtoul(token, 0, 0);
          break;
        case 2:
        {
          if (!SystemConfig_ApplyGainDb((int32_t)strtol(token, 0, 0), settings))
          {
            return false;
          }
          break;
        }
        default:
          return false;
      }
      positional++;
    }

    token = strtok(0, " ,\r\n");
  }

  return SystemConfig_ValidateAcquisition(settings);
}

static bool CommandProtocol_ParseSdFormatLine(const uint8_t *data, size_t len, command_request_t *request)
{
  char line[128];
  char *token;
  char *value;

  if ((data == 0) || (request == 0) || (len >= sizeof(line)))
  {
    return false;
  }

  memcpy(line, data, len);
  line[len] = '\0';
  request->data.sd_format.slot = TF_SLOT_1;
  request->data.sd_format.all_slots = false;
  request->data.sd_format.fs_is_exfat = true;
  request->data.sd_format.confirmed = false;
  request->data.sd_format.confirmed_all = false;

  token = strtok(line, " ,\r\n");
  while (token != 0)
  {
    if (strcmp(token, "SD_FORMAT") == 0)
    {
      token = strtok(0, " ,\r\n");
      continue;
    }

    value = strchr(token, '=');
    if (value == 0)
    {
      return false;
    }

    *value = '\0';
    value++;

    if (strcmp(token, "SLOT") == 0)
    {
      if (strcmp(value, "ALL") == 0)
      {
        request->data.sd_format.all_slots = true;
      }
      else
      {
        uint32_t slot = (uint32_t)strtoul(value, 0, 0);
        if ((slot < (uint32_t)TF_SLOT_1) || (slot > (uint32_t)TF_SLOT_4))
        {
          return false;
        }
        request->data.sd_format.slot = (tf_slot_t)slot;
        request->data.sd_format.all_slots = false;
      }
    }
    else if (strcmp(token, "FS") == 0)
    {
      request->data.sd_format.fs_is_exfat = (strcmp(value, "EXFAT") == 0);
    }
    else if (strcmp(token, "CONFIRM") == 0)
    {
      request->data.sd_format.confirmed =
          (strcmp(value, "YES") == 0) || (strcmp(value, "ERASE_ALL") == 0);
      request->data.sd_format.confirmed_all = (strcmp(value, "ERASE_ALL") == 0);
    }
    else
    {
      return false;
    }

    token = strtok(0, " ,\r\n");
  }

  return true;
}

static bool CommandProtocol_ParseSdDemoLine(const uint8_t *data, size_t len, command_request_t *request)
{
  char line[128];
  char *token;
  char *value;

  if ((data == 0) || (request == 0) || (len >= sizeof(line)))
  {
    return false;
  }

  memcpy(line, data, len);
  line[len] = '\0';
  request->data.sd_demo.slot_mask = 0x0Fu;
  request->data.sd_demo.bytes = 4096u;

  token = strtok(line, " ,\r\n");
  while (token != 0)
  {
    if (strcmp(token, "SD_DEMO") == 0)
    {
      token = strtok(0, " ,\r\n");
      continue;
    }

    value = strchr(token, '=');
    if (value == 0)
    {
      return false;
    }

    *value = '\0';
    value++;

    if (strcmp(token, "SLOT") == 0)
    {
      if (strcmp(value, "ALL") == 0)
      {
        request->data.sd_demo.slot_mask = 0x0Fu;
      }
      else
      {
        uint32_t slot = (uint32_t)strtoul(value, 0, 0);
        if ((slot < (uint32_t)TF_SLOT_1) || (slot > (uint32_t)TF_SLOT_4))
        {
          return false;
        }
        request->data.sd_demo.slot_mask = (uint8_t)(1u << (slot - 1u));
      }
    }
    else if (strcmp(token, "MASK") == 0)
    {
      request->data.sd_demo.slot_mask = (uint8_t)(strtoul(value, 0, 0) & 0x0Fu);
    }
    else if (strcmp(token, "BYTES") == 0)
    {
      request->data.sd_demo.bytes = (uint32_t)strtoul(value, 0, 0);
    }
    else
    {
      return false;
    }

    token = strtok(0, " ,\r\n");
  }

  return true;
}

static bool CommandProtocol_ParseTaskAddLine(const uint8_t *data, size_t len, acquisition_task_item_t *task)
{
  char line[192];
  char *token;
  char *value;

  if ((data == 0) || (task == 0) || (len >= sizeof(line)))
  {
    return false;
  }

  memcpy(line, data, len);
  line[len] = '\0';
  memset(task, 0, sizeof(*task));
  task->enabled = true;
  task->repeat_count = 1u;
  task->settings = *SystemConfig_GetDefaultAcquisition();

  token = strtok(line, " ,\r\n");
  while (token != 0)
  {
    if (strcmp(token, "TASK_ADD") == 0)
    {
      token = strtok(0, " ,\r\n");
      continue;
    }

    value = strchr(token, '=');
    if (value == 0)
    {
      return false;
    }

    *value = '\0';
    value++;

    if (strcmp(token, "ID") == 0)
    {
      task->task_id = (uint32_t)strtoul(value, 0, 0);
    }
    else if ((strcmp(token, "START") == 0) || (strcmp(token, "START_MS") == 0))
    {
      task->start_time_ms = CommandProtocol_ParseU64((const uint8_t *)value, strlen(value));
    }
    else if ((strcmp(token, "INTERVAL") == 0) || (strcmp(token, "INTERVAL_MS") == 0))
    {
      task->interval_ms = CommandProtocol_ParseU64((const uint8_t *)value, strlen(value));
    }
    else if ((strcmp(token, "DURATION") == 0) || (strcmp(token, "DURATION_MS") == 0))
    {
      task->duration_ms = CommandProtocol_ParseU64((const uint8_t *)value, strlen(value));
    }
    else if (strcmp(token, "REPEAT") == 0)
    {
      task->repeat_count = (uint32_t)strtoul(value, 0, 0);
    }
    else if (strcmp(token, "ENABLE") == 0)
    {
      task->enabled = strtoul(value, 0, 0) != 0u;
    }
    else if ((strcmp(token, "RATE") == 0) || (strcmp(token, "SR") == 0))
    {
      task->settings.sample_rate_hz = (uint32_t)strtoul(value, 0, 0);
    }
    else if ((strcmp(token, "PREC") == 0) || (strcmp(token, "BITS") == 0))
    {
      task->settings.precision = (sample_precision_t)strtoul(value, 0, 0);
    }
    else if (strcmp(token, "GAIN") == 0)
    {
      if (!SystemConfig_ApplyGainDb((int32_t)strtol(value, 0, 0), &task->settings))
      {
        return false;
      }
    }
    else
    {
      return false;
    }

    token = strtok(0, " ,\r\n");
  }

  return task->enabled && (task->task_id != 0u) && (task->duration_ms != 0u) &&
         SystemConfig_ValidateAcquisition(&task->settings);
}

static bool CommandProtocol_ParseTaskIndexLine(const uint8_t *data, size_t len, const char *command, uint32_t *index)
{
  char line[64];
  char *token;
  char *value;

  if ((data == 0) || (index == 0) || (len >= sizeof(line)))
  {
    return false;
  }

  memcpy(line, data, len);
  line[len] = '\0';
  *index = 0u;

  token = strtok(line, " ,\r\n");
  while (token != 0)
  {
    if (strcmp(token, command) == 0)
    {
      token = strtok(0, " ,\r\n");
      continue;
    }

    value = strchr(token, '=');
    if (value == 0)
    {
      return false;
    }

    *value = '\0';
    value++;
    if ((strcmp(token, "INDEX") == 0) || (strcmp(token, "I") == 0))
    {
      *index = (uint32_t)strtoul(value, 0, 0);
    }
    else
    {
      return false;
    }

    token = strtok(0, " ,\r\n");
  }

  return true;
}

static bool CommandProtocol_ParseTriggerConfigLine(const uint8_t *data, size_t len, command_request_t *request)
{
  char line[192];
  char *token;
  char *value;

  if ((data == 0) || (request == 0) || (len >= sizeof(line)))
  {
    return false;
  }

  memcpy(line, data, len);
  line[len] = '\0';
  request->data.trigger.settings = *SystemConfig_GetDefaultAcquisition();
  request->data.trigger.duration_ms = SYSTEM_TRIGGER_DURATION_DEFAULT_MS;
  request->data.trigger.output = TRIGGER_OUTPUT_STORAGE;
  request->data.trigger.continuous = false;

  token = strtok(line, " ,\r\n");
  while (token != 0)
  {
    if ((strcmp(token, "TRIG_CONFIG") == 0) || (strcmp(token, "TRIGGER_CONFIG") == 0))
    {
      token = strtok(0, " ,\r\n");
      continue;
    }

    value = strchr(token, '=');
    if (value == 0)
    {
      return false;
    }

    *value = '\0';
    value++;

    if ((strcmp(token, "RATE") == 0) || (strcmp(token, "SR") == 0))
    {
      request->data.trigger.settings.sample_rate_hz = (uint32_t)strtoul(value, 0, 0);
    }
    else if ((strcmp(token, "PREC") == 0) || (strcmp(token, "BITS") == 0))
    {
      request->data.trigger.settings.precision = (sample_precision_t)strtoul(value, 0, 0);
    }
    else if (strcmp(token, "GAIN") == 0)
    {
      if (!SystemConfig_ApplyGainDb((int32_t)strtol(value, 0, 0), &request->data.trigger.settings))
      {
        return false;
      }
    }
    else if ((strcmp(token, "DURATION") == 0) ||
             (strcmp(token, "DURATION_MS") == 0) ||
             (strcmp(token, "DUR") == 0))
    {
      request->data.trigger.duration_ms = CommandProtocol_ParseU64((const uint8_t *)value, strlen(value));
    }
    else if ((strcmp(token, "OUTPUT") == 0) || (strcmp(token, "MODE") == 0) || (strcmp(token, "TARGET") == 0))
    {
      if ((strcmp(value, "STORAGE") == 0) || (strcmp(value, "STORE") == 0) || (strcmp(value, "SD") == 0))
      {
        request->data.trigger.output = TRIGGER_OUTPUT_STORAGE;
      }
      else if ((strcmp(value, "REALTIME") == 0) || (strcmp(value, "RT") == 0) || (strcmp(value, "TCP") == 0))
      {
        request->data.trigger.output = TRIGGER_OUTPUT_REALTIME;
      }
      else
      {
        return false;
      }
    }
    else if ((strcmp(token, "REPEAT") == 0) || (strcmp(token, "REARM") == 0))
    {
      if ((strcmp(value, "CONTINUOUS") == 0) || (strcmp(value, "CONT") == 0))
      {
        request->data.trigger.continuous = true;
      }
      else if ((strcmp(value, "ONCE") == 0) || (strcmp(value, "SINGLE") == 0))
      {
        request->data.trigger.continuous = false;
      }
      else
      {
        return false;
      }
    }
    else if (strcmp(token, "CONTINUOUS") == 0)
    {
      if (strcmp(value, "1") == 0)
      {
        request->data.trigger.continuous = true;
      }
      else if (strcmp(value, "0") == 0)
      {
        request->data.trigger.continuous = false;
      }
      else
      {
        return false;
      }
    }
    else
    {
      return false;
    }

    token = strtok(0, " ,\r\n");
  }

  return (request->data.trigger.duration_ms > 0u) &&
         (request->data.trigger.duration_ms <= SYSTEM_TRIGGER_DURATION_MAX_MS) &&
         SystemConfig_ValidateAcquisition(&request->data.trigger.settings);
}

void CommandProtocol_Init(void)
{
  s_sequence = 1u;
  APP_LOG_INFO("CommandProtocol init");
}

bool CommandProtocol_ParseText(const uint8_t *data, size_t len, command_request_t *request)
{
  if ((data == 0) || (request == 0) || (len == 0u))
  {
    return false;
  }

  memset(request, 0, sizeof(*request));
  request->sequence = s_sequence++;

  if (CommandProtocol_StartsWith(data, len, "STATUS"))
  {
    request->type = COMMAND_QUERY_STATUS;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "SD_SCAN"))
  {
    request->type = COMMAND_SD_SCAN;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "SD_STATUS"))
  {
    request->type = COMMAND_QUERY_SD;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "SD_FORMAT"))
  {
    request->type = COMMAND_SD_FORMAT;
    return CommandProtocol_ParseSdFormatLine(data, len, request);
  }

  if (CommandProtocol_StartsWith(data, len, "SD_DEMO"))
  {
    request->type = COMMAND_SD_DEMO;
    return CommandProtocol_ParseSdDemoLine(data, len, request);
  }

  if (CommandProtocol_StartsWith(data, len, "CONFIG") || CommandProtocol_StartsWith(data, len, "SET_ACQ"))
  {
    request->type = COMMAND_SET_ACQUISITION;
    return CommandProtocol_ParseConfigLine(data, len, &request->data.acquisition);
  }

  if (CommandProtocol_StartsWith(data, len, "START_RT"))
  {
    request->type = COMMAND_START_REALTIME;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "STOP_RT"))
  {
    request->type = COMMAND_STOP_REALTIME;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "START_STORE"))
  {
    request->type = COMMAND_START_STORAGE;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "STOP_STORE"))
  {
    request->type = COMMAND_STOP_STORAGE;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "TASK_CLEAR"))
  {
    request->type = COMMAND_TASK_CLEAR;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "TASK_LIST"))
  {
    request->type = COMMAND_TASK_LIST;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "TASK_GET"))
  {
    request->type = COMMAND_TASK_GET;
    return CommandProtocol_ParseTaskIndexLine(data, len, "TASK_GET", &request->data.task_index);
  }

  if (CommandProtocol_StartsWith(data, len, "TASK_DELETE"))
  {
    request->type = COMMAND_TASK_DELETE;
    return CommandProtocol_ParseTaskIndexLine(data, len, "TASK_DELETE", &request->data.task_index);
  }

  if (CommandProtocol_StartsWith(data, len, "TASK_ADD"))
  {
    request->type = COMMAND_TASK_ADD;
    return CommandProtocol_ParseTaskAddLine(data, len, &request->data.task);
  }

  if (CommandProtocol_StartsWith(data, len, "TRIG_CONFIG") ||
      CommandProtocol_StartsWith(data, len, "TRIGGER_CONFIG"))
  {
    request->type = COMMAND_TRIGGER_CONFIG;
    return CommandProtocol_ParseTriggerConfigLine(data, len, request);
  }

  if (CommandProtocol_StartsWith(data, len, "TRIG_ARM") ||
      CommandProtocol_StartsWith(data, len, "TRIGGER_ARM"))
  {
    request->type = COMMAND_TRIGGER_ARM;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "TRIG_STOP") ||
      CommandProtocol_StartsWith(data, len, "TRIG_DISARM") ||
      CommandProtocol_StartsWith(data, len, "TRIGGER_STOP") ||
      CommandProtocol_StartsWith(data, len, "TRIGGER_DISARM"))
  {
    request->type = COMMAND_TRIGGER_STOP;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "TRIG_STATUS") ||
      CommandProtocol_StartsWith(data, len, "TRIGGER_STATUS"))
  {
    request->type = COMMAND_TRIGGER_STATUS;
    return true;
  }

  if (CommandProtocol_StartsWith(data, len, "TIME "))
  {
    request->type = COMMAND_SYNC_TIME;
    request->data.unix_time_ms = CommandProtocol_ParseU64(&data[5], len - 5u);
    return true;
  }

  return false;
}

void CommandProtocol_FormatResponse(const command_response_t *response, uint8_t *buffer, size_t buffer_len, size_t *out_len)
{
  int len;

  if ((response == 0) || (buffer == 0) || (buffer_len == 0u))
  {
    if (out_len != 0)
    {
      *out_len = 0u;
    }
    return;
  }

  len = snprintf((char *)buffer, buffer_len, "SEQ=%u STATUS=%d MSG=%s\r\n",
                 (unsigned int)response->sequence,
                 (int)response->status,
                 response->message);

  if (len < 0)
  {
    len = 0;
  }

  if ((size_t)len >= buffer_len)
  {
    len = (int)buffer_len - 1;
  }

  if (out_len != 0)
  {
    *out_len = (size_t)len;
  }
}
