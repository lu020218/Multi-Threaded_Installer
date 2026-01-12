#include "common/logging/log_message.h"

namespace logging {

LogMessage::LogMessage(LogLevel lvl, std::string module, std::string msg)
    : level(lvl)
    , timestamp(std::chrono::system_clock::now())
    , threadId(std::this_thread::get_id())
    , moduleName(std::move(module))
    , fileName()
    , lineNumber(0)
    , functionName()
    , message(std::move(msg))
    , duration()
    , memoryUsage()
{
}

LogMessage::LogMessage(LogLevel lvl, std::string module, std::string msg, 
                       std::string file, int line, std::string func)
    : level(lvl)
    , timestamp(std::chrono::system_clock::now())
    , threadId(std::this_thread::get_id())
    , moduleName(std::move(module))
    , fileName(std::move(file))
    , lineNumber(line)
    , functionName(std::move(func))
    , message(std::move(msg))
    , duration()
    , memoryUsage()
{
}

void LogMessage::setPerformanceInfo(std::chrono::microseconds dur, size_t mem) {
    duration = dur;
    memoryUsage = mem;
}

bool LogMessage::hasPerformanceInfo() const {
    return duration.has_value() && memoryUsage.has_value();
}

} // namespace logging