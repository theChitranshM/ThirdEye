// =====================================================================
//  THIRD EYE  -  renderer + behaviour ("brain")
//  Portable C++ (no Arduino calls). The same file runs on the
//  ESP32-S3 badge and on a laptop for the preview video.
//  Screen space: 480 x 320 landscape, RGB565.
// =====================================================================
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef THIRDEYE_ALLOC
#define THIRDEYE_ALLOC(sz) malloc(sz)          // small, hot buffers (internal RAM on the ESP32)
#endif
#ifndef THIRDEYE_ALLOC_BIG
#define THIRDEYE_ALLOC_BIG(sz) THIRDEYE_ALLOC(sz)  // big, streamed buffers (PSRAM on the ESP32)
#endif

namespace thirdeye {

static const int SW = 480;
static const int SH = 320;

// ---------------------------------------------------------------------
//  small helpers
// ---------------------------------------------------------------------
struct RGB { uint8_t r, g, b; };

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smooth01(float e0, float e1, float x) {
  float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}
static inline uint16_t pack565(int r, int g, int b) {
  r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static inline uint16_t pack565(const RGB& c) { return pack565(c.r, c.g, c.b); }
static inline RGB mixRGB(const RGB& a, const RGB& b, float t) {
  RGB o;
  o.r = (uint8_t)clampi((int)lerpf(a.r, b.r, t), 0, 255);
  o.g = (uint8_t)clampi((int)lerpf(a.g, b.g, t), 0, 255);
  o.b = (uint8_t)clampi((int)lerpf(a.b, b.b, t), 0, 255);
  return o;
}
// w = weight of "a", 0..256
static inline uint16_t blend565(uint16_t a, uint16_t b, int w) {
  if (w <= 0) return b;
  if (w >= 256) return a;
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  int r = br + (((ar - br) * w) >> 8);
  int g = bg + (((ag - bg) * w) >> 8);
  int bl = bb + (((ab - bb) * w) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}
// k = brightness 0..256 (256 = unchanged)
static inline uint16_t scale565(uint16_t a, int k) {
  if (k >= 256) return a;
  if (k <= 0) return 0;
  int r = ((a >> 11) * k) >> 8, g = (((a >> 5) & 63) * k) >> 8, b = ((a & 31) * k) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
// additive (saturating), "b" scaled by k 0..256
static inline uint16_t add565(uint16_t a, uint16_t b, int k) {
  int r = (a >> 11) + (((b >> 11) * k) >> 8);
  int g = ((a >> 5) & 63) + ((((b >> 5) & 63) * k) >> 8);
  int bl = (a & 31) + (((b & 31) * k) >> 8);
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (bl > 31) bl = 31;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed = 0x1234567u) : s(seed ? seed : 1u) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float f() { return (next() & 0xFFFFFF) / 16777216.0f; }          // 0..1
  float range(float a, float b) { return a + (b - a) * f(); }
};

static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}

// ---------------------------------------------------------------------
//  palettes  (all original colour schemes)
// ---------------------------------------------------------------------
struct Palette {
  const char* name;
  RGB core;    // hottest colour, ring around the pupil
  RGB mid;     // main iris colour
  RGB outer;   // outer iris
  RGB limbus;  // dark edge of the iris
  RGB aura;    // glow around the eye
  RGB sclera;  // white of the eye
  RGB vein;    // vein colour
  bool darkSclera;
};

static const int NUM_PALETTES = 4;      // user cycles through these
static const int PAL_ANGRY = 4;         // used automatically when angry
static const Palette PALETTES[5] = {
  // name       core            mid             outer           limbus        aura            sclera          vein          dark
  {"EMBER",   {255,236,170}, {255,128, 18}, {170, 34,  0}, {22, 4, 0},  {255, 84, 10}, {236,226,212}, {190, 20, 20}, false},
  {"FROST",   {225,255,255}, { 50,195,255}, { 18, 62,170}, { 4, 8,26},  { 40,150,255}, {228,234,242}, {170, 30, 60}, false},
  {"VOID",    {255,215,255}, {185, 55,255}, { 70, 10,150}, { 8, 0,20},  {160, 40,255}, { 16, 10, 22}, {200, 70,255}, true },
  {"PHANTOM", {255,255,255}, {205,210,225}, { 70, 74, 96}, { 6, 6,10},  {190,210,255}, { 14, 14, 18}, {150,170,230}, true },
  {"RAGE",    {255,235,190}, {255, 26, 14}, {110,  0,  0}, {14, 0, 0},  {255, 16, 10}, {226,196,186}, {210,  0,  0}, false},
};

// ---------------------------------------------------------------------
//  geometry
// ---------------------------------------------------------------------
static const float EYE_CX = 240.f;
static const float EYE_CY = 166.f;
static const float EYE_HW = 208.f;     // half width of the eye opening
static const float EYE_UP = 122.f;     // max height of upper lid above centre
static const float EYE_DN = 96.f;      // max depth of lower lid below centre
static const int   IRIS_R = 94;        // iris radius (px)
static const int   AURA_MAX = 92;      // aura reach (px)
static const float LOOK_MAX_X = 104.f;
static const float LOOK_MAX_Y = 40.f;

// ---------------------------------------------------------------------
//  touch input handed to the brain each frame
// ---------------------------------------------------------------------
struct TouchInput {
  bool down = false;     // one finger currently on the glass
  int  x = 0, y = 0;     // screen coords
};

// ---------------------------------------------------------------------
//  BRAIN: decides where the eye looks, how open it is, mood...
// ---------------------------------------------------------------------
enum Mood { M_BOOT = 0, M_IDLE, M_STARE, M_ANGRY, M_SUS };

struct Brain {
  // ---- public outputs (read by renderer) ----
  float lookX = 0, lookY = 0;
  float openU = 0, openL = 0;    // lid openness after blink
  float slant = 0;               // angry slanted upper lid
  float dil = 0.5f;              // pupil size (fraction of iris radius)
  float slit = 0;                // 0 round .. 1 slit pupil
  float veins = 0.35f;           // vein strength
  float aura = 0.6f;             // aura strength
  float rot = 0;                 // iris swirl angle (0..256)
  float twist = 0;               // spiral twist
  float shake = 0;               // screen shake px
  float glitch = 0;              // 0..1
  float waveT = -1;              // expanding ring position 0..1, <0 off
  float waveAmp = 0;
  float embers = 1.f;            // particle intensity
  int   palette = 0;             // user palette
  bool  angryTex = false;        // renderer should use the RAGE palette
  int   requestPalette = -1;     // renderer: rebuild texture for this palette (while lids closed)

  // ---- internal ----
  Mood mood = M_BOOT;
  float t = 0;                   // seconds since start
  float moodT = 0;
  Rng rng{0xC0FFEE};
  float tgtX = 0, tgtY = 0;
  float nextSaccade = 1.f;
  float microT = 0, microX = 0, microY = 0;
  float blink = 1.f;             // 1 = fully closed
  float blinkClock = -1;         // <0: not blinking
  float nextBlink = 3.f;
  bool  doubleBlink = false;
  float blinkGap = 0;
  float dilNoise = 0, dilNoiseT = 0;
  float tapReact = 0;            // decays after a tap
  float waveClock = 0;
  float nextIdleWave = 3.f;
  float nextSus = 25.f;
  float nextGlitch = 9.f;
  float glitchClock = 0;
  // touch tracking
  bool  wasDown = false;
  float downTime = 0;
  int   downX = 0, downY = 0;
  float tapTimes[6] = {-9, -9, -9, -9, -9, -9};
  int   tapIdx = 0;
  int   holdCycles = 0;
  float lastTouch = 0;
  int   pendingPalette = -1;     // switch when lids are closed
  float susPendingAt = -1;       // double-tap detected, waiting to be sure no 3rd tap comes
  float susSide = 1.f;           // which way the side-eye looks
  bool  pendingAngryTex = false;
  bool  hasPendingAngry = false;

  // smoothed mood targets
  float sOpenU = 0, sOpenL = 0, sSlant = 0, sDil = 0.55f, sSlit = 0, sVeins = 0.35f,
        sAura = 0.6f, sRotSpeed = 6.f, sTwist = 0, sEmbers = 1.f;

  void startBlink(bool dbl = false) {
    if (blinkClock >= 0) return;
    blinkClock = 0; doubleBlink = dbl;
  }
  void fireWave(float amp) { waveClock = 0; waveAmp = amp; }

  static float approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * (1.f - expf(-rate * dt));
  }

  void lookAtScreen(int sx, int sy) {
    tgtX = clampf((sx - EYE_CX) * 0.62f, -LOOK_MAX_X, LOOK_MAX_X);
    tgtY = clampf((sy - EYE_CY) * 0.50f, -LOOK_MAX_Y, LOOK_MAX_Y);
  }

  void setMood(Mood m) { mood = m; moodT = 0; }

  void triggerAngry() {
    if (mood != M_ANGRY) {
      setMood(M_ANGRY);
      pendingAngryTex = true; hasPendingAngry = true;
      startBlink();
    } else {
      moodT = 0;  // extend
    }
    glitchClock = 0.45f;
    fireWave(1.f);
  }

  void cyclePalette() {
    pendingPalette = (palette + 1) % NUM_PALETTES;
    startBlink();
  }

  // ------------------------------------------------------------------
  void update(float dt, const TouchInput& in) {
    if (dt > 0.1f) dt = 0.1f;
    t += dt; moodT += dt;

    // ================= touch handling =================
    if (in.down && !wasDown) {                     // finger down
      downTime = 0; downX = in.x; downY = in.y; holdCycles = 0;
      susPendingAt = -1;                           // more taps coming: not a double-tap
      lastTouch = t;
      if (mood != M_BOOT) {
        lookAtScreen(in.x, in.y);
        tapReact = 1.f;
        fireWave(0.8f);
        if (mood == M_SUS) setMood(M_IDLE);
      }
    }
    if (in.down) {
      downTime += dt; lastTouch = t;
      if (mood != M_BOOT) {
        lookAtScreen(in.x, in.y);
        if (downTime > 0.6f && mood != M_STARE && mood != M_ANGRY) setMood(M_STARE);
        if (mood == M_STARE) {
          int c = (int)((downTime - 0.6f) / 7.4f);   // every ~8 s of holding -> next palette
          if (c > holdCycles) { holdCycles = c; cyclePalette(); }
        }
      }
    }
    if (!in.down && wasDown) {                     // finger up
      if (mood != M_BOOT) {
        if (downTime < 0.35f) {                    // a tap
          tapTimes[tapIdx] = t; tapIdx = (tapIdx + 1) % 6;
          int recent = 0, quick = 0;
          for (int i = 0; i < 6; i++) {
            if (t - tapTimes[i] < 1.6f) recent++;
            if (t - tapTimes[i] < 0.6f) quick++;
          }
          if (recent >= 4) { triggerAngry(); susPendingAt = -1; }
          else if (quick == 2 && mood != M_ANGRY) { susPendingAt = t + 0.45f; susSide = (downX < EYE_CX) ? 1.f : -1.f; }
          else if (mood != M_ANGRY) startBlink();
        }
        if (mood == M_STARE) { setMood(M_IDLE); startBlink(); nextSaccade = 0.6f; }
      }
    }
    wasDown = in.down;

    // ================= mood logic =================
    float tOpenU = 0.88f, tOpenL = 0.92f, tSlant = 0, tDil = 0.36f, tSlit = 0, tVeins = 0.35f,
          tAura = 0.62f, tRot = 5.f, tTwist = 0, tEmb = 1.f;
    bool allowBlink = true, allowSaccade = true;

    switch (mood) {
      case M_BOOT: {
        // 0.0-0.9 closed + glitch, 0.9-2.2 slow open, look left, right, then at you
        allowSaccade = false; allowBlink = false;
        float op = smooth01(0.9f, 2.4f, moodT);
        tOpenU = 0.88f * op; tOpenL = 0.92f * op;
        tDil = moodT < 3.4f ? 0.56f : 0.30f;
        tAura = 0.25f + 0.6f * op;
        tEmb = op;
        glitch = (moodT < 0.9f) ? 0.7f : 0.f;
        if (moodT < 2.4f) { tgtX = 0; tgtY = 0; }
        else if (moodT < 2.9f) { tgtX = -LOOK_MAX_X * 0.9f; tgtY = 4; }
        else if (moodT < 3.4f) { tgtX = LOOK_MAX_X * 0.9f; tgtY = 4; }
        else { tgtX = 0; tgtY = 0; }
        if (moodT > 3.4f && moodT - dt <= 3.4f) fireWave(1.f);
        blink = 0; blinkClock = -1;
        if (moodT > 4.3f) { setMood(M_IDLE); nextSaccade = 0.8f; nextBlink = 2.f; }
        break;
      }
      case M_IDLE:
        if (t > nextSus && t - lastTouch > 8.f) { susSide = (rng.f() < 0.5f) ? 1.f : -1.f; setMood(M_SUS); }
        break;
      case M_SUS: {               // suspicious side-eye
        allowSaccade = false;
        tOpenU = 0.52f; tOpenL = 0.72f; tDil = 0.30f;
        tgtX = susSide * LOOK_MAX_X; tgtY = 6;
        if (moodT > 3.0f) { setMood(M_IDLE); nextSus = t + rng.range(22.f, 45.f); startBlink(); lastTouch = t; }
        break;
      }
      case M_STARE:
        allowSaccade = false; allowBlink = false;
        tOpenU = 1.13f; tOpenL = 1.08f; tDil = 0.17f; tVeins = 0.6f; tAura = 1.0f; tRot = 3.f;
        break;
      case M_ANGRY:
        allowSaccade = false;
        tOpenU = 0.50f; tOpenL = 0.76f; tSlant = 1.f; tDil = 0.30f; tSlit = 1.f; tVeins = 1.f;
        tAura = 1.25f; tRot = 26.f; tTwist = 40.f; tEmb = 2.2f;
        shake = moodT < 0.7f ? 5.f * (1.f - moodT / 0.7f) : 0.f;
        if (fmodf(moodT, 0.7f) < dt) fireWave(1.f);
        if (moodT > 4.2f) {
          setMood(M_IDLE);
          pendingAngryTex = false; hasPendingAngry = true;
          startBlink(); nextSaccade = 0.5f;
        }
        break;
    }
    if (mood != M_ANGRY) shake = approach(shake, 0, 10, dt);
    if (susPendingAt > 0 && t > susPendingAt) {    // double-tap -> side-eye
      susPendingAt = -1;
      if (mood == M_IDLE || mood == M_STARE) setMood(M_SUS);
    }

    // tap reaction: quick pupil constriction
    if (tapReact > 0) {
      tDil = fminf(tDil, lerpf(tDil, 0.22f, tapReact));
      tapReact -= dt * 1.4f;
    }

    // ================= saccades =================
    if (allowSaccade && t > nextSaccade && (t - lastTouch > 1.2f)) {
      float r = rng.f();
      if (r < 0.55f) { tgtX = rng.range(-0.35f, 0.35f) * LOOK_MAX_X; tgtY = rng.range(-0.35f, 0.4f) * LOOK_MAX_Y; }
      else if (r < 0.85f) { tgtX = rng.range(-1.f, 1.f) * LOOK_MAX_X; tgtY = rng.range(-0.8f, 0.8f) * LOOK_MAX_Y; }
      else { tgtX = 0; tgtY = 0; }                                   // stare at the viewer
      nextSaccade = t + rng.range(0.45f, 2.6f);
      if (rng.f() < 0.12f) startBlink();                            // people often blink with big saccades
    }
    microT -= dt;
    if (microT <= 0) { microT = rng.range(0.12f, 0.35f); microX = rng.range(-1.6f, 1.6f); microY = rng.range(-1.2f, 1.2f); }
    float rate = (mood == M_SUS) ? 3.5f : 28.f;
    lookX = approach(lookX, tgtX + microX, rate, dt);
    lookY = approach(lookY, tgtY + microY, rate, dt);

    // ================= blinking =================
    if (allowBlink && blinkClock < 0 && t > nextBlink && mood != M_BOOT) {
      startBlink(rng.f() < 0.18f);
    }
    if (blinkClock >= 0) {
      blinkClock += dt;
      const float closeT = 0.075f, holdT = 0.05f, openT = 0.14f;
      float bc = blinkClock;
      if (bc < closeT) blink = bc / closeT;
      else if (bc < closeT + holdT) {
        blink = 1.f;
        // swap textures while the lids are shut
        if (pendingPalette >= 0) { palette = pendingPalette; requestPalette = palette; pendingPalette = -1; }
        if (hasPendingAngry) { angryTex = pendingAngryTex; hasPendingAngry = false; }
      }
      else if (bc < closeT + holdT + openT) blink = 1.f - (bc - closeT - holdT) / openT;
      else {
        blink = 0; blinkClock = -1;
        if (doubleBlink) { doubleBlink = false; blinkGap = 0.09f; }
        nextBlink = t + rng.range(2.2f, 6.0f);
      }
    }
    // short gap before the second blink of a double blink
    if (blinkGap > 0) {
      blinkGap -= dt;
      if (blinkGap <= 0) startBlink(false);
    }
    // palette / rage swaps are hidden behind a blink
    if ((hasPendingAngry || pendingPalette >= 0) && blinkClock < 0 && blinkGap <= 0 && mood != M_BOOT) startBlink();

    // ================= smooth everything =================
    sOpenU = approach(sOpenU, tOpenU, 11.f, dt);
    sOpenL = approach(sOpenL, tOpenL, 11.f, dt);
    sSlant = approach(sSlant, tSlant, 9.f, dt);
    sDil = approach(sDil, tDil, 6.f, dt);
    sSlit = approach(sSlit, tSlit, 7.f, dt);
    sVeins = approach(sVeins, tVeins, 4.f, dt);
    sAura = approach(sAura, tAura, 4.f, dt);
    sRotSpeed = approach(sRotSpeed, tRot, 3.f, dt);
    sTwist = approach(sTwist, tTwist, 3.f, dt);
    sEmbers = approach(sEmbers, tEmb, 2.f, dt);

    // breathing pupil
    dilNoiseT -= dt;
    if (dilNoiseT <= 0) { dilNoiseT = rng.range(0.6f, 1.8f); dilNoise = rng.range(-0.035f, 0.035f); }
    float breathe = 0.022f * sinf(t * 1.3f);

    openU = sOpenU * (1.f - blink);
    openL = sOpenL * (1.f - blink);
    slant = sSlant;
    dil = clampf(sDil + breathe + dilNoise * (mood == M_IDLE ? 1.f : 0.3f), 0.12f, 0.62f);
    slit = sSlit;
    veins = sVeins;
    aura = sAura;
    twist = sTwist;
    embers = sEmbers;
    rot += sRotSpeed * dt;
    if (rot >= 256.f) rot -= 256.f;

    // ================= idle pulse rings =================
    if (mood == M_IDLE && t > nextIdleWave) { fireWave(0.45f); nextIdleWave = t + rng.range(2.5f, 5.0f); }
    if (waveClock >= 0) {
      waveClock += dt;
      waveT = waveClock / 0.85f;
      if (waveT > 1.15f) { waveT = -1; waveClock = -1; }
    }

    // ================= glitch bursts =================
    if (mood != M_BOOT) {
      if (t > nextGlitch) { glitchClock = rng.range(0.08f, 0.22f); nextGlitch = t + rng.range(7.f, 16.f); }
      if (glitchClock > 0) { glitchClock -= dt; glitch = (mood == M_ANGRY) ? 1.f : 0.6f; }
      else glitch = 0;
    }
  }
};

// ---------------------------------------------------------------------
//  RENDERER
// ---------------------------------------------------------------------
struct Ember {
  float x, y, vx, vy, life, age, size, phase;
};

class Renderer {
public:
  bool swapBytes = false;      // true on the ESP32 (SPI wants big-endian)

  bool hasVeins() const { return veinMap_ != nullptr; }

  bool begin(uint32_t seed = 0xA11CE) {
    seed_ = seed;
    irisTex_ = (uint16_t*)THIRDEYE_ALLOC(256 * 128 * 2);
    angryTex_ = (uint16_t*)THIRDEYE_ALLOC(256 * 128 * 2);
    const int L = IRIS_R + 2;
    rLut_ = (uint16_t*)THIRDEYE_ALLOC(L * L * 2);
    aLut_ = (uint8_t*)THIRDEYE_ALLOC(L * L);
    veinW_ = (int)(EYE_HW * 2) + 8;
    veinH_ = (int)(EYE_UP * 1.2f + EYE_DN * 1.15f) + 8;
    veinMap_ = (uint8_t*)THIRDEYE_ALLOC_BIG(veinW_ * veinH_);   // optional: no veins if this fails
    if (!irisTex_ || !angryTex_ || !rLut_ || !aLut_) return false;

    // quadrant lookup: radius (quarter pixels) and angle (0..64 for 0..90deg)
    for (int y = 0; y < L; y++)
      for (int x = 0; x < L; x++) {
        float r = sqrtf((float)(x * x + y * y));
        rLut_[y * L + x] = (uint16_t)(r * 4.f + 0.5f);
        float a = atan2f((float)y, (float)x) * (64.f / 1.5707963f);
        aLut_[y * L + x] = (uint8_t)clampi((int)(a + 0.5f), 0, 64);
      }
    buildIrisTexture(irisTex_, PALETTES[0], seed_);
    buildIrisTexture(angryTex_, PALETTES[PAL_ANGRY], seed_);
    if (veinMap_) buildVeins();
    Rng r(seed_ ^ 0x55);
    for (int i = 0; i < MAX_EMBERS; i++) spawnEmber(embers_[i], r, true);
    erng_ = Rng(seed_ ^ 0x77);
    return true;
  }

  // rebuild the normal iris texture for another palette (~100 ms on ESP32)
  void setPalette(int p) {
    pal_ = clampi(p, 0, NUM_PALETTES - 1);
    buildIrisTexture(irisTex_, PALETTES[pal_], seed_);
  }
  int palette() const { return pal_; }

  // ------------------------------------------------------------------
  //  call once per frame before the bands
  // ------------------------------------------------------------------
  void beginFrame(const Brain& b, float dt) {
    time_ += dt;
    frame_++;
    const Palette& P = b.angryTex ? PALETTES[PAL_ANGRY] : PALETTES[pal_];
    curPal_ = &P;
    tex_ = b.angryTex ? angryTex_ : irisTex_;

    float shx = 0, shy = 0;
    if (b.shake > 0.05f) {
      shx = (hash32(frame_ * 3u + 1u) % 1000 / 500.f - 1.f) * b.shake;
      shy = (hash32(frame_ * 7u + 5u) % 1000 / 500.f - 1.f) * b.shake;
    }
    cx_ = EYE_CX + shx; cy_ = EYE_CY + shy;
    eyeL_ = cx_ - EYE_HW; eyeR_ = cx_ + EYE_HW;

    // ----- lids per column -----
    topMin_ = 1e9f; botMax_ = -1e9f;
    for (int x = 0; x < SW; x++) {
      float u = (x - cx_) / EYE_HW;
      if (u <= -1.f || u >= 1.f) { lidU_[x] = 1e9f; lidL_[x] = -1e9f; continue; }
      float q = 1.f - u * u;
      float f = powf(q, 0.72f);
      float g = powf(q, 0.95f);
      float closedY = cy_ + 24.f * g;
      float topY = cy_ - EYE_UP * f;
      float botY = cy_ + EYE_DN * g;
      float yu = closedY - (closedY - topY) * b.openU;
      float yl = closedY + (botY - closedY) * b.openL;
      // angry: slanted, heavy upper lid
      yu += b.slant * (u * 26.f + 14.f * q) * clampf(b.openU * 1.6f, 0.f, 1.f);
      if (yu > yl) yu = yl;
      lidU_[x] = yu; lidL_[x] = yl;
      if (yu < topMin_) topMin_ = yu;
      if (yl > botMax_) botMax_ = yl;
    }
    // corner points for the aura outside the eye (follow the real lid corners)
    {
      int xl = clampi((int)ceilf(eyeL_ + 0.01f), 0, SW - 1), xr = clampi((int)floorf(eyeR_ - 0.01f), 0, SW - 1);
      while (xl < SW - 1 && lidU_[xl] > 1e8f) xl++;
      while (xr > 0 && lidU_[xr] > 1e8f) xr--;
      cornerYL_ = 0.5f * (lidU_[xl] + lidL_[xl]);
      cornerYR_ = 0.5f * (lidU_[xr] + lidL_[xr]);
    }

    // ----- aura lut -----
    float pulse = 0.86f + 0.14f * sinf(time_ * 2.3f);
    if (b.aura > 1.05f) pulse *= 0.85f + 0.3f * (hash32(frame_) % 100) / 100.f;   // flicker when angry
    uint16_t auraC = pack565(P.aura);
    for (int d = 0; d < AURA_MAX; d++) {
      float edge = 1.f - (float)d / AURA_MAX; edge *= edge;
      float k = b.aura * pulse * (0.60f * expf(-d / 11.f) + 0.30f * expf(-d / 38.f)) * edge;
      auraLut_[d] = scale565(auraC, (int)(clampf(k, 0.f, 1.f) * 256.f));
    }
    RGB rimC = mixRGB(P.aura, RGB{255, 255, 255}, 0.45f);
    rim565_ = pack565(rimC);
    core565_ = pack565(P.core);
    sclera565_ = pack565(P.sclera);
    scR_ = P.sclera.r; scG_ = P.sclera.g; scB_ = P.sclera.b;
    auraFull565_ = pack565(P.aura);
    vein565_ = pack565(P.vein);
    pupil565_ = pack565(P.limbus.r / 3, P.limbus.g / 3, P.limbus.b / 3);
    darkSclera_ = P.darkSclera;
    veinStrength_ = (int)(b.veins * (darkSclera_ ? 330.f : 210.f));

    // ----- iris + pupil -----
    icx_ = (int)lroundf(cx_ + b.lookX);
    icy_ = (int)lroundf(cy_ + b.lookY - 4.f);
    const float R = (float)IRIS_R;
    float pw = R * lerpf(b.dil, 0.085f, b.slit);
    float ph = R * lerpf(b.dil, 0.80f, b.slit);
    for (int a = 0; a < 256; a++) {
      float th = a * (6.2831853f / 256.f);
      float c = cosf(th), s = sinf(th);
      float rp = (pw * ph) / sqrtf((ph * c) * (ph * c) + (pw * s) * (pw * s));
      int rp4 = (int)(rp * 4.f);
      if (rp4 > IRIS_R * 4 - 8) rp4 = IRIS_R * 4 - 8;
      rpA_[a] = (uint16_t)rp4;
      invA_[a] = (int32_t)((127 << 16) / (IRIS_R * 4 - rp4));
    }
    rotI_ = (int)b.rot;
    twistI_ = (int)b.twist;
    waveOn_ = b.waveT >= 0.f && b.waveT <= 1.1f;
    waveT_ = (int)(b.waveT * 140.f) - 6;
    waveAmp_ = (int)(b.waveAmp * 230.f * clampf(1.15f - b.waveT, 0.f, 1.f));

    // highlights (on the cornea, ride along with the iris)
    h1x_ = icx_ - 0.34f * R; h1y_ = icy_ - 0.38f * R; h1r2_ = (0.17f * R) * (0.17f * R);
    h2x_ = icx_ + 0.30f * R; h2y_ = icy_ + 0.32f * R; h2r2_ = (0.065f * R) * (0.065f * R);

    // ----- embers -----
    emberCount_ = clampi((int)(MAX_EMBERS * 0.65f * b.embers), 0, MAX_EMBERS);
    for (int i = 0; i < emberCount_; i++) {
      Ember& e = embers_[i];
      e.age += dt;
      float spd = 1.f + (b.embers > 1.5f ? 1.2f : 0.f);
      e.y += e.vy * dt * spd;
      e.x += (e.vx + 9.f * sinf(time_ * 1.7f + e.phase)) * dt * spd;
      if (e.age > e.life || e.y < -6) spawnEmber(e, erng_, false);
    }
    emberC1_ = pack565(P.aura);
    emberC2_ = pack565(P.core);

    glitch_ = b.glitch;
    glitchSeed_ = hash32(frame_ / 2 + 99u);
  }

  // ------------------------------------------------------------------
  //  render rows [y0, y0+h) into dst (SW*h pixels)
  // ------------------------------------------------------------------
  void renderBand(uint16_t* dst, int y0, int h) {
    for (int row = 0; row < h; row++) {
      int y = y0 + row;
      uint16_t* line = dst + row * SW;
      renderRow(line, y);
      drawEmbersRow(line, y);
      if (glitch_ > 0.01f) glitchRow(line, y);
      if (swapBytes) for (int x = 0; x < SW; x++) line[x] = (uint16_t)((line[x] >> 8) | (line[x] << 8));
    }
  }

private:
  static const int MAX_EMBERS = 46;
  uint32_t seed_ = 1, frame_ = 0;
  float time_ = 0;
  int pal_ = 0;
  const Palette* curPal_ = &PALETTES[0];
  uint16_t *irisTex_ = nullptr, *angryTex_ = nullptr, *tex_ = nullptr;
  uint16_t* rLut_ = nullptr;
  uint8_t* aLut_ = nullptr;
  uint8_t* veinMap_ = nullptr;
  int veinW_ = 0, veinH_ = 0;
  float cx_ = EYE_CX, cy_ = EYE_CY, eyeL_ = 0, eyeR_ = 0, cornerYL_ = 0, cornerYR_ = 0;
  float lidU_[SW], lidL_[SW];
  float topMin_ = 0, botMax_ = 0;
  uint16_t auraLut_[AURA_MAX];
  uint16_t rim565_ = 0, core565_ = 0, sclera565_ = 0, vein565_ = 0, pupil565_ = 0;
  bool darkSclera_ = false;
  int scR_ = 0, scG_ = 0, scB_ = 0;
  uint16_t auraFull565_ = 0;
  int veinStrength_ = 0;
  int icx_ = 240, icy_ = 160;
  uint16_t rpA_[256];
  int32_t invA_[256];
  int rotI_ = 0, twistI_ = 0;
  bool waveOn_ = false;
  int waveT_ = 0, waveAmp_ = 0;
  float h1x_ = 0, h1y_ = 0, h1r2_ = 1, h2x_ = 0, h2y_ = 0, h2r2_ = 1;
  Ember embers_[MAX_EMBERS];
  int emberCount_ = 0;
  Rng erng_;
  uint16_t emberC1_ = 0, emberC2_ = 0;
  float glitch_ = 0;
  uint32_t glitchSeed_ = 0;

  // ---------------- iris texture ----------------
  void buildIrisTexture(uint16_t* tex, const Palette& P, uint32_t seed) {
    Rng r(seed);
    const int NF = 7;
    float fk[NF], fp[NF], fs[NF], fw[NF];
    for (int i = 0; i < NF; i++) {
      fk[i] = (float)(int)r.range(14.f, 96.f);          // integer => wraps around cleanly
      fp[i] = r.range(0.f, 6.283f);
      fs[i] = r.range(-5.f, 5.f);
      fw[i] = r.range(0.5f, 1.f);
    }
    float lk[3] = {3.f, 5.f, 8.f}, lp[3];
    for (int i = 0; i < 3; i++) lp[i] = r.range(0.f, 6.283f);
    const int NC = 14;                                   // dark crypts
    float ca[NC], cu[NC], cs[NC];
    for (int i = 0; i < NC; i++) { ca[i] = r.range(0.f, 6.283f); cu[i] = r.range(0.32f, 0.78f); cs[i] = r.range(0.03f, 0.075f); }
    float cp1 = r.range(0.f, 6.283f), cp2 = r.range(0.f, 6.283f);

    for (int a = 0; a < 256; a++) {
      float th = a * (6.2831853f / 256.f);
      float low = 0;
      for (int i = 0; i < 3; i++) low += sinf(lk[i] * th + lp[i]);
      low = low / 3.f;                                   // -1..1
      float collar = 0.31f + 0.035f * sinf(7.f * th + cp1) + 0.025f * sinf(13.f * th + cp2);
      for (int ti = 0; ti < 128; ti++) {
        float u = ti / 127.f;
        // fibres (radial streaks, slightly curved)
        float fsum = 0, wsum = 0;
        for (int i = 0; i < NF; i++) { fsum += fw[i] * sinf(fk[i] * th + fp[i] + u * fs[i]); wsum += fw[i]; }
        float fib = 0.5f + 0.5f * (fsum / wsum);         // 0..1
        fib = fib * fib * (3.f - 2.f * fib);
        // base gradient
        RGB c;
        if (u < 0.32f) c = mixRGB(P.core, P.mid, smooth01(0.0f, 0.32f, u));
        else c = mixRGB(P.mid, P.outer, smooth01(0.32f, 0.88f, u));
        float br = 0.50f + 0.62f * fib + 0.10f * low;
        // collarette ring
        float dc = (u - collar) / 0.028f;
        br += 0.45f * expf(-dc * dc);
        // inner fire ring
        br += 0.55f * expf(-(u / 0.06f) * (u / 0.06f));
        // crypts
        for (int i = 0; i < NC; i++) {
          float da = th - ca[i];
          while (da > 3.14159f) da -= 6.2831853f;
          while (da < -3.14159f) da += 6.2831853f;
          float d2 = (da * 0.55f) * (da * 0.55f) + (u - cu[i]) * (u - cu[i]);
          br -= 0.55f * expf(-d2 / (cs[i] * cs[i]));
        }
        br = clampf(br, 0.08f, 1.55f);
        int R8 = (int)(c.r * br), G8 = (int)(c.g * br), B8 = (int)(c.b * br);
        // over-bright pushes toward white
        if (br > 1.f) {
          float w = (br - 1.f) * 0.6f;
          R8 += (int)((255 - c.r) * w); G8 += (int)((255 - c.g) * w); B8 += (int)((255 - c.b) * w);
        }
        // pupillary ruff (thin dark edge right at the pupil)
        if (u < 0.018f) { R8 = R8 / 3; G8 = G8 / 3; B8 = B8 / 3; }
        // limbal ring
        float lim = smooth01(0.80f, 1.0f, u);
        R8 = (int)lerpf(R8, P.limbus.r, lim); G8 = (int)lerpf(G8, P.limbus.g, lim); B8 = (int)lerpf(B8, P.limbus.b, lim);
        tex[a * 128 + ti] = pack565(R8, G8, B8);
      }
    }
  }

  // ---------------- veins ----------------
  void splat(float px, float py, float rad, float inten) {
    int x0 = (int)(px - rad - 1), x1 = (int)(px + rad + 1);
    int y0 = (int)(py - rad - 1), y1 = (int)(py + rad + 1);
    for (int y = y0; y <= y1; y++) {
      if (y < 0 || y >= veinH_) continue;
      for (int x = x0; x <= x1; x++) {
        if (x < 0 || x >= veinW_) continue;
        float d = sqrtf((x - px) * (x - px) + (y - py) * (y - py));
        float k = 1.f - d / (rad + 0.8f);
        if (k <= 0) continue;
        int v = (int)(k * inten * 255.f);
        uint8_t& m = veinMap_[y * veinW_ + x];
        if (v > m) m = (uint8_t)clampi(v, 0, 255);
      }
    }
  }
  void growVein(Rng& r, float x, float y, float dir, float width, float len, int depth) {
    float steps = len / 1.4f;
    for (int i = 0; i < (int)steps; i++) {
      float k = 1.f - i / steps;
      splat(x, y, width * (0.35f + 0.65f * k), 0.55f + 0.45f * k);
      dir += r.range(-0.32f, 0.32f);
      x += cosf(dir) * 1.4f; y += sinf(dir) * 1.4f;
      if (depth < 3 && r.f() < 0.035f) growVein(r, x, y, dir + r.range(-0.9f, 0.9f), width * 0.65f, len * k * 0.6f, depth + 1);
    }
  }
  void buildVeins() {
    memset(veinMap_, 0, veinW_ * veinH_);
    Rng r(seed_ ^ 0xBEEF);
    // local coords: x 0..veinW (eye left..right), y with eye centre at oy
    float ox = 4.f, oy = EYE_UP * 1.2f + 4.f;
    for (int i = 0; i < 30; i++) {
      float side = (i & 1) ? 1.f : -1.f;
      float sx, sy, dir;
      if (i < 20) {                         // from the corners
        sx = ox + EYE_HW + side * EYE_HW * r.range(0.82f, 0.98f);
        sy = oy + r.range(-30.f, 30.f);
        dir = (side > 0 ? 3.14159f : 0.f) + r.range(-0.6f, 0.6f);
      } else {                              // from under the lids
        sx = ox + EYE_HW + r.range(-0.75f, 0.75f) * EYE_HW;
        bool top = r.f() < 0.5f;
        sy = oy + (top ? -EYE_UP * 0.85f : EYE_DN * 0.85f);
        dir = (top ? 1.5708f : -1.5708f) + r.range(-0.7f, 0.7f);
      }
      growVein(r, sx, sy, dir, r.range(1.0f, 1.9f), r.range(45.f, 130.f), 0);
    }
    veinOX_ = ox; veinOY_ = oy;
  }
  float veinOX_ = 0, veinOY_ = 0;

  // ---------------- embers ----------------
  void spawnEmber(Ember& e, Rng& r, bool anywhere) {
    e.x = r.range(EYE_CX - EYE_HW - 30.f, EYE_CX + EYE_HW + 30.f);
    e.y = anywhere ? r.range(0.f, (float)SH) : r.range((float)SH * 0.55f, (float)SH + 6.f);
    e.vx = r.range(-6.f, 6.f);
    e.vy = r.range(-38.f, -12.f);
    e.life = r.range(2.f, 5.5f);
    e.age = anywhere ? r.range(0.f, e.life) : 0.f;
    e.size = r.range(1.0f, 2.8f);
    e.phase = r.range(0.f, 6.28f);
  }

  // ---------------- one row of the eye ----------------
  inline bool insideOpening(int x, float y) const {
    return y > lidU_[x] && y < lidL_[x];
  }

  void renderRow(uint16_t* line, int y) {
    const float fy = (float)y + 0.5f;
    // whole row far from the eye -> black
    if (fy < topMin_ - AURA_MAX || fy > botMax_ + AURA_MAX) { memset(line, 0, SW * 2); return; }

    const int R4 = IRIS_R * 4;
    const int L = IRIS_R + 2;
    const int dyI = y - icy_;
    const int ady = dyI < 0 ? -dyI : dyI;
    const bool irisRow = ady <= IRIS_R;
    const int vy = (int)(fy - cy_ + veinOY_);

    for (int x = 0; x < SW; x++) {
      const float fu = lidU_[x], fl = lidL_[x];
      const bool inCols = fu < 1e8f;
      // -------- outside the opening: rim + aura --------
      if (!inCols || fy <= fu || fy >= fl) {
        float d;
        if (inCols) {
          d = (fy <= fu) ? (fu - fy) : (fy - fl);
          if (fu >= fl - 0.01f) d = fabsf(fy - fu);       // eye shut: distance to the lid line
        } else {
          float ddx = (x < cx_) ? (eyeL_ - x) : (x - eyeR_);
          float ddy = fy - ((x < cx_) ? cornerYL_ : cornerYR_);
          d = sqrtf(ddx * ddx + ddy * ddy);
        }
        uint16_t c;
        if (d < 2.4f) {
          c = rim565_;
          if (d > 1.4f) c = blend565(rim565_, auraLut_[2], (int)((2.4f - d) * 256.f));
        } else {
          int di = (int)d;
          c = di < AURA_MAX ? auraLut_[di] : 0;
        }
        line[x] = c;
        continue;
      }

      // -------- inside the opening --------
      const float dTop = fy - fu, dBot = fl - fy;
      uint16_t c;
      bool isIris = false;
      const int dxI = x - icx_;
      const int adx = dxI < 0 ? -dxI : dxI;
      int r4 = 0x7FFF;
      if (irisRow && adx <= IRIS_R + 1) r4 = rLut_[ady * L + adx];

      // sclera colour (also needed for the iris edge)
      uint16_t sc;
      {
        float u = (x - cx_) / EYE_HW;
        int shade = 256 - (int)(u * u * 120.f);
        int vx = (int)(x - cx_ + EYE_HW + veinOX_);
        int v = 0;
        if (veinMap_ && vx >= 0 && vx < veinW_ && vy >= 0 && vy < veinH_) v = veinMap_[vy * veinW_ + vx];
        int dth = BAYER4[y & 3][x & 3];
        if (darkSclera_) {
          sc = sclera565_;
          if (v) sc = add565(sc, vein565_, (v * veinStrength_) >> 8);
          // eclipse glow around the iris
          if (adx <= IRIS_R + 18 && ady <= IRIS_R + 18) {
            float dd = sqrtf((float)(dxI * dxI + dyI * dyI)) - IRIS_R;
            if (dd > -1.f && dd < 18.f) sc = add565(sc, auraFull565_, (int)((1.f - dd / 18.f) * (1.f - dd / 18.f) * 200.f));
          }
        } else {
          int sr = (scR_ * shade) >> 8, sg = (scG_ * shade) >> 8, sb = (scB_ * shade) >> 8;
          sc = pack565(sr + (dth >> 1), sg + (dth >> 2), sb + (dth >> 1));
          if (v) sc = blend565(vein565_, sc, (v * veinStrength_) >> 8);
        }
      }

      if (r4 < R4 + 4) {
        isIris = true;
        int aq = aLut_[ady * L + adx];
        int a;
        if (dxI >= 0) a = (dyI >= 0) ? aq : (256 - aq);
        else a = (dyI >= 0) ? (128 - aq) : (128 + aq);
        a &= 255;
        int rp4 = rpA_[a];
        if (r4 < rp4 - 3) {
          c = pupil565_;
        } else {
          int t = ((r4 - rp4) * invA_[a]) >> 16;
          if (t < 0) t = 0;
          if (t > 127) t = 127;
          int ar = (a + rotI_ + ((t * twistI_) >> 7)) & 255;
          c = tex_[ar * 128 + t];
          if (waveOn_) {
            int dd = t - waveT_;
            if (dd < 0) dd = -dd;
            if (dd < 12) c = add565(c, core565_, (waveAmp_ * (12 - dd)) / 12);
          }
          if (r4 < rp4 + 1) c = blend565(c, pupil565_, ((r4 - rp4 + 3) * 256) >> 2);   // AA pupil edge
        }
        if (r4 > R4 - 4) c = blend565(sc, c, ((r4 - (R4 - 4)) * 256) >> 3);          // AA iris edge
      } else {
        c = sc;
      }

      // specular highlights
      {
        float hx = x - h1x_, hy = (fy - h1y_) * 1.25f;
        float d2 = hx * hx + hy * hy;
        if (d2 < h1r2_) c = blend565(0xFFFF, c, (int)((1.f - d2 / h1r2_) * 300.f));
        hx = x - h2x_; hy = fy - h2y_;
        d2 = hx * hx + hy * hy;
        if (d2 < h2r2_) c = blend565(0xFFFF, c, (int)((1.f - d2 / h2r2_) * 420.f));
      }
      (void)isIris;

      // lid shadows
      if (dTop < 18.f) c = scale565(c, 120 + (int)(dTop * 7.5f));
      if (dBot < 9.f) c = scale565(c, 170 + (int)(dBot * 9.5f));
      // glowing rim, anti-aliased on the inside
      if (dTop < 1.4f) c = blend565(rim565_, c, (int)((1.4f - dTop) * 182.f));
      if (dBot < 1.4f) c = blend565(rim565_, c, (int)((1.4f - dBot) * 182.f));
      line[x] = c;
    }
  }

  void drawEmbersRow(uint16_t* line, int y) {
    for (int i = 0; i < emberCount_; i++) {
      const Ember& e = embers_[i];
      float dy = (y + 0.5f) - e.y;
      if (dy < -e.size - 1 || dy > e.size + 1) continue;
      float life = e.age / e.life;
      float fade = clampf(life * 4.f, 0.f, 1.f) * clampf((1.f - life) * 3.f, 0.f, 1.f);
      uint16_t col = blend565(emberC2_, emberC1_, (int)((1.f - life) * 200.f));
      int x0 = (int)(e.x - e.size - 1), x1 = (int)(e.x + e.size + 1);
      for (int x = x0; x <= x1; x++) {
        if (x < 0 || x >= SW) continue;
        if (insideOpening(x, y + 0.5f)) continue;
        float dx = (x + 0.5f) - e.x;
        float d = sqrtf(dx * dx + dy * dy);
        float k = 1.f - d / (e.size + 0.6f);
        if (k <= 0) continue;
        line[x] = add565(line[x], col, (int)(k * fade * 300.f));
      }
    }
  }

  void glitchRow(uint16_t* line, int y) {
    // slices of rows get shifted sideways and tinted
    uint32_t h = hash32((uint32_t)(y / 7) * 2654435761u ^ glitchSeed_);
    if ((h & 255) > (uint32_t)(glitch_ * 70.f)) return;
    int off = (int)((h >> 8) % 41) - 20;
    off = (int)(off * glitch_ * 1.6f);
    if (off > 0) { memmove(line + off, line, (SW - off) * 2); memset(line, 0, off * 2); }
    else if (off < 0) { int o = -off; memmove(line, line + o, (SW - o) * 2); memset(line + SW - o, 0, o * 2); }
    bool tint = (h >> 20) & 1;
    if (tint) for (int x = 0; x < SW; x++) line[x] = (uint16_t)(line[x] & 0xF81F);   // drop green -> magenta tear
  }
};

}  // namespace thirdeye
