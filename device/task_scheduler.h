#ifndef __TASK_SCHEDULER_H__
#define __TASK_SCHEDULER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "system_config.h"

typedef void (*task_scheduler_start_cb_t)(const acquisition_task_item_t *task);
typedef void (*task_scheduler_stop_cb_t)(const acquisition_task_item_t *task);

typedef struct
{
  uint32_t table_index;
  acquisition_task_item_t item;
  bool running;
  bool completed;
  uint64_t active_stop_time_ms;
  uint32_t executed_count;
} task_scheduler_entry_t;

void TaskScheduler_Init(void);
bool TaskScheduler_Add(const acquisition_task_item_t *task);
bool TaskScheduler_Delete(uint32_t index);
void TaskScheduler_Clear(void);
void TaskScheduler_Task(uint64_t now_ms, bool time_valid);
void TaskScheduler_SetCallbacks(task_scheduler_start_cb_t start_cb, task_scheduler_stop_cb_t stop_cb);
uint32_t TaskScheduler_Count(void);
uint32_t TaskScheduler_MaxCount(void);
bool TaskScheduler_GetEntry(uint32_t index, task_scheduler_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif /* __TASK_SCHEDULER_H__ */
