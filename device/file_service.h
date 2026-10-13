#ifndef __FILE_SERVICE_H__
#define __FILE_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void FileService_Init(void);
void FileService_Task(bool storage_available);

#ifdef __cplusplus
}
#endif

#endif /* __FILE_SERVICE_H__ */
