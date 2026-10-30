// =====================================================================
//  THIRD EYE  v1.0   -  a living eye badge for Halloween
//  Board : Freenove ESP32-S3 Display 4.0" (FNK0104S)  320x480 ST7796 + FT6336 touch
//  Libs  : LovyanGFX (Library Manager)  -  nothing else
//
//  Touch:
//    tap             -> it looks at your finger, flinches, blinks
//    double-tap      -> side-eye
//    4+ fast taps    -> RAGE (slit pupil, red, shaking, glitching)
//    hold            -> it stares at you, wide open, follows your finger
//    keep holding 8s -> changes colour (EMBER > FROST > VOID > PHANTOM)
//    2 fingers, 2s   -> setup screen (swap colours, flip 180, brightness)
// =====================================================================
#pragma GCC optimize("O2")
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <LovyanGFX.hpp>
#include "config.h"
#include "ui_types.h"

// ---- memory: hot buffers in internal RAM, big vein map in PSRAM ----
static void* thirdEyeAllocFast(size_t sz) {
  void* p = heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p;
}
static void* thirdEyeAllocBig(size_t sz) {
  void* p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}
#define THIRDEYE_ALLOC(sz) thirdEyeAllocFast(sz)
#define THIRDEYE_ALLOC_BIG(sz) thirdEyeAllocBig(sz)
#include "eye_render.h"

using namespace thirdeye;

// ---------------------------------------------------------------------
//  display driver (LovyanGFX)  -  same settings as Freenove's own files
// ---------------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
public:
  explicit LGFX(bool rgbOrder) {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = PANEL_SPI_HZ;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_LCD_SCLK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = PIN_LCD_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = PANEL_INVERT;
      cfg.rgb_order = rgbOrder;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

// ---------------------------------------------------------------------
//  globals
// ---------------------------------------------------------------------
static const int BAND = 16;                 // rows per DMA strip
static const int NBANDS = SH / BAND;        // 20 strips per frame
static const int NBUF = 4;                  // strip buffers (2 per core)
Preferences prefs;
LGFX* lcd = nullptr;
Renderer renderer;
Brain brain;
uint16_t* band[NBUF] = {nullptr, nullptr, nullptr, nullptr};
// dual-core: core 1 renders even strips, core 0 renders odd strips
SemaphoreHandle_t frameGo = nullptr;
SemaphoreHandle_t bufFree[NBUF];
SemaphoreHandle_t bufReady[NBUF];

static void helperTask(void*) {
  for (;;) {
    xSemaphoreTake(frameGo, portMAX_DELAY);
    for (int k = 1; k < NBANDS; k += 2) {
      int b = k % NBUF;
      xSemaphoreTake(bufFree[b], portMAX_DELAY);
      renderer.renderBand(band[b], k * BAND, BAND);
      xSemaphoreGive(bufReady[b]);
    }
  }
}

struct Settings {
  bool rgbOrder = false;   // Freenove's default is BGR
  bool flip = false;       // rotate 180
  uint8_t brightness = START_BRIGHTNESS;
  uint8_t palette = START_PALETTE;
} S;

float batteryV = 0;
bool lowBattery = false;

// ---------------------------------------------------------------------
//  touch (FT6336 at 0x38, read directly over I2C)
// ---------------------------------------------------------------------
static void mapTouch(int rx, int ry, int& sx, int& sy) {
  // Freenove landscape mapping: x = rawY, y = 320 - rawX
  if (!S.flip) { sx = ry; sy = 319 - rx; }
  else { sx = 479 - ry; sy = rx; }
  sx = clampi(sx, 0, SW - 1);
  sy = clampi(sy, 0, SH - 1);
}

static bool readTouch(Touch& t) {
  t.n = 0;
  Wire.beginTransmission(0x38);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t b[11];
  if (Wire.requestFrom(0x38, 11) != 11) return false;
  for (int i = 0; i < 11; i++) b[i] = Wire.read();
  int n = b[0] & 0x0F;
  if (n > 2) n = 0;            // garbage reading
  t.n = n;
  if (n >= 1) mapTouch(((b[1] & 0x0F) << 8) | b[2], ((b[3] & 0x0F) << 8) | b[4], t.x[0], t.y[0]);
  if (n >= 2) mapTouch(((b[7] & 0x0F) << 8) | b[8], ((b[9] & 0x0F) << 8) | b[10], t.x[1], t.y[1]);
  return true;
}

static void touchBegin() {
  pinMode(PIN_TOUCH_INT, INPUT);
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(300);
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);
}

// ---------------------------------------------------------------------
//  battery (GPIO9 through a 1/2 divider)
// ---------------------------------------------------------------------
static void readBattery() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BATTERY);
  batteryV = (mv / 8.f) * 2.f / 1000.f;
  bool wasLow = lowBattery;
  lowBattery = (batteryV > 2.8f && batteryV < 3.45f);   // < 2.8 V = no battery / USB only
  if (lowBattery != wasLow) lcd->setBrightness(lowBattery ? min<int>(S.brightness, 110) : S.brightness);
}

// ---------------------------------------------------------------------
//  settings
// ---------------------------------------------------------------------
static void loadSettings() {
  prefs.begin("thirdeye", false);
  S.rgbOrder = prefs.getBool("rgb", false);
  S.flip = prefs.getBool("flip", false);
  S.brightness = prefs.getUChar("bri", START_BRIGHTNESS);
  S.palette = prefs.getUChar("pal", START_PALETTE);
  if (S.palette >= NUM_PALETTES) S.palette = 0;
}

static void applyRotation() { lcd->setRotation(S.flip ? 7 : 5); }

static void drawButton(const Button& b, uint16_t col, const char* text = nullptr) {
  lcd->fillRoundRect(b.x, b.y, b.w, b.h, 12, col);
  lcd->drawRoundRect(b.x, b.y, b.w, b.h, 12, TFT_WHITE);
  lcd->setTextColor(TFT_WHITE);
  lcd->setTextDatum(textdatum_t::middle_center);
  lcd->setFont(&fonts::FreeSansBold9pt7b);
  lcd->drawString(text ? text : b.label, b.x + b.w / 2, b.y + b.h / 2);
}

static void runSettings() {
  const Button bSwap = {12, 120, 144, 72, "SWAP COLORS"};
  const Button bFlip = {168, 120, 144, 72, "FLIP 180"};
  const Button bPal  = {324, 120, 144, 72, "PALETTE"};
  const Button bDim  = {12, 210, 144, 72, "DIMMER -"};
  const Button bBri  = {168, 210, 144, 72, "BRIGHTER +"};
  const Button bDone = {324, 210, 144, 72, "DONE"};

  auto redraw = [&]() {
    lcd->fillScreen(TFT_BLACK);
    lcd->setTextDatum(textdatum_t::top_left);
    lcd->setFont(&fonts::FreeSansBold12pt7b);
    lcd->setTextColor(TFT_WHITE);
    lcd->drawString("THIRD EYE SETUP", 12, 8);
    lcd->setFont(&fonts::Font2);
    lcd->setTextColor(lcd->color565(170, 170, 170));
    lcd->drawString("labels must match their colours, if not: SWAP COLORS", 12, 36);
    // colour check swatches
    const char* names[3] = {"RED", "GREEN", "BLUE"};
    uint16_t cols[3] = {lcd->color565(255, 0, 0), lcd->color565(0, 255, 0), lcd->color565(0, 0, 255)};
    for (int i = 0; i < 3; i++) {
      lcd->fillRect(12 + i * 156, 60, 144, 46, cols[i]);
      lcd->setTextDatum(textdatum_t::middle_center);
      lcd->setFont(&fonts::FreeSansBold9pt7b);
      lcd->setTextColor(TFT_WHITE);
      lcd->drawString(names[i], 12 + i * 156 + 72, 83);
    }
    char palTxt[24];
    snprintf(palTxt, sizeof palTxt, "%s", PALETTES[S.palette].name);
    drawButton(bSwap, lcd->color565(60, 20, 20));
    drawButton(bFlip, lcd->color565(20, 40, 60));
    drawButton(bPal, lcd->color565(60, 30, 0), palTxt);
    drawButton(bDim, lcd->color565(30, 30, 30));
    drawButton(bBri, lcd->color565(30, 30, 30));
    drawButton(bDone, lcd->color565(0, 70, 30));
    lcd->setTextDatum(textdatum_t::bottom_left);
    lcd->setFont(&fonts::Font2);
    lcd->setTextColor(lcd->color565(170, 170, 170));
    char foot[64];
    if (batteryV > 2.8f) snprintf(foot, sizeof foot, "battery %.2f V   brightness %d", batteryV, S.brightness);
    else snprintf(foot, sizeof foot, "battery: none (USB)   brightness %d", S.brightness);
    lcd->drawString(foot, 12, 314);
  };

  auto hit = [](const Button& b, int x, int y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; };

  // wait until all fingers are lifted before accepting taps
  Touch t;
  do { readTouch(t); delay(20); } while (t.n > 0);
  redraw();

  bool wasDown = false;
  int lx = 0, ly = 0;
  uint32_t lastActivity = millis();
  while (true) {
    readTouch(t);
    bool down = t.n > 0;
    if (down) { lx = t.x[0]; ly = t.y[0]; lastActivity = millis(); }
    if (!down && wasDown) {           // act on release
      if (hit(bSwap, lx, ly)) {
        S.rgbOrder = !S.rgbOrder;
        prefs.putBool("rgb", S.rgbOrder);
        lcd->fillScreen(TFT_BLACK);
        lcd->setTextDatum(textdatum_t::middle_center);
        lcd->setFont(&fonts::FreeSansBold12pt7b);
        lcd->setTextColor(TFT_WHITE);
        lcd->drawString("restarting...", SW / 2, SH / 2);
        delay(600);
        ESP.restart();
      } else if (hit(bFlip, lx, ly)) {
        S.flip = !S.flip; prefs.putBool("flip", S.flip); applyRotation(); redraw();
      } else if (hit(bPal, lx, ly)) {
        S.palette = (S.palette + 1) % NUM_PALETTES; prefs.putUChar("pal", S.palette);
        renderer.setPalette(S.palette); brain.palette = S.palette; redraw();
      } else if (hit(bDim, lx, ly)) {
        S.brightness = (uint8_t)max(20, S.brightness - 25); prefs.putUChar("bri", S.brightness);
        lcd->setBrightness(S.brightness); redraw();
      } else if (hit(bBri, lx, ly)) {
        S.brightness = (uint8_t)min(255, S.brightness + 25); prefs.putUChar("bri", S.brightness);
        lcd->setBrightness(S.brightness); redraw();
      } else if (hit(bDone, lx, ly)) {
        break;
      }
    }
    wasDown = down;
    if (millis() - lastActivity > 30000) break;   // forgot to press DONE
    delay(20);
  }
  lcd->fillScreen(TFT_BLACK);
}

// ---------------------------------------------------------------------
//  helpers
// ---------------------------------------------------------------------
static void fatal(const char* line1, const char* line2) {
  lcd->fillScreen(TFT_BLACK);
  lcd->setTextColor(TFT_WHITE);
  lcd->setTextDatum(textdatum_t::middle_center);
  lcd->setFont(&fonts::FreeSansBold9pt7b);
  lcd->drawString(line1, SW / 2, SH / 2 - 14);
  lcd->drawString(line2, SW / 2, SH / 2 + 14);
  Serial.printf("FATAL: %s %s\n", line1, line2);
  while (true) delay(1000);
}

// ---------------------------------------------------------------------
//  setup / loop
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(50);
  loadSettings();

  lcd = new LGFX(S.rgbOrder);
  lcd->init();
  applyRotation();
  lcd->setBrightness(S.brightness);
  lcd->fillScreen(TFT_BLACK);

  touchBegin();
  analogReadResolution(12);

  for (int i = 0; i < NBUF; i++) {
    band[i] = (uint16_t*)heap_caps_malloc(SW * BAND * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!band[i]) fatal("out of memory (DMA)", "check Tools > PSRAM = OPI PSRAM");
  }

  renderer.swapBytes = true;
  if (!renderer.begin(esp_random() | 1u)) fatal("out of memory", "set Tools > PSRAM to OPI PSRAM");
  if (S.palette != 0) renderer.setPalette(S.palette);
  brain.palette = S.palette;
  brain.rng = Rng(esp_random() | 1u);

  frameGo = xSemaphoreCreateBinary();
  for (int i = 0; i < NBUF; i++) {
    bufFree[i] = xSemaphoreCreateBinary();
    bufReady[i] = xSemaphoreCreateBinary();
    xSemaphoreGive(bufFree[i]);
  }
  xTaskCreatePinnedToCore(helperTask, "thirdeye2", 6144, nullptr, 2, nullptr, 0);

  readBattery();
  Serial.printf("THIRD EYE ready. PSRAM %u KB free, heap %u KB free, veins %s, battery %.2f V\n",
                (unsigned)(ESP.getFreePsram() / 1024), (unsigned)(ESP.getFreeHeap() / 1024),
                renderer.hasVeins() ? "on" : "off", batteryV);
}

void loop() {
  static uint32_t lastUs = micros();
  static float twoFingerT = 0;
  static uint32_t lastBattery = 0, fpsT0 = 0;
  static int frames = 0;

  uint32_t nowUs = micros();
  float dt = (nowUs - lastUs) / 1000000.f;
  lastUs = nowUs;

  // ---- touch ----
  Touch t;
  TouchInput in;
  if (readTouch(t) && t.n >= 1) { in.down = true; in.x = t.x[0]; in.y = t.y[0]; }
  if (t.n >= 2) {
    twoFingerT += dt;
    in.down = false;                 // two fingers is the settings gesture, not a poke
    if (twoFingerT > 2.0f) {
      twoFingerT = 0;
      runSettings();
      lastUs = micros();
      return;
    }
  } else {
    twoFingerT = 0;
  }

  // ---- think ----
  brain.update(dt, in);
  if (brain.requestPalette >= 0) {
    renderer.setPalette(brain.requestPalette);
    S.palette = (uint8_t)brain.requestPalette;
    prefs.putUChar("pal", S.palette);
    brain.requestPalette = -1;
  }

  // ---- draw: both cores render strips while finished strips fly out over DMA ----
  renderer.beginFrame(brain, dt);
  xSemaphoreGive(frameGo);                       // core 0 starts on the odd strips
  lcd->startWrite();
  for (int k = 0; k < NBANDS; k++) {
    int b = k % NBUF;
    if ((k & 1) == 0) {                          // even strip: this core renders it
      xSemaphoreTake(bufFree[b], portMAX_DELAY);
      renderer.renderBand(band[b], k * BAND, BAND);
    } else {                                     // odd strip: wait for core 0
      xSemaphoreTake(bufReady[b], portMAX_DELAY);
    }
    lcd->pushImageDMA(0, k * BAND, SW, BAND, (const lgfx::swap565_t*)band[b]);
    // pushing strip k waited for strip k-1's DMA to finish, so its buffer is free again
    if (k > 0) xSemaphoreGive(bufFree[(k - 1) % NBUF]);
  }
  lcd->waitDMA();
  xSemaphoreGive(bufFree[(NBANDS - 1) % NBUF]);
  lcd->endWrite();

  // ---- housekeeping ----
  frames++;
  uint32_t ms = millis();
  static uint32_t lastLed = 0;
  if (BACK_GLOW > 0 && ms - lastLed > 60) {     // back LED breathes with the eye's aura
    lastLed = ms;
    const Palette& P = brain.angryTex ? PALETTES[PAL_ANGRY] : PALETTES[S.palette];
    float k = clampf(brain.aura, 0.f, 1.3f) * (0.75f + 0.25f * sinf(ms * 0.0023f)) * (BACK_GLOW / 255.f);
    rgbLedWrite(PIN_BACK_LED, (uint8_t)(P.aura.r * k), (uint8_t)(P.aura.g * k), (uint8_t)(P.aura.b * k));
  }
  if (ms - lastBattery > 5000) { lastBattery = ms; readBattery(); }
  if (SHOW_FPS_ON_SERIAL && ms - fpsT0 > 2000) {
    Serial.printf("fps %.1f  battery %.2f V%s  palette %s\n", frames * 1000.f / (ms - fpsT0), batteryV,
                  lowBattery ? " (LOW)" : "", PALETTES[S.palette].name);
    fpsT0 = ms; frames = 0;
  }
}
