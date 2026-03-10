#pragma once
/*
  esp32x_fixes.h - fix esp32x toolchain

  Copyright (C) 2021  Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * Xtensa toolchain declares `int32_t` as `int` but RISC-V toolchain
 * declares `int32_t` as `long int` which causes compilation errors.
 *
 * See:
 *   https://github.com/espressif/esp-idf/issues/6906
 *   https://github.com/espressif/arduino-esp32/issues/5086
 *
 * You need to add the following lines in `build_flags`:
 *                            -I$PROJECT_DIR/include
 *                            -include "esp32x_fixes.h"
 */

#include <stdint.h>
#include <string.h>   // memcpy, memcmp

#ifdef __riscv

#undef __INT32_TYPE__
#define __INT32_TYPE__      int

#undef __UINT32_TYPE__
#define __UINT32_TYPE__     unsigned int

#endif // __riscv

// alias, deprecated for the chips after esp32s2
#ifdef CONFIG_IDF_TARGET_ESP32
  #define SPI_HOST    SPI1_HOST
  #define HSPI_HOST   SPI2_HOST
  #define VSPI_HOST   SPI3_HOST

#elif CONFIG_IDF_TARGET_ESP32S2
  #define SPI_HOST    SPI1_HOST
  #define FSPI_HOST   SPI2_HOST
  #define HSPI_HOST   SPI3_HOST
  #define VSPI_HOST   SPI3_HOST

#elif CONFIG_IDF_TARGET_ESP32S3
  #define SPI_HOST    SPI1_HOST
  #define FSPI_HOST   SPI2_HOST
  #define HSPI_HOST   SPI3_HOST
  #define VSPI_HOST   SPI3_HOST
  #define SPI_MOSI_DLEN_REG(x) SPI_MS_DLEN_REG(x)

#elif CONFIG_IDF_TARGET_ESP32C2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32P4
  #define SPI_HOST    SPI1_HOST
  #define HSPI_HOST   SPI2_HOST
  #define VSPI_HOST   SPI2_HOST  /* No SPI3_host on C2/C6/P4 */
  #define VSPI        SPI
  #define SPI_MOSI_DLEN_REG(x) SPI_MS_DLEN_REG(x)

#endif // TARGET

// ---- legacy type aliases (ESP8266 style) ----
#ifdef __cplusplus
  using uint8  = uint8_t;
  using uint16 = uint16_t;
  using uint32 = uint32_t;

  using sint8_t  = int8_t;
  using sint16_t = int16_t;
  using sint32_t = int32_t;
#else
  typedef uint8_t  uint8;
  typedef uint16_t uint16;
  typedef uint32_t uint32;

  typedef int8_t   sint8_t;
  typedef int16_t  sint16_t;
  typedef int32_t  sint32_t;
#endif


// ---- commonly used buffer sizes ----
#ifndef BUFFER_LENGTH
  #define BUFFER_LENGTH 128
#endif
#ifndef HTTP_UPLOAD_BUFLEN
  #define HTTP_UPLOAD_BUFLEN 2048
#endif
#ifndef MQTT_MAX_PACKET_SIZE
  #define MQTT_MAX_PACKET_SIZE 1200
#endif

// CANCELLED - MAIN CAUSE OF COMPILATION ERRORS AND UNIDENTIFIED CRASH BEHAVIOURS WHEN COMPILING IN DIFFERENT MODES / DIFFERENT LINKERS!!!!
// This trick makes sure that 'lto' optimizer does not inline `delay()
// so we can override it with `-Wl,--wrap=delay` linker directive
// #ifdef __cplusplus
// extern "C"
// #endif // _cplusplus
// void  delay(__UINT32_TYPE__ ms) __attribute__((noinline)) __attribute__ ((noclone));

