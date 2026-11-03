#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "GBAWeb.h"
#include "Core/HW/GBACore.h"
#include "Core/HW/GBAExternalKeys.h"
using clk = std::chrono::steady_clock;
int main(int argc, char** argv)
{
  const int dev = argc > 1 ? atoi(argv[1]) : 0;
  auto core = std::make_shared<HW::GBA::Core>(dev);
  std::shared_ptr<GBAHostInterface> host = Libretro::GBAWeb::CreateHost(core);
  if (!host) { std::puts("HOST NULL"); return 1; }
  std::atomic<bool> run{true}; std::atomic<bool> paused{false}; std::atomic<int> mode{0}; std::atomic<bool> odd_first{false};
  std::atomic<long long> max_us{0}; std::atomic<long long> frames{0};
  std::thread producer([&] {
    std::vector<u32> px(240 * 160);
    auto next = clk::now();
    u32 n = 0;
    while (run) {
      if (paused) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); next = clk::now(); continue; }
      int burst = mode == 1 ? (odd_first.exchange(false) ? 5 : 4) : 1;   // 5 once: seq then stays 1 above a multiple of 4
      for (int b = 0; b < burst; ++b) {
      for (size_t i = 0; i < px.size(); ++i) {
        u32 x = i % 240, y = i / 240; int m = mode;
        auto rgb = [](u32 r, u32 g, u32 b) { return ((r & 31) << 3) | ((g & 31) << 11) | ((b & 31) << 19); };
        if (m <= 2) {   // 0/1 animated (blue = frame counter), 2 still
          px[i] = ((x % 32) << 3) | (((y % 32) << 3) << 8) | (((((m == 2 ? 0 : n) % 32)) << 3) << 16);
        } else if (m == 3) {   // noise: the codec's worst case
          u32 r = (u32)(i * 2654435761u) ^ 0x9E3779B9u; r ^= r >> 15; r *= 2246822519u; r ^= r >> 13;
          px[i] = rgb(r, r >> 5, r >> 10);
        } else if (m == 4) {   // flat UI: header bar, a window with a border, text-like specks, one 32x32 sprite
          u32 c = rgb(3, 5, 12);                                            // background
          if (y < 16) c = rgb(24, 24, 6);                                   // header
          else if (x >= 20 && x < 220 && y >= 40 && y < 140) c = (x == 20 || x == 219 || y == 40 || y == 139) ? rgb(31, 31, 31) : rgb(8, 8, 10);
          if (y >= 60 && y < 100 && x >= 40 && x < 200 && ((x * 7 + y * 13) % 11) < 2) c = rgb(31, 31, 31);   // text
          if (x >= 100 && x < 132 && y >= 100 && y < 132) { u32 r = (x * 31 + y * 17) * 2654435761u; c = rgb(r >> 3, r >> 8, r >> 13); }   // sprite
          px[i] = c;
        } else if (m == 5) {   // horizontal gradient
          px[i] = rgb(x * 31 / 239, 31 - x * 31 / 239, 16);
        } else if (m == 6) {   // 1-pixel checkerboard
          px[i] = ((x + y) & 1) ? rgb(31, 31, 31) : rgb(0, 0, 0);
        } else {               // diagonal gradient
          px[i] = rgb((x + y) * 31 / 398, x * 31 / 239, y * 31 / 159);
        }
      }
      auto t0 = clk::now();
      host->FrameEnded(px);
      long long us = std::chrono::duration_cast<std::chrono::microseconds>(clk::now() - t0).count();
      long long m = max_us.load(); while (us > m && !max_us.compare_exchange_weak(m, us)) {}
      ++frames; ++n;
      }
      next += std::chrono::microseconds(16667 * (mode == 1 ? 4 : 1));
      std::this_thread::sleep_until(next);
    }
  });
  std::thread keys([&] {
    u16 last = 0xFFFF;
    while (run) {
      u16 k = HW::GBA::ExternalKeys(dev).load();
      if (k != last) { std::printf("KEYS %d %03x\n", dev, k); std::fflush(stdout); last = k; }
      if (HW::GBA::ResetRequest(dev).exchange(false)) { std::printf("RESET %d\n", dev); std::fflush(stdout); }
      {
        auto& rq = HW::GBA::RequestsFor(dev); std::lock_guard lock(rq.lock);
        for (auto& r : rq.queue) std::printf("REQUEST %d %d\n", dev, static_cast<int>(r.first));
        rq.queue.clear(); std::fflush(stdout);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  });
  std::puts("READY"); std::fflush(stdout);
  std::string line; while (std::getline(std::cin, line)) { if (line == "quit") break; if (line == "pause") paused = true; else if (line == "resume") paused = false; else if (line == "burst4") { odd_first = true; mode = 1; } else if (line == "regular") mode = 0; else if (line == "still") mode = 2; else if (line.rfind("pat", 0) == 0) mode = atoi(line.c_str() + 3); else if (line == "stats") { std::printf("STATS frames=%lld max_framended_us=%lld\n", frames.load(), max_us.load()); std::fflush(stdout);} }
  run = false; producer.join(); keys.join();
  auto t0 = clk::now();
  host.reset();   // stops the server
  std::printf("SHUTDOWN_MS %lld\n", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count());
  std::printf("FINAL frames=%lld max_framended_us=%lld\n", frames.load(), max_us.load());
  return 0;
}
