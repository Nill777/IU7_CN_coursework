#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>

void init_logger(const std::string& path);
void log_info(const std::string& msg);
void log_error(const std::string& msg);

#endif
