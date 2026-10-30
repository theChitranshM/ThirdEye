// Native preview: runs the exact same renderer as the badge and writes frames.
// g++ -O2 -std=c++17 -I.. preview.cpp -o preview && ./preview out_dir [seconds] [fps]
#include "../eye_render.h"
#include <cstdio>
#include <vector>
#include <string>
#include <chrono>

using namespace thirdeye;

struct Script {   // scripted finger for the demo video
  thirdeye::TouchInput at(float t) {
    thirdeye::TouchInput in;
    auto tap = [&](float t0, int x, int y) { if (t >= t0 && t < t0 + 0.12f) { in.down = true; in.x = x; in.y = y; } };
    tap(6.0f, 90, 150);
    tap(8.0f, 400, 120);
    tap(9.6f, 250, 250);
    // hold + drag = stare (10.5 -> 13.5)
    if (t >= 10.5f && t < 13.5f) { in.down = true; float k = (t - 10.5f) / 3.f; in.x = (int)(120 + 260 * k); in.y = (int)(120 + 70 * k); }
    // 5 rapid taps = rage
    for (int i = 0; i < 5; i++) tap(14.5f + i * 0.22f, 200 + i * 20, 170);
    // double tap = side-eye
    tap(21.0f, 150, 160); tap(21.25f, 150, 160);
    // long holds = palette cycle (each ~8 s hold -> one switch)
    if (t >= 24.0f && t < 32.4f) { in.down = true; in.x = 240; in.y = 160; }
    if (t >= 34.0f && t < 42.4f) { in.down = true; in.x = 300; in.y = 140; }
    if (t >= 44.0f && t < 52.4f) { in.down = true; in.x = 180; in.y = 190; }
    return in;
  }
};

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : "frames";
  float secs = argc > 2 ? atof(argv[2]) : 48.f;
  int fps = argc > 3 ? atoi(argv[3]) : 30;
  Renderer R;
  if (!R.begin()) { printf("alloc failed\n"); return 1; }
  Brain B;
  Script S;
  std::vector<uint16_t> fb(SW * SH);
  std::vector<uint8_t> rgb(SW * SH * 3);
  float dt = 1.f / fps;
  int n = (int)(secs * fps);
  double total = 0;
  for (int f = 0; f < n; f++) {
    float t = f * dt;
    B.update(dt, S.at(t));
    if (B.requestPalette >= 0) { R.setPalette(B.requestPalette); B.requestPalette = -1; }
    auto t0 = std::chrono::high_resolution_clock::now();
    R.beginFrame(B, dt);
    for (int y = 0; y < SH; y += 32) R.renderBand(fb.data() + y * SW, y, 32);
    auto t1 = std::chrono::high_resolution_clock::now();
    total += std::chrono::duration<double, std::milli>(t1 - t0).count();
    for (int i = 0; i < SW * SH; i++) {
      uint16_t c = fb[i];
      rgb[i * 3 + 0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
      rgb[i * 3 + 1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
      rgb[i * 3 + 2] = (uint8_t)((c & 31) * 255 / 31);
    }
    char name[512];
    snprintf(name, sizeof name, "%s/f%05d.ppm", out.c_str(), f);
    FILE* fp = fopen(name, "wb");
    fprintf(fp, "P6\n%d %d\n255\n", SW, SH);
    fwrite(rgb.data(), 1, rgb.size(), fp);
    fclose(fp);
  }
  printf("frames=%d avg render %.2f ms/frame (laptop)\n", n, total / n);
  return 0;
}
