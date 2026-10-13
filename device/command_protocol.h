#ifndef __COMMAND_PROTOCOL_H__
#define __COMMAND_PROTOCOL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "storage_manager.h"
#include "system_config.h"

typedef enum
{
  COMMAND_NONE = 0,
  COMMAND_QUERY_STATUS,
  COMMAND_SYNC_TIME,
  COMMAND_SET_ACQUISITION,
  COMMAND_START_REALTIME,
  COMMAND_STOP_REALTIME,
  COMMAND_START_STORAGE,
  COMMAND_STOP_STORAGE,
  COMMAND_SD_SCAN,
  COMMAND_QUERY_SD,
  COMMAND_SD_FORMAT,
  COMMAND_SD_DEMO,
  COMMAND_TASK_ADD,
  COMMAND_TASK_CLEAR,
  COMMAND_TASK_LIST,
  COMMAND_TASK_GET,
  COMMAND_TASK_DELETE,
  COMMAND_TRIGGER_CONFIG,
  COMMAND_TRIGGER_ARM,
  COMMAND_TRIGGER_STOP,
  COMMAND_TRIGGER_STATUS,
  COMMAND_FILE_LIST,
  COMMAND_FILE_READ,
} command_type_t;

typedef struct
{
  command_type_t type;
  uint32_t sequence;
  union
  {
    uint64_t unix_time_ms;
    acquisition_settings_t acquisition;
    acquisition_task_item_t task;
    struct
    {
      tf_slot_t slot;
      bool all_slots;
      bool fs_is_exfat;
      bool confirmed;
      bool confirmed_all;
    } sd_format;
    struct
    {
      uint8_t slot_mask;
      uint32_t bytes;
    } sd_demo;
    struct
    {
      acquisition_settings_t settings;
      uint64_t duration_ms;
      trigger_output_t output;
      bool continuous;
    } trigger;
    uint32_t task_index;
  } data;
} command_request_t;

typedef struct
{
  uint32_t sequence;
  int32_t status;
  char message[256];
} command_response_t;

void CommandProtocol_Init(void);
bool CommandProtocol_ParseText(const uint8_t *data, size_t len, command_request_t *request);
void CommandProtocol_FormatResponse(const command_response_t *response, uint8_t *buffer, size_t buffer_len, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* __COMMAND_PROTOCOL_H__ */
