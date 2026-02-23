#ifdef USE_BERRY
#ifdef USE_BERRY_MODBUS_RTU

#include "berry.h"
#include "TasmotaModbus.h"
#include "asyncmodbus.h"

class TasmotaModbusTransport : public AsyncModbusRtu::Transport {
public:
  TasmotaModbusTransport(int rx, int tx, int tx_en) : _mb(rx, tx, tx_en) {}

  bool begin(long baud, uint32_t config) override {
    return _mb.Begin(baud, config) != 0;
  }
  int available() override { return _mb.available(); }
  int read() override { return _mb.read(); }
  size_t write(const uint8_t* data, size_t len) override { return _mb.write(data, len); }
  void flush() override { _mb.flush(); }

private:
  TasmotaModbus _mb;
};

static AsyncModbusRtu* bm_get(bvm *vm);
static AsyncModbusRtu* bm_get(bvm *vm) {
  be_getmember(vm, 1, ".p");
  AsyncModbusRtu* p = (AsyncModbusRtu*)be_tocomptr(vm, -1);
  be_pop(vm, 1);
  return p;
}

extern "C" {

int32_t b_mbrtu_init(bvm *vm);
int32_t b_mbrtu_init(bvm *vm) {
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

  if (inverted) {
    be_raise(vm, "value_error", "inverted not supported with TasmotaModbus");
  }

  TasmotaModbusTransport* tr = new TasmotaModbusTransport(rx, tx, tx_en);
  if (!tr) be_raise(vm, "internal_error", "oom");

  AsyncModbusRtu* e = new AsyncModbusRtu(tr, true);
  if (!e) { delete tr; be_raise(vm, "internal_error", "oom"); }

  if (!e->begin(baud, cfg, false, tx_en)) {
    delete e;
    be_raise(vm, "internal_error", "Unable to start modbus serial");
  }

  be_pushcomptr(vm, (void*)e);
  be_setmember(vm, 1, ".p");
  be_return_nil(vm);
}

int32_t b_mbrtu_deinit(bvm *vm);
int32_t b_mbrtu_deinit(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
  if (e) {
    delete e;
    be_pushcomptr(vm, (void*)nullptr);
    be_setmember(vm, 1, ".p");
  }
  be_return_nil(vm);
}

int32_t b_mbrtu_start(bvm *vm);
int32_t b_mbrtu_start(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
  if (e) e->start();
  be_return_nil(vm);
}

int32_t b_mbrtu_stop(bvm *vm);
int32_t b_mbrtu_stop(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
  if (e) e->stop();
  be_return_nil(vm);
}

int32_t b_mbrtu_add_device(bvm *vm);
int32_t b_mbrtu_add_device(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
  int32_t argc = be_top(vm);
  if (!e || argc < 2 || !be_isint(vm, 2)) be_raise(vm, kTypeError, nullptr);

  uint8_t addr = (uint8_t)be_toint(vm, 2);
  uint32_t timeout_ms = (argc >= 3 && be_isint(vm, 3)) ? (uint32_t)be_toint(vm, 3) : 250;
  uint8_t retry = (argc >= 4 && be_isint(vm, 4)) ? (uint8_t)be_toint(vm, 4) : 0;
  uint32_t ir_iv = (argc >= 5 && be_isint(vm, 5)) ? (uint32_t)be_toint(vm, 5) : 0;
  uint32_t hr_iv = (argc >= 6 && be_isint(vm, 6)) ? (uint32_t)be_toint(vm, 6) : 0;
  uint8_t hr_div = (argc >= 7 && be_isint(vm, 7)) ? (uint8_t)be_toint(vm, 7) : 3;

  e->configDevice(addr, timeout_ms, retry, ir_iv, hr_iv, hr_div);
  be_return_nil(vm);
}

int32_t b_mbrtu_enable_device(bvm *vm);
int32_t b_mbrtu_enable_device(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
  if (!e || be_top(vm) < 3 || !be_isint(vm, 2) || !be_isbool(vm, 3)) be_raise(vm, kTypeError, nullptr);
  uint8_t addr = (uint8_t)be_toint(vm, 2);
  bool en = be_tobool(vm, 3);
  e->enableDevice(addr, en);
  be_return_nil(vm);
}

int32_t b_mbrtu_clear_patterns(bvm *vm);
int32_t b_mbrtu_clear_patterns(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
  if (!e || be_top(vm) < 2 || !be_isint(vm, 2)) be_raise(vm, kTypeError, nullptr);
  uint8_t addr = (uint8_t)be_toint(vm, 2);
  e->clearPatterns(addr);
  be_return_nil(vm);
}

int32_t b_mbrtu_add_pattern(bvm *vm);
int32_t b_mbrtu_add_pattern(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
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

int32_t b_mbrtu_get_block(bvm *vm);
int32_t b_mbrtu_get_block(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
  if (!e || be_top(vm) < 5) be_raise(vm, kTypeError, nullptr);
  if (!be_isint(vm, 2) || !be_isint(vm, 3) || !be_isint(vm, 4) || !be_isint(vm, 5)) be_raise(vm, kTypeError, nullptr);

  uint8_t addr = (uint8_t)be_toint(vm, 2);
  uint8_t func = (uint8_t)be_toint(vm, 3);
  uint16_t start = (uint16_t)be_toint(vm, 4);
  uint16_t count = (uint16_t)be_toint(vm, 5);

  uint8_t* data = nullptr;
  uint16_t len = 0;
  uint32_t ts = 0;

  if (!e->getBlockCopy(addr, func, start, count, &data, &len, &ts) || !data || len == 0) {
    if (data) free(data);
    be_return_nil(vm);
  }

  be_pushbytes(vm, (const char*)data, len);
  free(data);
  be_return(vm);
}

int32_t b_mbrtu_get_block_ts(bvm *vm);
int32_t b_mbrtu_get_block_ts(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
  if (!e || be_top(vm) < 5) be_raise(vm, kTypeError, nullptr);
  if (!be_isint(vm, 2) || !be_isint(vm, 3) || !be_isint(vm, 4) || !be_isint(vm, 5)) be_raise(vm, kTypeError, nullptr);

  uint8_t addr = (uint8_t)be_toint(vm, 2);
  uint8_t func = (uint8_t)be_toint(vm, 3);
  uint16_t start = (uint16_t)be_toint(vm, 4);
  uint16_t count = (uint16_t)be_toint(vm, 5);

  uint8_t* data = nullptr;
  uint16_t len = 0;
  uint32_t ts = 0;

  bool ok = e->getBlockCopy(addr, func, start, count, &data, &len, &ts);
  if (data) free(data);
  if (!ok) be_return_nil(vm);

  be_pushint(vm, (int)ts);
  be_return(vm);
}

int32_t b_mbrtu_write_reg(bvm *vm);
int32_t b_mbrtu_write_reg(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
  if (!e || be_top(vm) < 4) be_raise(vm, kTypeError, nullptr);
  if (!be_isint(vm, 2) || !be_isint(vm, 3) || !be_isint(vm, 4)) be_raise(vm, kTypeError, nullptr);

  uint8_t addr = (uint8_t)be_toint(vm, 2);
  uint16_t reg = (uint16_t)be_toint(vm, 3);
  uint16_t val = (uint16_t)be_toint(vm, 4);

  be_pushbool(vm, e->writeReg(addr, reg, val));
  be_return(vm);
}

int32_t b_mbrtu_write_regs(bvm *vm);
int32_t b_mbrtu_write_regs(bvm *vm) {
  AsyncModbusRtu* e = bm_get(vm);
  if (!e || be_top(vm) < 4) be_raise(vm, kTypeError, nullptr);
  if (!be_isint(vm, 2) || !be_isint(vm, 3) || !be_isbytes(vm, 4)) be_raise(vm, kTypeError, nullptr);

  uint8_t addr = (uint8_t)be_toint(vm, 2);
  uint16_t start = (uint16_t)be_toint(vm, 3);

  size_t len = 0;
  const uint8_t* payload = (const uint8_t*)be_tobytes(vm, 4, &len);

  be_pushbool(vm, e->writeRegsFromBytes(addr, start, payload, (uint16_t)len));
  be_return(vm);
}

}

#endif
#endif