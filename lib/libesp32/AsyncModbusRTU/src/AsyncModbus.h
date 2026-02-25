//
// AsyncModbus.h by HexaMaster
//

#pragma once

#include <Arduino.h>

#ifdef ESP32
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <freertos/semphr.h>
#endif

#ifndef ASYNCMODBUS_TASK_STACK
  #define ASYNCMODBUS_TASK_STACK 6144
#endif

#ifndef ASYNCMODBUS_TASK_PRIO
  #define ASYNCMODBUS_TASK_PRIO (tskIDLE_PRIORITY + 1)
#endif

class AsyncModbusRtu {
public:
  struct Transport {
    virtual ~Transport() {}
    virtual bool begin(long baud, uint32_t config) = 0;
    virtual int  available() = 0;
    virtual int  read() = 0;
    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual void flush() = 0;
    virtual void setTxEnablePin(int pin) { (void)pin; }
    virtual void setInverted(bool inv) { (void)inv; }
  };

  // ---- Datamap definitions (C-side) ----
  // Endian flags (B1)
  static constexpr uint8_t DM_EF_SWAP_BYTES16 = 0x01;
  static constexpr uint8_t DM_EF_SWAP_WORDS32 = 0x02;

  // Option flags (B7) - STRING (type=0x05)
  static constexpr uint8_t DM_SF_NULLTERM        = 0x01;
  static constexpr uint8_t DM_SF_TRIM_TAIL       = 0x02;
  static constexpr uint8_t DM_SF_ALLOW_NONPRINT  = 0x04;

  // Option flags (B7) - TIME6 (type=0x06)
  static constexpr uint8_t DM_TF_USE_HI_BYTE_FIELDS = 0x10;
  static constexpr uint8_t DM_TF_BCD_FIELDS         = 0x20;
  static constexpr uint8_t DM_TF_BCD_YEAR           = 0x40;

  // Type enum (B5)
  static constexpr uint8_t DM_T_INT16  = 0x01;  // count=1
  static constexpr uint8_t DM_T_UINT16 = 0x02;  // count=1
  static constexpr uint8_t DM_T_INT32  = 0x03;  // count=2
  static constexpr uint8_t DM_T_UINT32 = 0x04;  // count=2
  static constexpr uint8_t DM_T_STRING = 0x05;  // count>=1
  static constexpr uint8_t DM_T_TIME6  = 0x06;  // count=6

  struct DataPointDef {
    uint8_t  func;         // B0
    uint8_t  endian_flags; // B1
    uint16_t start;        // B2..B3
    uint8_t  count;        // B4
    uint8_t  type;         // B5
    uint8_t  decimals;     // B6
    uint8_t  options;      // B7
    uint8_t  unit_id;      // B8 (or 0 if not provided)
  };

  enum ValueKind : uint8_t {
    VK_NIL  = 0,
    VK_INT  = 1,
    VK_REAL = 2,
    VK_STR  = 3
  };

  struct DataItem {
    uint8_t  func;
    uint16_t start;
    uint8_t  unit_id;

    ValueKind raw_kind;
    int64_t   raw_i;
    char*     raw_s;     // HEX string for STRING/TIME6, nullptr otherwise

    ValueKind fmt_kind;
    int64_t   fmt_i;
    double    fmt_r;
    char*     fmt_s;     // string for STRING/TIME6, nullptr otherwise
  };

  explicit AsyncModbusRtu(Transport* t, bool take_ownership = false);
  ~AsyncModbusRtu();

  bool begin(long baud, uint32_t config, bool inverted = false, int tx_en_pin = -1);
  void start();
  void stop();

  void setSilentGapMs(uint32_t ms);
  uint32_t silentGapMs() const;

  // datamap_no: 0 = disabled (raw only)
  void configDevice(uint8_t addr, uint32_t timeout_ms = 250, uint8_t retry = 0,
                    uint32_t ir_iv_ms = 0, uint32_t hr_iv_ms = 0, uint8_t hr_div = 3,
                    uint8_t datamap_no = 0);

  void enableDevice(uint8_t addr, bool en);
  void clearPatterns(uint8_t addr);
  void addPattern(uint8_t addr, bool is_hr, uint8_t func, uint16_t start, uint16_t count, uint32_t interval_ms = 0);

  bool getBlockCopy(uint8_t addr, uint8_t func, uint16_t start, uint16_t count,
                    uint8_t** data_out, uint16_t* len_out, uint32_t* last_good_ms_out) const;

  bool writeReg(uint8_t addr, uint16_t reg, uint16_t value);
  bool writeRegsFromBytes(uint8_t addr, uint16_t start, const uint8_t* payload, uint16_t payload_len);

  // ---- Datamap API ----
  // Adds/replaces datamap id. defs are already parsed (8B/9B resolved to unit_id).
  // Returns true if stored.
  bool addDatamap(uint8_t id, const DataPointDef* defs, uint16_t count);

  // Optional helper: change device datamap after it exists.
  void setDeviceDatamap(uint8_t addr, uint8_t datamap_no);

  // Copies decoded data snapshot for device (caller frees via freeDataCopy).
  // Returns false if no device or no decoded data.
  bool getDataCopy(uint8_t addr, DataItem** items_out, uint16_t* count_out, uint32_t* ts_out) const;
  static void freeDataCopy(DataItem* items, uint16_t count);

private:
  AsyncModbusRtu(const AsyncModbusRtu&) = delete;
  AsyncModbusRtu& operator=(const AsyncModbusRtu&) = delete;

  struct PatternNode;
  struct Device;
  struct CacheEntry;
  struct WriteReq;

  struct DataMap;       // internal
  struct DecodedEntry;  // internal

  enum State : uint8_t { IDLE = 0, WAIT_RX = 1, SILENT = 2 };

  static void taskThunk(void* pv);
  void taskLoop();

  static inline bool timeReached(uint32_t deadline);
  static uint16_t crc16(const uint8_t* buf, size_t len);
  static uint16_t fp16(const uint8_t* buf, uint16_t len);
  static uint64_t key64(uint8_t addr, uint8_t func, uint16_t start, uint16_t count);

  void lock() const;
  void unlock() const;

  void rxReset();
  void rxDrain();
  void rxPump();

  void enterSilent();

  Device* getOrAddDevice(uint8_t addr);
  void freePatterns(PatternNode* p);
  void appendPattern(PatternNode** head, PatternNode* p);

  CacheEntry* findOrCreateCache(uint8_t addr, uint8_t func, uint16_t start, uint16_t count);
  const CacheEntry* findCache(uint8_t addr, uint8_t func, uint16_t start, uint16_t count) const;

  bool wqPush(uint8_t addr, uint8_t func, uint16_t start, uint16_t count, uint16_t* values);
  bool wqPop(WriteReq& out);

  Device* selectNextDevice();

  void updateNextDue(Device* d, bool is_hr, uint32_t interval_ms);

  void sendRead(Device* d, PatternNode* p, bool is_hr);
  void sendReadRetry(Device* d, uint8_t func, uint16_t start, uint16_t count);
  void sendWrite(WriteReq& w);

  void handleFrame();
  void handleTimeout();

  // ---- Datamap internals ----
  DataMap* findDatamap(uint8_t id) const;
  void freeDatamap(DataMap* dm);
  void clearAllDatamaps();

  void rebuildDeviceDecoded(Device* d);
  void freeDeviceDecoded(Device* d);

  void processDecodedOnBlock(Device* d, const CacheEntry* ce, uint16_t blk_start, uint16_t blk_count);

  void clearAll();

private:
  Transport* _t = nullptr;
  bool _own_t = false;

  mutable SemaphoreHandle_t _mtx = nullptr;
  TaskHandle_t _task = nullptr;
  volatile bool _task_stop = false;

  volatile bool _running = false;

  uint32_t _silent_ms = 4;

  volatile State _state = IDLE;
  uint16_t _expected_len = 0;
  uint16_t _rx_len = 0;
  uint8_t  _rx_buf[300];
  uint32_t _deadline = 0;

  Device* _cur_dev = nullptr;
  bool _cur_is_write = false;
  bool _cur_is_hr = false;
  uint8_t _cur_func = 0;
  uint16_t _cur_start = 0;
  uint16_t _cur_count = 0;
  uint8_t _cur_retry_left = 0;

  static constexpr uint8_t WQ_MAX = 8;
  WriteReq* _wq = nullptr;
  uint8_t _wq_head = 0;
  uint8_t _wq_tail = 0;
  uint8_t _wq_len = 0;

  WriteReq* _cur_write = nullptr;

  Device* _dev_head = nullptr;
  CacheEntry* _cache_head = nullptr;

  // Datamaps
  DataMap* _dm_head = nullptr;
};