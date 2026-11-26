#include "log.hxx"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace pluginhost
{
namespace
{
std::mutex& logMutex()
{
    static std::mutex m;
    return m;
}
}

void logEvent(const char* module, const char* event, const std::string& data)
{
    try
    {
        const char* path = std::getenv("PLUGINHOST_LOG");
        if (!path || !*path)
            return;

        using namespace std::chrono;
        const auto now = system_clock::now();
        const std::time_t t = system_clock::to_time_t(now);
        const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream line;
        line << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms
             << '|' << module << '|' << event;
        if (!data.empty())
            line << '|' << data;
        line << '\n';

        std::lock_guard<std::mutex> guard(logMutex());
        std::ofstream out(path, std::ios::app);
        out << line.str();
    }
    catch (...)
    {
        // Logging must never take LibreOffice down.
    }
}
}
