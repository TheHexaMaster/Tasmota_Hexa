/*
  xdrv_94_at24cxx.ino - External EEPROM over I2C with console / berry R/W operations

  Copyright (C) 2026 by Martin Macák - HexaMaster <hexamaster@icloud.com>

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



#ifdef USE_I2C
#ifdef USE_AT24CXX

#define XDRV_94 94
#define XI2C_99 99

#ifndef AT24CXX_I2C_ADD
#define AT24CXX_I2C_ADD 0x57
#endif

#ifndef AT24CXX_BLOCK_SIZE
#define AT24CXX_BLOCK_SIZE 0
#endif

#ifndef AT24CXX_CAPACITY
#define AT24CXX_CAPACITY 8192 
#endif

#ifndef AT24CXX_I2C_CHUNK
#define AT24CXX_I2C_CHUNK 32
#endif

#ifndef AT24CXX_MAX_WRITE_BYTES
#define AT24CXX_MAX_WRITE_BYTES 4096
#endif

#ifndef AT24CXX_JSON_MAX_BYTES
#define AT24CXX_JSON_MAX_BYTES 4096
#endif

#ifndef AT24CXX_JSON_MAX_STRING
#define AT24CXX_JSON_MAX_STRING 4096
#endif

// EEPROM write-cycle delay after each page write (ms).
// Typical AT24Cxx: 3..10 ms (5 ms is common).
#ifndef AT24CXX_WRITE_DELAY_MS
#define AT24CXX_WRITE_DELAY_MS 5
#endif

// EEPROM page size for page-write (bytes). Typical values: 8/16/32/64/128.
// MUST be correct for your chip, otherwise write across page boundary corrupts data.
#ifndef AT24CXX_PAGE_SIZE
#define AT24CXX_PAGE_SIZE 32
#endif

// ---------- XOR "encryption" (obfuscation) ----------
// Pozn.: AT24CXX_XOR nech je true/false (C++), NEPOUŽÍVAM #if AT24CXX_XOR,
// lebo preprocesor by token "true" vyhodnotil ako 0.

#ifndef AT24CXX_XOR
#define AT24CXX_XOR false
#endif

#ifdef AT24CXX_XOR
#include <t_bearssl.h>
#endif


// Default key (aby build nezlyhal aj bez definície). Keď si dáš vlastný, prepíšeš toto.
#ifndef AT24CXX_XOR_KEY
#define AT24CXX_XOR_KEY  0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x10,0x11,0x12,0x13,0x14,0x15,0x16
#endif


// TX payload must also include 2 address bytes in the same Wire transmission.
// Typical Wire TX buffer is 32 bytes -> 30 bytes payload + 2 bytes addr.
#define AT24CXX_TX_PAYLOAD_MAX  (AT24CXX_I2C_CHUNK > 2 ? (AT24CXX_I2C_CHUNK - 2) : 1)

struct {
  bool    detected = false;
  uint8_t address  = (uint8_t)AT24CXX_I2C_ADD;
  uint8_t bus      = 0;

  uint32_t total_bytes = (uint32_t)AT24CXX_CAPACITY;

  uint32_t block_size  = (uint32_t)AT24CXX_BLOCK_SIZE;  // 0 => whole chip
  uint16_t block_count = 1;

  uint32_t last_block_size = 0;  // if total_bytes not divisible
} at24;

// ---------- Helpers ----------

// XOR HELPERS:

// XOR HELPERS (SHA256-keystream via BearSSL)

// User key (16B) z build-time definície
static const uint8_t kAt24_XorUserKey[16] = { AT24CXX_XOR_KEY };
static_assert(sizeof(kAt24_XorUserKey) == 16, "AT24CXX_XOR_KEY must be exactly 16 bytes");

static uint8_t at24_xor_user_key[16];   // RAM kópia po init (aby si už nečítal z rodata)

// Derived secret (32B) + cache na 32B keystream blok
static uint8_t  at24_xor_secret[32];
static bool     at24_xor_inited = false;

static uint32_t at24_xor_cache_block = 0xFFFFFFFFu; // pos>>5
static uint8_t  at24_xor_cache[32];
static bool     at24_xor_cache_valid = false;

static void At24_Sha256_Begin(br_sha256_context *ctx) {
  br_sha256_init(ctx);
}

static void At24_Sha256_Update(br_sha256_context *ctx, const void *data, size_t len) {
  br_sha256_update(ctx, data, len);
}

static void At24_Sha256_Out(br_sha256_context *ctx, uint8_t out32[32]) {
  br_sha256_out(ctx, out32);
}

static void At24_XorInit(void) {
  if (at24_xor_inited) return;

  uint64_t id = 0;

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
  id = ESP.getEfuseMac();              // unikát per chip (eFuse MAC)
#elif defined(ESP8266)
  id = (uint64_t)ESP.getChipId();      // fallback
  id = (id << 32) ^ 0xA5A5A5A5u;       // rozšírenie na 64b (aby to nebolo "slabé")
#else
  id = 0x0123456789ABCDEFULL;
#endif

  // seed = (id[8]) || (chiprev[1]) || (userkey[16]) || ("AT24CXX"[6])
  uint8_t seed[8 + 1 + 16 + 6];
  for (uint8_t i = 0; i < 8; i++) seed[i] = (uint8_t)(id >> (i * 8));

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
  seed[8] = (uint8_t)ESP.getChipRevision();
#else
  seed[8] = 0;
#endif

  for (uint8_t i = 0; i < 16; i++) {
    at24_xor_user_key[i] = kAt24_XorUserKey[i];
    seed[9 + i]          = at24_xor_user_key[i];
  }

  // malý "domain separator" aby sa to náhodou nepoužilo inde v projekte s rovnakým seed
  seed[25] = 'A';
  seed[26] = 'T';
  seed[27] = '2';
  seed[28] = '4';
  seed[29] = 'C';
  seed[30] = 'X';
  seed[31] = 'X';

  br_sha256_context ctx;
  At24_Sha256_Begin(&ctx);
  At24_Sha256_Update(&ctx, seed, sizeof(seed));
  At24_Sha256_Out(&ctx, at24_xor_secret);

  at24_xor_cache_valid = false;
  at24_xor_cache_block = 0xFFFFFFFFu;
  at24_xor_inited = true;
}

// vygeneruj 32B keystream pre blok (pos>>5)
static void At24_XorGenBlock(uint32_t block, uint8_t out32[32]) {
  uint8_t b[4];
  b[0] = (uint8_t)(block >> 24);
  b[1] = (uint8_t)(block >> 16);
  b[2] = (uint8_t)(block >> 8);
  b[3] = (uint8_t)(block);

  br_sha256_context ctx;
  At24_Sha256_Begin(&ctx);
  At24_Sha256_Update(&ctx, at24_xor_secret, sizeof(at24_xor_secret));
  At24_Sha256_Update(&ctx, b, sizeof(b));

  // extra domain separator pre "stream"
  const uint8_t tag[4] = { 'S','T','R','M' };
  At24_Sha256_Update(&ctx, tag, sizeof(tag));

  At24_Sha256_Out(&ctx, out32);
}

static inline uint8_t At24_XorStreamByte(uint32_t pos) {
  // pos = absolútna EEPROM adresa bajtu
  if (!at24_xor_inited) At24_XorInit();

  uint32_t block = (pos >> 5);          // 32B bloky
  uint32_t off   = (pos & 31);

  if (!at24_xor_cache_valid || at24_xor_cache_block != block) {
    At24_XorGenBlock(block, at24_xor_cache);
    at24_xor_cache_block = block;
    at24_xor_cache_valid = true;
  }

  return at24_xor_cache[off];
}

// OTHER HELPERS 

static char* At24_JsonEscapeAlloc(const char *in, uint32_t len) {
  // worst-case: every char -> \u00XX (6 chars)
  uint32_t need = 0;
  for (uint32_t i = 0; i < len; i++) {
    uint8_t c = (uint8_t)in[i];
    if (c == '\"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') need += 2;
    else if (c < 0x20) need += 6;   // control char -> \u00XX
    else need += 1;
  }

  char *out = (char*)malloc(need + 1);
  if (!out) return nullptr;

  static const char kHex[] = "0123456789ABCDEF";
  char *d = out;

  for (uint32_t i = 0; i < len; i++) {
    uint8_t c = (uint8_t)in[i];
    switch (c) {
      case '\"': *d++='\\'; *d++='\"'; break;
      case '\\': *d++='\\'; *d++='\\'; break;
      case '\b': *d++='\\'; *d++='b';  break;
      case '\f': *d++='\\'; *d++='f';  break;
      case '\n': *d++='\\'; *d++='n';  break;
      case '\r': *d++='\\'; *d++='r';  break;
      case '\t': *d++='\\'; *d++='t';  break;
      default:
        if (c < 0x20) {
          *d++='\\'; *d++='u'; *d++='0'; *d++='0';
          *d++=kHex[(c >> 4) & 0x0F];
          *d++=kHex[c & 0x0F];
        } else {
          *d++ = (char)c;
        }
        break;
    }
  }

  *d = 0;
  return out;
}




static void At24_RecalcBlocks(void) {
  uint32_t bs = at24.block_size;

  if (bs == 0 || bs >= at24.total_bytes) {
    at24.block_size = at24.total_bytes;
    at24.block_count = 1;
    at24.last_block_size = at24.total_bytes;
    return;
  }

  uint32_t full = at24.total_bytes / bs;
  uint32_t rem  = at24.total_bytes % bs;

  at24.block_count = (uint16_t)full;
  if (rem) at24.block_count++;

  at24.last_block_size = rem ? rem : bs;
}

static bool At24_GetBlockParams(uint32_t block, uint32_t *start, uint32_t *len) {
  if (!at24.detected) return false;
  if (block >= at24.block_count) return false;

  uint32_t s = block * at24.block_size;
  uint32_t l = at24.block_size;

  if ((block == (uint32_t)(at24.block_count - 1)) && (at24.last_block_size != at24.block_size)) {
    l = at24.last_block_size;
  }

  if (s + l > at24.total_bytes) return false;
  *start = s;
  *len   = l;
  return true;
}

// Robust hex parser: accepts "AABBCC", "AA BB CC", "0xAA,0xBB,0xCC", etc.
static uint32_t At24_ParseHexBytes(const char *s, uint8_t *out, uint32_t out_max) {
  uint32_t n = 0;
  int hi = -1;

  while (*s && n < out_max) {

    // Skip 0x / 0X prefix as a whole token (do NOT consume '0' as a nibble)
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
      s += 2;
      continue;
    }

    char c = *s++;
    int v = -1;

    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
    else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
    else continue; // separators

    if (hi < 0) hi = v;
    else {
      out[n++] = (uint8_t)((hi << 4) | v);
      hi = -1;
    }
  }

  return n;
}

static void At24_LogHexLine(uint32_t addr, const uint8_t *buf, uint32_t len) {
  AddLog(LOG_LEVEL_INFO, PSTR("AT24CXX: 0x%04X: %*_H"), (uint16_t)addr, (int)len, buf);
}

// ---------- Low-level I2C EEPROM access (16-bit address, MSB first) ----------

static bool At24_I2cRead(uint16_t mem_addr, uint8_t *buf, uint32_t len) {

  TwoWire& myWire = I2cGetWire(at24.bus);
  if (&myWire == nullptr) { return false; }

  // XOR
  if (AT24CXX_XOR) { At24_XorInit(); }

  uint32_t done = 0;
  while (done < len) {
    uint32_t chunk = len - done;
    if (chunk > AT24CXX_I2C_CHUNK) chunk = AT24CXX_I2C_CHUNK;

    // Set internal address pointer
    myWire.beginTransmission(at24.address);
    myWire.write((uint8_t)(mem_addr >> 8));       // MSB
    myWire.write((uint8_t)(mem_addr & 0xFF));     // LSB
    if (myWire.endTransmission(true) != 0) {      // STOP
      return false;
    }

    // Read data
    if (chunk != (uint32_t)myWire.requestFrom((uint8_t)at24.address, (uint8_t)chunk)) {
      // Drain
      while (myWire.available()) (void)myWire.read();
      return false;
    }

    for (uint32_t i = 0; i < chunk; i++) {
      if (!myWire.available()) return false;
      uint8_t v = (uint8_t)myWire.read();
      if (AT24CXX_XOR) {
        v ^= At24_XorStreamByte((uint32_t)mem_addr + i);
      }
      buf[done + i] = v;
    }

    done += chunk;
    mem_addr = (uint16_t)(mem_addr + chunk);
    yield();
  }

  return true;
}

static bool At24_I2cWrite(uint16_t mem_addr, const uint8_t *buf, uint32_t len) {

  TwoWire& myWire = I2cGetWire(at24.bus);

  // XOR
  if (AT24CXX_XOR) { At24_XorInit(); }

  uint32_t done = 0;
  while (done < len) {
    uint32_t chunk = len - done;

    // limit by Wire TX payload (addr(2B) + data)
    if (chunk > AT24CXX_TX_PAYLOAD_MAX) chunk = AT24CXX_TX_PAYLOAD_MAX;

    // --- PAGE SPLITTING (critical for AT24Cxx) ---
    // Never cross a page boundary in one write, otherwise EEPROM wraps inside page.
#if (AT24CXX_PAGE_SIZE > 0)
    {
      uint32_t page_left = (uint32_t)AT24CXX_PAGE_SIZE - ((uint32_t)mem_addr % (uint32_t)AT24CXX_PAGE_SIZE);
      if (page_left && chunk > page_left) chunk = page_left;
    }
#endif

    myWire.beginTransmission(at24.address);
    myWire.write((uint8_t)(mem_addr >> 8));       // MSB
    myWire.write((uint8_t)(mem_addr & 0xFF));     // LSB
    for (uint32_t i = 0; i < chunk; i++) {
      uint8_t v = buf[done + i];
      if (AT24CXX_XOR) {
        v ^= At24_XorStreamByte((uint32_t)mem_addr + i);
      }
      myWire.write(v);
    }

    if (myWire.endTransmission(true) != 0) {
      return false;
    }

    // --- WRITE CYCLE DELAY (critical for AT24Cxx) ---
#if (AT24CXX_WRITE_DELAY_MS > 0)
    delay(AT24CXX_WRITE_DELAY_MS);
#endif

    done += chunk;
    mem_addr = (uint16_t)(mem_addr + chunk);
    yield();
  }

  return true;
}

static bool At24_WriteFill(uint16_t mem_addr, uint8_t fill, uint32_t len) {
  uint8_t tmp[AT24CXX_TX_PAYLOAD_MAX];
  memset(tmp, fill, sizeof(tmp));

  uint32_t done = 0;
  while (done < len) {
    uint32_t chunk = len - done;
    if (chunk > sizeof(tmp)) chunk = sizeof(tmp);

    if (!At24_I2cWrite((uint16_t)(mem_addr + done), tmp, chunk)) {
      return false;
    }
    done += chunk;
    yield();
  }
  return true;
}

// ---------- Detect ----------

static void At24_Detect(void) {
  if (at24.detected) return;
  if (!I2cEnabled(XI2C_99)) return;

  at24.address     = (uint8_t)AT24CXX_I2C_ADD;
  at24.total_bytes = (uint32_t)AT24CXX_CAPACITY;
  at24.block_size  = (uint32_t)AT24CXX_BLOCK_SIZE;
  At24_RecalcBlocks();

  for (at24.bus = 0; at24.bus < 2; at24.bus++) {
    if (!I2cSetDevice(at24.address, at24.bus)) continue;

    uint8_t b = 0;
    at24.detected = At24_I2cRead(0x0000, &b, 1);

    if (at24.detected) {
      I2cSetActiveFound(at24.address, "AT24CXX", at24.bus);
      AddLog(LOG_LEVEL_INFO, PSTR("AT24CXX: detected bus=%d addr=0x%02X size=%u block=%u blocks=%u"),
            at24.bus, at24.address,
            (unsigned)at24.total_bytes,
            (unsigned)at24.block_size,
            (unsigned)at24.block_count);
      return;
    }
  }

  AddLog(LOG_LEVEL_DEBUG, PSTR("AT24CXX: not detected (addr=0x%02X)"), (uint8_t)AT24CXX_I2C_ADD);
}

// ---------- Tasmota console command handling ----------

#define D_PRFX_EEPROM             "Eeprom"
#define D_CMND_EEPROM_INFO        "Info"              // General info (etc... {"EepromInfo":{"bus":1,"addr":"0x50","size":65535,"block":1024,"blocks":64}})
#define D_CMND_EEPROM_READ_FORMAT "ReadFormat"        // Read HEX values from block and format them in tasmota console (STREAM)
#define D_CMND_EEPROM_READ        "Read"              // Read HEX values from block to result map (easy access from Berry)
#define D_CMND_EEPROM_WRITE       "Write"             // Write HEX values to block. Unused block bytes are filled with zeros
#define D_CMND_EEPROM_FORMAT      "Erase"             // Erase selected block or whole fram (if "all") - fill with zeros
#define D_CMND_EEPROM_READ_STRING  "ReadString"       // Read block data as STRING - json output
#define D_CMND_EEPROM_WRITE_STRING "WriteString"      // Write data to block as STRING. 

static void CmndEepromInfo(void);
static void CmndEepromReadFormat(void);
static void CmndEepromRead(void);
static void CmndEepromWrite(void);
static void CmndEepromErase(void);
static void CmndEepromReadString(void);
static void CmndEepromWriteString(void);

const char kEepromCommands[] PROGMEM =
  D_PRFX_EEPROM "|" D_CMND_EEPROM_INFO "|" D_CMND_EEPROM_READ_FORMAT "|" D_CMND_EEPROM_READ "|" D_CMND_EEPROM_WRITE "|" D_CMND_EEPROM_FORMAT "|" D_CMND_EEPROM_READ_STRING "|" D_CMND_EEPROM_WRITE_STRING;

void (*const EepromCommand[])(void) PROGMEM = {
  &CmndEepromInfo, &CmndEepromReadFormat, &CmndEepromRead, &CmndEepromWrite, &CmndEepromErase, &CmndEepromReadString, &CmndEepromWriteString
};

static void CmndEepromInfo(void) {
  At24_Detect();
  if (!at24.detected) {
    Response_P(PSTR("{\"EepromInfo\":\"not detected\"}"));
    return;
  }

  Response_P(PSTR("{\"EepromInfo\":{\"bus\":%d,\"addr\":\"0x%02X\",\"size\":%u,\"block\":%u,\"blocks\":%u}}"),
             at24.bus, at24.address,
             (unsigned)at24.total_bytes,
             (unsigned)at24.block_size,
             (unsigned)at24.block_count);
}

static void CmndEepromReadFormat(void) {
  At24_Detect();
  if (!at24.detected) {
    Response_P(PSTR("{\"EepromReadFormat\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) { ResponseCmndFailed(); return; }

  uint32_t block = (uint32_t)strtoul(XdrvMailbox.data, nullptr, 0);
  uint32_t start = 0, len = 0;
  if (!At24_GetBlockParams(block, &start, &len)) { ResponseCmndFailed(); return; }

  AddLog(LOG_LEVEL_INFO, PSTR("AT24CXX: READ block=%u start=0x%04X len=%u"),
         (unsigned)block, (uint16_t)start, (unsigned)len);

  uint8_t tmp[16];
  for (uint32_t off = 0; off < len; off += sizeof(tmp)) {
    uint32_t chunk = len - off;
    if (chunk > sizeof(tmp)) chunk = sizeof(tmp);

    if (!At24_I2cRead((uint16_t)(start + off), tmp, chunk)) {
      AddLog(LOG_LEVEL_ERROR, PSTR("AT24CXX: READ failed at 0x%04X"), (uint16_t)(start + off));
      ResponseCmndFailed();
      return;
    }
    At24_LogHexLine(start + off, tmp, chunk);
    yield();
  }

  Response_P(PSTR("{\"EepromReadFormat\":{\"block\":%u,\"bytes\":%u}}"), (unsigned)block, (unsigned)len);
}

static void CmndEepromRead(void) {
  At24_Detect();
  if (!at24.detected) {
    Response_P(PSTR("{\"EepromRead\":\"not detected\"}"));
    return;
  }

  char *p = XdrvMailbox.data;
  while (p && (*p == ' ' || *p == '\t' || *p == ',')) p++;

  if (!p || *p == 0) {
    Response_P(PSTR("{\"EepromRead\":\"usage: EepromRead <block>\"}"));
    return;
  }

  uint32_t block = (uint32_t)strtoul(p, nullptr, 0);

  uint32_t start = 0, len = 0;
  if (!At24_GetBlockParams(block, &start, &len)) {
    ResponseCmndFailed();
    return;
  }

  // buffer protection
  if (len > AT24CXX_JSON_MAX_BYTES) {
    Response_P(PSTR("{\"EepromRead\":{\"error\":\"too big\",\"block\":%u,\"bytes\":%u,\"max\":%u}}"),
               (unsigned)block, (unsigned)len, (unsigned)AT24CXX_JSON_MAX_BYTES);
    return;
  }

  uint8_t *buf = (uint8_t*)malloc(len);
  if (!buf) { ResponseCmndFailed(); return; }

  if (!At24_I2cRead((uint16_t)start, buf, len)) {
    free(buf);
    ResponseCmndFailed();
    return;
  }

  char *hex = (char*)malloc((len * 2) + 1);
  if (!hex) { free(buf); ResponseCmndFailed(); return; }
  hex[len * 2] = 0;

  static const char kHex[] = "0123456789ABCDEF";
  for (uint32_t i = 0; i < len; i++) {
    uint8_t v = buf[i];
    hex[i * 2 + 0] = kHex[v >> 4];
    hex[i * 2 + 1] = kHex[v & 0x0F];
  }

  Response_P(PSTR("{\"EepromRead\":{\"block\":%u,\"bytes\":%u,\"data\":\"%s\"}}"),
             (unsigned)block, (unsigned)len, hex);

  free(hex);
  free(buf);
}

static void CmndEepromWrite(void) {
  At24_Detect();
  if (!at24.detected) {
    Response_P(PSTR("{\"EepromWrite\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) { ResponseCmndFailed(); return; }

  char *p = XdrvMailbox.data;
  while (p && (*p == ' ' || *p == '\t' || *p == ',')) p++;

  char *save = nullptr;
  char *a1 = strtok_r(p, " \t,", &save);
  if (!a1) { ResponseCmndFailed(); return; }

  uint32_t block = (uint32_t)strtoul(a1, nullptr, 0);
  uint32_t start = 0, len = 0;
  if (!At24_GetBlockParams(block, &start, &len)) { ResponseCmndFailed(); return; }

  char *hex = save;
  while (hex && (*hex == ' ' || *hex == '\t' || *hex == ',')) hex++;

  if (!hex || *hex == 0) {
    Response_P(PSTR("{\"EepromWrite\":\"write needs hex payload\"}"));
    return;
  }

  uint32_t max_write = len;
  if (max_write > AT24CXX_MAX_WRITE_BYTES) max_write = AT24CXX_MAX_WRITE_BYTES;

  uint8_t *data = (uint8_t*)malloc(max_write);
  if (!data) { ResponseCmndFailed(); return; }

  uint32_t n = At24_ParseHexBytes(hex, data, max_write);
  if (n == 0) {
    free(data);
    Response_P(PSTR("{\"EepromWrite\":\"no hex bytes parsed\"}"));
    return;
  }

  AddLog(LOG_LEVEL_INFO, PSTR("AT24CXX: WRITE block=%u start=0x%04X len=%u parsed=%u (fill rest with 0x00)"),
         (unsigned)block, (uint16_t)start, (unsigned)len, (unsigned)n);

  if (!At24_I2cWrite((uint16_t)start, data, n)) {
    free(data);
    AddLog(LOG_LEVEL_ERROR, PSTR("AT24CXX: WRITE failed at 0x%04X"), (uint16_t)start);
    ResponseCmndFailed();
    return;
  }
  free(data);

  if (n < len) {
    if (!At24_WriteFill((uint16_t)(start + n), 0x00, (len - n))) {
      AddLog(LOG_LEVEL_ERROR, PSTR("AT24CXX: FILL failed at 0x%04X"), (uint16_t)(start + n));
      ResponseCmndFailed();
      return;
    }
  }

  Response_P(PSTR("{\"EepromWrite\":{\"block\":%u,\"bytes\":%u,\"filled\":%u}}"),
             (unsigned)block, (unsigned)n, (unsigned)(len - (n < len ? n : len)));
}

static void CmndEepromErase(void) {
  At24_Detect();
  if (!at24.detected) {
    Response_P(PSTR("{\"EepromErase\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) { ResponseCmndFailed(); return; }

  char *p = XdrvMailbox.data;
  while (p && (*p == ' ' || *p == '\t' || *p == ',')) p++;

  if (!strcasecmp(p, "all")) {
    AddLog(LOG_LEVEL_INFO, PSTR("AT24CXX: FORMAT all blocks (%u), fill=0x00"), (unsigned)at24.block_count);

    for (uint32_t b = 0; b < at24.block_count; b++) {
      uint32_t start = 0, len = 0;
      if (!At24_GetBlockParams(b, &start, &len)) { ResponseCmndFailed(); return; }

      if (!At24_WriteFill((uint16_t)start, 0x00, len)) {
        AddLog(LOG_LEVEL_ERROR, PSTR("AT24CXX: FORMAT failed at block=%u addr=0x%04X"), (unsigned)b, (uint16_t)start);
        ResponseCmndFailed();
        return;
      }
      yield();
    }

    Response_P(PSTR("{\"EepromErase\":\"all\"}"));
    return;
  }

  uint32_t block = (uint32_t)strtoul(p, nullptr, 0);
  uint32_t start = 0, len = 0;
  if (!At24_GetBlockParams(block, &start, &len)) { ResponseCmndFailed(); return; }

  AddLog(LOG_LEVEL_INFO, PSTR("AT24CXX: FORMAT block=%u start=0x%04X len=%u fill=0x00"),
         (unsigned)block, (uint16_t)start, (unsigned)len);

  if (!At24_WriteFill((uint16_t)start, 0x00, len)) {
    AddLog(LOG_LEVEL_ERROR, PSTR("AT24CXX: FORMAT failed at 0x%04X"), (uint16_t)start);
    ResponseCmndFailed();
    return;
  }

  Response_P(PSTR("{\"EepromErase\":%u}"), (unsigned)block);
}

static void CmndEepromReadString(void) {
  At24_Detect();
  if (!at24.detected) {
    Response_P(PSTR("{\"EepromReadString\":\"not detected\"}"));
    return;
  }

  char *p = XdrvMailbox.data;
  while (p && (*p == ' ' || *p == '\t' || *p == ',')) p++;

  if (!p || *p == 0) {
    Response_P(PSTR("{\"EepromReadString\":\"usage: EepromReadString <block>\"}"));
    return;
  }

  uint32_t block = (uint32_t)strtoul(p, nullptr, 0);

  uint32_t start = 0, len = 0;
  if (!At24_GetBlockParams(block, &start, &len)) { ResponseCmndFailed(); return; }

  // chunked read
  uint32_t max_raw = len;
  if (max_raw > AT24CXX_JSON_MAX_STRING) max_raw = AT24CXX_JSON_MAX_STRING;

  char *raw = (char*)malloc(max_raw + 1);
  if (!raw) { ResponseCmndFailed(); return; }

  uint32_t raw_len = 0;
  bool zero_found = false;

  uint8_t tmp[AT24CXX_I2C_CHUNK];

  for (uint32_t off = 0; off < len && raw_len < max_raw && !zero_found; ) {
    uint32_t chunk = len - off;
    if (chunk > sizeof(tmp)) chunk = sizeof(tmp);

    if (!At24_I2cRead((uint16_t)(start + off), tmp, chunk)) {
      free(raw);
      ResponseCmndFailed();
      return;
    }

    for (uint32_t i = 0; i < chunk && raw_len < max_raw; i++) {
      uint8_t c = tmp[i];
      if (c == 0x00) { zero_found = true; break; }
      raw[raw_len++] = (char)c;
    }

    off += chunk;
    yield();
  }

  raw[raw_len] = 0;

  bool truncated = (!zero_found) && (raw_len == max_raw) && (max_raw < len);

  char *esc = At24_JsonEscapeAlloc(raw, raw_len);
  if (!esc) {
    free(raw);
    ResponseCmndFailed();
    return;
  }

  Response_P(PSTR("{\"EepromReadString\":{\"block\":%u,\"bytes\":%u,\"truncated\":%u,\"data\":\"%s\"}}"),
             (unsigned)block, (unsigned)raw_len, (unsigned)(truncated ? 1 : 0), esc);

  free(esc);
  free(raw);
}

static void CmndEepromWriteString(void) {
  At24_Detect();
  if (!at24.detected) {
    Response_P(PSTR("{\"EepromWriteString\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) { ResponseCmndFailed(); return; }

  char *p = XdrvMailbox.data;
  while (p && (*p == ' ' || *p == '\t' || *p == ',')) p++;

  char *save = nullptr;
  char *a1 = strtok_r(p, " \t,", &save);
  if (!a1) { ResponseCmndFailed(); return; }

  uint32_t block = (uint32_t)strtoul(a1, nullptr, 0);

  uint32_t start = 0, len = 0;
  if (!At24_GetBlockParams(block, &start, &len)) { ResponseCmndFailed(); return; }

  char *str = save;
  while (str && (*str == ' ' || *str == '\t' || *str == ',')) str++;

  if (!str || *str == 0) {
    Response_P(PSTR("{\"EepromWriteString\":\"write needs string payload\"}"));
    return;
  }

  // quote string support ("")
  if (str[0] == '\"') {
    str++;
    char *endq = strrchr(str, '\"');
    if (endq) *endq = 0;
  }

  uint32_t max_write = len;
  if (max_write > AT24CXX_MAX_WRITE_BYTES) max_write = AT24CXX_MAX_WRITE_BYTES;

  uint32_t n = (uint32_t)strnlen(str, max_write);
  bool truncated = (str[n] != 0);   // strnlen hitol limit

  AddLog(LOG_LEVEL_INFO, PSTR("AT24CXX: WRITE_STRING block=%u start=0x%04X str_len=%u (fill rest with 0x00)"),
         (unsigned)block, (uint16_t)start, (unsigned)n);

  if (n > 0) {
    if (!At24_I2cWrite((uint16_t)start, (const uint8_t*)str, n)) {
      AddLog(LOG_LEVEL_ERROR, PSTR("AT24CXX: WRITE_STRING failed at 0x%04X"), (uint16_t)start);
      ResponseCmndFailed();
      return;
    }
  }

  if (n < len) {
    if (!At24_WriteFill((uint16_t)(start + n), 0x00, (len - n))) {
      AddLog(LOG_LEVEL_ERROR, PSTR("AT24CXX: WRITE_STRING fill failed at 0x%04X"), (uint16_t)(start + n));
      ResponseCmndFailed();
      return;
    }
  }

  Response_P(PSTR("{\"EepromWriteString\":{\"block\":%u,\"bytes\":%u,\"filled\":%u,\"truncated\":%u}}"),
             (unsigned)block, (unsigned)n, (unsigned)(len - (n < len ? n : len)), (unsigned)(truncated ? 1 : 0));
}

// ---------- Interface ----------

bool Xdrv94(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      At24_Detect();
      break;

    case FUNC_COMMAND:
      result = DecodeCommand(kEepromCommands, EepromCommand);
      break;

    case FUNC_ACTIVE:
      result = at24.detected;
      break;
  }

  return result;
}

#endif  // USE_AT24CXX
#endif  // USE_I2C