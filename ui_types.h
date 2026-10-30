// small structs used by ThirdEye.ino (kept in a header so the Arduino IDE's
// auto-generated function prototypes can see them)
#pragma once
struct Touch { int n = 0; int x[2] = {0, 0}; int y[2] = {0, 0}; };
struct Button { int x, y, w, h; const char* label; };
