#ifndef SERVER_HPP
#define SERVER_HPP

#include "config.hpp"
#include <sys/wait.h>

int run_server(const ServerConfig& cfg);

#endif
