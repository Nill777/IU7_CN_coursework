#include "server.hpp"
#include "connection.hpp"
#include "logger.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include <unordered_map>
#include <vector>
#include <cstring>
#include <cerrno>
#include <sstream>
#include <iostream>

static volatile sig_atomic_t server_running = 1;

static void handle_signal(int sig) {
    server_running = 0;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int create_listen_socket(const ServerConfig& cfg) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg.port);
    addr.sin_addr.s_addr = inet_addr(cfg.host.c_str());

    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 128) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    if (set_nonblocking(fd) < 0) {
        perror("fcntl");
        close(fd);
        return -1;
    }

    return fd;
}

static void worker_loop(int listen_fd, const ServerConfig& cfg) {
    std::unordered_map<int, Connection> conns;
    while (server_running) {
        fd_set readfds, writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        int maxfd = listen_fd;
        FD_SET(listen_fd, &readfds);

        for (auto& kv : conns) {
            int fd = kv.first;
            Connection& c = kv.second;
            if (c.state == ConnState::READING_REQUEST) {
                FD_SET(fd, &readfds);
            } else if (c.state == ConnState::SENDING_HEADERS ||
                       c.state == ConnState::SENDING_BODY) {
                FD_SET(fd, &writefds);
            }
            if (fd > maxfd) maxfd = fd;
        }

        struct timespec timeout;
        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;

        sigset_t empty_mask;
        sigemptyset(&empty_mask);

        int ready = pselect(maxfd + 1,
                            &readfds, &writefds, nullptr,
                            &timeout, &empty_mask);
        
        if (!server_running) break; 

        if (ready < 0) {
            if (errno == EINTR) continue;
            log_error("pselect error: " + std::string(std::strerror(errno)));
            continue;
        }

        if (FD_ISSET(listen_fd, &readfds)) {
            bool keep_accepting = true;
            while (keep_accepting && server_running) {
                sockaddr_in cli{};
                socklen_t len = sizeof(cli);
                int client_fd = ::accept(listen_fd, (sockaddr*)&cli, &len);
                if (client_fd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        keep_accepting = false; 
                    } else {
                        log_error("accept error: " +
                                  std::string(std::strerror(errno)));
                        keep_accepting = false;
                    }
                } else {
                    if (client_fd >= FD_SETSIZE) {
                        log_error("Socket fd (" + std::to_string(client_fd) + 
                                    ") >= FD_SETSIZE, closing connection");
                        ::close(client_fd);
                    } else {
                        set_nonblocking(client_fd);
                        Connection c;
                        c.fd = client_fd;
                        conns.emplace(client_fd, std::move(c));
                    }
                }
            }
        }

        std::vector<int> to_close;

        for (auto& kv : conns) {
            int fd = kv.first;
            Connection& c = kv.second;
            if (FD_ISSET(fd, &readfds)) {
                bool want_close = false;
                handle_read(c, cfg, want_close);
                if (want_close) to_close.push_back(fd);
            }
        }

        for (auto& kv : conns) {
            int fd = kv.first;
            Connection& c = kv.second;
            if (FD_ISSET(fd, &writefds)) {
                bool want_close = false;
                handle_write(c, want_close);
                if (want_close) to_close.push_back(fd);
            }
        }

        for (int fd : to_close) {
            auto it = conns.find(fd);
            if (it != conns.end()) {
                if (it->second.file_fd >= 0)
                    ::close(it->second.file_fd);
                ::close(fd);
                conns.erase(it);
            }
        }
    }
}

static void run_worker(int listen_fd, const ServerConfig& cfg) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    log_info("Worker started, pid=" + std::to_string(getpid()));
    worker_loop(listen_fd, cfg);
    log_info("Worker shutting down cleanly, pid=" + std::to_string(getpid()));
}

int run_server(const ServerConfig& cfg) {
    int listen_fd = create_listen_socket(cfg);
    if (listen_fd < 0) return 1;

    init_logger(cfg.log_path);
    log_info("Server starting (prefork + pselect)");

    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr); 
    sigaction(SIGTERM, &sa, nullptr);
    
    signal(SIGPIPE, SIG_IGN);

    // prefork
    for (int i = 0; i < cfg.workers; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        } else if (pid == 0) {
            run_worker(listen_fd, cfg);
            _exit(0);
        }
    }

    while (server_running) {
        int status = 0;
        pid_t pid = wait(&status);
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                break;
            }
            log_error("wait error: " + std::string(std::strerror(errno)));
            break;
        }
        log_info("Worker " + std::to_string(pid) + " exited");
    }

    ::close(listen_fd);
    log_info("Bye");
    return 0;
}
