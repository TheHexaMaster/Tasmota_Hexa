/*
  xdrv_96_xl9535.ino - XL9535 (I2C) Expander driver for Tasmota

  Copyright (C) 2026 by Martin Macák - HexaMaster <hexamaster@icloud.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#ifdef USE_I2C
#ifdef USE_XL9535

#define XDRV_96   96
#define XI2C_101  101

#ifndef XL9535_I2C_ADD
#define XL9535_I2C_ADD 0x20    // default A2=A1=A0=0
#endif

// ---------- Factory mode ----------
#ifndef XL9535_FACTORY
#define XL9535_FACTORY false
#endif

#ifndef XL9535_FACTORY_CONFIG
// 1=input, 0=output (bit=1 => input)
// default: all inputs
#define XL9535_FACTORY_CONFIG 0x0000
#endif

#ifndef XL9535_FACTORY_DEF_STATE
// default output latch after boot (OUT0/OUT1)
#define XL9535_FACTORY_DEF_STATE 0x0000
#endif

// ---------- FRAM persistence ----------
#ifndef XL9535_FRAM_STATE
#define XL9535_FRAM_STATE false
#endif

#ifndef XL9535_FRAM_BLOCK
#define XL9535_FRAM_BLOCK 1
#endif

// compiled-with-FRAM flag
#if defined(USE_FM24CXX)
  #define XL9535_HAS_FRAM 1
  extern bool     Fm24_Available(void);
  extern uint32_t Fm24_BlockSize(void);
  extern bool     Fm24_ReadBlock(uint32_t block, uint8_t *buf, uint32_t len);
  extern bool     Fm24_WriteBlock(uint32_t block, const uint8_t *buf, uint32_t len);
#else
  #define XL9535_HAS_FRAM 0
#endif

static constexpr bool     kXlFramEnable = (bool)(XL9535_FRAM_STATE) && (XL9535_HAS_FRAM != 0);
static constexpr uint32_t kXlFramBlock  = (uint32_t)(XL9535_FRAM_BLOCK);

static constexpr bool     kXl9535Factory     = (bool)(XL9535_FACTORY);
static constexpr uint16_t kXl9535FactoryCfg  = (uint16_t)(XL9535_FACTORY_CONFIG);
static constexpr uint16_t kXl9535FactoryOut  = (uint16_t)(XL9535_FACTORY_DEF_STATE);

// XL9535 registers (PCA/TCA9535 compatible)
#define XL9535_REG_IN0   0x00
#define XL9535_REG_IN1   0x01
#define XL9535_REG_OUT0  0x02
#define XL9535_REG_OUT1  0x03
#define XL9535_REG_POL0  0x04
#define XL9535_REG_POL1  0x05
#define XL9535_REG_CFG0  0x06
#define XL9535_REG_CFG1  0x07

struct {
  bool    detected = false;
  uint8_t address  = (uint8_t)XL9535_I2C_ADD;
  uint8_t bus      = 0;

  // cache
  uint8_t in0  = 0, in1  = 0;
  uint8_t out0 = 0, out1 = 0;
  uint8_t pol0 = 0, pol1 = 0;
  uint8_t cfg0 = 0xFF, cfg1 = 0xFF;
} xl;

// ---------- Low-level I2C helpers ----------

// ===== Persist format in FRAM block (first 32 bytes) =====
// Two slots (2x16B) to survive torn writes on power loss.

#define XL9535_PERSIST_MAGIC 0x35394C58u  // 'XL95' (little-endian)

struct __attribute__((packed)) Xl9535PersistSlot {
  uint32_t magic;   // XL9535_PERSIST_MAGIC
  uint32_t seq;     // increments each save
  uint16_t out;     // OUT latch (16b)
  uint16_t cfg;     // CFG (16b) 1=input,0=output
  uint16_t crc16;   // CRC16 of bytes [0..11]
  uint16_t rsv;     // reserved
};
static_assert(sizeof(Xl9535PersistSlot) == 16, "slot must be 16 bytes");

static uint16_t Xl_Crc16_Ibm(const uint8_t *data, uint32_t len) {
  uint16_t crc = 0xFFFF;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
  }
  return crc;
}

static bool Xl_SlotValid(const void *pslot) {
  const Xl9535PersistSlot *s = (const Xl9535PersistSlot*)pslot;
  if (s->magic != XL9535_PERSIST_MAGIC) return false;
  uint16_t c = Xl_Crc16_Ibm((const uint8_t*)s, 12);
  return (c == s->crc16);
}

static uint32_t xl_persist_seq = 0;
static uint8_t  xl_persist_slot = 0;     // 0 or 1 = which slot is current
static bool     xl_persist_loaded = false;

static bool Xl9535_ApplyOutCfg(uint16_t out, uint16_t cfg) {
  const uint8_t out0 = (uint8_t)(out & 0xFF);
  const uint8_t out1 = (uint8_t)((out >> 8) & 0xFF);
  const uint8_t cfg0 = (uint8_t)(cfg & 0xFF);
  const uint8_t cfg1 = (uint8_t)((cfg >> 8) & 0xFF);

  if (!Xl9535_I2cWriteReg(XL9535_REG_OUT0, out0) ||
      !Xl9535_I2cWriteReg(XL9535_REG_OUT1, out1)) {
    return false;
  }

  if (!Xl9535_I2cWriteReg(XL9535_REG_CFG0, cfg0) ||
      !Xl9535_I2cWriteReg(XL9535_REG_CFG1, cfg1)) {
    return false;
  }

  // update cache
  xl.out0 = out0; xl.out1 = out1;
  xl.cfg0 = cfg0; xl.cfg1 = cfg1;
  return true;
}
// LOAD / SAVE TO FRAM

static bool Xl9535_LoadFromFram(uint16_t *out, uint16_t *cfg) {
  if (!kXlFramEnable) return false;
#if XL9535_HAS_FRAM
  if (!Fm24_Available()) return false;

  uint32_t bs = Fm24_BlockSize();
  if (bs < 32) {
    AddLog(LOG_LEVEL_ERROR, PSTR("XL9535: FRAM block_size=%u < 32 (cannot persist)"), (unsigned)bs);
    return false;
  }

  uint8_t raw[32];
  if (!Fm24_ReadBlock(kXlFramBlock, raw, sizeof(raw))) return false;

  Xl9535PersistSlot a, b;
  memcpy(&a, raw + 0, 16);
  memcpy(&b, raw + 16, 16);

bool va = Xl_SlotValid(&a);
bool vb = Xl_SlotValid(&b);

  if (!va && !vb) return false;

  const Xl9535PersistSlot *best = nullptr;
  uint8_t best_slot = 0;

  if (va && vb) {
    if (a.seq >= b.seq) { best = &a; best_slot = 0; }
    else               { best = &b; best_slot = 1; }
  } else if (va) {
    best = &a; best_slot = 0;
  } else {
    best = &b; best_slot = 1;
  }

  *out = best->out;
  *cfg = best->cfg;

  xl_persist_seq = best->seq;
  xl_persist_slot = best_slot;
  xl_persist_loaded = true;

  return true;
#else
  (void)out; (void)cfg;
  return false;
#endif
}

static bool Xl9535_SaveToFram(uint16_t out, uint16_t cfg) {
  if (!kXlFramEnable) return false;
#if XL9535_HAS_FRAM
  if (!Fm24_Available()) return false;

  uint32_t bs = Fm24_BlockSize();
  if (bs < 32) return false;

  // ensure we have a baseline seq/slot
  if (!xl_persist_loaded) {
    uint16_t o=0, c=0;
    (void)Xl9535_LoadFromFram(&o, &c);   // if fails, seq stays 0
  }

  uint8_t raw[32];
  // preserve the other slot if exists
  if (!Fm24_ReadBlock(kXlFramBlock, raw, sizeof(raw))) {
    memset(raw, 0, sizeof(raw));
  }

  uint8_t new_slot = (xl_persist_loaded ? (uint8_t)(xl_persist_slot ^ 1u) : 0u);

  Xl9535PersistSlot s;
  memset(&s, 0, sizeof(s));
  s.magic = XL9535_PERSIST_MAGIC;
  s.seq   = xl_persist_seq + 1;
  s.out   = out;
  s.cfg   = cfg;
  s.crc16 = Xl_Crc16_Ibm((const uint8_t*)&s, 12);

  memcpy(raw + (new_slot ? 16 : 0), &s, 16);

  if (!Fm24_WriteBlock(kXlFramBlock, raw, sizeof(raw))) return false;

  xl_persist_seq = s.seq;
  xl_persist_slot = new_slot;
  xl_persist_loaded = true;
  return true;
#else
  (void)out; (void)cfg;
  return false;
#endif
}

static void Xl9535_PersistNow(void) {
  if (!kXlFramEnable) return;

#if XL9535_HAS_FRAM
  if (!Fm24_Available()) return;

  // Read actual HW state (robust against stale cache)
  uint8_t o0 = 0, o1 = 0;
  uint8_t c0 = 0, c1 = 0;

  if (!Xl9535_I2cReadReg(XL9535_REG_OUT0, &o0)) return;
  if (!Xl9535_I2cReadReg(XL9535_REG_OUT1, &o1)) return;

  uint16_t out16 = (uint16_t)o0 | ((uint16_t)o1 << 8);

  uint16_t cfg16;
  if (kXl9535Factory) {
    cfg16 = (uint16_t)kXl9535FactoryCfg;
  } else {
    if (!Xl9535_I2cReadReg(XL9535_REG_CFG0, &c0)) return;
    if (!Xl9535_I2cReadReg(XL9535_REG_CFG1, &c1)) return;
    cfg16 = (uint16_t)c0 | ((uint16_t)c1 << 8);
  }

  // update cache (optional but good)
  xl.out0 = o0; xl.out1 = o1;
  if (!kXl9535Factory) { xl.cfg0 = c0; xl.cfg1 = c1; }
  else { xl.cfg0 = (uint8_t)(kXl9535FactoryCfg & 0xFF); xl.cfg1 = (uint8_t)(kXl9535FactoryCfg >> 8); }

  (void)Xl9535_SaveToFram(out16, cfg16);
#endif
}

// Native Helpers

static bool Xl9535_I2cReadReg(uint8_t reg, uint8_t *val) {
  TwoWire &w = I2cGetWire(xl.bus);

  w.beginTransmission(xl.address);
  w.write(reg);
  if (w.endTransmission(false) != 0) {   // repeated start
    return false;
  }

  if (w.requestFrom((uint8_t)xl.address, (uint8_t)1) != 1) {
    while (w.available()) (void)w.read();
    return false;
  }

  if (!w.available()) return false;
  *val = (uint8_t)w.read();
  return true;
}

static bool Xl9535_I2cWriteReg(uint8_t reg, uint8_t val) {
  TwoWire &w = I2cGetWire(xl.bus);

  w.beginTransmission(xl.address);
  w.write(reg);
  w.write(val);
  return (w.endTransmission(true) == 0);  // STOP
}

static bool Xl9535_ReadAll(void) {
  if (!Xl9535_I2cReadReg(XL9535_REG_IN0,  &xl.in0))  return false;
  if (!Xl9535_I2cReadReg(XL9535_REG_IN1,  &xl.in1))  return false;
  if (!Xl9535_I2cReadReg(XL9535_REG_OUT0, &xl.out0)) return false;
  if (!Xl9535_I2cReadReg(XL9535_REG_OUT1, &xl.out1)) return false;
  if (!Xl9535_I2cReadReg(XL9535_REG_POL0, &xl.pol0)) return false;
  if (!Xl9535_I2cReadReg(XL9535_REG_POL1, &xl.pol1)) return false;
  if (!Xl9535_I2cReadReg(XL9535_REG_CFG0, &xl.cfg0)) return false;
  if (!Xl9535_I2cReadReg(XL9535_REG_CFG1, &xl.cfg1)) return false;
  return true;
}

// ---------- Factory apply ----------
// Safe order: set OUT latch first, then set CFG (direction).
static void Xl9535_ApplyFactory(void) {
  if (!xl.detected) return;

  const uint8_t out0 = (uint8_t)(kXl9535FactoryOut & 0xFF);
  const uint8_t out1 = (uint8_t)((kXl9535FactoryOut >> 8) & 0xFF);
  const uint8_t cfg0 = (uint8_t)(kXl9535FactoryCfg & 0xFF);
  const uint8_t cfg1 = (uint8_t)((kXl9535FactoryCfg >> 8) & 0xFF);

  // OUT first
  if (!Xl9535_I2cWriteReg(XL9535_REG_OUT0, out0) ||
      !Xl9535_I2cWriteReg(XL9535_REG_OUT1, out1)) {
    AddLog(LOG_LEVEL_ERROR, PSTR("XL9535: FACTORY apply OUT failed"));
    return;
  }

  // then CFG
  if (!Xl9535_I2cWriteReg(XL9535_REG_CFG0, cfg0) ||
      !Xl9535_I2cWriteReg(XL9535_REG_CFG1, cfg1)) {
    AddLog(LOG_LEVEL_ERROR, PSTR("XL9535: FACTORY apply CFG failed"));
    return;
  }

  // update cache
  xl.out0 = out0; xl.out1 = out1;
  xl.cfg0 = cfg0; xl.cfg1 = cfg1;

  AddLog(LOG_LEVEL_INFO, PSTR("XL9535: FACTORY cfg=0x%04X def_out=0x%04X"),
         (unsigned)kXl9535FactoryCfg, (unsigned)kXl9535FactoryOut);
}

// ---------- Detect ----------

static void Xl9535_Detect(void) {
  if (xl.detected) return;
  if (!I2cEnabled(XI2C_101)) return;

  xl.address = (uint8_t)XL9535_I2C_ADD;

  for (uint8_t bus = 0; bus < 2; bus++) {
    if (!I2cSetDevice(xl.address, bus)) continue;

    xl.bus = bus;

    // robust detect: read CFG regs
    uint8_t c0 = 0, c1 = 0;
    if (!Xl9535_I2cReadReg(XL9535_REG_CFG0, &c0)) continue;
    if (!Xl9535_I2cReadReg(XL9535_REG_CFG1, &c1)) continue;

    xl.cfg0 = c0;
    xl.cfg1 = c1;

    (void)Xl9535_ReadAll();

    xl.detected = true;
    I2cSetActiveFound(xl.address, "XL9535", xl.bus);
    AddLog(LOG_LEVEL_INFO, PSTR("XL9535: detected bus=%d addr=0x%02X cfg=0x%02X%02X"),
           xl.bus, xl.address, xl.cfg1, xl.cfg0);

    // apply boot state:
    // 1) try FRAM restore (preferred)
    // 2) if FRAM not available/invalid -> factory defaults (only if factory build)
    {
      uint16_t out = 0, cfg = 0;
      bool restored = Xl9535_LoadFromFram(&out, &cfg);

      if (restored) {
        uint16_t use_cfg = kXl9535Factory ? (uint16_t)kXl9535FactoryCfg : cfg;

        if (!Xl9535_ApplyOutCfg(out, use_cfg)) {
          AddLog(LOG_LEVEL_ERROR, PSTR("XL9535: restore apply failed"));
          // fallback to factory if defined
          if (kXl9535Factory) {
            Xl9535_ApplyFactory();
          }
        } else {
          AddLog(LOG_LEVEL_INFO, PSTR("XL9535: restored from FRAM block=%u out=0x%04X cfg=0x%04X seq=%u"),
                 (unsigned)kXlFramBlock, (unsigned)out, (unsigned)use_cfg, (unsigned)xl_persist_seq);
        }
      } else {
        if (kXl9535Factory) {
          Xl9535_ApplyFactory();
          // voliteľné: keď FRAM je dostupná ale prázdna/nevalidná, zapíš factory ako prvý record
          if (kXlFramEnable) {
            uint16_t fo = (uint16_t)kXl9535FactoryOut;
            uint16_t fc = (uint16_t)kXl9535FactoryCfg;
            (void)Xl9535_SaveToFram(fo, fc);
          }
        } else {
          // non-factory build: nič netlačíme, user si nastaví runtime
          AddLog(LOG_LEVEL_DEBUG, PSTR("XL9535: no FRAM state (block=%u)"), (unsigned)kXlFramBlock);
        }
      }
    }

    return;
  }

  AddLog(LOG_LEVEL_DEBUG, PSTR("XL9535: not detected (addr=0x%02X)"), (uint8_t)XL9535_I2C_ADD);
}

// ---------- Parsing helpers ----------

static bool Xl9535_ParsePin(const char *s, uint8_t *pin_out) {
  if (!s || !*s) return false;
  uint32_t p = (uint32_t)strtoul(s, nullptr, 0);
  if (p > 15) return false;
  *pin_out = (uint8_t)p;
  return true;
}

static bool Xl9535_ParseBitValue(const char *s, uint8_t *v_out) {
  if (!s || !*s) return false;
  uint32_t v = (uint32_t)strtoul(s, nullptr, 0);
  *v_out = (v ? 1 : 0);
  return true;
}

static bool Xl9535_ParseMode(const char *s, uint8_t *is_input_out) {
  if (!s || !*s) return false;
  if (!strcasecmp(s, "in") || !strcasecmp(s, "input"))  { *is_input_out = 1; return true; }
  if (!strcasecmp(s, "out")|| !strcasecmp(s, "output")) { *is_input_out = 0; return true; }
  uint32_t v = (uint32_t)strtoul(s, nullptr, 0);
  *is_input_out = (v ? 1 : 0);   // 1=input, 0=output
  return true;
}

static bool Xl9535_ReadRegForPin(uint8_t pin, uint8_t reg0, uint8_t reg1, uint8_t *val) {
  return Xl9535_I2cReadReg((pin < 8) ? reg0 : reg1, val);
}

static bool Xl9535_WriteRegForPin(uint8_t pin, uint8_t reg0, uint8_t reg1, uint8_t val) {
  return Xl9535_I2cWriteReg((pin < 8) ? reg0 : reg1, val);
}

// ---------- Console commands ----------

#define D_PRFX_XL9535             "Xl9535"
#define D_CMND_XL9535_INFO        "Info"
#define D_CMND_XL9535_READ        "Read"        // Xl9535Read <pin>
#define D_CMND_XL9535_WRITE       "Write"       // Xl9535Write <pin> <0/1>
#define D_CMND_XL9535_TOGGLE      "Toggle"      // Xl9535Toggle <pin>
#define D_CMND_XL9535_MODE        "Mode"        // Xl9535Mode <pin> <in|out|0|1>
#define D_CMND_XL9535_POLARITY    "Polarity"    // Xl9535Polarity <pin> <0/1>
#define D_CMND_XL9535_WRITEPORT   "WritePort"   // Xl9535WritePort <0x0000..0xFFFF>
#define D_CMND_XL9535_CONFIGPORT  "ConfigPort"  // Xl9535ConfigPort <0x0000..0xFFFF>
#define D_CMND_XL9535_STATE       "State"       // Xl9535State

static void CmndXl9535Info(void);
static void CmndXl9535Read(void);
static void CmndXl9535Write(void);
static void CmndXl9535Toggle(void);
static void CmndXl9535Mode(void);
static void CmndXl9535Polarity(void);
static void CmndXl9535WritePort(void);
static void CmndXl9535ConfigPort(void);
static void CmndXl9535State(void);

typedef void (*XlCmdFn)(void);

// --- Command maps (2 variants) ---

const char kXl9535Commands_User[] PROGMEM =
  D_PRFX_XL9535 "|"
  D_CMND_XL9535_INFO "|"
  D_CMND_XL9535_READ "|"
  D_CMND_XL9535_WRITE "|"
  D_CMND_XL9535_TOGGLE "|"
  D_CMND_XL9535_MODE "|"
  D_CMND_XL9535_POLARITY "|"
  D_CMND_XL9535_WRITEPORT "|"
  D_CMND_XL9535_CONFIGPORT "|"
  D_CMND_XL9535_STATE;

const XlCmdFn Xl9535Command_User[] PROGMEM = {
  &CmndXl9535Info,
  &CmndXl9535Read,
  &CmndXl9535Write,
  &CmndXl9535Toggle,
  &CmndXl9535Mode,
  &CmndXl9535Polarity,
  &CmndXl9535WritePort,
  &CmndXl9535ConfigPort,
  &CmndXl9535State
};

const char kXl9535Commands_Factory[] PROGMEM =
  D_PRFX_XL9535 "|"
  D_CMND_XL9535_INFO "|"
  D_CMND_XL9535_READ "|"
  D_CMND_XL9535_WRITE "|"
  D_CMND_XL9535_TOGGLE "|"
  D_CMND_XL9535_POLARITY "|"
  D_CMND_XL9535_WRITEPORT "|"
  D_CMND_XL9535_STATE;

const XlCmdFn Xl9535Command_Factory[] PROGMEM = {
  &CmndXl9535Info,
  &CmndXl9535Read,
  &CmndXl9535Write,
  &CmndXl9535Toggle,
  &CmndXl9535Polarity,
  &CmndXl9535WritePort,
  &CmndXl9535State
};

// ---------- Command handlers ----------

static void CmndXl9535Info(void) {
  Xl9535_Detect();
  if (!xl.detected) {
    Response_P(PSTR("{\"Xl9535Info\":\"not detected\"}"));
    return;
  }

  if (!Xl9535_ReadAll()) {
    Response_P(PSTR("{\"Xl9535Info\":\"read failed\"}"));
    return;
  }

  uint16_t in  = (uint16_t)xl.in0  | ((uint16_t)xl.in1  << 8);
  uint16_t out = (uint16_t)xl.out0 | ((uint16_t)xl.out1 << 8);
  uint16_t pol = (uint16_t)xl.pol0 | ((uint16_t)xl.pol1 << 8);
  uint16_t cfg = (uint16_t)xl.cfg0 | ((uint16_t)xl.cfg1 << 8);

  Response_P(PSTR("{\"Xl9535Info\":{\"bus\":%d,\"addr\":\"0x%02X\",\"in\":\"0x%04X\",\"out\":\"0x%04X\",\"pol\":\"0x%04X\",\"cfg\":\"0x%04X\",\"factory\":%u}}"),
             xl.bus, xl.address, in, out, pol, cfg, (unsigned)(kXl9535Factory ? 1 : 0));
}

static void CmndXl9535Read(void) {
  Xl9535_Detect();
  if (!xl.detected) {
    Response_P(PSTR("{\"Xl9535Read\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) {
    Response_P(PSTR("{\"Xl9535Read\":\"usage: Xl9535Read <pin>\"}"));
    return;
  }

  char *p = XdrvMailbox.data;
  while (p && (*p == ' ' || *p == '\t' || *p == ',')) p++;

  uint8_t pin = 0;
  if (!Xl9535_ParsePin(p, &pin)) {
    ResponseCmndFailed();
    return;
  }

  uint8_t inr = 0, outr = 0, cfgr = 0;
  if (!Xl9535_ReadRegForPin(pin, XL9535_REG_IN0,  XL9535_REG_IN1,  &inr))  { ResponseCmndFailed(); return; }
  if (!Xl9535_ReadRegForPin(pin, XL9535_REG_OUT0, XL9535_REG_OUT1, &outr)) { ResponseCmndFailed(); return; }
  if (!Xl9535_ReadRegForPin(pin, XL9535_REG_CFG0, XL9535_REG_CFG1, &cfgr)) { ResponseCmndFailed(); return; }

  uint8_t bit = (uint8_t)(1u << (pin & 7u));
  uint8_t v_in  = (inr  & bit) ? 1 : 0;
  uint8_t v_out = (outr & bit) ? 1 : 0;
  uint8_t is_in = (cfgr & bit) ? 1 : 0;

  Response_P(PSTR("{\"Xl9535Read\":{\"pin\":%u,\"mode\":\"%s\",\"input\":%u,\"output\":%u}}"),
             pin, is_in ? "in" : "out", v_in, v_out);
}

static void CmndXl9535Write(void) {
  Xl9535_Detect();
  if (!xl.detected) {
    Response_P(PSTR("{\"Xl9535Write\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) {
    Response_P(PSTR("{\"Xl9535Write\":\"usage: Xl9535Write <pin> <0|1>\"}"));
    return;
  }

  char *p = XdrvMailbox.data;
  while (p && (*p == ' ' || *p == '\t' || *p == ',')) p++;

  char *save = nullptr;
  char *a1 = strtok_r(p, " \t,", &save);
  char *a2 = strtok_r(nullptr, " \t,", &save);

  uint8_t pin = 0, v = 0;
  if (!Xl9535_ParsePin(a1, &pin) || !Xl9535_ParseBitValue(a2, &v)) {
    ResponseCmndFailed();
    return;
  }

  uint8_t outr = 0;
  if (!Xl9535_ReadRegForPin(pin, XL9535_REG_OUT0, XL9535_REG_OUT1, &outr)) {
    ResponseCmndFailed();
    return;
  }

  uint8_t bit = (uint8_t)(1u << (pin & 7u));
  uint8_t new_out = (uint8_t)(v ? (outr | bit) : (outr & (uint8_t)~bit));

  if (!Xl9535_WriteRegForPin(pin, XL9535_REG_OUT0, XL9535_REG_OUT1, new_out)) {
    ResponseCmndFailed();
    return;
  }

  // persist current OUT+CFG
  if (kXlFramEnable) {
    if (!Xl9535_ReadAll()) {
      // ak nechceš čítať, vieš si to poskladať z new_out + cache,
      // ale toto je najbezpečnejšie (state/cfg vždy presné)
    } else {
      uint16_t out16 = (uint16_t)xl.out0 | ((uint16_t)xl.out1 << 8);
      uint16_t cfg16 = kXl9535Factory ? (uint16_t)kXl9535FactoryCfg
                                      : ((uint16_t)xl.cfg0 | ((uint16_t)xl.cfg1 << 8));
      (void)Xl9535_SaveToFram(out16, cfg16);
    }
  }

  Response_P(PSTR("{\"Xl9535Write\":{\"pin\":%u,\"value\":%u}}"), pin, v);
}

static void CmndXl9535Toggle(void) {
  Xl9535_Detect();
  if (!xl.detected) {
    Response_P(PSTR("{\"Xl9535Toggle\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) {
    Response_P(PSTR("{\"Xl9535Toggle\":\"usage: Xl9535Toggle <pin>\"}"));
    return;
  }

  char *p = XdrvMailbox.data;
  while (p && (*p == ' ' || *p == '\t' || *p == ',')) p++;

  uint8_t pin = 0;
  if (!Xl9535_ParsePin(p, &pin)) { ResponseCmndFailed(); return; }

  uint8_t outr = 0;
  if (!Xl9535_ReadRegForPin(pin, XL9535_REG_OUT0, XL9535_REG_OUT1, &outr)) { ResponseCmndFailed(); return; }

  uint8_t bit = (uint8_t)(1u << (pin & 7u));
  uint8_t new_out = (uint8_t)(outr ^ bit);

  if (!Xl9535_WriteRegForPin(pin, XL9535_REG_OUT0, XL9535_REG_OUT1, new_out)) { ResponseCmndFailed(); return; }

  Xl9535_PersistNow();   // <-- DOPLNIŤ

  uint8_t v = (new_out & bit) ? 1 : 0;
  Response_P(PSTR("{\"Xl9535Toggle\":{\"pin\":%u,\"value\":%u}}"), pin, v);
}

static void CmndXl9535Mode(void) {
  // dostupné len v USER variante (factory ho ani nemapuje)
  Xl9535_Detect();
  if (!xl.detected) {
    Response_P(PSTR("{\"Xl9535Mode\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) {
    Response_P(PSTR("{\"Xl9535Mode\":\"usage: Xl9535Mode <pin> <in|out|0|1>\"}"));
    return;
  }

  char *p = XdrvMailbox.data;
  while (p && (*p == ' ' || *p == '\t' || *p == ',')) p++;

  char *save = nullptr;
  char *a1 = strtok_r(p, " \t,", &save);
  char *a2 = strtok_r(nullptr, " \t,", &save);

  uint8_t pin = 0, is_in = 0;
  if (!Xl9535_ParsePin(a1, &pin) || !Xl9535_ParseMode(a2, &is_in)) {
    ResponseCmndFailed();
    return;
  }

  uint8_t cfgr = 0;
  if (!Xl9535_ReadRegForPin(pin, XL9535_REG_CFG0, XL9535_REG_CFG1, &cfgr)) { ResponseCmndFailed(); return; }

  uint8_t bit = (uint8_t)(1u << (pin & 7u));
  uint8_t new_cfg = (uint8_t)(is_in ? (cfgr | bit) : (cfgr & (uint8_t)~bit));

  if (!Xl9535_WriteRegForPin(pin, XL9535_REG_CFG0, XL9535_REG_CFG1, new_cfg)) { ResponseCmndFailed(); return; }

  Response_P(PSTR("{\"Xl9535Mode\":{\"pin\":%u,\"mode\":\"%s\"}}"), pin, is_in ? "in" : "out");
}

static void CmndXl9535Polarity(void) {
  Xl9535_Detect();
  if (!xl.detected) {
    Response_P(PSTR("{\"Xl9535Polarity\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) {
    Response_P(PSTR("{\"Xl9535Polarity\":\"usage: Xl9535Polarity <pin> <0|1>\"}"));
    return;
  }

  char *p = XdrvMailbox.data;
  while (p && (*p == ' ' || *p == '\t' || *p == ',')) p++;

  char *save = nullptr;
  char *a1 = strtok_r(p, " \t,", &save);
  char *a2 = strtok_r(nullptr, " \t,", &save);

  uint8_t pin = 0, v = 0;
  if (!Xl9535_ParsePin(a1, &pin) || !Xl9535_ParseBitValue(a2, &v)) { ResponseCmndFailed(); return; }

  uint8_t polr = 0;
  if (!Xl9535_ReadRegForPin(pin, XL9535_REG_POL0, XL9535_REG_POL1, &polr)) { ResponseCmndFailed(); return; }

  uint8_t bit = (uint8_t)(1u << (pin & 7u));
  uint8_t new_pol = (uint8_t)(v ? (polr | bit) : (polr & (uint8_t)~bit));

  if (!Xl9535_WriteRegForPin(pin, XL9535_REG_POL0, XL9535_REG_POL1, new_pol)) { ResponseCmndFailed(); return; }

  Response_P(PSTR("{\"Xl9535Polarity\":{\"pin\":%u,\"invert\":%u}}"), pin, v);
}

static void CmndXl9535WritePort(void) {
  Xl9535_Detect();
  if (!xl.detected) {
    Response_P(PSTR("{\"Xl9535WritePort\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) {
    Response_P(PSTR("{\"Xl9535WritePort\":\"usage: Xl9535WritePort <0x0000..0xFFFF>\"}"));
    return;
  }

  uint32_t v = (uint32_t)strtoul(XdrvMailbox.data, nullptr, 0);
  uint8_t out0 = (uint8_t)(v & 0xFF);
  uint8_t out1 = (uint8_t)((v >> 8) & 0xFF);

  if (!Xl9535_I2cWriteReg(XL9535_REG_OUT0, out0)) { ResponseCmndFailed(); return; }
  if (!Xl9535_I2cWriteReg(XL9535_REG_OUT1, out1)) { ResponseCmndFailed(); return; }

  Xl9535_PersistNow();   // <-- DOPLNIŤ

  Response_P(PSTR("{\"Xl9535WritePort\":{\"out\":\"0x%04X\"}}"), (uint16_t)v);
}

static void CmndXl9535ConfigPort(void) {
  // dostupné len v USER variante (factory ho ani nemapuje)
  Xl9535_Detect();
  if (!xl.detected) {
    Response_P(PSTR("{\"Xl9535ConfigPort\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) {
    Response_P(PSTR("{\"Xl9535ConfigPort\":\"usage: Xl9535ConfigPort <0x0000..0xFFFF> (1=input,0=output)\"}"));
    return;
  }

  uint32_t v = (uint32_t)strtoul(XdrvMailbox.data, nullptr, 0);
  uint8_t cfg0 = (uint8_t)(v & 0xFF);
  uint8_t cfg1 = (uint8_t)((v >> 8) & 0xFF);

  if (!Xl9535_I2cWriteReg(XL9535_REG_CFG0, cfg0)) { ResponseCmndFailed(); return; }
  if (!Xl9535_I2cWriteReg(XL9535_REG_CFG1, cfg1)) { ResponseCmndFailed(); return; }

  Response_P(PSTR("{\"Xl9535ConfigPort\":{\"cfg\":\"0x%04X\"}}"), (uint16_t)v);
}

static void CmndXl9535State(void) {
  Xl9535_Detect();
  if (!xl.detected) {
    Response_P(PSTR("{\"Xl9535State\":\"not detected\"}"));
    return;
  }

  if (!Xl9535_ReadAll()) {
    Response_P(PSTR("{\"Xl9535State\":\"read failed\"}"));
    return;
  }

  const uint16_t in  = (uint16_t)xl.in0  | ((uint16_t)xl.in1  << 8);
  const uint16_t out = (uint16_t)xl.out0 | ((uint16_t)xl.out1 << 8);
  const uint16_t cfg = (uint16_t)xl.cfg0 | ((uint16_t)xl.cfg1 << 8);

  Response_P(PSTR("{\"Xl9535State\":{"));

  for (uint8_t pin = 0; pin < 16; pin++) {
    const uint16_t bit = (uint16_t)(1u << pin);
    const uint8_t  c   = (cfg & bit) ? 1 : 0;                 // 1=input,0=output
    const uint8_t  s   = c ? ((in  & bit) ? 1 : 0)            // input -> IN
                           : ((out & bit) ? 1 : 0);           // output -> OUT latch

    ResponseAppend_P(PSTR("\"pin%u\":{\"state\":%u,\"cfg\":%u}%c"),
                     pin, s, c, (pin == 15) ? ' ' : ',');
  }

  ResponseAppend_P(PSTR("}}"));
}

// ---------- Interface ----------

bool Xdrv96(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      Xl9535_Detect();
      break;

    case FUNC_COMMAND: {
      const char *cmds = kXl9535Factory ? kXl9535Commands_Factory : kXl9535Commands_User;
      const XlCmdFn *tbl = kXl9535Factory ? Xl9535Command_Factory : Xl9535Command_User;
      result = DecodeCommand(cmds, tbl);
      break;
    }

    case FUNC_ACTIVE:
      result = xl.detected;
      break;
  }

  return result;
}

#endif  // USE_XL9535
#endif  // USE_I2C