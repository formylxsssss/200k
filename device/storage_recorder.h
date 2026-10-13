#ifndef __STORAGE_RECORDER_H__
#define __STORAGE_RECORDER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "system_config.h"
#include "tf_slots.h"

typedef enum
{
  RECORDER_SOURCE_MANUAL = 0,
  RECORDER_SOURCE_TASK_TABLE,
  RECORDER_SOURCE_TRIGGER,
} recorder_source_t;

typedef struct
{
  bool active;
  uint32_t run_id;
  uint32_t task_id;
  recorder_source_t source;
  uint64_t start_time_ms;
  uint64_t samples_written;
  uint64_t bytes_written;
  uint32_t segment_index;
  uint32_t switch_count;
  uint32_t last_switch_duration_ms;
  uint32_t write_error_count;
  uint32_t partial_write_bytes;
  tf_slot_t slot;
  acquisition_settings_t settings;
  char run_path[128];
} recorder_status_t;

void StorageRecorder_Init(void);
bool StorageRecorder_Start(const acquisition_settings_t *settings, recorder_source_t source, uint32_t task_id);
void StorageRecorder_Stop(uint64_t stop_time_ms);
bool StorageRecorder_WriteSamples(const int32_t *samples, size_t count);
const recorder_status_t *StorageRecorder_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __STORAGE_RECORDER_H__ */
