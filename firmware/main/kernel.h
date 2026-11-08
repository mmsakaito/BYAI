#pragma once
#include "sysdef.h"

#define ADDR_PSRAM 0x3F800000
#define SIZE_PSRAM 8388608
#define SIZE_SRAM 64 * 1024
extern spi_device_handle_t hspi2;
void krnl_setup(void *p);
