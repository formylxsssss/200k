#include "task_scheduler.h"
#include "app_log.h"
#include <string.h>

typedef struct
{
  acquisition_task_item_t item;
  bool running;
  bool completed;
  uint64_t active_stop_time_ms;
  uint32_t executed_count;
} task_runtime_t;

static task_runtime_t s_tasks[SYSTEM_TASK_TABLE_MAX_ITEMS];
static task_scheduler_start_cb_t s_start_cb;
static task_scheduler_stop_cb_t s_stop_cb;

void TaskScheduler_Init(void)
{
  memset(s_tasks, 0, sizeof(s_tasks));
  s_start_cb = 0;
  s_stop_cb = 0;
  APP_LOG_INFO("TaskScheduler init max=%u", (unsigned int)SYSTEM_TASK_TABLE_MAX_ITEMS);
}

bool TaskScheduler_Add(const acquisition_task_item_t *task)
{
  if ((task == 0) || !task->enabled || !SystemConfig_ValidateAcquisition(&task->settings))
  {
    APP_LOG_ERR("task add invalid");
    return false;
  }

  for (uint32_t i = 0; i < SYSTEM_TASK_TABLE_MAX_ITEMS; ++i)
  {
    if (!s_tasks[i].item.enabled)
    {
      memset(&s_tasks[i], 0, sizeof(s_tasks[i]));
      s_tasks[i].item = *task;
      APP_LOG_OK("task add id=%u start_low=%u interval_low=%u duration_low=%u",
                 (unsigned int)task->task_id,
                 (unsigned int)(task->start_time_ms & 0xFFFFFFFFu),
                 (unsigned int)(task->interval_ms & 0xFFFFFFFFu),
                 (unsigned int)(task->duration_ms & 0xFFFFFFFFu));
      return true;
    }
  }

  APP_LOG_ERR("task table full");
  return false;
}

bool TaskScheduler_Delete(uint32_t index)
{
  if (index >= SYSTEM_TASK_TABLE_MAX_ITEMS)
  {
    APP_LOG_ERR("task delete invalid index=%u", (unsigned int)index);
    return false;
  }

  if (!s_tasks[index].item.enabled)
  {
    APP_LOG_WARN("task delete empty index=%u", (unsigned int)index);
    return false;
  }

  APP_LOG_WARN("task delete index=%u id=%u",
               (unsigned int)index,
               (unsigned int)s_tasks[index].item.task_id);
  if (s_tasks[index].running && (s_stop_cb != 0))
  {
    s_stop_cb(&s_tasks[index].item);
  }
  memset(&s_tasks[index], 0, sizeof(s_tasks[index]));
  return true;
}

void TaskScheduler_Clear(void)
{
  for (uint32_t i = 0u; i < SYSTEM_TASK_TABLE_MAX_ITEMS; ++i)
  {
    if (s_tasks[i].running && (s_stop_cb != 0))
    {
      s_stop_cb(&s_tasks[i].item);
    }
  }

  memset(s_tasks, 0, sizeof(s_tasks));
  APP_LOG_WARN("task table cleared");
}

void TaskScheduler_Task(uint64_t now_ms, bool time_valid)
{
  if (!time_valid)
  {
    return;
  }

  for (uint32_t i = 0; i < SYSTEM_TASK_TABLE_MAX_ITEMS; ++i)
  {
    task_runtime_t *task = &s_tasks[i];
    uint64_t start_time;

    if (!task->item.enabled)
    {
      continue;
    }

    if (task->completed)
    {
      continue;
    }

    if (task->running)
    {
      if ((task->item.duration_ms > 0u) && (now_ms >= task->active_stop_time_ms))
      {
        task->running = false;
        task->executed_count++;
        APP_LOG_OK("task stop id=%u executed=%u",
                   (unsigned int)task->item.task_id,
                   (unsigned int)task->executed_count);
        if (s_stop_cb != 0)
        {
          s_stop_cb(&task->item);
        }
      }
      continue;
    }

    if ((task->item.repeat_count != 0u) && (task->executed_count >= task->item.repeat_count))
    {
      task->completed = true;
      APP_LOG_INFO("task complete id=%u", (unsigned int)task->item.task_id);
      continue;
    }

    start_time = task->item.start_time_ms;
    if ((task->item.interval_ms > 0u) && (task->executed_count > 0u))
    {
      start_time += task->item.interval_ms * (uint64_t)task->executed_count;
    }

    if (now_ms >= start_time)
    {
      task->running = true;
      task->active_stop_time_ms = now_ms + task->item.duration_ms;
      APP_LOG_OK("task start id=%u now_low=%u",
                 (unsigned int)task->item.task_id,
                 (unsigned int)(now_ms & 0xFFFFFFFFu));
      if (s_start_cb != 0)
      {
        s_start_cb(&task->item);
      }
    }
  }
}

void TaskScheduler_SetCallbacks(task_scheduler_start_cb_t start_cb, task_scheduler_stop_cb_t stop_cb)
{
  s_start_cb = start_cb;
  s_stop_cb = stop_cb;
}

uint32_t TaskScheduler_Count(void)
{
  uint32_t count = 0u;

  for (uint32_t i = 0; i < SYSTEM_TASK_TABLE_MAX_ITEMS; ++i)
  {
    if (s_tasks[i].item.enabled)
    {
      count++;
    }
  }

  return count;
}

uint32_t TaskScheduler_MaxCount(void)
{
  return SYSTEM_TASK_TABLE_MAX_ITEMS;
}

bool TaskScheduler_GetEntry(uint32_t index, task_scheduler_entry_t *entry)
{
  if ((entry == 0) || (index >= SYSTEM_TASK_TABLE_MAX_ITEMS))
  {
    return false;
  }

  memset(entry, 0, sizeof(*entry));
  entry->table_index = index;
  entry->item = s_tasks[index].item;
  entry->running = s_tasks[index].running;
  entry->completed = s_tasks[index].completed;
  entry->active_stop_time_ms = s_tasks[index].active_stop_time_ms;
  entry->executed_count = s_tasks[index].executed_count;
  return true;
}
