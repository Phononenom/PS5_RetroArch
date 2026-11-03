#pragma once
#include <string>
namespace HW::GBA {
struct CoreInfo { int device_number; bool is_gba; };
class Core { public: explicit Core(int d) : m_d(d) {} CoreInfo GetCoreInfo() const { return {m_d, true}; } private: int m_d; };
}
