#include "buzzer_backend.h"
#include "settings.h"

#if defined(BOARD_HAS_ES8311_AUDIO)

#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

namespace {

// ES8311 register addresses
constexpr uint8_t ES_REG_RESET  = 0x00;
constexpr uint8_t ES_REG_CLK1   = 0x01;
constexpr uint8_t ES_REG_CLK2   = 0x02;
constexpr uint8_t ES_REG_CLK3   = 0x03;
constexpr uint8_t ES_REG_CLK4   = 0x04;
constexpr uint8_t ES_REG_CLK5   = 0x05;
constexpr uint8_t ES_REG_CLK6   = 0x06;
constexpr uint8_t ES_REG_CLK7   = 0x07;
constexpr uint8_t ES_REG_CLK8   = 0x08;
constexpr uint8_t ES_REG_SDP_IN = 0x09;
constexpr uint8_t ES_REG_SDP_OUT = 0x0A;
constexpr uint8_t ES_REG_SYS_0D = 0x0D;
constexpr uint8_t ES_REG_SYS_0E = 0x0E;
constexpr uint8_t ES_REG_SYS_12 = 0x12;
constexpr uint8_t ES_REG_SYS_13 = 0x13;
constexpr uint8_t ES_REG_ADC_1C = 0x1C;
constexpr uint8_t ES_REG_DAC_31 = 0x31;
constexpr uint8_t ES_REG_DAC_32 = 0x32;
constexpr uint8_t ES_REG_DAC_37 = 0x37;

// Audio parameters
constexpr uint32_t kSampleRate     = 16000;
constexpr size_t   kFramesPerChunk = 128;           // per DMA buffer
constexpr size_t   kChunkSamples   = kFramesPerChunk * 2;  // stereo
constexpr int16_t  kToneAmplitude  = 9000;
constexpr uint8_t  kCodecVolume    = 75;            // percent

// Envelope for click-free transitions between tones
constexpr uint16_t kEnvMax  = 256;
constexpr uint16_t kEnvStep = 4;   // ~4ms full ramp at 16 kHz / 128 frames

// Idle timeout - shutdown audio pipeline after this much silence
constexpr uint32_t kIdleTimeoutMs = 1500;

// Audio lifecycle state
enum AudioState : uint8_t { AUDIO_OFF = 0, AUDIO_RUNNING };
volatile AudioState gAudioState = AUDIO_OFF;
volatile bool gShutdownRequested = false;
uint32_t gIdleStartMs = 0;

// Tone state - shared between main loop and audio task
volatile uint16_t gCurrentFreq  = 0;
volatile uint32_t gPhaseStep    = 0;
volatile uint16_t gTargetGain   = 0;
volatile uint16_t gCurrentGain  = 0;

// Internal state
bool     gWireReady   = false;
bool     gI2sReady    = false;
bool     gCodecReady  = false;
volatile bool gTaskStarted = false;
bool     gAmpEnabled  = false;
uint32_t gPhase       = 0;
uint32_t gLastTonePhaseStep = 0;

int16_t  gWaveTable[256];
bool     gWaveReady = false;
volatile TaskHandle_t gAudioTask = nullptr;

// ----- helpers -----

void buildWaveTable() {
  if (gWaveReady) return;
  for (int i = 0; i < 256; i++)
    gWaveTable[i] = (int16_t)lroundf(sinf(2.0f * PI * i / 256.0f) * 32767.0f);
  gWaveReady = true;
}

void setAmpEnabled(bool on) {
  if (gAmpEnabled == on) return;
  digitalWrite(AUDIO_PA_CTRL, on ? HIGH : LOW);
  gAmpEnabled = on;
}

void ensureWire() {
  if (gWireReady) return;
  Wire.begin(AUDIO_I2C_SDA, AUDIO_I2C_SCL);
  Wire.setClock(400000);
  gWireReady = true;
}

bool esWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(AUDIO_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission(true) == 0;
}

bool esRead(uint8_t reg, uint8_t& val) {
  Wire.beginTransmission(AUDIO_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    Wire.beginTransmission(AUDIO_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return false;
  }
  if (Wire.requestFrom((uint8_t)AUDIO_I2C_ADDR, (uint8_t)1, (uint8_t)true) != 1) return false;
  val = Wire.read();
  return true;
}

// ----- I2S -----

bool initI2s() {
  if (gI2sReady) return true;

  i2s_config_t cfg = {};
  cfg.mode            = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate     = kSampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format  = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count   = 6;
  cfg.dma_buf_len     = kFramesPerChunk;
  cfg.use_apll        = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk      = kSampleRate * 256;
  cfg.mclk_multiple   = I2S_MCLK_MULTIPLE_256;
  cfg.bits_per_chan    = I2S_BITS_PER_CHAN_16BIT;

  if (i2s_driver_install((i2s_port_t)AUDIO_I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("ES8311: i2s_driver_install failed");
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.mck_io_num    = AUDIO_I2S_MCLK;
  pins.bck_io_num    = AUDIO_I2S_BCLK;
  pins.ws_io_num     = AUDIO_I2S_LRC;
  pins.data_out_num  = AUDIO_I2S_DOUT;
  pins.data_in_num   = I2S_PIN_NO_CHANGE;

  if (i2s_set_pin((i2s_port_t)AUDIO_I2S_PORT, &pins) != ESP_OK) {
    Serial.println("ES8311: i2s_set_pin failed");
    i2s_driver_uninstall((i2s_port_t)AUDIO_I2S_PORT);
    return false;
  }

  i2s_zero_dma_buffer((i2s_port_t)AUDIO_I2S_PORT);
  gI2sReady = true;
  return true;
}

// ----- ES8311 codec -----

bool initCodec() {
  if (gCodecReady) return true;
  ensureWire();

  Wire.beginTransmission(AUDIO_I2C_ADDR);
  if (Wire.endTransmission(true) != 0) {
    Serial.printf("ES8311: not found on I2C 0x%02X\n", AUDIO_I2C_ADDR);
    return false;
  }

  // Reset (20 ms settling required)
  if (!esWrite(ES_REG_RESET, 0x1F)) return false;
  delay(20);
  if (!esWrite(ES_REG_RESET, 0x00)) return false;
  if (!esWrite(ES_REG_RESET, 0x80)) return false;

  // Clock registers - tuned for ESP32 I2S master providing 4.096 MHz MCLK
  if (!esWrite(ES_REG_CLK1, 0x3F)) return false;
  if (!esWrite(ES_REG_CLK2, 0x00)) return false;
  if (!esWrite(ES_REG_CLK3, 0x10)) return false;
  if (!esWrite(ES_REG_CLK4, 0x10)) return false;
  if (!esWrite(ES_REG_CLK5, 0x00)) return false;

  uint8_t reg06 = 0;
  esRead(ES_REG_CLK6, reg06);
  reg06 = (reg06 & 0xE0) | 0x03;
  if (!esWrite(ES_REG_CLK6, reg06)) return false;

  if (!esWrite(ES_REG_CLK7, 0x00)) return false;
  if (!esWrite(ES_REG_CLK8, 0xFF)) return false;

  // I2S slave mode
  uint8_t reg00 = 0;
  if (!esRead(ES_REG_RESET, reg00)) return false;
  reg00 &= 0xBF;
  if (!esWrite(ES_REG_RESET, reg00)) return false;

  // 16-bit I2S format
  if (!esWrite(ES_REG_SDP_IN,  0x0C)) return false;
  if (!esWrite(ES_REG_SDP_OUT, 0x0C)) return false;

  // System power-up
  if (!esWrite(ES_REG_SYS_0D, 0x01)) return false;
  if (!esWrite(ES_REG_SYS_0E, 0x02)) return false;
  if (!esWrite(ES_REG_SYS_12, 0x00)) return false;
  if (!esWrite(ES_REG_SYS_13, 0x10)) return false;

  if (!esWrite(ES_REG_ADC_1C, 0x6A)) return false;
  if (!esWrite(ES_REG_DAC_37, 0x08)) return false;

  uint8_t vol = (kCodecVolume == 0) ? 0 : (uint8_t)(((kCodecVolume * 256) / 100) - 1);
  if (!esWrite(ES_REG_DAC_32, vol)) return false;

  uint8_t reg31 = 0;
  esRead(ES_REG_DAC_31, reg31);
  reg31 &= ~0x60;
  if (!esWrite(ES_REG_DAC_31, reg31)) return false;

  gCodecReady = true;
  Serial.println("ES8311: codec initialized");
  return true;
}

// ----- tone generation -----

uint32_t phaseStepFor(uint16_t freq) {
  if (freq == 0) return 0;
  return (uint32_t)(((uint64_t)freq << 32) / kSampleRate);
}

void fillChunk(int16_t* out) {
  uint32_t step = gPhaseStep;
  if (step != 0) gLastTonePhaseStep = step;
  else step = gLastTonePhaseStep;

  for (size_t i = 0; i < kFramesPerChunk; i++) {
    uint16_t target = gTargetGain;
    if (gCurrentGain < target) {
      uint16_t next = gCurrentGain + kEnvStep;
      gCurrentGain = (next > target) ? target : next;
    } else if (gCurrentGain > target) {
      gCurrentGain = (gCurrentGain > kEnvStep + target)
                     ? (uint16_t)(gCurrentGain - kEnvStep)
                     : target;
    }

    int16_t sample = 0;
    if (gCurrentGain > 0 && step != 0) {
      uint8_t idx = (uint8_t)(gPhase >> 24);
      int32_t raw = gWaveTable[idx];
      int64_t scaled = (int64_t)raw * kToneAmplitude * gCurrentGain;
      sample = (int16_t)(scaled / ((int64_t)32767 * kEnvMax));
      gPhase += step;
    }
    out[i * 2]     = sample;
    out[i * 2 + 1] = sample;
  }
}

// ----- audio task -----
// Streams tones when requested, silence between tones.
// Exits cooperatively when gShutdownRequested is set.

void audioTask(void*) {
  static int16_t chunk[kChunkSamples];

  while (!gShutdownRequested) {
    fillChunk(chunk);
    size_t written = 0;
    i2s_write((i2s_port_t)AUDIO_I2S_PORT, chunk, sizeof(chunk),
              &written, pdMS_TO_TICKS(50));
  }

  // Clean self-exit
  gTaskStarted = false;
  gAudioTask = nullptr;
  vTaskDelete(nullptr);
}

void ensureTaskStarted() {
  if (gTaskStarted) return;
  gShutdownRequested = false;
  BaseType_t ok = xTaskCreatePinnedToCore(
      audioTask, "buzzer_es8311", 4096, nullptr, 1, (TaskHandle_t*)&gAudioTask, tskNO_AFFINITY);
  if (ok == pdPASS) {
    gTaskStarted = true;
  } else {
    Serial.println("ES8311: failed to start audio task");
  }
}

// ----- lifecycle management -----

// PA amplifier settling time after power-on.
// Coupling capacitors need to charge before clean audio output.
constexpr uint32_t kPaSettleMs = 30;

bool ensureAudioRunning() {
  if (gAudioState == AUDIO_RUNNING) return true;

  if (!initI2s()) return false;
  if (!initCodec()) return false;
  setAmpEnabled(true);
  ensureTaskStarted();

  // Let PA settle while task streams silence through DMA.
  // Without this, the first ~30ms of audio is muffled/cut.
  delay(kPaSettleMs);

  gAudioState = AUDIO_RUNNING;
  gIdleStartMs = 0;
  Serial.println("ES8311: audio activated");
  return true;
}

void shutdownAudio() {
  if (gAudioState == AUDIO_OFF) return;

  // Signal task to stop cooperatively
  gShutdownRequested = true;

  // Wait for task to finish current i2s_write and self-exit
  if (gTaskStarted && gAudioTask != nullptr) {
    uint32_t waitStart = millis();
    while (gTaskStarted && (millis() - waitStart < 100)) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    // Forceful delete as fallback
    if (gTaskStarted) {
      vTaskDelete(gAudioTask);
      gTaskStarted = false;
    }
    gAudioTask = nullptr;
  }

  // Disable PA before I2S teardown (envelope already at 0, no pop)
  setAmpEnabled(false);

  // Tear down I2S
  if (gI2sReady) {
    i2s_zero_dma_buffer((i2s_port_t)AUDIO_I2S_PORT);
    i2s_driver_uninstall((i2s_port_t)AUDIO_I2S_PORT);
    gI2sReady = false;
  }

  // Put codec in standby for real power savings
  // Do NOT call Wire.end() - shared with CST816D touch controller
  if (gCodecReady) {
    esWrite(ES_REG_SYS_0D, 0x00);  // power down DAC
    esWrite(ES_REG_SYS_0E, 0x00);  // power down analog
    gCodecReady = false;
  }

  // Reset tone state
  gCurrentFreq = 0;
  gPhaseStep = 0;
  gTargetGain = 0;
  gCurrentGain = 0;
  gIdleStartMs = 0;
  gShutdownRequested = false;

  gAudioState = AUDIO_OFF;
  Serial.println("ES8311: audio shutdown");
}

}  // namespace

// ----- public API -----

void buzzerBackendInit() {
  pinMode(AUDIO_PA_CTRL, OUTPUT);
  digitalWrite(AUDIO_PA_CTRL, LOW);
  gAmpEnabled = false;
  buildWaveTable();
  gCurrentFreq = 0;
  gPhaseStep = 0;
  gTargetGain = 0;
  gCurrentGain = 0;
  gLastTonePhaseStep = 0;
  gAudioState = AUDIO_OFF;
  // Do NOT start I2S/codec/task here - lazy init on first sound
}

void buzzerBackendApplyStep(uint16_t freq) {
  if (freq > 0) {
    if (!ensureAudioRunning()) return;
    gIdleStartMs = 0;  // reset idle timer
    if (gPhaseStep == 0) {
      gPhase = 0;  // new tone starting - reset phase for clean waveform
    }
  }
  gCurrentFreq = freq;
  gPhaseStep = phaseStepFor(freq);
  if (gPhaseStep != 0) gLastTonePhaseStep = gPhaseStep;
  gTargetGain = (freq > 0) ? kEnvMax : 0;
}

void buzzerBackendStop() {
  gCurrentFreq = 0;
  gPhaseStep = 0;
  gTargetGain = 0;
  // Envelope decays naturally in audio task.
  // Idle timer will be armed by buzzerBackendTick() once gCurrentGain reaches 0.
}

void buzzerBackendTick() {
  if (gAudioState != AUDIO_RUNNING) return;

  // Check if audio pipeline is idle (silence + envelope fully decayed)
  if (gTargetGain == 0 && gCurrentGain == 0) {
    if (gIdleStartMs == 0) {
      gIdleStartMs = millis();
    } else if (millis() - gIdleStartMs >= kIdleTimeoutMs) {
      shutdownAudio();
    }
  } else {
    gIdleStartMs = 0;  // sound is playing or envelope ramping
  }
}

void buzzerBackendShutdown() {
  // Force immediate silence (skip envelope ramp)
  gCurrentGain = 0;
  gTargetGain = 0;
  gPhaseStep = 0;
  gCurrentFreq = 0;
  shutdownAudio();
}

#endif // BOARD_HAS_ES8311_AUDIO
