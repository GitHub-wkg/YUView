#pragma once

#include <cstddef>
#include <filesystem>

namespace nativeCrashHandler
{
bool openLog(const std::filesystem::path &path);
void install();
void writeRaw(const char *data, size_t size);
void flushLog();
}