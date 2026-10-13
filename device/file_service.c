#include "file_service.h"
#include "app_log.h"
#include "ff.h"
#include "format_utils.h"
#include "storage_manager.h"
#include "system_config.h"
#include "tf_slots.h"
#include "w5500_ll.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_SERVICE_RX_BUFFER_SIZE 192u
#define FILE_SERVICE_TX_BUFFER_SIZE 512u
#define FILE_SERVICE_MAX_ENTRIES    64u
#define FILE_SERVICE_SEND_TIMEOUT   2000u

typedef enum
{
  FILE_SERVICE_IDLE = 0,
  FILE_SERVICE_BUSY,
} file_service_state_t;

typedef struct
{
  file_service_state_t state;
  uint8_t rx_buffer[FILE_SERVICE_RX_BUFFER_SIZE];
  size_t rx_len;
  uint8_t last_status;
} file_service_context_t;

static file_service_context_t s_file_service;

static bool FileService_RxHasLine(void)
{
  for (size_t i = 0u; i < s_file_service.rx_len; ++i)
  {
    if ((s_file_service.rx_buffer[i] == (uint8_t)'\n') ||
        (s_file_service.rx_buffer[i] == (uint8_t)'\r'))
    {
      return true;
    }
  }

  return s_file_service.rx_len >= sizeof(s_file_service.rx_buffer);
}

static bool FileService_PollLine(char *line, size_t line_len)
{
  size_t line_end = 0u;
  size_t consume_len;
  size_t copy_len;
  bool has_line = false;

  if ((line == 0) || (line_len == 0u) || (s_file_service.rx_len == 0u) || !FileService_RxHasLine())
  {
    return false;
  }

  for (size_t i = 0u; i < s_file_service.rx_len; ++i)
  {
    if ((s_file_service.rx_buffer[i] == (uint8_t)'\n') ||
        (s_file_service.rx_buffer[i] == (uint8_t)'\r'))
    {
      line_end = i;
      has_line = true;
      break;
    }
  }

  if (!has_line)
  {
    line_end = s_file_service.rx_len;
  }

  consume_len = line_end;
  while ((consume_len < s_file_service.rx_len) &&
         ((s_file_service.rx_buffer[consume_len] == (uint8_t)'\n') ||
          (s_file_service.rx_buffer[consume_len] == (uint8_t)'\r')))
  {
    consume_len++;
  }

  copy_len = (line_end < (line_len - 1u)) ? line_end : (line_len - 1u);
  memcpy(line, s_file_service.rx_buffer, copy_len);
  while ((copy_len > 0u) && ((line[copy_len - 1u] == ' ') || (line[copy_len - 1u] == '\t')))
  {
    copy_len--;
  }
  line[copy_len] = '\0';

  memmove(s_file_service.rx_buffer,
          &s_file_service.rx_buffer[consume_len],
          s_file_service.rx_len - consume_len);
  s_file_service.rx_len -= consume_len;
  return copy_len > 0u;
}

static bool FileService_SendBytes(const uint8_t *data, size_t len)
{
  size_t sent_total = 0u;
  uint32_t start_tick;

  if ((data == 0) || (len == 0u))
  {
    return false;
  }

  while (sent_total < len)
  {
    size_t remain = len - sent_total;
    size_t chunk = (remain > FILE_SERVICE_TX_BUFFER_SIZE) ? FILE_SERVICE_TX_BUFFER_SIZE : remain;

    start_tick = HAL_GetTick();
    while (W5500_SocketTxFree(W5500_SOCKET_FILE) < chunk)
    {
      if (W5500_SocketStatus(W5500_SOCKET_FILE) != W5500_SOCK_SR_ESTABLISHED)
      {
        return false;
      }
      if ((HAL_GetTick() - start_tick) > FILE_SERVICE_SEND_TIMEOUT)
      {
        return false;
      }
    }

    if (W5500_SocketSend(W5500_SOCKET_FILE, &data[sent_total], chunk) != chunk)
    {
      return false;
    }
    sent_total += chunk;
  }

  return true;
}

static bool FileService_SendLine(const char *line)
{
  char tx[FILE_SERVICE_TX_BUFFER_SIZE];
  int len;

  if (line == 0)
  {
    return false;
  }

  len = snprintf(tx, sizeof(tx), "%s\r\n", line);
  if (len <= 0)
  {
    return false;
  }
  if ((size_t)len >= sizeof(tx))
  {
    len = (int)sizeof(tx) - 1;
  }

  return FileService_SendBytes((const uint8_t *)tx, (size_t)len);
}

static bool FileService_ParseSlotPath(char *line, const char *command, tf_slot_t *slot, char *path, size_t path_len)
{
  char *token;
  char *value;

  if ((line == 0) || (command == 0) || (slot == 0) || (path == 0) || (path_len == 0u))
  {
    return false;
  }

  *slot = TF_SLOT_1;
  (void)snprintf(path, path_len, "/HYDROPHONE");

  token = strtok(line, " \r\n");
  while (token != 0)
  {
    if (strcmp(token, command) == 0)
    {
      token = strtok(0, " \r\n");
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
      uint32_t parsed_slot = (uint32_t)strtoul(value, 0, 0);
      if ((parsed_slot < (uint32_t)TF_SLOT_1) || (parsed_slot > (uint32_t)TF_SLOT_4))
      {
        return false;
      }
      *slot = (tf_slot_t)parsed_slot;
    }
    else if (strcmp(token, "PATH") == 0)
    {
      if ((value[0] == '\0') || (strstr(value, "..") != 0))
      {
        return false;
      }
      if (value[0] != '/')
      {
        return false;
      }
      (void)snprintf(path, path_len, "%s", value);
    }
    else
    {
      return false;
    }

    token = strtok(0, " \r\n");
  }

  return true;
}

static bool FileService_BuildFullPath(tf_slot_t slot, const char *path, char *full_path, size_t full_path_len)
{
  char volume[4];

  if ((path == 0) || (full_path == 0) || (full_path_len == 0u))
  {
    return false;
  }
  if (!StorageManager_GetVolumePath(slot, volume, sizeof(volume)))
  {
    return false;
  }

  return snprintf(full_path, full_path_len, "%s%s", volume, path) > 0;
}

static bool FileService_Mount(tf_slot_t slot, FATFS *fs, char *volume, size_t volume_len)
{
  FRESULT result;

  if ((fs == 0) || !StorageManager_GetVolumePath(slot, volume, (uint32_t)volume_len))
  {
    return false;
  }

  memset(fs, 0, sizeof(*fs));
  result = f_mount(fs, volume, 1);
  if (result != FR_OK)
  {
    char line[80];
    (void)snprintf(line, sizeof(line), "ERR MOUNT SLOT=%u FR=%u", (unsigned int)slot, (unsigned int)result);
    (void)FileService_SendLine(line);
    APP_LOG_ERR("file service mount failed slot=%u fr=%u", (unsigned int)slot, (unsigned int)result);
    TFSlot_AllOff();
    return false;
  }

  return true;
}

static void FileService_Unmount(const char *volume)
{
  if ((volume != 0) && (volume[0] != '\0'))
  {
    (void)f_mount(0, volume, 0);
  }
  TFSlot_AllOff();
}

static void FileService_HandleList(char *line)
{
  FATFS fs;
  DIR dir;
  FILINFO info;
  FRESULT result;
  tf_slot_t slot;
  char path[128];
  char full_path[160];
  char volume[4];
  char tx[FILE_SERVICE_TX_BUFFER_SIZE];
  char size_text[FORMAT_U64_DECIMAL_SIZE];
  uint32_t count = 0u;

  if (!FileService_ParseSlotPath(line, "LIST", &slot, path, sizeof(path)) ||
      !FileService_BuildFullPath(slot, path, full_path, sizeof(full_path)))
  {
    (void)FileService_SendLine("ERR BAD_LIST");
    return;
  }

  if (!FileService_Mount(slot, &fs, volume, sizeof(volume)))
  {
    return;
  }

  result = f_opendir(&dir, full_path);
  if (result != FR_OK)
  {
    (void)snprintf(tx, sizeof(tx), "ERR LIST SLOT=%u FR=%u", (unsigned int)slot, (unsigned int)result);
    (void)FileService_SendLine(tx);
    FileService_Unmount(volume);
    return;
  }

  (void)snprintf(tx, sizeof(tx), "OK LIST SLOT=%u PATH=%s", (unsigned int)slot, path);
  (void)FileService_SendLine(tx);
  while (count < FILE_SERVICE_MAX_ENTRIES)
  {
    result = f_readdir(&dir, &info);
    if ((result != FR_OK) || (info.fname[0] == '\0'))
    {
      break;
    }
    if ((strcmp(info.fname, ".") == 0) || (strcmp(info.fname, "..") == 0))
    {
      continue;
    }

    (void)Format_U64Decimal((uint64_t)info.fsize, size_text, sizeof(size_text));
    (void)snprintf(tx,
                   sizeof(tx),
                   "ENTRY TYPE=%c NAME=%s SIZE=%s",
                   (info.fattrib & AM_DIR) ? 'D' : 'F',
                   info.fname,
                   size_text);
    if (!FileService_SendLine(tx))
    {
      break;
    }
    count++;
  }

  (void)f_closedir(&dir);
  (void)snprintf(tx, sizeof(tx), "END LIST COUNT=%u", (unsigned int)count);
  (void)FileService_SendLine(tx);
  FileService_Unmount(volume);
  APP_LOG_OK("file list slot=%u path=%s count=%u", (unsigned int)slot, path, (unsigned int)count);
}

static void FileService_HandleGet(char *line)
{
  FATFS fs;
  FIL file;
  FRESULT result;
  UINT read_len = 0u;
  tf_slot_t slot;
  char path[128];
  char full_path[160];
  char volume[4];
  char tx[FILE_SERVICE_TX_BUFFER_SIZE];
  char size_text[FORMAT_U64_DECIMAL_SIZE];
  uint8_t data[FILE_SERVICE_TX_BUFFER_SIZE];
  FSIZE_t file_size;
  FSIZE_t sent = 0u;

  if (!FileService_ParseSlotPath(line, "GET", &slot, path, sizeof(path)) ||
      !FileService_BuildFullPath(slot, path, full_path, sizeof(full_path)))
  {
    (void)FileService_SendLine("ERR BAD_GET");
    return;
  }

  if (!FileService_Mount(slot, &fs, volume, sizeof(volume)))
  {
    return;
  }

  result = f_open(&file, full_path, FA_READ);
  if (result != FR_OK)
  {
    (void)snprintf(tx, sizeof(tx), "ERR GET SLOT=%u FR=%u", (unsigned int)slot, (unsigned int)result);
    (void)FileService_SendLine(tx);
    FileService_Unmount(volume);
    return;
  }

  file_size = f_size(&file);
  (void)Format_U64Decimal((uint64_t)file_size, size_text, sizeof(size_text));
  (void)snprintf(tx,
                 sizeof(tx),
                 "OK FILE SLOT=%u PATH=%s SIZE=%s",
                 (unsigned int)slot,
                 path,
                 size_text);
  if (!FileService_SendLine(tx))
  {
    (void)f_close(&file);
    FileService_Unmount(volume);
    return;
  }

  while (sent < file_size)
  {
    UINT request = (file_size - sent) > sizeof(data) ? (UINT)sizeof(data) : (UINT)(file_size - sent);

    read_len = 0u;
    result = f_read(&file, data, request, &read_len);
    if ((result != FR_OK) || (read_len == 0u))
    {
      APP_LOG_ERR("file get read failed slot=%u fr=%u sent_low=%u",
                  (unsigned int)slot,
                  (unsigned int)result,
                  (unsigned int)(sent & 0xFFFFFFFFu));
      break;
    }
    if (!FileService_SendBytes(data, read_len))
    {
      APP_LOG_ERR("file get send failed slot=%u sent_low=%u",
                  (unsigned int)slot,
                  (unsigned int)(sent & 0xFFFFFFFFu));
      break;
    }
    sent += read_len;
  }

  (void)f_close(&file);
  FileService_Unmount(volume);
  APP_LOG_OK("file get slot=%u path=%s size_low=%u sent_low=%u",
             (unsigned int)slot,
             path,
             (unsigned int)(file_size & 0xFFFFFFFFu),
             (unsigned int)(sent & 0xFFFFFFFFu));
}

void FileService_Init(void)
{
  memset(&s_file_service, 0, sizeof(s_file_service));
  s_file_service.last_status = 0xFFu;
  APP_LOG_INFO("FileService init port=%u", (unsigned int)SYSTEM_TCP_FILE_PORT);
}

void FileService_Task(bool storage_available)
{
  uint8_t status;
  uint16_t rx_size;
  size_t room;
  size_t received;
  char line[FILE_SERVICE_RX_BUFFER_SIZE];

  status = W5500_SocketStatus(W5500_SOCKET_FILE);
  if (status != s_file_service.last_status)
  {
    APP_LOG_INFO("TCP file socket sr=0x%02X", (unsigned int)status);
    s_file_service.last_status = status;
  }

  if (status != W5500_SOCK_SR_ESTABLISHED)
  {
    s_file_service.rx_len = 0u;
    return;
  }

  rx_size = W5500_SocketRxSize(W5500_SOCKET_FILE);
  if (rx_size > 0u)
  {
    room = sizeof(s_file_service.rx_buffer) - s_file_service.rx_len;
    if (room == 0u)
    {
      s_file_service.rx_len = 0u;
      room = sizeof(s_file_service.rx_buffer);
    }

    received = W5500_SocketRecv(W5500_SOCKET_FILE, &s_file_service.rx_buffer[s_file_service.rx_len], room);
    s_file_service.rx_len += received;
  }

  if (!FileService_PollLine(line, sizeof(line)))
  {
    return;
  }

  if (!storage_available)
  {
    (void)FileService_SendLine("ERR BUSY");
    return;
  }

  s_file_service.state = FILE_SERVICE_BUSY;
  if (strncmp(line, "LIST", 4u) == 0)
  {
    FileService_HandleList(line);
  }
  else if (strncmp(line, "GET", 3u) == 0)
  {
    FileService_HandleGet(line);
  }
  else
  {
    (void)FileService_SendLine("ERR UNKNOWN");
  }
  s_file_service.state = FILE_SERVICE_IDLE;
}
