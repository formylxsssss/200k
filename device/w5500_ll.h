#ifndef __W5500_LL_H__
#define __W5500_LL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "stm32u5xx_hal.h"

#define W5500_BSB_COMMON      0x00u
#define W5500_REG_MR          0x0000u
#define W5500_REG_GAR         0x0001u
#define W5500_REG_SUBR        0x0005u
#define W5500_REG_SHAR        0x0009u
#define W5500_REG_SIPR        0x000Fu
#define W5500_REG_VERSIONR    0x0039u

#define W5500_SOCKET_CONTROL   0u
#define W5500_SOCKET_DATA      1u
#define W5500_SOCKET_FILE      2u

#define W5500_SOCK_SR_CLOSED       0x00u
#define W5500_SOCK_SR_INIT         0x13u
#define W5500_SOCK_SR_LISTEN       0x14u
#define W5500_SOCK_SR_SYNSENT      0x15u
#define W5500_SOCK_SR_SYNRECV      0x16u
#define W5500_SOCK_SR_ESTABLISHED  0x17u
#define W5500_SOCK_SR_FIN_WAIT     0x18u
#define W5500_SOCK_SR_CLOSING      0x1Au
#define W5500_SOCK_SR_TIME_WAIT    0x1Bu
#define W5500_SOCK_SR_CLOSE_WAIT   0x1Cu
#define W5500_SOCK_SR_LAST_ACK     0x1Du

void W5500_Reset(void);
uint8_t W5500_Read8(uint16_t address, uint8_t block);
void W5500_Write8(uint16_t address, uint8_t block, uint8_t value);
void W5500_Read(uint16_t address, uint8_t block, uint8_t *buffer, size_t len);
void W5500_Write(uint16_t address, uint8_t block, const uint8_t *buffer, size_t len);
HAL_StatusTypeDef W5500_BasicInit(const uint8_t mac[6], const uint8_t ip[4], const uint8_t gateway[4], const uint8_t netmask[4]);
HAL_StatusTypeDef W5500_SocketOpenTcpServer(uint8_t socket_id, uint16_t port, uint8_t tx_kb, uint8_t rx_kb);
HAL_StatusTypeDef W5500_SocketServiceTcpServer(uint8_t socket_id, uint16_t port);
uint8_t W5500_SocketStatus(uint8_t socket_id);
uint16_t W5500_SocketRxSize(uint8_t socket_id);
uint16_t W5500_SocketTxFree(uint8_t socket_id);
size_t W5500_SocketRecv(uint8_t socket_id, uint8_t *buffer, size_t len);
size_t W5500_SocketSend(uint8_t socket_id, const uint8_t *buffer, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* __W5500_LL_H__ */
