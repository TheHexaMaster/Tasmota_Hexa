//
// AsyncModbus.cpp by HexaMaster
//

#include "AsyncModbus.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

struct AsyncModbusRtu::PatternNode {
  uint8_t func;
  uint16_t start;
  uint16_t count;
  uint32_t interval_ms;
  PatternNode* next;
};

struct AsyncModbusRtu::DecodedEntry {
  DataPointDef def;
  bool has_value;

  // raw
  int64_t raw_i;
  char*   raw_hex;   // for STRING/TIME6 only
  uint16_t raw_fp;   // fingerprint of raw bytes slice for STRING/TIME6

  // formatted
  ValueKind fmt_kind;
  int64_t   fmt_i;
  double    fmt_r;
  char*     fmt_s;   // for STRING/TIME6 only
};

struct AsyncModbusRtu::Device {
  uint8_t addr;
  bool enabled;

  uint32_t timeout_ms;
  uint8_t retry;

  uint32_t ir_interval_ms;
  uint32_t hr_interval_ms;
  uint8_t hr_divider;

  uint8_t hr_state;
  bool hr_round;

  PatternNode* ir_head;
  PatternNode* hr_head;
  PatternNode* ir_cur;
  PatternNode* hr_cur;

  uint32_t next_ir_ms;
  uint32_t next_hr_ms;

  // datamap binding
  uint8_t datamap_no;

  // decoded store
  uint32_t data_ts;
  uint16_t dm_n3;
  uint16_t dm_n4;
  DecodedEntry* dm_data;   // size = dm_n3 + dm_n4

  Device* next;
};

struct AsyncModbusRtu::CacheEntry {
  uint64_t k;
  uint8_t addr;
  uint8_t func;
  uint16_t start;
  uint16_t count;
  uint16_t len;
  uint8_t* data;
  uint32_t last_good_ms;
  uint16_t fp;
  bool valid;
  CacheEntry* next;
};

struct AsyncModbusRtu::WriteReq {
  bool used;
  uint8_t addr;
  uint8_t func;
  uint16_t start;
  uint16_t count;
  uint16_t* values;
};

struct AsyncModbusRtu::DataMap {
  uint8_t id;

  DataPointDef* p3;
  uint16_t n3;

  DataPointDef* p4;
  uint16_t n4;

  DataMap* next;
};

static uint16_t* am_new_u16(size_t n) {
  if (n == 0) return nullptr;
  void* p = std::malloc(n * sizeof(uint16_t));
  return (uint16_t*)p;
}

static inline uint16_t am_u16_be(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static inline uint16_t am_bcd2(uint8_t v) {
  return (uint16_t)(((v >> 4) & 0x0F) * 10u + (v & 0x0F));
}

static inline uint16_t am_bcd4_u16(uint16_t v) {
  uint8_t hi = (uint8_t)(v >> 8);
  uint8_t lo = (uint8_t)(v & 0xFF);
  return (uint16_t)(am_bcd2(hi) * 100u + am_bcd2(lo));
}

static inline uint16_t am_swap16(uint16_t v) {
  return (uint16_t)((v << 8) | (v >> 8));
}

static uint16_t am_lower_bound_start(const AsyncModbusRtu::DataPointDef* a, uint16_t n, uint16_t start) {
  uint16_t lo = 0, hi = n;
  while (lo < hi) {
    uint16_t mid = (uint16_t)((lo + hi) >> 1);
    if (a[mid].start < start) lo = (uint16_t)(mid + 1);
    else hi = mid;
  }
  return lo;
}

static void am_sort_by_start(AsyncModbusRtu::DataPointDef* a, uint16_t n) {
  // simple insertion sort (n is typically small/moderate)
  for (uint16_t i = 1; i < n; i++) {
    AsyncModbusRtu::DataPointDef key = a[i];
    int16_t j = (int16_t)i - 1;
    while (j >= 0 && a[j].start > key.start) {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = key;
  }
}

static char* am_to_hex(const uint8_t* data, uint16_t len) {
  static const char kHexDigits[] = "0123456789ABCDEF";
  if (!data || len == 0) {
    char* s = (char*)std::malloc(1);
    if (s) s[0] = 0;
    return s;
  }
  size_t out_len = (size_t)len * 2u + 1u;
  char* out = (char*)std::malloc(out_len);
  if (!out) return nullptr;

  for (uint16_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    out[i * 2]     = kHexDigits[(b >> 4) & 0x0F];
    out[i * 2 + 1] = kHexDigits[b & 0x0F];
  }
  out[out_len - 1] = 0;
  return out;
}

static char* am_strdup_c(const char* s) {
  if (!s) return nullptr;
  size_t n = std::strlen(s);
  char* o = (char*)std::malloc(n + 1);
  if (!o) return nullptr;
  std::memcpy(o, s, n);
  o[n] = 0;
  return o;
}

AsyncModbusRtu::AsyncModbusRtu(Transport* t, bool take_ownership)
: _t(t), _own_t(take_ownership) {
#ifdef ESP32
  _mtx = xSemaphoreCreateMutex();
#endif
  _wq = (WriteReq*)std::malloc(WQ_MAX * sizeof(WriteReq));
  if (_wq) {
    std::memset(_wq, 0, WQ_MAX * sizeof(WriteReq));
  }
  _cur_write = (WriteReq*)std::malloc(sizeof(WriteReq));
  if (_cur_write) std::memset(_cur_write, 0, sizeof(WriteReq));
  std::memset(_rx_buf, 0, sizeof(_rx_buf));
}

AsyncModbusRtu::~AsyncModbusRtu() {
  stop();
#ifdef ESP32
  _task_stop = true;
  if (_task) {
    vTaskDelete(_task);
    _task = nullptr;
  }
#endif
  lock();
  clearAll();
  clearAllDatamaps();
  unlock();

  if (_cur_write) { std::free(_cur_write); _cur_write = nullptr; }
  if (_wq) { std::free(_wq); _wq = nullptr; }

#ifdef ESP32
  if (_mtx) { vSemaphoreDelete(_mtx); _mtx = nullptr; }
#endif

  if (_own_t && _t) { delete _t; _t = nullptr; }
}

bool AsyncModbusRtu::begin(long baud, uint32_t config, bool inverted, int tx_en_pin) {
  if (!_t) return false;
  _t->setInverted(inverted);
  if (tx_en_pin >= 0) _t->setTxEnablePin(tx_en_pin);
  if (!_t->begin(baud, config)) return false;

#ifdef ESP32
  if (!_task) {
    _task_stop = false;
    BaseType_t ok = xTaskCreate(&AsyncModbusRtu::taskThunk, "AMBRtu",
                               ASYNCMODBUS_TASK_STACK / sizeof(StackType_t),
                               this, ASYNCMODBUS_TASK_PRIO, &_task);
    if (ok != pdPASS) {
      _task = nullptr;
      return false;
    }
  }
#endif
  return true;
}

void AsyncModbusRtu::start() {
  _running = true;
}

void AsyncModbusRtu::stop() {
  _running = false;
  lock();
  _state = IDLE;
  rxReset();
  _cur_dev = nullptr;
  _cur_is_write = false;
  unlock();
}

void AsyncModbusRtu::setSilentGapMs(uint32_t ms) {
  lock();
  _silent_ms = ms;
  unlock();
}

uint32_t AsyncModbusRtu::silentGapMs() const {
  return _silent_ms;
}

void AsyncModbusRtu::configDevice(uint8_t addr, uint32_t timeout_ms, uint8_t retry,
                                  uint32_t ir_iv_ms, uint32_t hr_iv_ms, uint8_t hr_div,
                                  uint8_t datamap_no) {
  lock();
  Device* d = getOrAddDevice(addr);
  if (d) {
    d->timeout_ms = timeout_ms;
    d->retry = retry;
    d->ir_interval_ms = ir_iv_ms;
    d->hr_interval_ms = hr_iv_ms;
    d->hr_divider = (hr_div == 0 ? 1 : hr_div);

    if (d->datamap_no != datamap_no) {
      d->datamap_no = datamap_no;
      rebuildDeviceDecoded(d);
    }
  }
  unlock();
}

void AsyncModbusRtu::setDeviceDatamap(uint8_t addr, uint8_t datamap_no) {
  lock();
  Device* d = getOrAddDevice(addr);
  if (d) {
    if (d->datamap_no != datamap_no) {
      d->datamap_no = datamap_no;
      rebuildDeviceDecoded(d);
    }
  }
  unlock();
}

void AsyncModbusRtu::enableDevice(uint8_t addr, bool en) {
  lock();
  Device* d = getOrAddDevice(addr);
  if (d) d->enabled = en;
  unlock();
}

void AsyncModbusRtu::clearPatterns(uint8_t addr) {
  lock();
  Device* d = getOrAddDevice(addr);
  if (d) {
    freePatterns(d->ir_head);
    freePatterns(d->hr_head);
    d->ir_head = d->hr_head = nullptr;
    d->ir_cur = d->hr_cur = nullptr;
    d->hr_state = 0;
    d->hr_round = false;
    d->next_ir_ms = 0;
    d->next_hr_ms = 0;
  }
  unlock();
}

void AsyncModbusRtu::addPattern(uint8_t addr, bool is_hr, uint8_t func, uint16_t start, uint16_t count, uint32_t interval_ms) {
  if (count == 0 || count > 125) return;
  if (func != 0x03 && func != 0x04) return;

  lock();
  Device* d = getOrAddDevice(addr);
  if (!d) { unlock(); return; }

  PatternNode* p = (PatternNode*)std::malloc(sizeof(PatternNode));
  if (!p) { unlock(); return; }
  p->func = func;
  p->start = start;
  p->count = count;
  p->interval_ms = interval_ms;
  p->next = nullptr;

  if (is_hr) {
    appendPattern(&d->hr_head, p);
    if (!d->hr_cur) d->hr_cur = d->hr_head;
  } else {
    appendPattern(&d->ir_head, p);
    if (!d->ir_cur) d->ir_cur = d->ir_head;
  }

  (void)findOrCreateCache(addr, func, start, count);
  unlock();
}

bool AsyncModbusRtu::getBlockCopy(uint8_t addr, uint8_t func, uint16_t start, uint16_t count,
                                 uint8_t** data_out, uint16_t* len_out, uint32_t* last_good_ms_out) const {
  if (data_out) *data_out = nullptr;
  if (len_out) *len_out = 0;
  if (last_good_ms_out) *last_good_ms_out = 0;

  lock();
  const CacheEntry* ce = findCache(addr, func, start, count);
  if (!ce || !ce->valid || !ce->data || ce->len == 0) { unlock(); return false; }

  uint8_t* b = (uint8_t*)std::malloc(ce->len);
  if (!b) { unlock(); return false; }
  std::memcpy(b, ce->data, ce->len);

  if (data_out) *data_out = b;
  if (len_out) *len_out = ce->len;
  if (last_good_ms_out) *last_good_ms_out = ce->last_good_ms;
  unlock();
  return true;
}

bool AsyncModbusRtu::writeReg(uint8_t addr, uint16_t reg, uint16_t value) {
  uint16_t* vals = am_new_u16(1);
  if (!vals) return false;
  vals[0] = value;

  lock();
  bool ok = wqPush(addr, 0x06, reg, 1, vals);
  unlock();
  return ok;
}

bool AsyncModbusRtu::writeRegsFromBytes(uint8_t addr, uint16_t start, const uint8_t* payload, uint16_t payload_len) {
  if (!payload || payload_len == 0 || (payload_len & 1)) return false;
  uint16_t count = payload_len / 2;
  if (count == 0 || count > 123) return false;

  uint16_t* vals = am_new_u16(count);
  if (!vals) return false;

  for (uint16_t i = 0; i < count; i++) {
    vals[i] = (uint16_t)((uint16_t)payload[i * 2] << 8) | (uint16_t)payload[i * 2 + 1];
  }

  lock();
  bool ok = wqPush(addr, 0x10, start, count, vals);
  unlock();
  return ok;
}

bool AsyncModbusRtu::addDatamap(uint8_t id, const DataPointDef* defs, uint16_t count) {
  if (id == 0) return false;

  lock();

  // remove existing same id
  DataMap* prev = nullptr;
  DataMap* cur = _dm_head;
  while (cur) {
    if (cur->id == id) break;
    prev = cur;
    cur = cur->next;
  }
  if (cur) {
    if (prev) prev->next = cur->next;
    else _dm_head = cur->next;
    freeDatamap(cur);
  }

  DataMap* dm = (DataMap*)std::malloc(sizeof(DataMap));
  if (!dm) { unlock(); return false; }
  std::memset(dm, 0, sizeof(DataMap));
  dm->id = id;

  // count func buckets
  uint16_t n3 = 0, n4 = 0;
  for (uint16_t i = 0; i < count; i++) {
    if (!defs) break;
    if (defs[i].func == 0x03) n3++;
    else if (defs[i].func == 0x04) n4++;
  }

  if (n3) dm->p3 = (DataPointDef*)std::malloc((size_t)n3 * sizeof(DataPointDef));
  if (n4) dm->p4 = (DataPointDef*)std::malloc((size_t)n4 * sizeof(DataPointDef));
  if ((n3 && !dm->p3) || (n4 && !dm->p4)) {
    freeDatamap(dm);
    unlock();
    return false;
  }

  // copy
  uint16_t i3 = 0, i4 = 0;
  for (uint16_t i = 0; i < count; i++) {
    const DataPointDef& d = defs[i];

    // validate minimal
    if (d.func != 0x03 && d.func != 0x04) continue;
    if (d.count == 0 || d.count > 125) continue;

    // validate type/count
    bool ok = true;
    if (d.type == DM_T_INT16 || d.type == DM_T_UINT16) ok = (d.count == 1);
    else if (d.type == DM_T_INT32 || d.type == DM_T_UINT32) ok = (d.count == 2);
    else if (d.type == DM_T_TIME6) ok = (d.count == 6);
    else if (d.type == DM_T_STRING) ok = (d.count >= 1);
    else ok = false;
    if (!ok) continue;

    if (d.func == 0x03 && dm->p3 && i3 < n3) dm->p3[i3++] = d;
    else if (d.func == 0x04 && dm->p4 && i4 < n4) dm->p4[i4++] = d;
  }
  dm->n3 = i3;
  dm->n4 = i4;

  if (dm->n3) am_sort_by_start(dm->p3, dm->n3);
  if (dm->n4) am_sort_by_start(dm->p4, dm->n4);

  dm->next = _dm_head;
  _dm_head = dm;

  // rebuild devices bound to this datamap
  for (Device* d = _dev_head; d; d = d->next) {
    if (d->datamap_no == id) rebuildDeviceDecoded(d);
  }

  unlock();
  return true;
}

bool AsyncModbusRtu::getDataCopy(uint8_t addr, DataItem** items_out, uint16_t* count_out, uint32_t* ts_out) const {
  if (items_out) *items_out = nullptr;
  if (count_out) *count_out = 0;
  if (ts_out) *ts_out = 0;

  lock();
  Device* d = _dev_head;
  while (d) {
    if (d->addr == addr) break;
    d = d->next;
  }
  if (!d || !d->dm_data) { unlock(); return false; }

  uint16_t total = (uint16_t)(d->dm_n3 + d->dm_n4);
  uint16_t have = 0;
  for (uint16_t i = 0; i < total; i++) {
    if (d->dm_data[i].has_value) have++;
  }
  if (have == 0) { unlock(); return false; }

  DataItem* out = (DataItem*)std::malloc((size_t)have * sizeof(DataItem));
  if (!out) { unlock(); return false; }
  std::memset(out, 0, (size_t)have * sizeof(DataItem));

  uint16_t w = 0;
  for (uint16_t i = 0; i < total; i++) {
    const DecodedEntry& e = d->dm_data[i];
    if (!e.has_value) continue;

    DataItem& it = out[w++];
    it.func = e.def.func;
    it.start = e.def.start;
    it.unit_id = e.def.unit_id;

    if (e.def.type == DM_T_STRING || e.def.type == DM_T_TIME6) {
      it.raw_kind = VK_STR;
      it.raw_s = am_strdup_c(e.raw_hex);
      it.raw_i = 0;
    } else {
      it.raw_kind = VK_INT;
      it.raw_i = e.raw_i;
      it.raw_s = nullptr;
    }

    it.fmt_kind = e.fmt_kind;
    it.fmt_i = e.fmt_i;
    it.fmt_r = e.fmt_r;
    it.fmt_s = (e.fmt_kind == VK_STR) ? am_strdup_c(e.fmt_s) : nullptr;
  }

  if (items_out) *items_out = out;
  if (count_out) *count_out = have;
  if (ts_out) *ts_out = d->data_ts;

  unlock();
  return true;
}

void AsyncModbusRtu::freeDataCopy(DataItem* items, uint16_t count) {
  if (!items) return;
  for (uint16_t i = 0; i < count; i++) {
    if (items[i].raw_kind == VK_STR && items[i].raw_s) std::free(items[i].raw_s);
    if (items[i].fmt_kind == VK_STR && items[i].fmt_s) std::free(items[i].fmt_s);
  }
  std::free(items);
}

void AsyncModbusRtu::taskThunk(void* pv) {
#ifdef ESP32
  AsyncModbusRtu* self = (AsyncModbusRtu*)pv;
  if (self) self->taskLoop();
  vTaskDelete(nullptr);
#else
  (void)pv;
#endif
}

void AsyncModbusRtu::taskLoop() {
#ifdef ESP32
  for (;;) {
    if (_task_stop) return;

    if (!_running) {
      vTaskDelay(pdMS_TO_TICKS(25));
      continue;
    }

    lock();

    if (_state == WAIT_RX) {
      rxPump();
      if (_expected_len > 0 && _rx_len >= _expected_len) {
        handleFrame();
        unlock();
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
      if (timeReached(_deadline)) {
        handleTimeout();
        unlock();
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
      unlock();
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (_state == SILENT) {
      if (timeReached(_deadline)) _state = IDLE;
      else {
        uint32_t now = millis();
        uint32_t left = (uint32_t)((int32_t)(_deadline - now) > 0 ? (_deadline - now) : 0);
        unlock();
        vTaskDelay(pdMS_TO_TICKS(left > 25 ? 25 : left));
        continue;
      }
    }

    if (_state != IDLE) {
      unlock();
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    WriteReq wtmp;
    std::memset(&wtmp, 0, sizeof(wtmp));
    if (wqPop(wtmp)) {
      *_cur_write = wtmp;
      sendWrite(*_cur_write);
      unlock();
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    Device* d = selectNextDevice();
    if (!d) {
      unlock();
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    uint32_t now = millis();

    if (d->ir_head == nullptr && d->hr_head != nullptr) {
      if (!timeReached(d->next_hr_ms)) {
        uint32_t left = d->next_hr_ms - now;
        unlock();
        vTaskDelay(pdMS_TO_TICKS(left > 25 ? 25 : left));
        continue;
      }
      if (!d->hr_cur) d->hr_cur = d->hr_head;
      sendRead(d, d->hr_cur, true);
      d->hr_cur = d->hr_cur->next ? d->hr_cur->next : d->hr_head;
      unlock();
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (d->hr_head != nullptr && d->hr_round) {
      if (!timeReached(d->next_hr_ms)) {
        uint32_t left = d->next_hr_ms - now;
        unlock();
        vTaskDelay(pdMS_TO_TICKS(left > 25 ? 25 : left));
        continue;
      }
      if (!d->hr_cur) d->hr_cur = d->hr_head;
      sendRead(d, d->hr_cur, true);
      d->hr_cur = d->hr_cur->next ? d->hr_cur->next : d->hr_head;

      if (d->hr_cur == d->hr_head) {
        d->hr_round = false;
        d->hr_state = 0;
      }
      unlock();
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (d->ir_head == nullptr) {
      unlock();
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (!timeReached(d->next_ir_ms)) {
      uint32_t left = d->next_ir_ms - now;
      unlock();
      vTaskDelay(pdMS_TO_TICKS(left > 25 ? 25 : left));
      continue;
    }

    if (!d->ir_cur) d->ir_cur = d->ir_head;
    sendRead(d, d->ir_cur, false);
    d->ir_cur = d->ir_cur->next ? d->ir_cur->next : d->ir_head;

    if (d->ir_cur == d->ir_head) {
      d->hr_state++;
      if (d->hr_head != nullptr && d->hr_state >= d->hr_divider) d->hr_round = true;
    }

    unlock();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
#endif
}

bool AsyncModbusRtu::timeReached(uint32_t deadline) {
  return (int32_t)(millis() - deadline) >= 0;
}

uint16_t AsyncModbusRtu::crc16(const uint8_t* buf, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}

uint16_t AsyncModbusRtu::fp16(const uint8_t* buf, uint16_t len) {
  uint16_t fpv = 0;
  for (uint16_t i = 0; i < len; i++) {
    fpv = (uint16_t)((fpv + (uint16_t)(buf[i] * 131u)) & 0xFFFFu);
  }
  return fpv;
}

uint64_t AsyncModbusRtu::key64(uint8_t addr, uint8_t func, uint16_t start, uint16_t count) {
  return ((uint64_t)addr  << 40) |
         ((uint64_t)func  << 32) |
         ((uint64_t)start << 16) |
         (uint64_t)count;
}

void AsyncModbusRtu::lock() const {
#ifdef ESP32
  if (_mtx) xSemaphoreTake(_mtx, portMAX_DELAY);
#endif
}

void AsyncModbusRtu::unlock() const {
#ifdef ESP32
  if (_mtx) xSemaphoreGive(_mtx);
#endif
}

void AsyncModbusRtu::rxReset() {
  _rx_len = 0;
  _expected_len = 0;
}

void AsyncModbusRtu::rxDrain() {
  if (!_t) return;
  while (_t->available() > 0) (void)_t->read();
}

void AsyncModbusRtu::rxPump() {
  if (!_t) return;

  uint8_t expect_addr = 0;
  if (_cur_is_write && _cur_write) expect_addr = _cur_write->addr;
  else if (_cur_dev) expect_addr = _cur_dev->addr;

  int avail = _t->available();
  while (avail-- > 0 && _rx_len < sizeof(_rx_buf)) {
    int c = _t->read();
    if (c < 0) break;
    uint8_t b = (uint8_t)c;

    if (_rx_len == 0 && expect_addr && b != expect_addr) continue;

    _rx_buf[_rx_len++] = b;

    if (_expected_len == 0 && _rx_len >= 2) {
      uint8_t func = _rx_buf[1];
      if (func & 0x80) _expected_len = 5;
      else if (func == 0x06 || func == 0x10) _expected_len = 8;
    }

    if (_expected_len == 0 && _rx_len >= 3) {
      uint8_t func = _rx_buf[1];
      if (func == 0x03 || func == 0x04) {
        uint16_t el = (uint16_t)(3u + (uint16_t)_rx_buf[2] + 2u);
        if (el <= sizeof(_rx_buf)) _expected_len = el;
        else rxReset();
      }
    }
  }
}

void AsyncModbusRtu::enterSilent() {
  if (_silent_ms > 0) {
    _state = SILENT;
    _deadline = millis() + _silent_ms;
  } else {
    _state = IDLE;
  }
}

AsyncModbusRtu::Device* AsyncModbusRtu::getOrAddDevice(uint8_t addr) {
  Device* d = _dev_head;
  while (d) {
    if (d->addr == addr) return d;
    d = d->next;
  }

  d = (Device*)std::malloc(sizeof(Device));
  if (!d) return nullptr;
  std::memset(d, 0, sizeof(Device));

  d->addr = addr;
  d->enabled = true;
  d->timeout_ms = 250;
  d->retry = 0;
  d->ir_interval_ms = 0;
  d->hr_interval_ms = 0;
  d->hr_divider = 3;

  d->hr_state = 0;
  d->hr_round = false;

  d->ir_head = d->hr_head = nullptr;
  d->ir_cur  = d->hr_cur  = nullptr;

  d->next_ir_ms = 0;
  d->next_hr_ms = 0;

  d->datamap_no = 0;
  d->data_ts = 0;
  d->dm_n3 = d->dm_n4 = 0;
  d->dm_data = nullptr;

  d->next = _dev_head;
  _dev_head = d;
  return d;
}

void AsyncModbusRtu::freePatterns(PatternNode* p) {
  while (p) {
    PatternNode* n = p->next;
    std::free(p);
    p = n;
  }
}

void AsyncModbusRtu::appendPattern(PatternNode** head, PatternNode* p) {
  if (!(*head)) { *head = p; return; }
  PatternNode* t = *head;
  while (t->next) t = t->next;
  t->next = p;
}

AsyncModbusRtu::CacheEntry* AsyncModbusRtu::findOrCreateCache(uint8_t addr, uint8_t func, uint16_t start, uint16_t count) {
  uint64_t k = key64(addr, func, start, count);
  CacheEntry* e = _cache_head;
  while (e) {
    if (e->k == k) return e;
    e = e->next;
  }

  e = (CacheEntry*)std::malloc(sizeof(CacheEntry));
  if (!e) return nullptr;
  std::memset(e, 0, sizeof(CacheEntry));

  e->k = k;
  e->addr = addr;
  e->func = func;
  e->start = start;
  e->count = count;
  e->len = (uint16_t)(2u * count);
  e->data = (uint8_t*)std::malloc(e->len);
  if (e->data) std::memset(e->data, 0, e->len);
  e->valid = false;
  e->last_good_ms = 0;
  e->fp = 0;

  e->next = _cache_head;
  _cache_head = e;
  return e;
}

const AsyncModbusRtu::CacheEntry* AsyncModbusRtu::findCache(uint8_t addr, uint8_t func, uint16_t start, uint16_t count) const {
  uint64_t k = key64(addr, func, start, count);
  CacheEntry* e = _cache_head;
  while (e) {
    if (e->k == k) return e;
    e = e->next;
  }
  return nullptr;
}

bool AsyncModbusRtu::wqPush(uint8_t addr, uint8_t func, uint16_t start, uint16_t count, uint16_t* values) {
  if (!_wq) { if (values) std::free(values); return false; }
  if (_wq_len >= WQ_MAX) { if (values) std::free(values); return false; }

  WriteReq* w = &_wq[_wq_tail];
  w->used = true;
  w->addr = addr;
  w->func = func;
  w->start = start;
  w->count = count;
  w->values = values;

  _wq_tail = (uint8_t)((_wq_tail + 1) % WQ_MAX);
  _wq_len++;
  return true;
}

bool AsyncModbusRtu::wqPop(WriteReq& out) {
  if (!_wq || _wq_len == 0) return false;
  WriteReq* w = &_wq[_wq_head];
  out = *w;
  w->used = false;
  w->values = nullptr;
  _wq_head = (uint8_t)((_wq_head + 1) % WQ_MAX);
  _wq_len--;
  return true;
}

AsyncModbusRtu::Device* AsyncModbusRtu::selectNextDevice() {
  uint32_t now = millis();
  Device* best = nullptr;
  int32_t best_delta = 0x7FFFFFFF;

  for (Device* d = _dev_head; d; d = d->next) {
    if (!d->enabled) continue;
    if (!d->ir_head && !d->hr_head) continue;

    uint32_t due;
    if (!d->ir_head && d->hr_head) due = d->next_hr_ms;
    else if (d->hr_round && d->hr_head) due = d->next_hr_ms;
    else if (d->ir_head) due = d->next_ir_ms;
    else due = d->next_hr_ms;

    int32_t delta = (int32_t)(due - now);
    if (delta < best_delta) {
      best_delta = delta;
      best = d;
    }
  }
  return best;
}

void AsyncModbusRtu::updateNextDue(Device* d, bool is_hr, uint32_t interval_ms) {
  uint32_t now = millis();
  uint32_t iv = interval_ms;
  if (iv == 0) iv = is_hr ? d->hr_interval_ms : d->ir_interval_ms;
  if (iv == 0) iv = 200;
  if (is_hr) d->next_hr_ms = now + iv;
  else d->next_ir_ms = now + iv;
}

void AsyncModbusRtu::sendRead(Device* d, PatternNode* p, bool is_hr) {
  if (!_t || !d || !p) return;

  uint8_t tx[16];
  uint16_t n = 0;

  tx[n++] = d->addr;
  tx[n++] = p->func;
  tx[n++] = (uint8_t)(p->start >> 8);
  tx[n++] = (uint8_t)(p->start & 0xFF);
  tx[n++] = (uint8_t)(p->count >> 8);
  tx[n++] = (uint8_t)(p->count & 0xFF);

  uint16_t c = crc16(tx, n);
  tx[n++] = (uint8_t)(c & 0xFF);
  tx[n++] = (uint8_t)((c >> 8) & 0xFF);

  rxDrain();
  rxReset();

  (void)_t->write(tx, n);
  _t->flush();

  _cur_dev = d;
  _cur_is_write = false;
  _cur_is_hr = is_hr;
  _cur_func = p->func;
  _cur_start = p->start;
  _cur_count = p->count;
  _cur_retry_left = d->retry;

  _expected_len = 0;
  _deadline = millis() + d->timeout_ms;
  _state = WAIT_RX;

  updateNextDue(d, is_hr, p->interval_ms);
}

void AsyncModbusRtu::sendReadRetry(Device* d, uint8_t func, uint16_t start, uint16_t count) {
  if (!_t || !d) return;

  uint8_t tx[16];
  uint16_t n = 0;

  tx[n++] = d->addr;
  tx[n++] = func;
  tx[n++] = (uint8_t)(start >> 8);
  tx[n++] = (uint8_t)(start & 0xFF);
  tx[n++] = (uint8_t)(count >> 8);
  tx[n++] = (uint8_t)(count & 0xFF);

  uint16_t c = crc16(tx, n);
  tx[n++] = (uint8_t)(c & 0xFF);
  tx[n++] = (uint8_t)((c >> 8) & 0xFF);

  rxDrain();
  rxReset();

  (void)_t->write(tx, n);
  _t->flush();

  _cur_dev = d;
  _cur_is_write = false;
  _cur_func = func;
  _cur_start = start;
  _cur_count = count;

  _expected_len = 0;
  _deadline = millis() + d->timeout_ms;
  _state = WAIT_RX;
}

void AsyncModbusRtu::sendWrite(WriteReq& w) {
  if (!_t || !_cur_write) return;

  uint8_t tx[300];
  uint16_t n = 0;

  tx[n++] = w.addr;
  tx[n++] = w.func;
  tx[n++] = (uint8_t)(w.start >> 8);
  tx[n++] = (uint8_t)(w.start & 0xFF);

  if (w.func == 0x06) {
    uint16_t v = (w.values && w.count == 1) ? w.values[0] : 0;
    tx[n++] = (uint8_t)(v >> 8);
    tx[n++] = (uint8_t)(v & 0xFF);
  } else {
    uint16_t bc = (uint16_t)(2u * w.count);
    tx[n++] = (uint8_t)(w.count >> 8);
    tx[n++] = (uint8_t)(w.count & 0xFF);
    tx[n++] = (uint8_t)(bc & 0xFF);
    for (uint16_t i = 0; i < w.count; i++) {
      uint16_t v = w.values ? w.values[i] : 0;
      tx[n++] = (uint8_t)(v >> 8);
      tx[n++] = (uint8_t)(v & 0xFF);
    }
  }

  uint16_t c = crc16(tx, n);
  tx[n++] = (uint8_t)(c & 0xFF);
  tx[n++] = (uint8_t)((c >> 8) & 0xFF);

  rxDrain();
  rxReset();

  (void)_t->write(tx, n);
  _t->flush();

  _cur_is_write = true;
  _cur_dev = nullptr;

  _expected_len = 8;
  _deadline = millis() + 300;
  _state = WAIT_RX;
}

void AsyncModbusRtu::handleTimeout() {
  rxReset();

  if (_cur_is_write && _cur_write) {
    if (_cur_write->values) { std::free(_cur_write->values); _cur_write->values = nullptr; }
    _cur_is_write = false;
    _cur_dev = nullptr;
    enterSilent();
    return;
  }

  if (!_cur_is_write && _cur_dev && _cur_retry_left > 0) {
    _cur_retry_left--;
    sendReadRetry(_cur_dev, _cur_func, _cur_start, _cur_count);
    return;
  }

  _cur_dev = nullptr;
  _cur_is_write = false;
  enterSilent();
}

AsyncModbusRtu::DataMap* AsyncModbusRtu::findDatamap(uint8_t id) const {
  for (DataMap* dm = _dm_head; dm; dm = dm->next) {
    if (dm->id == id) return dm;
  }
  return nullptr;
}

void AsyncModbusRtu::freeDatamap(DataMap* dm) {
  if (!dm) return;
  if (dm->p3) std::free(dm->p3);
  if (dm->p4) std::free(dm->p4);
  std::free(dm);
}

void AsyncModbusRtu::clearAllDatamaps() {
  while (_dm_head) {
    DataMap* n = _dm_head->next;
    freeDatamap(_dm_head);
    _dm_head = n;
  }
}

void AsyncModbusRtu::freeDeviceDecoded(Device* d) {
  if (!d || !d->dm_data) return;
  uint16_t total = (uint16_t)(d->dm_n3 + d->dm_n4);
  for (uint16_t i = 0; i < total; i++) {
    if (d->dm_data[i].raw_hex) { std::free(d->dm_data[i].raw_hex); d->dm_data[i].raw_hex = nullptr; }
    if (d->dm_data[i].fmt_s)   { std::free(d->dm_data[i].fmt_s);   d->dm_data[i].fmt_s = nullptr; }
  }
  std::free(d->dm_data);
  d->dm_data = nullptr;
  d->dm_n3 = d->dm_n4 = 0;
  d->data_ts = 0;
}

void AsyncModbusRtu::rebuildDeviceDecoded(Device* d) {
  if (!d) return;

  freeDeviceDecoded(d);

  if (d->datamap_no == 0) return;

  DataMap* dm = findDatamap(d->datamap_no);
  if (!dm) return;

  uint16_t total = (uint16_t)(dm->n3 + dm->n4);
  if (!total) return;

  d->dm_data = (DecodedEntry*)std::malloc((size_t)total * sizeof(DecodedEntry));
  if (!d->dm_data) return;
  std::memset(d->dm_data, 0, (size_t)total * sizeof(DecodedEntry));

  d->dm_n3 = dm->n3;
  d->dm_n4 = dm->n4;

  for (uint16_t i = 0; i < dm->n3; i++) {
    d->dm_data[i].def = dm->p3[i];
    d->dm_data[i].has_value = false;
    d->dm_data[i].raw_i = 0;
    d->dm_data[i].raw_hex = nullptr;
    d->dm_data[i].raw_fp = 0;
    d->dm_data[i].fmt_kind = VK_NIL;
    d->dm_data[i].fmt_i = 0;
    d->dm_data[i].fmt_r = 0.0;
    d->dm_data[i].fmt_s = nullptr;
  }
  for (uint16_t i = 0; i < dm->n4; i++) {
    DecodedEntry& e = d->dm_data[dm->n3 + i];
    e.def = dm->p4[i];
    e.has_value = false;
    e.raw_i = 0;
    e.raw_hex = nullptr;
    e.raw_fp = 0;
    e.fmt_kind = VK_NIL;
    e.fmt_i = 0;
    e.fmt_r = 0.0;
    e.fmt_s = nullptr;
  }

  d->data_ts = 0;
}

void AsyncModbusRtu::processDecodedOnBlock(Device* d, const CacheEntry* ce, uint16_t blk_start, uint16_t blk_count) {
  if (!d || !ce) return;
  if (!d->dm_data) return;
  if (d->datamap_no == 0) return;

  DataMap* dm = findDatamap(d->datamap_no);
  if (!dm) return;

  const uint16_t blk_end = (uint16_t)(blk_start + blk_count);

  const DataPointDef* defs = nullptr;
  uint16_t n = 0;
  uint16_t base = 0;

  if (ce->func == 0x03) { defs = dm->p3; n = dm->n3; base = 0; }
  else if (ce->func == 0x04) { defs = dm->p4; n = dm->n4; base = dm->n3; }
  else return;

  if (!defs || n == 0) return;

  uint16_t idx = am_lower_bound_start(defs, n, blk_start);
  bool any_changed = false;

  // pow10 table (up to 9 decimals)
  static const double POW10[] = {1.0,10.0,100.0,1000.0,10000.0,100000.0,1000000.0,10000000.0,100000000.0,1000000000.0};

  while (idx < n) {
    const DataPointDef& dp = defs[idx];
    if (dp.start >= blk_end) break;

    uint16_t dp_end = (uint16_t)(dp.start + dp.count);
    if (dp.start < blk_start || dp_end > blk_end) {
      idx++;
      continue;
    }

    uint16_t byte_off = (uint16_t)((dp.start - blk_start) * 2u);
    uint16_t byte_len = (uint16_t)(dp.count * 2u);
    if (!ce->data || (uint32_t)byte_off + (uint32_t)byte_len > (uint32_t)ce->len) {
      idx++;
      continue;
    }

    DecodedEntry& out = d->dm_data[base + idx];

    const uint8_t* slice = ce->data + byte_off;

    // STRING/TIME6 raw change detection via fp16 on slice
    if (dp.type == DM_T_STRING || dp.type == DM_T_TIME6) {
      uint16_t new_fp = fp16(slice, byte_len);
      if (out.has_value && out.raw_fp == new_fp) {
        idx++;
        continue;
      }
      out.raw_fp = new_fp;

      // raw hex
      char* new_hex = am_to_hex(slice, byte_len);
      if (new_hex) {
        if (!out.raw_hex || std::strcmp(out.raw_hex, new_hex) != 0) {
          if (out.raw_hex) std::free(out.raw_hex);
          out.raw_hex = new_hex;
          any_changed = true;
        } else {
          std::free(new_hex);
        }
      }

      // formatted
      if (out.fmt_s) { std::free(out.fmt_s); out.fmt_s = nullptr; }
      out.fmt_kind = VK_NIL;
      out.fmt_i = 0;
      out.fmt_r = 0.0;

      if (dp.type == DM_T_STRING) {
        // build string from registers (after endian transforms)
        uint16_t maxc = (uint16_t)(dp.count * 2u);
        char* buf = (char*)std::malloc((size_t)maxc + 1u);
        if (!buf) {
          out.has_value = true;
          idx++;
          continue;
        }

        uint16_t wpos = 0;
        bool bad = false;
        bool nullterm = (dp.options & DM_SF_NULLTERM) != 0;
        bool trimtail = (dp.options & DM_SF_TRIM_TAIL) != 0;
        bool relax = (dp.options & DM_SF_ALLOW_NONPRINT) != 0;

        for (uint8_t r = 0; r < dp.count; r++) {
          uint16_t w = am_u16_be(slice + (uint16_t)r * 2u);
          if (dp.endian_flags & DM_EF_SWAP_BYTES16) w = am_swap16(w);

          uint8_t hi = (uint8_t)(w >> 8);
          uint8_t lo = (uint8_t)(w & 0xFF);

          uint8_t bytes2[2] = {hi, lo};

          for (uint8_t bi = 0; bi < 2; bi++) {
            uint8_t ch = bytes2[bi];

            if (ch == 0x00 && nullterm) {
              r = dp.count; // break outer
              break;
            }

            if (!relax) {
              if (ch < 0x20 || ch > 0x7E) { bad = true; break; }
              buf[wpos++] = (char)ch;
            } else {
              if (ch < 0x20 || ch > 0x7E) {
                if (ch == 0x00) { bad = true; break; } // avoid embedded NUL in C string
                buf[wpos++] = '?';
              } else {
                buf[wpos++] = (char)ch;
              }
            }

            if (wpos >= maxc) break;
          }

          if (bad) break;
        }

        if (bad) {
          std::free(buf);
        } else {
          buf[wpos] = 0;

          if (trimtail && wpos > 0) {
            while (wpos > 0) {
              char c = buf[wpos - 1];
              if (c == 0 || c == ' ') { buf[wpos - 1] = 0; wpos--; continue; }
              break;
            }
          }

          out.fmt_kind = VK_STR;
          out.fmt_s = buf;
          any_changed = true;
        }
      } else if (dp.type == DM_T_TIME6) {
        // time6 decode
        if (dp.count != 6) {
          out.has_value = true;
          idx++;
          continue;
        }

        uint16_t regs[6];
        for (uint8_t r = 0; r < 6; r++) {
          uint16_t w = am_u16_be(slice + (uint16_t)r * 2u);
          if (dp.endian_flags & DM_EF_SWAP_BYTES16) w = am_swap16(w);
          regs[r] = w;
        }

        uint16_t year = regs[0];
        if (dp.options & DM_TF_BCD_YEAR) year = am_bcd4_u16(year);

        bool use_hi = (dp.options & DM_TF_USE_HI_BYTE_FIELDS) != 0;
        bool bcd = (dp.options & DM_TF_BCD_FIELDS) != 0;

        uint8_t m = (uint8_t)(use_hi ? (regs[1] >> 8) : (regs[1] & 0xFF));
        uint8_t d0 = (uint8_t)(use_hi ? (regs[2] >> 8) : (regs[2] & 0xFF));
        uint8_t h = (uint8_t)(use_hi ? (regs[3] >> 8) : (regs[3] & 0xFF));
        uint8_t mi = (uint8_t)(use_hi ? (regs[4] >> 8) : (regs[4] & 0xFF));
        uint8_t s = (uint8_t)(use_hi ? (regs[5] >> 8) : (regs[5] & 0xFF));

        if (bcd) {
          m  = (uint8_t)am_bcd2(m);
          d0 = (uint8_t)am_bcd2(d0);
          h  = (uint8_t)am_bcd2(h);
          mi = (uint8_t)am_bcd2(mi);
          s  = (uint8_t)am_bcd2(s);
        }

        if (m < 1 || m > 12 || d0 < 1 || d0 > 31 || h > 23 || mi > 59 || s > 59) {
          out.has_value = true;
          idx++;
          continue;
        }

        char tmp[24];
        std::snprintf(tmp, sizeof(tmp), "%04u-%02u-%02u %02u:%02u:%02u",
                      (unsigned)year, (unsigned)m, (unsigned)d0,
                      (unsigned)h, (unsigned)mi, (unsigned)s);

        out.fmt_kind = VK_STR;
        out.fmt_s = am_strdup_c(tmp);
        if (out.fmt_s) any_changed = true;
      }

      out.has_value = true;
      idx++;
      continue;
    }

    // ---- Numeric types ----
    uint16_t w0 = am_u16_be(slice);
    if (dp.endian_flags & DM_EF_SWAP_BYTES16) w0 = am_swap16(w0);

    int64_t raw = 0;

    if (dp.type == DM_T_INT16) {
      raw = (int64_t)(int16_t)w0;
    } else if (dp.type == DM_T_UINT16) {
      raw = (int64_t)w0;
    } else if (dp.type == DM_T_INT32 || dp.type == DM_T_UINT32) {
      uint16_t w1 = am_u16_be(slice + 2);
      if (dp.endian_flags & DM_EF_SWAP_BYTES16) w1 = am_swap16(w1);

      uint16_t hi = w0;
      uint16_t lo = w1;
      if (dp.endian_flags & DM_EF_SWAP_WORDS32) {
        hi = w1;
        lo = w0;
      }

      uint32_t u32 = ((uint32_t)hi << 16) | (uint32_t)lo;
      if (dp.type == DM_T_INT32) raw = (int64_t)(int32_t)u32;
      else raw = (int64_t)u32;
    } else {
      idx++;
      continue;
    }

    if (!out.has_value || out.raw_i != raw) {
      out.raw_i = raw;
      out.has_value = true;

      if (out.fmt_s) { std::free(out.fmt_s); out.fmt_s = nullptr; }
      out.fmt_kind = VK_NIL;

      uint8_t dec = dp.decimals;
      if (dec == 0) {
        out.fmt_kind = VK_INT;
        out.fmt_i = raw;
        out.fmt_r = 0.0;
      } else {
        if (dec > 9) dec = 9;
        out.fmt_kind = VK_REAL;
        out.fmt_r = (double)raw / POW10[dec];
        out.fmt_i = 0;
      }

      any_changed = true;
    }

    idx++;
  }

  if (any_changed) {
    d->data_ts = millis();
  }
}

void AsyncModbusRtu::handleFrame() {
  if (_expected_len == 0 || _rx_len < _expected_len) return;
  if (_rx_len > _expected_len) _rx_len = _expected_len;
  if (_rx_len < 5) { rxReset(); enterSilent(); return; }

  uint16_t crc_rx = (uint16_t)_rx_buf[_rx_len - 2] | ((uint16_t)_rx_buf[_rx_len - 1] << 8);
  uint16_t crc_calc = crc16(_rx_buf, _rx_len - 2);
  if (crc_rx != crc_calc) { rxReset(); enterSilent(); return; }

  uint8_t addr = _rx_buf[0];
  uint8_t func = _rx_buf[1];

  if (_cur_is_write && _cur_write) {
    if (addr != _cur_write->addr || func != _cur_write->func) { rxReset(); enterSilent(); return; }
    if (_cur_write->values) { std::free(_cur_write->values); _cur_write->values = nullptr; }
    rxReset();
    enterSilent();
    return;
  }

  if (!_cur_dev || addr != _cur_dev->addr) { rxReset(); enterSilent(); return; }
  if (func & 0x80) { rxReset(); enterSilent(); return; }
  if (func != _cur_func) { rxReset(); enterSilent(); return; }

  if (func == 0x03 || func == 0x04) {
    uint8_t byte_count = _rx_buf[2];
    uint16_t expected_bc = (uint16_t)(2u * _cur_count);
    if (byte_count != expected_bc) { rxReset(); enterSilent(); return; }

    uint16_t frame_len = (uint16_t)(3u + byte_count + 2u);
    if (_rx_len != frame_len) { rxReset(); enterSilent(); return; }

    CacheEntry* ce = findOrCreateCache(addr, func, _cur_start, _cur_count);
    if (!ce || !ce->data || ce->len != byte_count) { rxReset(); enterSilent(); return; }

    const uint8_t* payload = _rx_buf + 3;
    uint16_t new_fp = fp16(payload, ce->len);

    bool changed = (!ce->valid || new_fp != ce->fp || std::memcmp(ce->data, payload, ce->len) != 0);
    if (changed) {
      std::memcpy(ce->data, payload, ce->len);
      ce->fp = new_fp;

      // datamap decode on changed block
      processDecodedOnBlock(_cur_dev, ce, _cur_start, _cur_count);
    }

    ce->valid = true;
    ce->last_good_ms = millis();
  }

  rxReset();
  enterSilent();
}

void AsyncModbusRtu::clearAll() {
  while (_wq_len > 0) {
    WriteReq w;
    if (!wqPop(w)) break;
    if (w.values) std::free(w.values);
  }

  if (_cur_write && _cur_write->values) {
    std::free(_cur_write->values);
    _cur_write->values = nullptr;
  }

  while (_dev_head) {
    Device* n = _dev_head->next;
    freePatterns(_dev_head->ir_head);
    freePatterns(_dev_head->hr_head);
    freeDeviceDecoded(_dev_head);
    std::free(_dev_head);
    _dev_head = n;
  }

  while (_cache_head) {
    CacheEntry* n = _cache_head->next;
    if (_cache_head->data) std::free(_cache_head->data);
    std::free(_cache_head);
    _cache_head = n;
  }

  _cur_dev = nullptr;
  _cur_is_write = false;
  _state = IDLE;
  rxReset();
}