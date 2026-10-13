#ifndef __FORMAT_UTILS_H__
#define __FORMAT_UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FORMAT_U64_DECIMAL_SIZE 21u

bool Format_U64Decimal(uint64_t value, char *buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif

#endif /* __FORMAT_UTILS_H__ */
