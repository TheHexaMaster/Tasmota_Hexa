/*
  support_pwm.ino - command support for Tasmota

  Copyright (C) 2021  Theo Arends & Stephan Hadinger

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

// This is from Arduino code -- not sure why it is necessary
//Use XTAL clock if possible to avoid timer frequency error when setting APB clock < 80 Mhz
//Need to be fixed in ESP-IDF

#include "driver/ledc.h"

#define LEDC_DEFAULT_CLK        LEDC_USE_XTAL_CLK

#if (ESP_IDF_VERSION_MAJOR >= 5)
  #define LEDC_MAX_BIT_WIDTH      SOC_LEDC_TIMER_BIT_WIDTH
#else
  #define LEDC_MAX_BIT_WIDTH      SOC_LEDC_TIMER_BIT_WIDE_NUM
#endif

// define our limits to ease any change from esp-idf
#define MAX_TIMERS              LEDC_TIMER_MAX            // 4 timers for all ESP32 variants
#ifdef SOC_LEDC_SUPPORT_HS_MODE
  #define PWM_HAS_HIGHSPEED  SOC_LEDC_SUPPORT_HS_MODE     // are there 2 banks of timers/ledc
#endif


// current configuration of timers: frequency and resolution

static uint32_t  timer_freq_hz[MAX_TIMERS] = {0};
static uint8_t   timer_duty_resolution[MAX_TIMERS] = {0};

// channel mapping
static int8_t pin_to_channel[SOC_GPIO_PIN_COUNT] = { 0 }; // contains the channel assigned to each pin, 0 means unassigned, substract 1
static uint8_t  pwm_timer[MAX_PWMS] = {0};          // contains the timer assigned to each channel

static const uint32_t pwm_def_frequency = 977;      // Default 977Hz
static const ledc_timer_bit_t  pwm_def_bit_num = LEDC_TIMER_10_BIT;         // Default 1023
static bool     pwm_impl_inited = false;  // trigger initialization

/*********************************************************************************************\
 * ESP32 analogWrite emulation support
\*********************************************************************************************/

void _analog_applyTimerConfig(int32_t timer) {
  esp_err_t ret;
  if (timer < 0 || timer >= MAX_TIMERS) { return; }   // avoid overflow or underflow

  // AddLog(LOG_LEVEL_INFO, "PWM: ledc_timer_config(res=%i timer=%i freq=%i)", timer_duty_resolution[timer], timer, timer_freq_hz[timer]);
  // we apply configuration to timer
  ledc_timer_config_t cfg = {
    (ledc_mode_t) 0,                                        // speed mode - first bank
    (ledc_timer_bit_t) timer_duty_resolution[timer],   // duty_resolution
    (ledc_timer_t) timer,                                   // timer_num
    timer_freq_hz[timer],                              // freq_hz
    LEDC_DEFAULT_CLK                                        // clk_cfg
  };
  ret = ledc_timer_config(&cfg);
  if (ret != ESP_OK) {
    AddLog(LOG_LEVEL_ERROR, "PWM: ledc_timer_config %i failed ret=%i", timer, ret);
  }
#ifdef PWM_HAS_HIGHSPEED
  cfg.speed_mode = (ledc_mode_t) 1;         // first bank
  ret = ledc_timer_config(&cfg);
  if (ret != ESP_OK) {
    AddLog(LOG_LEVEL_ERROR, "PWM: ledc_timer_config %i failed ret=%i", timer + MAX_TIMERS, ret);
  }
#endif
}

void _analogInit(void) {
  if (pwm_impl_inited) { return; }

  for (uint32_t i = 0; i < MAX_TIMERS; i++) {
    timer_freq_hz[i] = pwm_def_frequency;
    timer_duty_resolution[i] = pwm_def_bit_num;
    _analog_applyTimerConfig(i); 
  }
  pwm_impl_inited = true;
}

void ledcSetTimer(uint8_t chan, uint8_t timer) {
  if (timer >= MAX_TIMERS || chan > MAX_PWMS) { return; }
  uint8_t cur_timer = pwm_timer[chan];

  if (timer != cur_timer) { 
    pwm_timer[chan] = timer;  
    // apply to hardware
    uint8_t group=(chan/8);
    uint8_t channel=(chan%8);
    esp_err_t ret = ledc_bind_channel_timer((ledc_mode_t) group, (ledc_channel_t) channel, (ledc_timer_t) timer);
    if (ret != ESP_OK) {
      AddLog(LOG_LEVEL_ERROR, "PWM: ledc_bind_channel_timer %i failed ret=%i", timer, ret);
    }
  }
}

int32_t analogGetChannel2(uint32_t pin) {    // returns -1 if uallocated
  if (pin >= SOC_GPIO_PIN_COUNT) { return -1; }
  return pin_to_channel[pin] - 1;
}

int32_t _analog_pin2timer(uint32_t pin) {    // returns -1 if uallocated
  _analogInit();      // make sure the mapping array is initialized
  int chan = analogGetChannel2(pin);
  if (chan < 0) { return -1; }
  int32_t timer = pwm_timer[chan];
  if (timer > MAX_TIMERS) { timer = 0; }
  return timer;
}

static int32_t analogNextFreeTimer() {
  _analogInit();  
  bool assigned[MAX_TIMERS] = {};
  assigned[0] = true;

  for (uint32_t chan = 0; chan < MAX_PWMS; chan++) {
    assigned[pwm_timer[chan]] = true;
  }

  for (uint32_t j = 0; j < MAX_TIMERS; j++) {
    if (!assigned[j]) {
      return j;
      }
  }
  return -1;    // none available
}

uint32_t _analogGetResolution(uint32_t x) {
  uint32_t bits = 0;
  while (x) {
    bits++;
    x >>= 1;
  }
  return bits;
}

void analogWriteRange(uint32_t range, int32_t pin) {
  _analogInit();
  int32_t timer = (pin < 0) ? 0 : _analog_pin2timer(pin);
  if (timer < 0) { return; }

  uint32_t pwm_bit_num = _analogGetResolution(range);
  if (pwm_bit_num > LEDC_MAX_BIT_WIDTH || pwm_bit_num == 0) {
    AddLog(LOG_LEVEL_ERROR, "PWM: range is invalid: %i", range);
    return;
  }
  timer_duty_resolution[timer] = (ledc_timer_bit_t) pwm_bit_num;
  _analog_applyTimerConfig(timer);
}

void analogWriteFreqRange(int32_t freq, int32_t range, int32_t pin) {
  _analogInit();
  uint32_t timer0_freq = timer_freq_hz[0]; 
  uint8_t  timer0_res = timer_duty_resolution[0];

  int32_t timer = 0;
  int32_t res = timer0_res;
  if (pin < 0) {
    if (freq <= 0) { freq = timer0_freq; }
    if (range > 0) {
      res = _analogGetResolution(range);
      if (res >= LEDC_TIMER_BIT_MAX) { return; }
    }
  } else {
    int32_t chan = analogGetChannel2(pin);
    if (chan < 0) { return; }
    timer = pwm_timer[chan];
    if (freq < 0) { freq = timer_freq_hz[timer]; }
    if (freq == 0) { freq = timer0_freq; }

    res = timer0_res;
    if (range < 0) { res = timer_duty_resolution[timer]; }
    if (range != 0) { res = _analogGetResolution(range); }
    if (res >= LEDC_TIMER_BIT_MAX) { return; }

    if (freq == timer0_freq && res == timer0_res) {
      // settings match with the global value
      if (timer != 0) {
        ledcSetTimer(chan, 0);
        timer = 0;
      }
      // else nothing to change
    } else {
      // specific (non-global) values, require a specific timer
      if (timer == 0) {   // currently using the global timer, need to change
        // we need to allocate a new timer to this pin
        int32_t next_timer = analogNextFreeTimer();
        if (next_timer < 0) {
          AddLog(LOG_LEVEL_ERROR, "PWM: failed to assign a timer to GPIO %i", pin);
        } else {
          ledcSetTimer(chan, next_timer);
          timer = next_timer;
        }
      }
    }
    pwm_timer[chan] = timer;
  }

  if (timer_freq_hz[timer] != freq || timer_duty_resolution[timer] != res) {
    timer_freq_hz[timer] = freq;
    timer_duty_resolution[timer] = res;
    _analog_applyTimerConfig(timer);
  }
}

void analogWriteFreq(uint32_t freq, int32_t pin) {
  analogWriteFreqRange(freq, 0, pin);
}

static int32_t findEmptyChannel() {
  bool chan_used[MAX_PWMS] = {0};
  for (uint32_t pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++) {
    if (pin_to_channel[pin] > 0) {
      chan_used[pin_to_channel[pin] - 1] = true;
    }
  }

  // find empty slot
  for (uint32_t chan = 0; chan < MAX_PWMS; chan++) {
    if (!chan_used[chan]) {
      return chan;
    }
  }
  return -1;
}

int32_t analogAttach(uint32_t pin, bool output_invert) {    // returns ledc channel used, or -1 if failed
  _analogInit();      // make sure the mapping array is initialized
  // Find if pin is already attached
  int32_t chan = analogGetChannel2(pin);
  if (chan >= 0) { return chan; }
  // Find an empty channel
  chan = findEmptyChannel();
  if (chan < 0) {
    AddLog(LOG_LEVEL_INFO, "PWM: no more PWM (ledc) channel for GPIO %i", pin);
    return -1;
  }

  pin_to_channel[pin] = chan + 1;

  uint8_t group=(chan/8);
  uint8_t channel=(chan%8);
  uint8_t timer=0;

  ledc_channel_config_t ledc_channel = {
      (int)pin,          // gpio
      (ledc_mode_t)group,        // speed-mode
      (ledc_channel_t)channel,      // channel
      (ledc_intr_type_t)LEDC_INTR_DISABLE,  // intr_type
      (ledc_timer_t)timer,        // timer_sel
      0,            // duty
      0,            // hpoint
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
      (ledc_sleep_mode_t) 2,
#endif
      { output_invert ? 1u : 0u },// output_invert
  };
  ledc_channel_config(&ledc_channel);

  return chan;
}

void analogDetach(uint32_t pin) {
  if (pin_to_channel[pin] > 0) {
#if ESP_IDF_VERSION_MAJOR < 5
    ledcDetachPin(pin);
#else
    ledcDetach(pin);
#endif
    pin_to_channel[pin] = 0;
  }
}

void analogDetachAll(void) {
  for (uint32_t pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++) { 
    analogDetach(pin);
  }
}

uint32_t ledcReadFreq2(uint8_t chan) {
  if (chan > MAX_PWMS) {
    return 0;     // wrong channel
  }
  int32_t timer = pwm_timer[chan];
  int32_t freq = timer_freq_hz[timer];
  return freq;
}

uint8_t ledcReadResolution(uint8_t chan) {
  if (chan > MAX_PWMS) {
    return 0;     // wrong channel
  }
  int32_t timer = pwm_timer[chan];
  int32_t res = timer_duty_resolution[timer];
  return res;
}

int32_t ledcReadDutyResolution(uint8_t pin) {
  int32_t chan = analogGetChannel2(pin);
  if (chan >= 0) {
    return (1 << ledcReadResolution(chan));
  }
  return -1;
}

int32_t ledcRead2(uint8_t pin) {
  int32_t chan = analogGetChannel2(pin);
  if (chan >= 0) {
    uint8_t group=(chan/8), channel=(chan%8);
    return ledc_get_duty((ledc_mode_t)group, (ledc_channel_t)channel);
  }
  return -1;
}

void analogWritePhase(uint8_t pin, uint32_t duty, uint32_t phase)
{
  int32_t chan = analogGetChannel2(pin);
  if (chan < 0) {   
    chan = analogAttach(pin);
    if (chan < 0) {
      AddLog(LOG_LEVEL_INFO, "PWM: analogWritePhase invalid chan=%i", chan);
      return;
    }  
  }
  int32_t timer = _analog_pin2timer(pin);
  if (timer < 0) {
    AddLog(LOG_LEVEL_INFO, "PWM: analogWritePhase invalid timer=%i", timer);
    return;
  }

  int32_t pwm_bit_num = timer_duty_resolution[timer];
  if (duty >> (pwm_bit_num-1) ) ++duty; 
  if (phase >> (pwm_bit_num-1) ) ++phase;
  uint8_t group=(chan/8), channel=(chan%8);
  uint32_t max_duty = (1 << pwm_bit_num) - 1;
  phase = phase & max_duty;
  esp_err_t err1, err2;
  err1 = ledc_set_duty_with_hpoint((ledc_mode_t)group, (ledc_channel_t)channel, duty, phase);
  err2 = ledc_update_duty((ledc_mode_t)group, (ledc_channel_t)channel);
}


int32_t analogGetTimer(uint8_t pin) {
  return _analog_pin2timer(pin);
}

int32_t analogGetTimerForChannel(uint8_t chan) {
  _analogInit();     
  if (chan > MAX_PWMS) { return -1; }
  int32_t timer = pwm_timer[chan];
  if (timer > MAX_TIMERS) { timer = 0; }
  return timer;
}


uint8_t analogGetTimerResolution(uint8_t timer) {
  _analogInit();   
  if (timer >= MAX_TIMERS) { timer = 0; }
  return timer_duty_resolution[timer];
}

uint32_t analogGetTimerFrequency(uint8_t timer) {
  _analogInit();      // make sure the mapping array is initialized
  if (timer >= MAX_TIMERS) { timer = 0; }
  return timer_freq_hz[timer]; // TODO check validity of value
}


////////////////////////////////////////////////////


/////////////////////////////////////////////


int16_t analog_write_state[MAX_GPIO_PIN] = { -1 };

void AnalogWrite(uint8_t pin, int val) {
  analog_write_state[pin] = val;
  analogWrite(pin, val);
}

uint32_t AnalogRead(uint8_t pin) {
  return analog_write_state[pin];
}

/***********************************************************************\
 * PWM Control for ESP32
\***********************************************************************/

// All changes in PWM have been applied, rearm all change indicators
void PwmRearmChanges(void) {
  for (uint32_t i = 0; i < MAX_PWMS; i++) {
    // Init expected changes
    TasmotaGlobal.pwm_value[i] = -1;          // no change wanted
    TasmotaGlobal.pwm_phase[i] = -1;          // no change wanted
  }
}

// Load PWM values from settings and intiliaze values
// void PwmLoadFromSettings(void) {
//   for (uint32_t i = 0; i < MAX_PWMS; i++) {
//     if (i < MAX_PWMS_LEGACY) {
//       TasmotaGlobal.pwm_cur_value[i] = Settings->pwm_value[i];    // retrieve in Legacy pool for 0..4
//     } else {
//       TasmotaGlobal.pwm_cur_value[i] = Settings->pwm_value_ext[i - MAX_PWMS_LEGACY];    // retrieve in Legacy pool for 5..15
//     }
//     TasmotaGlobal.pwm_cur_phase[i] = 0;       // no phase shift for now, will be recomputed at first push to GPIOs
//   }
//   PwmRearmChanges();    // reset expected changes
// }

// Copy current values to Settings
void PwmSaveToSettings(void) {
  for (uint32_t i = 0; i < MAX_PWMS; i++) {
    if (i < MAX_PWMS_LEGACY) {
      Settings->pwm_value[i] = TasmotaGlobal.pwm_cur_value[i];    // store in Legacy pool for 0..4
    } else {
      Settings->pwm_value_ext[i - MAX_PWMS_LEGACY] = TasmotaGlobal.pwm_cur_value[i];    // retrieve in Legacy pool for 5..15
    }
  }
}

/***********************************************************************\
 * PWM Control for ESP32
\***********************************************************************/
// Apply PWM expected values to actual GPIO PWM
// As input, `TasmotaGlobal.pwm_value[]` and `TasmotaGlobal.pwm_phase[]` contain the new expected values
// or `-1` if no change.
// Auto-phasing is recomputed, and changes are applied to GPIO if there is a physical GPIO configured and an actual change needed
//
// force_update_all: force applying the PWM values even if the value didn't change (necessary at initialization)
void PwmApplyGPIO(bool force_update_all) {
  uint32_t pwm_phase_accumulator = 0;     // dephase each PWM channel with the value of the previous

  uint8_t  timer0_resolution = analogGetTimerResolution(0);
  uint32_t timer0_freq = analogGetTimerFrequency(0);

  // AddLog(LOG_LEVEL_INFO, "PWM: resol0=%i freq0=%i", timer0_resolution, timer0_freq);

  for (uint32_t i = 0; i < MAX_PWMS; i++) {

    // compute `pwm_val`, the virtual value of PWM (not taking into account inverted)
    uint32_t pwm_val = TasmotaGlobal.pwm_cur_value[i];      // logical value of PWM, 0..1023
    uint32_t pwm_phase = TasmotaGlobal.pwm_cur_phase[i];    // pwm_phase is the logical phase of the active pulse, ignoring inverted

    // apply new values to GPIO if GPIO is set
    if (PinUsed(GPIO_PWM1, i)) {
      int32_t pin = Pin(GPIO_PWM1, i);
      int32_t chan = analogGetChannel2(pin);
      uint32_t res = ledcReadResolution(chan);
      uint32_t range = (1 << res) - 1;
      uint32_t freq = ledcReadFreq2(chan);

      // AddLog(LOG_LEVEL_INFO, "PWM: res0=%i freq0=%i pin=%i chan=%i res=%i timer=%i range=%i freq=%i", timer0_resolution, timer0_freq, pin, chan, res, analogGetTimerForChannel(chan), range, freq);

      if (TasmotaGlobal.pwm_value[i] >= 0) { pwm_val = TasmotaGlobal.pwm_value[i]; }    // new value explicitly specified
      if (pwm_val > range) { pwm_val = range; } // prevent overflow

      // compute phase
      if (TasmotaGlobal.pwm_phase[i] >= 0) {
        pwm_phase = TasmotaGlobal.pwm_phase[i]; // if explicit set explicitly, 
      } else if (Settings->flag5.pwm_force_same_phase) {
        pwm_phase = 0;                          // if auto-phase is off
      } else {
        if (freq == timer0_freq && res == timer0_resolution) {    // only apply if the frequency is equl to global one
          // compute auto-phase only if default frequency
          pwm_phase = pwm_phase_accumulator;
          // accumulate phase for next GPIO
          pwm_phase_accumulator = (pwm_phase + pwm_val) & range;
        }
      }
      // AddLog(LOG_LEVEL_INFO, "PWM: i=%i used=%i pwm_val=%03X vs %03X pwm_phase=%03X vs %03X", i, PinUsed(GPIO_PWM1, i), pwm_val, TasmotaGlobal.pwm_cur_value[i], pwm_phase, TasmotaGlobal.pwm_cur_phase[i]);

      if (force_update_all || (pwm_val != TasmotaGlobal.pwm_cur_value[i]) || (pwm_phase != TasmotaGlobal.pwm_cur_phase[i])) {
        // GPIO has PWM and there is a chnage to apply, apply it
        analogWritePhase(pin, pwm_val, pwm_phase);
        // AddLog(LOG_LEVEL_INFO, "PWM: analogWritePhase i=%i val=%03X phase=%03X", i, pwm_val, pwm_phase);
      }
    }

    // set new current values
    TasmotaGlobal.pwm_cur_value[i] = pwm_val;
    TasmotaGlobal.pwm_cur_phase[i] = pwm_phase;
  }
  // AddLog(LOG_LEVEL_INFO, "PWM: Val=%03X-%03X-%03X-%03X-%03X Phase=%03X-%03X-%03X-%03X-%03X Range=%03X",
  //                         TasmotaGlobal.pwm_cur_value[0], TasmotaGlobal.pwm_cur_value[1], TasmotaGlobal.pwm_cur_value[2], TasmotaGlobal.pwm_cur_value[3],
  //                         TasmotaGlobal.pwm_cur_value[4],
  //                         TasmotaGlobal.pwm_cur_phase[0], TasmotaGlobal.pwm_cur_phase[1], TasmotaGlobal.pwm_cur_phase[2], TasmotaGlobal.pwm_cur_phase[3],
  //                         TasmotaGlobal.pwm_cur_phase[4],
  //                         Settings->pwm_range
  //                         );
  PwmSaveToSettings();    // copy to Settings
  PwmRearmChanges();      // reset expected changes
}

void CmndPwm(void)
{
  if (TasmotaGlobal.pwm_present && (XdrvMailbox.index > 0) && (XdrvMailbox.index <= MAX_PWMS)) {
    if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= Settings->pwm_range) && PinUsed(GPIO_PWM1, XdrvMailbox.index -1)) {
      TasmotaGlobal.pwm_value[XdrvMailbox.index - 1] = XdrvMailbox.payload;
      PwmApplyGPIO(false);
    }
    Response_P(PSTR("{"));
    MqttShowPWMState();  // Render the PWM status to MQTT
    ResponseJsonEnd();
  }
}

void GpioInitPwm(void) {
  PwmRearmChanges();

  for (uint32_t i = 0; i < MAX_PWMS; i++) {     // Basic PWM control only
    if (PinUsed(GPIO_PWM1, i)) {
      analogAttach(Pin(GPIO_PWM1, i), bitRead(TasmotaGlobal.pwm_inverted, i));

      if (i < TasmotaGlobal.light_type) {
        // force PWM GPIOs to black
        TasmotaGlobal.pwm_value[i] = 0;
      } else {
        TasmotaGlobal.pwm_present = true;
        if (i < MAX_PWMS_LEGACY) {
          TasmotaGlobal.pwm_value[i] = Settings->pwm_value[i];
        } else {
          TasmotaGlobal.pwm_value[i] = Settings->pwm_value_ext[i - MAX_PWMS_LEGACY];
        }
      }
    }
  }
  PwmApplyGPIO(true);   // apply all changes
}

/********************************************************************************************/

void ResetPwm(void)
{
  for (uint32_t i = 0; i < MAX_PWMS; i++) {     // Basic PWM control only
    TasmotaGlobal.pwm_value[i] = 0;
  }
  PwmApplyGPIO(true);
  analogDetachAll();     // Fix PWM activity on unconfigured PWM GPIOs after restart
}

void CmndPwmfrequency(void)
{
  int32_t pwm_frequency = Settings->pwm_frequency;
  int32_t pwm = -1;      // PWM being targeted, or -1 for global value applied to Timer 0

  // check if index if above 100, meaning we target only a specific PWM channel
  uint32_t parm[2] = { 0, 0 };
  ParseParameters(2, parm);

  if (parm[1]) {    // we have a second parameter
    pwm = parm[1] - 1;
    if (pwm < 0 || pwm >= MAX_PWMS) { pwm = -1; }    // if invalid, revert to global value
  }

  // AddLog(LOG_LEVEL_INFO, "PWM: payload=%i index=%i pwm=%i pwm_freqency=%i", XdrvMailbox.payload, XdrvMailbox.index , pwm, pwm_frequency);
  if ((1 == XdrvMailbox.payload) || ((XdrvMailbox.payload >= PWM_MIN) && (XdrvMailbox.payload <= PWM_MAX))) {
    pwm_frequency = (1 == XdrvMailbox.payload) ? PWM_FREQ : XdrvMailbox.payload;

    if (pwm >= 0 && PinUsed(GPIO_PWM1, pwm)) {
      analogWriteFreq(pwm_frequency, Pin(GPIO_PWM1, pwm));
    } else {
      // apply to all default PWM
      // AddLog(LOG_LEVEL_INFO, "PWM: apply global freq=%i", pwm_frequency);
      Settings->pwm_frequency = pwm_frequency;
      analogWriteFreq(pwm_frequency);   // Default is 977
    }
  }
  ResponseCmndNumber(pwm_frequency);
}



void CmndPwmrange(void) {
  // Support only 8 (=255), 9 (=511) and 10 (=1023) bits resolution
  if ((1 == XdrvMailbox.payload) || ((XdrvMailbox.payload > 254) && (XdrvMailbox.payload < 1024))) {
    uint32_t pwm_range = XdrvMailbox.payload;
    uint32_t pwm_resolution = 0;
    while (pwm_range) {
      pwm_resolution++;
      pwm_range >>= 1;
    }
    pwm_range = (1 << pwm_resolution) - 1;
    uint32_t old_pwm_range = Settings->pwm_range;
    Settings->pwm_range = (1 == XdrvMailbox.payload) ? PWM_RANGE : pwm_range;
    for (uint32_t i = 0; i < MAX_PWMS; i++) {
      if (i < MAX_PWMS_LEGACY) {
        if (Settings->pwm_value[i] > Settings->pwm_range) {
          Settings->pwm_value[i] = Settings->pwm_range;
        }
      } else {
        if (Settings->pwm_value_ext[i - MAX_PWMS_LEGACY] > Settings->pwm_range) {
          Settings->pwm_value_ext[i - MAX_PWMS_LEGACY] = Settings->pwm_range;
        }
      }
    }
    if (Settings->pwm_range != old_pwm_range) {  // On ESP32 this prevents loss of duty state
      analogWriteRange(Settings->pwm_range);     // Default is 1023 (Arduino.h)
    }
  }
  ResponseCmndNumber(Settings->pwm_range);
}

void MqttShowPWMState(void)
{
  ResponseAppend_P(PSTR("\"" D_CMND_PWM "\":{"));
  bool first = true;
  for (uint32_t i = 0; i < MAX_PWMS; i++) {   // TODO
    if (PinUsed(GPIO_PWM1, i)) {
      uint32_t pwm_val = (i < MAX_PWMS_LEGACY) ? Settings->pwm_value[i] : Settings->pwm_value_ext[i - MAX_PWMS_LEGACY];
      ResponseAppend_P(PSTR("%s\"" D_CMND_PWM "%d\":%d"), first ? "" : ",", i+1, pwm_val);
      first = false;
    }
  }
  ResponseJsonEnd();
}
