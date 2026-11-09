#include "CrashHandler.h"
#include "NativeCrashHandler.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <cstdio>

namespace
{
Q_LOGGING_CATEGORY(crashLog, "yuview.crash", QtInfoMsg)

QtMessageHandler previousMessageHandler{};
using nativeCrashHandler::flushLog;
using nativeCrashHandler::writeRaw;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
  const auto formatted = (qFormatLogMessage(type, context, message) + QLatin1Char('\n')).toUtf8();
  writeRaw(formatted.constData(), static_cast<size_t>(formatted.size()));
  if (type == QtCriticalMsg || type == QtFatalMsg)
    flushLog();

  if (previousMessageHandler)
    previousMessageHandler(type, context, message);
  else
    std::fwrite(formatted.constData(), 1, static_cast<size_t>(formatted.size()), stderr);
}

bool openLog(const QString &directory, const QString &session)
{
  if (!QDir().mkpath(directory))
    return false;
  const auto path = QDir(directory).filePath(session + QStringLiteral(".log"));
#ifdef Q_OS_WIN
  return nativeCrashHandler::openLog(path.toStdWString());
#else
  return nativeCrashHandler::openLog(QFile::encodeName(path).constData());
#endif
}
}

void crashHandler::initialize()
{
  const auto session = QStringLiteral("YUView-%1-%2")
                           .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz"))
                           .arg(QCoreApplication::applicationPid());
  auto directory = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                       .filePath(QStringLiteral("YUView/logs"));
  if (!openLog(directory, session))
  {
    directory = QDir(QDir::tempPath()).filePath(QStringLiteral("YUView/logs"));
    if (!openLog(directory, session))
    {
      qCWarning(crashLog) << "Unable to create a crash log in the data or temporary directory";
      return;
    }
  }

  qSetMessagePattern(QStringLiteral("[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{type}] "
                                    "[%{category}] [thread %{threadid}] %{message} "
                                    "(%{file}:%{line}, %{function})"));
  previousMessageHandler = qInstallMessageHandler(messageHandler);
  nativeCrashHandler::install();
  qCInfo(crashLog).noquote() << "Logging to" << QDir(directory).filePath(session + ".log")
                            << "version" << YUVIEW_VERSION << "commit" << YUVIEW_HASH;
}