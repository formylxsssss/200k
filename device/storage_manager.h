#ifndef __STORAGE_MANAGER_H__
#define __STORAGE_MANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "system_config.h"
#include "tf_slots.h"

typedef struct
{
  tf_slot_t slot;
  bool present;
  bool initialized;
  bool filesystem_ok;
  uint32_t last_error;
  uint32_t fs_error;
  uint64_t capacity_bytes;
  uint64_t free_bytes;
  uint32_t block_size;
} storage_card_info_t;

typedef struct
{
  bool scan_done;
  uint8_t present_count;
  uint8_t writable_count;
  uint64_t total_capacity_bytes;
  uint64_t total_free_bytes;
  storage_card_info_t cards[SYSTEM_STORAGE_MAX_CARDS];
} storage_summary_t;

void StorageManager_Init(void);
void StorageManager_ScanCards(void);
const storage_summary_t *StorageManager_GetSummary(void);
const storage_card_info_t *StorageManager_GetCard(tf_slot_t slot);
bool StorageManager_HasWritableCard(void);
bool StorageManager_SelectWritableCard(tf_slot_t *slot);
bool StorageManager_SelectWritableCardAfter(tf_slot_t previous_slot, tf_slot_t *slot);
bool StorageManager_GetVolumePath(tf_slot_t slot, char *buffer, uint32_t buffer_len);
bool StorageManager_FormatCard(tf_slot_t slot, char *message, uint32_t message_len);
bool StorageManager_FormatAllCards(char *message, uint32_t message_len);
bool StorageManager_WriteDemoData(uint8_t slot_mask, uint32_t bytes, char *message, uint32_t message_len);
void StorageManager_FormatSummary(char *buffer, uint32_t buffer_len);
void StorageManager_BuildRunPath(char *buffer,
                                 uint32_t buffer_len,
                                 uint64_t start_time_ms,
                                 uint32_t sample_rate_hz,
                                 uint32_t run_id,
                                 const char *source_tag);

#ifdef __cplusplus
}
#endif

#endif /* __STORAGE_MANAGER_H__ */
