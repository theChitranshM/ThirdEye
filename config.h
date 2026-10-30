// =====================================================================
//  THIRD EYE  -  settings you might want to change
//  Board: Freenove ESP32-S3 Display 4.0" (FNK0104S), 320x480 ST7796,
//         FT6336 capacitive touch.  Pins below come from Freenove's own files.
// =====================================================================
#pragma once

// ---------- look ----------
#define START_PALETTE      0      // 0 EMBER, 1 FROST, 2 VOID, 3 PHANTOM  (also changeable by touch)
#define START_BRIGHTNESS   200    // 0-255. Lower = longer battery. 160 is plenty in a dark room.

// ---------- display (only touch these if the screen looks wrong) ----------
#define PANEL_SPI_HZ       80000000   // if you see snow / glitches that aren't part of the show, try 40000000
#define PANEL_INVERT       true       // colours look like a photo negative? flip this
// Colour order (RGB vs BGR) is a touch setting now: hold 2 fingers for 2 s -> SWAP COLORS.

// ---------- behaviour ----------
#define SHOW_FPS_ON_SERIAL true       // prints frames per second + battery to the Serial Monitor
#define BACK_GLOW          40         // RGB LED on the back of the board glows with the eye (0 = off, max 255)

// ---------- pins (Freenove FNK0104S) ----------
#define PIN_LCD_MOSI   11
#define PIN_LCD_SCLK   12
#define PIN_LCD_CS     10
#define PIN_LCD_DC     46
#define PIN_LCD_BL     45
#define PIN_TOUCH_SDA  16
#define PIN_TOUCH_SCL  15
#define PIN_TOUCH_RST  18
#define PIN_TOUCH_INT  17
#define PIN_BATTERY    9      // battery voltage through a 1/2 divider
#define PIN_BACK_LED    42     // WS2812 on the back of the board
