#include "logger.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctime>
#include <cstring>
#include <sstream>

static int g_log_fd = -1;

void init_logger(const std::string& path) {
    g_log_fd = ::open(path.c_str(),
                      O_WRONLY | O_CREAT | O_APPEND,
                      0644);
}

static void log_write(const std::string& level, const std::string& msg) {
    if (g_log_fd < 0) return;

    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);

    char timebuf[64];
    std::strftime(timebuf, sizeof(timebuf),
                  "%Y-%m-%d %H:%M:%S", &tm);

    std::ostringstream oss;
    oss << timebuf << " [" << level << "] "
        << "pid " << getpid() << " "
        << msg << "\n";
    std::string line = oss.str();

    ::write(g_log_fd, line.data(), line.size());
}

void log_info(const std::string& msg) {
    log_write("INFO", msg);
}

void log_error(const std::string& msg) {
    log_write("ERROR", msg);
}
