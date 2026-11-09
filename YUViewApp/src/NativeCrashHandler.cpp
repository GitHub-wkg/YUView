#include "NativeCrashHandler.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
HANDLE logFile = INVALID_HANDLE_VALUE;
std::wstring dumpPath;
volatile LONG handlingException{};
#else
int logFile = -1;
#endif

using nativeCrashHandler::flushLog;
using nativeCrashHandler::writeRaw;

template <size_t Size> void writeLiteral(const char (&message)[Size])
{
  writeRaw(message, Size - 1);
}

void signalHandler(int signal)
{
  switch (signal)
  {
  case SIGABRT: writeLiteral("\n[FATAL] Signal SIGABRT (abort)\n"); break;
  case SIGSEGV: writeLiteral("\n[FATAL] Signal SIGSEGV (invalid memory access)\n"); break;
  case SIGILL: writeLiteral("\n[FATAL] Signal SIGILL (illegal instruction)\n"); break;
  case SIGFPE: writeLiteral("\n[FATAL] Signal SIGFPE (arithmetic exception)\n"); break;
#ifdef SIGBUS
  case SIGBUS: writeLiteral("\n[FATAL] Signal SIGBUS (bus error)\n"); break;
#endif
  default: writeLiteral("\n[FATAL] Unexpected fatal signal\n"); break;
  }
  flushLog();
  std::signal(signal, SIG_DFL);
  if (std::raise(signal) != 0)
    std::_Exit(128 + signal);
}

void terminateHandler() noexcept
{
  writeLiteral("\n[FATAL] std::terminate called\n");
  if (const auto exception = std::current_exception())
  {
    try
    {
      std::rethrow_exception(exception);
    }
    catch (const std::exception &error)
    {
      writeLiteral("Unhandled C++ exception: ");
      const auto message = error.what();
      writeRaw(message, std::strlen(message));
      writeLiteral("\n");
    }
    catch (...)
    {
      writeLiteral("Unhandled C++ exception of unknown type\n");
    }
  }
  flushLog();
  std::abort();
}

#ifdef _WIN32
LONG WINAPI exceptionHandler(EXCEPTION_POINTERS *exception)
{
  if (InterlockedCompareExchange(&handlingException, 1, 0) != 0)
    return EXCEPTION_EXECUTE_HANDLER;

  SYSTEMTIME time{};
  GetLocalTime(&time);
  char message[512]{};
  const auto size = std::snprintf(
      message, sizeof(message),
      "\n[%04u-%02u-%02u %02u:%02u:%02u.%03u] [FATAL] Windows exception "
      "code=0x%08lX address=%p thread=%lu\n",
      time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
      time.wMilliseconds, exception->ExceptionRecord->ExceptionCode,
      exception->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
  if (size > 0 && static_cast<size_t>(size) < sizeof(message))
    writeRaw(message, static_cast<size_t>(size));
  // Persist the text before attempting a best-effort dump of a damaged process.
  flushLog();

  const auto dump = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dump != INVALID_HANDLE_VALUE)
  {
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
    if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpNormal,
                          &info, nullptr, nullptr))
      writeLiteral("[FATAL] Crash dump saved beside this log (.dmp)\n");
    else
      writeLiteral("[FATAL] Failed to write crash dump\n");
    FlushFileBuffers(dump);
    CloseHandle(dump);
  }
  else
    writeLiteral("[FATAL] Unable to create crash dump file\n");
  flushLog();
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

}

bool nativeCrashHandler::openLog(const std::filesystem::path &path)
{
#ifdef _WIN32
  dumpPath = std::filesystem::path(path).replace_extension(L".dmp").wstring();
  logFile = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  return logFile != INVALID_HANDLE_VALUE;
#else
  logFile = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_APPEND, 0600);
  return logFile >= 0;
#endif
}

// Use native I/O so a crash can be logged without Qt or a buffered file lock.
void nativeCrashHandler::writeRaw(const char *data, size_t size)
{
#ifdef _WIN32
  if (logFile == INVALID_HANDLE_VALUE)
    return;
  DWORD written{};
  WriteFile(logFile, data, static_cast<DWORD>(size), &written, nullptr);
#else
  if (logFile < 0)
    return;
  while (size > 0)
  {
    const auto written = ::write(logFile, data, size);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      break;
    data += written;
    size -= written;
  }
#endif
}

void nativeCrashHandler::flushLog()
{
#ifdef _WIN32
  if (logFile != INVALID_HANDLE_VALUE)
    FlushFileBuffers(logFile);
#else
  if (logFile >= 0)
    fsync(logFile);
#endif
}

void nativeCrashHandler::install()
{
  std::set_terminate(terminateHandler);
  std::signal(SIGABRT, signalHandler);
#ifdef _WIN32
  // Retain the exception address and CPU context for Windows access violations and other faults.
  SetUnhandledExceptionFilter(exceptionHandler);
#else
  std::signal(SIGSEGV, signalHandler);
  std::signal(SIGILL, signalHandler);
  std::signal(SIGFPE, signalHandler);
#ifdef SIGBUS
  std::signal(SIGBUS, signalHandler);
#endif
#endif
}