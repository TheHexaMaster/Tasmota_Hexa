//
// xdrv_52_3_berry_modbus_rtu.ino (glue from CPP driver to tasmota / berry) by HexaMaster
//

#ifdef USE_BERRY
#ifdef USE_BERRY_MODBUS_RTU

#include "berry.h"
#include "TasmotaSerial.h"
#include "asyncmodbus.h"

class TasmotaSerialTransport : public AsyncModbusRtu::Transport {
public:
  TasmotaSerialTransport(int rx, int tx) : _rx(rx), _tx(tx) {}
  ~TasmotaSerialTransport() override {
    if (_ser) { delete _ser; _ser = nullptr; }
  }

  void setInverted(bool inv) override { _inv = inv; }

  void setTxEnablePin(int pin) override {
    _tx_en = pin;
    if (_ser) {
      if (_tx_en >= 0) _ser->setTransmitEnablePin(_tx_en);
      else _ser->clearTransmitEnablePin();
    }
  }

  bool begin(long baud, uint32_t config) override {
    if (_ser) return true;
    _ser = new TasmotaSerial(_rx, _tx, 0, 0, TM_SERIAL_BUFFER_SIZE, _inv);
    if (!_ser) return false;
    bool ok = _ser->begin(baud, config);
    if (!ok) {
      delete _ser;
      _ser = nullptr;
      return false;
    }
    if (_ser->hardwareSerial()) {
      ClaimSerial();
    }
    if (_tx_en >= 0) _ser->setTransmitEnablePin(_tx_en);
    return true;
  }

  int available() override { return _ser ? _ser->available() : 0; }
  int read() override { return _ser ? _ser->read() : -1; }

  size_t write(const uint8_t* data, size_t len) override {
    if (!_ser || !data || !len) return 0;
    for (size_t i = 0; i < len; i++) _ser->write(data[i]);
    return len;
  }

  void flush() override { if (_ser) _ser->flush(); }

private:
  int _rx;
  int _tx;
  int _tx_en = -1;
  bool _inv = false;
  TasmotaSerial* _ser = nullptr;
};

static void* bm_get(struct bvm *vm) {
  be_getmember(vm, 1, ".p");
  void *p = be_tocomptr(vm, -1);
  be_pop(vm, 1);
  return p;
}

static const char* bm_unit_str(uint8_t id) {
  switch (id) {
    case 0x00: return nullptr;
    case 0x01: return "V";
    case 0x02: return "VA";
    case 0x03: return "A";
    case 0x04: return "W";
    case 0x05: return "Wh";
    case 0x06: return "kWh";
    case 0x07: return "Hz";
    case 0x08: return "°C";
    case 0x09: return "%";
    case 0x0A: return "Ah";
    case 0x0B: return "kW";
    case 0x0C: return "kVA";
    case 0x0D: return "VAr";
    case 0x0E: return "Ω";
    case 0x0F: return "ms";
    default:   return nullptr;
  }
}

static uint8_t bm_u8_from_int(struct bvm *vm, int idx) {
  if (!be_isint(vm, idx)) be_raise(vm, kTypeError, nullptr);
  int v = be_toint(vm, idx);
  if (v < 0 || v > 255) be_raise(vm, "value_error", "byte out of range");
  return (uint8_t)v;
}


extern "C" {

int32_t b_mbrtu_init(struct bvm *vm);
int32_t b_mbrtu_init(struct bvm *vm) {
  int32_t argc = be_top(vm);
  if (argc < 5 || !be_isint(vm, 2) || !be_isint(vm, 3) || !be_isint(vm, 4) || !be_isint(vm, 5)) {
    be_raise(vm, kTypeError, nullptr);
  }

  int rx = be_toint(vm, 2);
  int tx = be_toint(vm, 3);
  long baud = (long)be_toint(vm, 4);
  uint32_t cfg = (uint32_t)be_toint(vm, 5);

  bool inverted = false;
  int tx_en = -1;

  if (argc >= 6 && be_isbool(vm, 6)) inverted = be_tobool(vm, 6);
  if (argc >= 7 && be_isint(vm, 7)) tx_en = be_toint(vm, 7);

  TasmotaSerialTransport* tr = new TasmotaSerialTransport(rx, tx);
  if (!tr) be_raise(vm, "internal_error", "oom");

  AsyncModbusRtu* e = new AsyncModbusRtu(tr, true);
  if (!e) { delete tr; be_raise(vm, "internal_error", "oom"); }

  if (!e->begin(baud, cfg, inverted, tx_en)) {
    delete e;
    be_raise(vm, "internal_error", "Unable to start modbus serial");
  }

  be_pushcomptr(vm, (void*)e);
  be_setmember(vm, 1, ".p");
  be_return_nil(vm);
}

int32_t b_mbrtu_deinit(struct bvm *vm);
int32_t b_mbrtu_deinit(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  if (e) {
    delete e;
    be_pushcomptr(vm, (void*)nullptr);
    be_setmember(vm, 1, ".p");
  }
  be_return_nil(vm);
}

int32_t b_mbrtu_start(struct bvm *vm);
int32_t b_mbrtu_start(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  if (e) e->start();
  be_return_nil(vm);
}

int32_t b_mbrtu_stop(struct bvm *vm);
int32_t b_mbrtu_stop(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  if (e) e->stop();
  be_return_nil(vm);
}

int32_t b_mbrtu_add_device(struct bvm *vm);
int32_t b_mbrtu_add_device(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  int32_t argc = be_top(vm);
  if (!e || argc < 2 || !be_isint(vm, 2)) be_raise(vm, kTypeError, nullptr);

  uint8_t addr = (uint8_t)be_toint(vm, 2);
  uint32_t timeout_ms = (argc >= 3 && be_isint(vm, 3)) ? (uint32_t)be_toint(vm, 3) : 250;
  uint8_t retry = (argc >= 4 && be_isint(vm, 4)) ? (uint8_t)be_toint(vm, 4) : 0;
  uint32_t ir_iv = (argc >= 5 && be_isint(vm, 5)) ? (uint32_t)be_toint(vm, 5) : 0;
  uint32_t hr_iv = (argc >= 6 && be_isint(vm, 6)) ? (uint32_t)be_toint(vm, 6) : 0;
  uint8_t hr_div = (argc >= 7 && be_isint(vm, 7)) ? (uint8_t)be_toint(vm, 7) : 3;
  uint8_t dm_no  = (argc >= 8 && be_isint(vm, 8)) ? (uint8_t)be_toint(vm, 8) : 0;

  e->configDevice(addr, timeout_ms, retry, ir_iv, hr_iv, hr_div, dm_no);
  be_return_nil(vm);
}

int32_t b_mbrtu_enable_device(struct bvm *vm);
int32_t b_mbrtu_enable_device(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  if (!e || be_top(vm) < 3 || !be_isint(vm, 2) || !be_isbool(vm, 3)) be_raise(vm, kTypeError, nullptr);
  uint8_t addr = (uint8_t)be_toint(vm, 2);
  bool en = be_tobool(vm, 3);
  e->enableDevice(addr, en);
  be_return_nil(vm);
}

int32_t b_mbrtu_clear_patterns(struct bvm *vm);
int32_t b_mbrtu_clear_patterns(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  if (!e || be_top(vm) < 2 || !be_isint(vm, 2)) be_raise(vm, kTypeError, nullptr);
  uint8_t addr = (uint8_t)be_toint(vm, 2);
  e->clearPatterns(addr);
  be_return_nil(vm);
}

int32_t b_mbrtu_add_pattern(struct bvm *vm);
int32_t b_mbrtu_add_pattern(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  int32_t argc = be_top(vm);
  if (!e || argc < 6) be_raise(vm, kTypeError, nullptr);
  if (!be_isint(vm, 2) || !be_isbool(vm, 3) || !be_isint(vm, 4) || !be_isint(vm, 5) || !be_isint(vm, 6)) {
    be_raise(vm, kTypeError, nullptr);
  }

  uint8_t addr = (uint8_t)be_toint(vm, 2);
  bool is_hr = be_tobool(vm, 3);
  uint8_t func = (uint8_t)be_toint(vm, 4);
  uint16_t start = (uint16_t)be_toint(vm, 5);
  uint16_t count = (uint16_t)be_toint(vm, 6);
  uint32_t interval_ms = (argc >= 7 && be_isint(vm, 7)) ? (uint32_t)be_toint(vm, 7) : 0;

  e->addPattern(addr, is_hr, func, start, count, interval_ms);
  be_return_nil(vm);
}

int32_t b_mbrtu_get_block(struct bvm *vm);
int32_t b_mbrtu_get_block(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  if (!e || be_top(vm) < 5) be_raise(vm, kTypeError, nullptr);
  if (!be_isint(vm, 2) || !be_isint(vm, 3) || !be_isint(vm, 4) || !be_isint(vm, 5)) {
    be_raise(vm, kTypeError, nullptr);
  }

  uint8_t  addr  = (uint8_t)be_toint(vm, 2);
  uint8_t  func  = (uint8_t)be_toint(vm, 3);
  uint16_t start = (uint16_t)be_toint(vm, 4);
  uint16_t count = (uint16_t)be_toint(vm, 5);

  uint8_t* data = nullptr;
  uint16_t len  = 0;
  uint32_t ts   = 0;

  if (!e->getBlockCopy(addr, func, start, count, &data, &len, &ts) || !data || len == 0) {
    if (data) free(data);
    be_return_nil(vm);
  }

  be_newobject(vm, "map");

  be_pushstring(vm, "data");
  be_pushbytes(vm, data, len);
  be_data_insert(vm, -3);
  be_pop(vm, 2);

  be_pushstring(vm, "ts");
  be_pushint(vm, (int)ts);
  be_data_insert(vm, -3);
  be_pop(vm, 2);

  free(data);
  be_pop(vm, 1);
  be_return(vm);
}

int32_t b_mbrtu_write_reg(struct bvm *vm);
int32_t b_mbrtu_write_reg(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  if (!e || be_top(vm) < 4) be_raise(vm, kTypeError, nullptr);
  if (!be_isint(vm, 2) || !be_isint(vm, 3) || !be_isint(vm, 4)) be_raise(vm, kTypeError, nullptr);

  uint8_t addr = (uint8_t)be_toint(vm, 2);
  uint16_t reg = (uint16_t)be_toint(vm, 3);
  uint16_t val = (uint16_t)be_toint(vm, 4);

  be_pushbool(vm, e->writeReg(addr, reg, val));
  be_return(vm);
}

int32_t b_mbrtu_write_regs(struct bvm *vm);
int32_t b_mbrtu_write_regs(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  if (!e || be_top(vm) < 4) be_raise(vm, kTypeError, nullptr);
  if (!be_isint(vm, 2) || !be_isint(vm, 3) || !be_isbytes(vm, 4)) be_raise(vm, kTypeError, nullptr);

  uint8_t addr = (uint8_t)be_toint(vm, 2);
  uint16_t start = (uint16_t)be_toint(vm, 3);

  size_t len = 0;
  const uint8_t* payload = (const uint8_t*)be_tobytes(vm, 4, &len);

  be_pushbool(vm, e->writeRegsFromBytes(addr, start, payload, (uint16_t)len));
  be_return(vm);
}

// ---- NEW: add_datamap(datamap_id, points) ----
// points: [ [b0..b7] or [b0..b8], ... ]
int32_t b_mbrtu_add_datamap(struct bvm *vm);
int32_t b_mbrtu_add_datamap(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  if (!e || be_top(vm) < 3 || !be_isint(vm, 2)) be_raise(vm, kTypeError, nullptr);

  uint8_t dm_id = (uint8_t)be_toint(vm, 2);

  be_pushvalue(vm, 3);                          // points
  if (!be_isinstance(vm, -1)) {
    be_pop(vm, 1);
    be_raise(vm, kTypeError, "points must be list");
  }

  be_getbuiltin(vm, "list");                    // points, list_class
  if (!be_isderived(vm, -2)) {                  // points derived from list?
    be_pop(vm, 2);
    be_raise(vm, kTypeError, "points must be list");
  }
  be_pop(vm, 1);                                // remove list_class, keep points

  int32_t n = get_list_size(vm);                // points size
  if (n <= 0) {
    be_pop(vm, 1);
    be_pushbool(vm, false);
    be_return(vm);
  }

  AsyncModbusRtu::DataPointDef* defs =
      (AsyncModbusRtu::DataPointDef*)malloc((size_t)n * sizeof(AsyncModbusRtu::DataPointDef));
  if (!defs) {
    be_pop(vm, 1);
    be_raise(vm, "internal_error", "oom");
  }
  memset(defs, 0, (size_t)n * sizeof(AsyncModbusRtu::DataPointDef));

  int32_t w = 0;

  for (int32_t i = 0; i < n; i++) {
    get_list_item(vm, i);                       // points, point

    if (!be_isinstance(vm, -1)) { be_pop(vm, 1); continue; }

    be_getbuiltin(vm, "list");                  // points, point, list_class
    if (!be_isderived(vm, -2)) {                // point derived from list?
      be_pop(vm, 2);                             // pop list_class + point
      continue;
    }
    be_pop(vm, 1);                               // pop list_class, keep point

    int32_t m = get_list_size(vm);               // point size
    if (m != 8 && m != 9) { be_pop(vm, 1); continue; }

    uint8_t b[9];
    memset(b, 0, sizeof(b));

    for (int32_t j = 0; j < m; j++) {
      get_list_item(vm, j);                      // points, point, value
      b[j] = bm_u8_from_int(vm, -1);
      be_pop(vm, 1);                             // pop value
    }

    AsyncModbusRtu::DataPointDef d;
    memset(&d, 0, sizeof(d));
    d.func         = b[0];
    d.endian_flags = b[1];
    d.start        = (uint16_t)(((uint16_t)b[2] << 8) | (uint16_t)b[3]);
    d.count        = b[4];
    d.type         = b[5];
    d.decimals     = b[6];
    d.options      = b[7];
    d.unit_id      = (m == 9) ? b[8] : 0;

    defs[w++] = d;

    be_pop(vm, 1);                               // pop point -> back to points
  }

  be_pop(vm, 1);                                 // pop points

  if (w == 0) {
    free(defs);
    be_pushbool(vm, false);
    be_return(vm);
  }

  bool ok = e->addDatamap(dm_id, defs, (uint16_t)w);
  free(defs);

  be_pushbool(vm, ok);
  be_return(vm);
}
// ---- NEW: get_data(addr) -> map ----
int32_t b_mbrtu_get_data(struct bvm *vm);
int32_t b_mbrtu_get_data(struct bvm *vm) {
  AsyncModbusRtu* e = (AsyncModbusRtu*)bm_get(vm);
  if (!e || be_top(vm) < 2 || !be_isint(vm, 2)) be_raise(vm, kTypeError, nullptr);

  uint8_t addr = (uint8_t)be_toint(vm, 2);

  AsyncModbusRtu::DataItem* items = nullptr;
  uint16_t count = 0;
  uint32_t ts = 0;

  if (!e->getDataCopy(addr, &items, &count, &ts) || !items || count == 0) {
    if (items) AsyncModbusRtu::freeDataCopy(items, count);
    be_return_nil(vm);
  }

  be_newobject(vm, "map");
  int outer_inst = be_top(vm) - 1;
  int outer_data = be_top(vm);

  be_pushstring(vm, "ts");
  be_pushint(vm, (bint)ts);
  be_data_insert(vm, outer_data);
  be_pop(vm, 2);

  be_newobject(vm, "map");
  int map3_inst = be_top(vm) - 1;
  int map3_data = be_top(vm);

  be_newobject(vm, "map");
  int map4_inst = be_top(vm) - 1;
  int map4_data = be_top(vm);

  for (uint16_t i = 0; i < count; i++) {
    const AsyncModbusRtu::DataItem& it = items[i];
    int inner_data = (it.func == 0x03) ? map3_data : map4_data;

    be_newobject(vm, "list");
    int list_inst = be_top(vm) - 1;
    int list_data = be_top(vm);

    if (it.raw_kind == AsyncModbusRtu::VK_INT) {
      be_pushint(vm, (bint)it.raw_i);
    } else if (it.raw_kind == AsyncModbusRtu::VK_STR && it.raw_s) {
      be_pushstring(vm, it.raw_s);
    } else {
      be_pushnil(vm);
    }
    be_data_push(vm, list_data);
    be_pop(vm, 1);

    if (it.fmt_kind == AsyncModbusRtu::VK_INT) {
      be_pushint(vm, (bint)it.fmt_i);
    } else if (it.fmt_kind == AsyncModbusRtu::VK_REAL) {
      be_pushreal(vm, (breal)it.fmt_r);
    } else if (it.fmt_kind == AsyncModbusRtu::VK_STR && it.fmt_s) {
      be_pushstring(vm, it.fmt_s);
    } else {
      be_pushnil(vm);
    }
    be_data_push(vm, list_data);
    be_pop(vm, 1);

    const char* u = bm_unit_str(it.unit_id);
    if (u) be_pushstring(vm, u);
    else be_pushnil(vm);
    be_data_push(vm, list_data);
    be_pop(vm, 1);

    be_pushint(vm, (bint)it.start);
    be_pushvalue(vm, list_inst);
    be_data_insert(vm, inner_data);
    be_pop(vm, 2);

    be_pop(vm, 2);
  }

  be_pushint(vm, 3);
  be_pushvalue(vm, map3_inst);
  be_data_insert(vm, outer_data);
  be_pop(vm, 2);

  be_pushint(vm, 4);
  be_pushvalue(vm, map4_inst);
  be_data_insert(vm, outer_data);
  be_pop(vm, 2);

  AsyncModbusRtu::freeDataCopy(items, count);

  be_pop(vm, 5);
  be_return(vm);
}

} // extern "C"

#endif
#endif