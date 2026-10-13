#include "app_config.h"

static const app_net_config_t s_net_config =
{
  .mac = {0x02, 0x20, 0x0A, 0x20, 0x00, 0x01},
  .ip = {192, 168, 1, 80},
  .gateway = {192, 168, 1, 1},
  .netmask = {255, 255, 255, 0},
};

const app_net_config_t *AppConfig_GetNet(void)
{
  return &s_net_config;
}
