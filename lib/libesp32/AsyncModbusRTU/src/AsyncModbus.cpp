#include "AsyncModbus.h"
#include <cstring> 
#include <cstdlib>

struct AsyncModbusRtu::PatternNode {
  uint8_t func;
  uint16_t start;
  uint16_t count;
  uint32_t interval_ms;
  PatternNode* next;
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

static uint16_t* am_new_u16(size_t n) {
  if (n == 0) return nullptr;
  void* p = std::malloc(n * sizeof(uint16_t));
  return (uint16_t*)p;
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
                                  uint32_t ir_iv_ms, uint32_t hr_iv_ms, uint8_t hr_div) {
  lock();
  Device* d = getOrAddDevice(addr);
  if (d) {
    d->timeout_ms = timeout_ms;
    d->retry = retry;
    d->ir_interval_ms = ir_iv_ms;
    d->hr_interval_ms = hr_iv_ms;
    d->hr_divider = (hr_div == 0 ? 1 : hr_div);
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
  int32_t best_delta = 0x7FFFFFFF;  // smallest (most negative) wins = most overdue

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

    uint16_t new_fp = fp16(_rx_buf + 3, ce->len);
    if (!ce->valid || new_fp != ce->fp || std::memcmp(ce->data, _rx_buf + 3, ce->len) != 0) {
      std::memcpy(ce->data, _rx_buf + 3, ce->len);
      ce->fp = new_fp;
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