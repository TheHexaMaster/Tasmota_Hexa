/*
  xdrv_95_ip5306.ino - IP5306 (I2C) PMIC console driver for Tasmota

  Copyright (C) 2026 by Martin Macák - HexaMaster <hexamaster@icloud.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#ifdef USE_I2C
#ifdef USE_IP5306

#define XDRV_95   95
#define XI2C_100  100

// Default IP5306 I2C address (commonly 0x75 on IP5306-I2C boards).
#ifndef IP5306_I2C_ADDR
#define IP5306_I2C_ADDR 0x75
#endif

// Optional: auto-apply your MSB Master defaults on successful detect.
#ifndef IP5306_APPLY_MSB_DEFAULTS_ON_BOOT
#define IP5306_APPLY_MSB_DEFAULTS_ON_BOOT 0
#endif

// ---- Registers used in your Berry driver ----
#define IP5306_REG_SYS_CTL0        0x00
#define IP5306_REG_SYS_CTL1        0x01
#define IP5306_REG_SYS_CTL2        0x02

#define IP5306_REG_CHG_CTL0        0x20
#define IP5306_REG_CHG_CTL1        0x21
#define IP5306_REG_CHG_CTL2        0x22
#define IP5306_REG_CHG_CTL3        0x23
#define IP5306_REG_CHG_DIG_CTL0    0x24

#define IP5306_REG_STATUS0         0x70
#define IP5306_REG_STATUS1         0x71
#define IP5306_REG_STATUS2         0x72

// ---- Bits used in your Berry driver ----
// SYS_CTL0 (0x00)
#define IP5306_SYS0_BOOST_EN       0x20
#define IP5306_SYS0_CHARGER_EN     0x10
#define IP5306_SYS0_AUTO_PWR_ON    0x04
#define IP5306_SYS0_BOOST_ALWAYS   0x02
#define IP5306_SYS0_BTN_SHDN       0x01

// STATUS bits (as per your Berry logic)
#define IP5306_STATUS0_CHARGING_BIT   0x04   // charging_state(): (state & 0x04) == 0
#define IP5306_STATUS1_FULL_BIT       0x08   // fully_charged(): (state & 0x08) != 0
#define IP5306_STATUS2_LIGHTLOAD_BIT  0x04   // is_light_load(): (state & 0x04) != 0

struct {
  bool    detected = false;
  uint8_t address  = (uint8_t)IP5306_I2C_ADDR;
  uint8_t bus      = 0;
} ip53;

// ---------- Low-level I2C helpers ----------

static bool Ip5306_ReadReg(uint8_t reg, uint8_t *val) {

  TwoWire& w = I2cGetWire(ip53.bus);

  w.beginTransmission(ip53.address);
  w.write(reg);
  if (w.endTransmission(true) != 0) {  // STOP
    return false;
  }

  if (1 != (uint32_t)w.requestFrom((uint8_t)ip53.address, (uint8_t)1)) {
    while (w.available()) (void)w.read();
    return false;
  }

  if (!w.available()) return false;
  *val = (uint8_t)w.read();
  return true;
}

static bool Ip5306_WriteReg(uint8_t reg, uint8_t val) {
  if (!ip53.detected) return false;

  TwoWire& w = I2cGetWire(ip53.bus);

  w.beginTransmission(ip53.address);
  w.write(reg);
  w.write(val);
  if (w.endTransmission(true) != 0) {  // STOP
    return false;
  }
  return true;
}

// Read-modify-write for single-bit toggles (keeps reserved bits intact)
static bool Ip5306_UpdateBit(uint8_t reg, uint8_t mask, bool enable) {
  uint8_t v = 0;
  if (!Ip5306_ReadReg(reg, &v)) return false;
  if (enable) v |= mask; else v &= (uint8_t)~mask;
  return Ip5306_WriteReg(reg, v);
}

// Your MSB Master default init sequence from Berry
static bool Ip5306_ApplyMsbDefaults(void) {
  // From your Berry:
  // write_reg(0x21, 29)
  // write_reg(0x01, 221)
  // write_reg(0x00, 53)
  // write_reg(0x24, 13)
  if (!Ip5306_WriteReg(IP5306_REG_CHG_CTL1,     29))  return false;
  if (!Ip5306_WriteReg(IP5306_REG_SYS_CTL1,     221)) return false;
  if (!Ip5306_WriteReg(IP5306_REG_SYS_CTL0,     53))  return false;
  if (!Ip5306_WriteReg(IP5306_REG_CHG_DIG_CTL0, 13))  return false;
  return true;
}

// ---------- Detect ----------

static void Ip5306_Detect(void) {
  if (ip53.detected) return;
  if (!I2cEnabled(XI2C_100)) return;

  ip53.address  = (uint8_t)IP5306_I2C_ADDR;

  for (ip53.bus = 0; ip53.bus < 2; ip53.bus++) {
    if (!I2cSetDevice(ip53.address, ip53.bus)) continue;

    // Probe by reading SYS_CTL0
    uint8_t v = 0;
    ip53.detected = Ip5306_ReadReg(IP5306_REG_SYS_CTL0, &v);

    if (ip53.detected) {
      I2cSetActiveFound(ip53.address, "IP5306", ip53.bus);
      AddLog(LOG_LEVEL_INFO, PSTR("IP5306: detected bus=%d addr=0x%02X SYS_CTL0=%u"),
             ip53.bus, ip53.address, (unsigned)v);

#if (IP5306_APPLY_MSB_DEFAULTS_ON_BOOT)
      if (Ip5306_ApplyMsbDefaults()) {
        AddLog(LOG_LEVEL_INFO, PSTR("IP5306: MSB defaults applied"));
      } else {
        AddLog(LOG_LEVEL_ERROR, PSTR("IP5306: MSB defaults apply failed"));
      }
#endif
      return;
    }
  }

  AddLog(LOG_LEVEL_DEBUG, PSTR("IP5306: not detected (addr=0x%02X)"), (uint8_t)IP5306_I2C_ADDR);
}

// ---------- Tasmota console command handling ----------
// Commands:
//   IP5306Info
//   IP5306Read <reg>
//   IP5306Write <reg> <value>
//   IP5306ApplyDefaults
//   IP5306Boost <0/1>
//   IP5306Charger <0/1>
//   IP5306AutoPowerOn <0/1>
//   IP5306BoostAlwaysOn <0/1>
//   IP5306ButtonShutdown <0/1>
//   IP5306ChargingCurrent <0..255>
//   IP5306ChargingState
//   IP5306FullyCharged
//   IP5306LightLoad

#define D_PRFX_IP5306                 "IP5306"
#define D_CMND_IP5306_INFO            "Info"
#define D_CMND_IP5306_READ            "Read"
#define D_CMND_IP5306_WRITE           "Write"
#define D_CMND_IP5306_APPLY_DEFAULTS  "ApplyDefaults"

#define D_CMND_IP5306_BOOST           "Boost"
#define D_CMND_IP5306_CHARGER         "Charger"
#define D_CMND_IP5306_AUTO_POWER_ON   "AutoPowerOn"
#define D_CMND_IP5306_BOOST_ALWAYS_ON "BoostAlwaysOn"
#define D_CMND_IP5306_BUTTON_SHUTDOWN "ButtonShutdown"

#define D_CMND_IP5306_CHG_CURRENT     "ChargingCurrent"
#define D_CMND_IP5306_CHG_STATE       "ChargingState"
#define D_CMND_IP5306_FULLY_CHARGED   "FullyCharged"
#define D_CMND_IP5306_LIGHT_LOAD      "LightLoad"

static void CmndIp5306Info(void);
static void CmndIp5306Read(void);
static void CmndIp5306Write(void);
static void CmndIp5306ApplyDefaults(void);

static void CmndIp5306Boost(void);
static void CmndIp5306Charger(void);
static void CmndIp5306AutoPowerOn(void);
static void CmndIp5306BoostAlwaysOn(void);
static void CmndIp5306ButtonShutdown(void);

static void CmndIp5306ChargingCurrent(void);
static void CmndIp5306ChargingState(void);
static void CmndIp5306FullyCharged(void);
static void CmndIp5306LightLoad(void);

const char kIp5306Commands[] PROGMEM =
  D_PRFX_IP5306 "|"
  D_CMND_IP5306_INFO "|"
  D_CMND_IP5306_READ "|"
  D_CMND_IP5306_WRITE "|"
  D_CMND_IP5306_APPLY_DEFAULTS "|"
  D_CMND_IP5306_BOOST "|"
  D_CMND_IP5306_CHARGER "|"
  D_CMND_IP5306_AUTO_POWER_ON "|"
  D_CMND_IP5306_BOOST_ALWAYS_ON "|"
  D_CMND_IP5306_BUTTON_SHUTDOWN "|"
  D_CMND_IP5306_CHG_CURRENT "|"
  D_CMND_IP5306_CHG_STATE "|"
  D_CMND_IP5306_FULLY_CHARGED "|"
  D_CMND_IP5306_LIGHT_LOAD;

void (*const Ip5306Command[])(void) PROGMEM = {
  &CmndIp5306Info,
  &CmndIp5306Read,
  &CmndIp5306Write,
  &CmndIp5306ApplyDefaults,
  &CmndIp5306Boost,
  &CmndIp5306Charger,
  &CmndIp5306AutoPowerOn,
  &CmndIp5306BoostAlwaysOn,
  &CmndIp5306ButtonShutdown,
  &CmndIp5306ChargingCurrent,
  &CmndIp5306ChargingState,
  &CmndIp5306FullyCharged,
  &CmndIp5306LightLoad
};

static void CmndIp5306Info(void) {
  Ip5306_Detect();
  if (!ip53.detected) {
    Response_P(PSTR("{\"IP5306Info\":\"not detected\"}"));
    return;
  }

  uint8_t sys0=0, sys1=0, sys2=0, c1=0, c24=0, s0=0, s1=0, s2=0;
  (void)Ip5306_ReadReg(IP5306_REG_SYS_CTL0, &sys0);
  (void)Ip5306_ReadReg(IP5306_REG_SYS_CTL1, &sys1);
  (void)Ip5306_ReadReg(IP5306_REG_SYS_CTL2, &sys2);
  (void)Ip5306_ReadReg(IP5306_REG_CHG_CTL1, &c1);
  (void)Ip5306_ReadReg(IP5306_REG_CHG_DIG_CTL0, &c24);
  (void)Ip5306_ReadReg(IP5306_REG_STATUS0, &s0);
  (void)Ip5306_ReadReg(IP5306_REG_STATUS1, &s1);
  (void)Ip5306_ReadReg(IP5306_REG_STATUS2, &s2);

  Response_P(PSTR("{\"IP5306Info\":{\"bus\":%d,\"addr\":\"0x%02X\","
                  "\"SYS_CTL0\":%u,\"SYS_CTL1\":%u,\"SYS_CTL2\":%u,"
                  "\"CHG_CTL1\":%u,\"CHG_DIG_CTL0\":%u,"
                  "\"STATUS0\":%u,\"STATUS1\":%u,\"STATUS2\":%u}}"),
             ip53.bus, ip53.address,
             (unsigned)sys0, (unsigned)sys1, (unsigned)sys2,
             (unsigned)c1, (unsigned)c24,
             (unsigned)s0, (unsigned)s1, (unsigned)s2);
}

static void CmndIp5306Read(void) {
  Ip5306_Detect();
  if (!ip53.detected) {
    Response_P(PSTR("{\"IP5306Read\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) {
    Response_P(PSTR("{\"IP5306Read\":\"usage: IP5306Read <reg>\"}"));
    return;
  }

  uint32_t reg = (uint32_t)strtoul(XdrvMailbox.data, nullptr, 0);
  if (reg > 0xFF) { ResponseCmndFailed(); return; }

  uint8_t v = 0;
  if (!Ip5306_ReadReg((uint8_t)reg, &v)) { ResponseCmndFailed(); return; }

  Response_P(PSTR("{\"IP5306Read\":{\"reg\":\"0x%02X\",\"value\":%u}}"), (unsigned)reg, (unsigned)v);
}

static void CmndIp5306Write(void) {
  Ip5306_Detect();
  if (!ip53.detected) {
    Response_P(PSTR("{\"IP5306Write\":\"not detected\"}"));
    return;
  }

  if (!XdrvMailbox.data_len) {
    Response_P(PSTR("{\"IP5306Write\":\"usage: IP5306Write <reg> <value>\"}"));
    return;
  }

  char *p = XdrvMailbox.data;
  while (*p == ' ' || *p == '\t' || *p == ',') p++;

  char *save = nullptr;
  char *a1 = strtok_r(p, " \t,", &save);
  char *a2 = strtok_r(nullptr, " \t,", &save);
  if (!a1 || !a2) { ResponseCmndFailed(); return; }

  uint32_t reg = (uint32_t)strtoul(a1, nullptr, 0);
  uint32_t val = (uint32_t)strtoul(a2, nullptr, 0);
  if (reg > 0xFF || val > 0xFF) { ResponseCmndFailed(); return; }

  if (!Ip5306_WriteReg((uint8_t)reg, (uint8_t)val)) { ResponseCmndFailed(); return; }

  Response_P(PSTR("{\"IP5306Write\":{\"reg\":\"0x%02X\",\"value\":%u}}"), (unsigned)reg, (unsigned)val);
}

static void CmndIp5306ApplyDefaults(void) {
  Ip5306_Detect();
  if (!ip53.detected) {
    Response_P(PSTR("{\"IP5306ApplyDefaults\":\"not detected\"}"));
    return;
  }

  bool ok = Ip5306_ApplyMsbDefaults();
  Response_P(PSTR("{\"IP5306ApplyDefaults\":%u}"), (unsigned)(ok ? 1 : 0));
}

static bool Ip5306_ParseBoolArg(bool *out) {
  if (!XdrvMailbox.data_len) return false;
  uint32_t v = (uint32_t)strtoul(XdrvMailbox.data, nullptr, 0);
  *out = (v != 0);
  return true;
}

static void CmndIp5306Boost(void) {
  Ip5306_Detect();
  if (!ip53.detected) { Response_P(PSTR("{\"IP5306Boost\":\"not detected\"}")); return; }
  bool en=false; if (!Ip5306_ParseBoolArg(&en)) { ResponseCmndFailed(); return; }
  if (!Ip5306_UpdateBit(IP5306_REG_SYS_CTL0, IP5306_SYS0_BOOST_EN, en)) { ResponseCmndFailed(); return; }
  Response_P(PSTR("{\"IP5306Boost\":%u}"), (unsigned)(en ? 1 : 0));
}

static void CmndIp5306Charger(void) {
  Ip5306_Detect();
  if (!ip53.detected) { Response_P(PSTR("{\"IP5306Charger\":\"not detected\"}")); return; }
  bool en=false; if (!Ip5306_ParseBoolArg(&en)) { ResponseCmndFailed(); return; }
  if (!Ip5306_UpdateBit(IP5306_REG_SYS_CTL0, IP5306_SYS0_CHARGER_EN, en)) { ResponseCmndFailed(); return; }
  Response_P(PSTR("{\"IP5306Charger\":%u}"), (unsigned)(en ? 1 : 0));
}

static void CmndIp5306AutoPowerOn(void) {
  Ip5306_Detect();
  if (!ip53.detected) { Response_P(PSTR("{\"IP5306AutoPowerOn\":\"not detected\"}")); return; }
  bool en=false; if (!Ip5306_ParseBoolArg(&en)) { ResponseCmndFailed(); return; }
  if (!Ip5306_UpdateBit(IP5306_REG_SYS_CTL0, IP5306_SYS0_AUTO_PWR_ON, en)) { ResponseCmndFailed(); return; }
  Response_P(PSTR("{\"IP5306AutoPowerOn\":%u}"), (unsigned)(en ? 1 : 0));
}

static void CmndIp5306BoostAlwaysOn(void) {
  Ip5306_Detect();
  if (!ip53.detected) { Response_P(PSTR("{\"IP5306BoostAlwaysOn\":\"not detected\"}")); return; }
  bool en=false; if (!Ip5306_ParseBoolArg(&en)) { ResponseCmndFailed(); return; }
  if (!Ip5306_UpdateBit(IP5306_REG_SYS_CTL0, IP5306_SYS0_BOOST_ALWAYS, en)) { ResponseCmndFailed(); return; }
  Response_P(PSTR("{\"IP5306BoostAlwaysOn\":%u}"), (unsigned)(en ? 1 : 0));
}

static void CmndIp5306ButtonShutdown(void) {
  Ip5306_Detect();
  if (!ip53.detected) { Response_P(PSTR("{\"IP5306ButtonShutdown\":\"not detected\"}")); return; }
  bool en=false; if (!Ip5306_ParseBoolArg(&en)) { ResponseCmndFailed(); return; }
  if (!Ip5306_UpdateBit(IP5306_REG_SYS_CTL0, IP5306_SYS0_BTN_SHDN, en)) { ResponseCmndFailed(); return; }
  Response_P(PSTR("{\"IP5306ButtonShutdown\":%u}"), (unsigned)(en ? 1 : 0));
}

static void CmndIp5306ChargingCurrent(void) {
  Ip5306_Detect();
  if (!ip53.detected) { Response_P(PSTR("{\"IP5306ChargingCurrent\":\"not detected\"}")); return; }
  if (!XdrvMailbox.data_len) { Response_P(PSTR("{\"IP5306ChargingCurrent\":\"usage: IP5306ChargingCurrent <0..255>\"}")); return; }

  uint32_t v = (uint32_t)strtoul(XdrvMailbox.data, nullptr, 0);
  if (v > 255) { ResponseCmndFailed(); return; }

  if (!Ip5306_WriteReg(IP5306_REG_CHG_DIG_CTL0, (uint8_t)v)) { ResponseCmndFailed(); return; }
  Response_P(PSTR("{\"IP5306ChargingCurrent\":%u}"), (unsigned)v);
}

static void CmndIp5306ChargingState(void) {
  Ip5306_Detect();
  if (!ip53.detected) { Response_P(PSTR("{\"IP5306ChargingState\":\"not detected\"}")); return; }

  uint8_t s = 0;
  if (!Ip5306_ReadReg(IP5306_REG_STATUS0, &s)) { ResponseCmndFailed(); return; }

  // Your Berry: (state & 0x04) == 0
  uint8_t charging = ((s & IP5306_STATUS0_CHARGING_BIT) == 0) ? 1 : 0;

  Response_P(PSTR("{\"IP5306ChargingState\":{\"STATUS0\":%u,\"charging\":%u}}"),
             (unsigned)s, (unsigned)charging);
}

static void CmndIp5306FullyCharged(void) {
  Ip5306_Detect();
  if (!ip53.detected) { Response_P(PSTR("{\"IP5306FullyCharged\":\"not detected\"}")); return; }

  uint8_t s = 0;
  if (!Ip5306_ReadReg(IP5306_REG_STATUS1, &s)) { ResponseCmndFailed(); return; }

  uint8_t full = ((s & IP5306_STATUS1_FULL_BIT) != 0) ? 1 : 0;

  Response_P(PSTR("{\"IP5306FullyCharged\":{\"STATUS1\":%u,\"full\":%u}}"),
             (unsigned)s, (unsigned)full);
}

static void CmndIp5306LightLoad(void) {
  Ip5306_Detect();
  if (!ip53.detected) { Response_P(PSTR("{\"IP5306LightLoad\":\"not detected\"}")); return; }

  uint8_t s = 0;
  if (!Ip5306_ReadReg(IP5306_REG_STATUS2, &s)) { ResponseCmndFailed(); return; }

  uint8_t light = ((s & IP5306_STATUS2_LIGHTLOAD_BIT) != 0) ? 1 : 0;

  Response_P(PSTR("{\"IP5306LightLoad\":{\"STATUS2\":%u,\"light_load\":%u}}"),
             (unsigned)s, (unsigned)light);
}

// ---------- Interface ----------

bool Xdrv95(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      Ip5306_Detect();
      break;

    case FUNC_COMMAND:
      result = DecodeCommand(kIp5306Commands, Ip5306Command);
      break;

    case FUNC_ACTIVE:
      result = ip53.detected;
      break;
  }

  return result;
}

#endif  // USE_IP5306
#endif  // USE_I2C