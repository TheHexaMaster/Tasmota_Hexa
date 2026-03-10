/*
  TasmotaSerial.cpp - Implementation of software serial with hardware serial fallback for Tasmota

  Copyright (C) 2021  Theo Arends

  This library is free software: you can redistribute it and/or modify
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

#include <Arduino.h>

#include <TasmotaSerial.h>

extern void AddLog(uint32_t loglevel, PGM_P formatP, ...);
enum LoggingLevels {LOG_LEVEL_NONE, LOG_LEVEL_ERROR, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG_MORE};


#ifdef ESP32

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"

static uint32_t tasmota_serial_uart_bitmap = 0;      // Assigned UARTs

#endif  // ESP32

TasmotaSerial::TasmotaSerial(int receive_pin, int transmit_pin, int hardware_fallback, int nwmode, int buffer_size, bool invert) {
  m_valid = false;
  m_hardserial = false;
  m_hardswap = false;
  m_overflow = false;
  m_invert = invert;
  m_data_bits = 8;
  m_stop_bits = 1;
  m_nwmode = nwmode;
  serial_buffer_size = buffer_size;
  m_rx_pin = receive_pin;
  m_tx_pin = transmit_pin;
  m_tx_enable_pin = -1;
  m_in_pos = 0;
  m_out_pos = 0;

#ifdef ESP32
  if ((receive_pin >= 0) && !GPIO_IS_VALID_GPIO(receive_pin)) { return; }
  if ((transmit_pin >= 0) && !GPIO_IS_VALID_OUTPUT_GPIO(transmit_pin)) { return; }
  m_hardserial = true;
  TSerial = nullptr;
#endif  // ESP32
  m_valid = true;
}

void TasmotaSerial::end(void) {

#ifdef ESP32
//  Serial.printf("TSR: Freeing UART%d\n", m_uart);

  TSerial->end();
  bitClear(tasmota_serial_uart_bitmap, m_uart);
#endif  // ESP32
}

TasmotaSerial::~TasmotaSerial(void) {
  if (m_valid) {
    end();
  }
}

bool TasmotaSerial::isValidGPIOpin(int pin) {

#ifdef ESP32
  return GPIO_IS_VALID_OUTPUT_GPIO(pin);
#endif
}

void TasmotaSerial::setTransmitEnablePin(int tx_enable_pin) {
#ifdef ESP32
  if ((tx_enable_pin > -1) && isValidGPIOpin(tx_enable_pin)) {
#endif
    m_tx_enable_pin = tx_enable_pin;
    pinMode(m_tx_enable_pin, OUTPUT);
    digitalWrite(m_tx_enable_pin, LOW);
  }
}

#ifdef ESP32
bool TasmotaSerial::freeUart(void) {
  // If users selects default serial interface keep using UART0
  //  From cores\esp32\HardwareSerial.cpp: There is always Seria0 for UART0
  int pin_soc_rx0 = gpioNumberToDigitalPin(SOC_RX0);
  int pin_soc_tx0 = gpioNumberToDigitalPin(SOC_TX0);
  if (((pin_soc_rx0 == m_rx_pin) && (pin_soc_tx0 == m_tx_pin)) ||
      ((pin_soc_rx0 == m_tx_pin) && (pin_soc_tx0 == m_rx_pin))) {
    m_uart = uart_port_t(0);
    bitSet(tasmota_serial_uart_bitmap, m_uart);
    return true;
  } else {
    // Find a free UART which may end up as UART0
    for (uint32_t i = SOC_UART_HP_NUM -1; i >= 0; i--) {
      if (0 == bitRead(tasmota_serial_uart_bitmap, i)) {
        m_uart = uart_port_t(i);
        bitSet(tasmota_serial_uart_bitmap, m_uart);
        return true;
      }
    }
  }
  return false;
}

void TasmotaSerial::Esp32Begin(void) {
  // Workaround IDF #14787 introduced in Tasmota v14.5.0, Core 3.1.1, IDF 5.3.2.250120
  //  which kept new Rx low instead of float
  pinMode(m_rx_pin, INPUT_PULLUP);
  pinMode(m_tx_pin, INPUT_PULLUP);
  TSerial->begin(m_speed, m_config, m_rx_pin, m_tx_pin, m_invert);
  // For low bit rate, below 9600, set the Full RX threshold at 10 bytes instead of the default 120
  if (m_speed <= 9600) {
    // At 9600, 10 chars are ~10ms
    uart_set_rx_full_threshold(m_uart, 10);
  } else {
    // At 19200, 120 chars are ~60ms
    // At 76800, 120 chars are ~15ms
    uart_set_rx_full_threshold(m_uart, 120);
  }
/*
  } else if (m_speed < 115200) {
    // At 19200, 120 chars are ~60ms
    // At 76800, 120 chars are ~15ms
    uart_set_rx_full_threshold(m_uart, 120);
  } else if (m_speed == 115200) {
    // At 115200, 256 chars are ~20ms
    // Zigbee requires to keep frames together, i.e. 256 bytes max
    uart_set_rx_full_threshold(m_uart, 256);
  } else {
    // At even higher speeds set 75% of the buffer
    uart_set_rx_full_threshold(m_uart, serial_buffer_size * 3 / 4);
  }
*/
  // For bitrate below 115200, set the Rx time out to 6 chars instead of the default 10
  if (m_speed < 115200) {
    // At 76800 the timeout is ~1ms
    uart_set_rx_timeout(m_uart, 6);
  }
}
#endif

size_t TasmotaSerial::setRxBufferSize(size_t size) {
  if (size != serial_buffer_size) {
    if (m_hardserial) {
      if (size > 256) {      // Default hardware serial Rx buffer size

#ifdef ESP32
        if (TSerial) {
          // RX Buffer can't be resized when Serial is already running
          serial_buffer_size = size;
          TSerial->flush();
          TSerial->end();
          delay(10);         // Allow time to cleanup queues - if not used hangs ESP32
          TSerial->setRxBufferSize(serial_buffer_size);
          Esp32Begin();
        }
#endif  // ESP32
      }
    }
    else if (m_buffer) {
      uint8_t *m_buffer_temp = (uint8_t*)malloc(size);  // Allocate new buffer
      if (m_buffer_temp) {                              // If succesful de-allocate old buffer
        free(m_buffer);
        m_buffer = m_buffer_temp;
        serial_buffer_size = size;
      }
    }
  }
  return serial_buffer_size;
}

bool TasmotaSerial::begin(uint32_t speed, uint32_t config) {
  if (!m_valid) { return false; }

  if (m_hardserial) {
    if (serial_buffer_size < 256) {
      serial_buffer_size = 256;
    }

#ifdef ESP32
    if (TSerial == nullptr) {      // Allow for dynamic change in baudrate or config
      if (freeUart()) {            // We prefer UART1 and UART2 and keep UART0 for debugging
#if ARDUINO_USB_MODE
        TSerial = new HardwareSerial(m_uart);
#else
        if (0 == m_uart) {         // From cores\esp32\HardwareSerial.cpp: There is always Seria0 for UART0
/*
          // Not needed anymore since Core 3.1.0
          Serial.flush();
          Serial.end();
          delay(10);             // Allow time to cleanup queues - if not used hangs ESP32
*/
          TSerial = &Serial;
        } else {
          TSerial = new HardwareSerial(m_uart);
        }
#endif  // ARDUINO_USB_MODE
        if (serial_buffer_size > 256) {  // RX Buffer can't be resized when Serial is already running (HardwareSerial.cpp)
          TSerial->setRxBufferSize(serial_buffer_size);
        }
      } else {
        m_valid = false;
        return m_valid;            // As we currently only support hardware serial on ESP32 it's safe to exit here
      }
    }
    m_speed = speed;
    m_config = config;
    Esp32Begin();
//    Serial.printf("TSR: Using UART%d\n", m_uart);
#endif  // ESP32
  } else {
    // #define UART_NB_BIT_5         0B00000000
    // #define UART_NB_BIT_6         0B00000100
    // #define UART_NB_BIT_7         0B00001000
    // #define UART_NB_BIT_8         0B00001100
    m_data_bits = 5 + ((config &0x0C) >> 2);
    // Software serial fakes two stop bits if either stop bits is 2 or parity is not None
    // #define UART_NB_STOP_BIT_0    0B00000000
    // #define UART_NB_STOP_BIT_1    0B00010000
    // #define UART_NB_STOP_BIT_15   0B00100000
    // #define UART_NB_STOP_BIT_2    0B00110000
    m_stop_bits = 1 + ((config &0x30) >> 5);
    // #define UART_PARITY_NONE      0B00000000
    // #define UART_PARITY_EVEN      0B00000010
    // #define UART_PARITY_ODD       0B00000011
    if ((1 == m_stop_bits) && (config &0x03)) {
      m_stop_bits++;
    }
    // Use getCycleCount() loop to get as exact timing as possible
    m_bit_time = ESP.getCpuFreqMHz() * 1000000 / speed;
    m_bit_start_time = m_bit_time + m_bit_time/3 - (ESP.getCpuFreqMHz() > 120 ? 700 : 500); // pre-compute first wait
    m_high_speed = (speed >= 9600);
    m_very_high_speed = (speed >= 50000);
  }
  return m_valid;
}

void TasmotaSerial::setReadChunkMode(bool mode) {
  m_very_high_speed = mode;
}

bool TasmotaSerial::hardwareSerial(void) {

#ifdef ESP32
  return (0 == m_uart);  // We prefer UART1 and UART2 and keep UART0 for debugging
#endif  // ESP32
}

bool TasmotaSerial::overflow(void) {
  if (m_hardserial) {

#ifdef ESP32
    return false;  // Not implemented
#endif  // ESP32
  } else {
    bool res = m_overflow;
    m_overflow = false;
    return res;
  }
}

void TasmotaSerial::flush(void) {
  if (m_hardserial) {

#ifdef ESP32
    TSerial->flush();  // Flushes Tx only https://github.com/espressif/arduino-esp32/pull/4263
    while (TSerial->available()) { TSerial->read(); }  // Flushes Rx
#endif  // ESP32
  } else {
    m_in_pos = 0;      // Flushes Rx, Tx is always flushed
    m_out_pos = 0;
  }
}

int TasmotaSerial::peek(void) {
  if (m_hardserial) {

#ifdef ESP32
    return TSerial->peek();
#endif  // ESP32
  } else {
    if ((-1 == m_rx_pin) || (m_in_pos == m_out_pos)) return -1;
    return m_buffer[m_out_pos];
  }
}

int TasmotaSerial::read(void) {
  if (m_hardserial) {

#ifdef ESP32
    return TSerial->read();
#endif  // ESP32
  } else {
    if ((-1 == m_rx_pin) || (m_in_pos == m_out_pos)) {
      return -1;
    }
    uint32_t ch = m_buffer[m_out_pos];
    m_out_pos = (m_out_pos +1) % serial_buffer_size;
    return ch;
  }
}

size_t TasmotaSerial::read(char* buffer, size_t size) {
  if (m_hardserial) {

#ifdef ESP32
    return TSerial->read(buffer, size);
#endif  // ESP32
  } else {
    if ((-1 == m_rx_pin) || (m_in_pos == m_out_pos)) {
      return 0;
    }
    size_t count = 0;
    for( ; size && (m_in_pos != m_out_pos) ; --size, ++count) {
      *buffer++ = m_buffer[m_out_pos];
      m_out_pos = (m_out_pos +1) % serial_buffer_size;
    }
    return count;
  }
}

int TasmotaSerial::available(void) {
  if (m_hardserial) {

#ifdef ESP32
    return TSerial->available();
#endif  // ESP32
  } else {
    int avail = m_in_pos - m_out_pos;
    if (avail < 0) avail += serial_buffer_size;

//    if (!avail) {
//      optimistic_yield(10000);
//    }

    return avail;
  }
}

#define TM_SERIAL_WAIT_SND      { while (ESP.getCycleCount() < (wait + start)) if (!m_high_speed) optimistic_yield(1); wait += m_bit_time; } // Watchdog timeouts
#define TM_SERIAL_WAIT_SND_FAST { while (ESP.getCycleCount() < (wait + start)); wait += m_bit_time; }
#define TM_SERIAL_WAIT_RCV      { while (ESP.getCycleCount() < (wait + start)); wait += m_bit_time; }
#define TM_SERIAL_WAIT_RCV_LOOP { while (ESP.getCycleCount() < (wait + start)); }

void IRAM_ATTR TasmotaSerial::_fast_write(uint8_t b) {
  uint32_t wait = m_bit_time;
  uint32_t start = ESP.getCycleCount();
  // Start bit;
  digitalWrite(m_tx_pin, LOW);
  TM_SERIAL_WAIT_SND_FAST;
  for (uint32_t i = 0; i < m_data_bits; i++) {
    digitalWrite(m_tx_pin, (b & 1) ? HIGH : LOW);
    TM_SERIAL_WAIT_SND_FAST;
    b >>= 1;
  }
  // Stop bit(s)
  digitalWrite(m_tx_pin, HIGH);
  for (uint32_t i = 0; i < m_stop_bits; i++) {
    TM_SERIAL_WAIT_SND_FAST;
  }
}

size_t TasmotaSerial::write(uint8_t b) {
  if (!m_hardserial && (-1 == m_tx_pin)) { return 0; }

  if (m_tx_enable_pin > -1) {
    digitalWrite(m_tx_enable_pin, HIGH);
  }
  size_t size = 0;
  if (m_hardserial) {

#ifdef ESP32
    size = TSerial->write(b);
#endif  // ESP32
  } else {
    if (m_high_speed) {
      cli();  // Disable interrupts in order to get a clean transmit
      _fast_write(b);
      sei();
    } else {
      uint32_t wait = m_bit_time;
      //digitalWrite(m_tx_pin, HIGH);     // already in HIGH mode
      uint32_t start = ESP.getCycleCount();
      // Start bit;
      digitalWrite(m_tx_pin, LOW);
      TM_SERIAL_WAIT_SND;
      for (uint32_t i = 0; i < m_data_bits; i++) {
        digitalWrite(m_tx_pin, (b & 1) ? HIGH : LOW);
        TM_SERIAL_WAIT_SND;
        b >>= 1;
      }
      // Stop bit(s)
      digitalWrite(m_tx_pin, HIGH);
      // re-enable interrupts during stop bits, it's not an issue if they are longer than expected
      for (uint32_t i = 0; i < m_stop_bits; i++) {
        TM_SERIAL_WAIT_SND;
      }
    }
    size = 1;
  }
  if (m_tx_enable_pin > -1) {
    flush();  // Must wait for all data sent
    digitalWrite(m_tx_enable_pin, LOW);
  }
  return size;
}

#ifdef ESP32
// Add ability to change parity on the fly, for RS-485
// See https://github.com/arendst/Tasmota/discussions/22272
int32_t TasmotaSerial::setConfig(uint32_t config) {

  uint32_t data_bits_before = (m_config & 0xc) >> 2;
  uint32_t parity_before = m_config & 0x3;
  uint32_t stop_bits_before = (m_config & 0x30) >> 4;

  uint32_t data_bits = (config & 0xc) >> 2;
  uint32_t parity = config & 0x3;
  uint32_t stop_bits = (config & 0x30) >> 4;

  esp_err_t err;

  if (data_bits_before != data_bits) {
    if (err = uart_set_word_length(m_uart, (uart_word_length_t) data_bits)) {
      return (int32_t) err;
    }
  }
  if (parity_before != parity) {
    if (err = uart_set_parity(m_uart, (uart_parity_t) parity)) {
      return (int32_t) err;
    }
  }
  if (stop_bits_before != stop_bits) {
    if (err = uart_set_stop_bits(m_uart, (uart_stop_bits_t) stop_bits)) {
      return (int32_t) err;
    }
  }

  m_config = config;
  return 0;   // no error
}
#endif

