// Test stub of Dolphin's Common/Logging/Log.h: the *_LOG_FMT macros GBAWeb.cpp uses, each line on
// stderr (where test.py reads them), with its level, as fmt would format it (std::format here).
#pragma once
#include <cstdio>
#include <format>
#include <string>

namespace TestLog
{
template <typename... Args>
void Line(const char* level, std::format_string<Args...> format, Args&&... args)
{
  const std::string text = std::format(format, std::forward<Args>(args)...);
  std::fprintf(stderr, "%s: %s\n", level, text.c_str());
}
}  // namespace TestLog

#define ERROR_LOG_FMT(type, ...) TestLog::Line("E", __VA_ARGS__)
#define WARN_LOG_FMT(type, ...) TestLog::Line("W", __VA_ARGS__)
#define NOTICE_LOG_FMT(type, ...) TestLog::Line("N", __VA_ARGS__)
#define INFO_LOG_FMT(type, ...) TestLog::Line("I", __VA_ARGS__)
#define DEBUG_LOG_FMT(type, ...) TestLog::Line("D", __VA_ARGS__)
