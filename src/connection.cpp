#include "connection.hpp"
#include "config.hpp"
#include "http.hpp"
#include "logger.hpp"

#include <unistd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>

static const size_t READ_CHUNK = 4096;
static const size_t FILE_CHUNK = 16 * 1024;

void handle_read(Connection& conn,
                 const ServerConfig& cfg,
                 bool& want_close)
{
    want_close = false;
    char buf[READ_CHUNK];
    bool keep_reading = true;

    if (conn.state != ConnState::READING_REQUEST) 
        return;

    while (keep_reading) {
        ssize_t n = ::recv(conn.fd, buf, sizeof(buf), 0);
        if (n > 0) {
            conn.in_buf.append(buf, n);
            if (conn.in_buf.find("\r\n\r\n") != std::string::npos) {
                if (!parse_request(conn)) {
                    want_close = true;
                    conn.state = ConnState::CLOSING;
                    keep_reading = false;
                } else {
                    prepare_response(conn, cfg);
                    keep_reading = false;
                }
            } else if (conn.in_buf.size() > 16 * 1024) {
                want_close = true;
                conn.state = ConnState::CLOSING;
                keep_reading = false;
            }
        } else if (n == 0) {
            want_close = true;
            conn.state = ConnState::CLOSING;
            keep_reading = false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                keep_reading = false;
            } else {
                log_error("recv error: " + std::string(std::strerror(errno)));
                want_close = true;
                conn.state = ConnState::CLOSING;
                keep_reading = false;
            }
        }
    }
}

void handle_write(Connection& conn,
                  bool& want_close)
{
    want_close = false;

    if (conn.state != ConnState::SENDING_HEADERS &&
        conn.state != ConnState::SENDING_BODY)
        return;

    while (conn.out_sent < conn.out_buf.size()) {
        ssize_t n = ::send(conn.fd,
                           conn.out_buf.data() + conn.out_sent,
                           conn.out_buf.size() - conn.out_sent,
                           0);
        if (n > 0) {
            conn.out_sent += n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else {
            want_close = true;
            conn.state = ConnState::CLOSING;
            return;
        }
    }

    if (conn.state == ConnState::SENDING_HEADERS) {
        if (conn.head_only || conn.file_size == 0 || conn.file_fd < 0) {
            want_close = !conn.keep_alive;
            conn.state = want_close ? ConnState::CLOSING
                                    : ConnState::READING_REQUEST;
            conn.in_buf.clear();
            conn.out_buf.clear();
            conn.out_sent = 0;
            if (conn.file_fd >= 0) {
                ::close(conn.file_fd);
                conn.file_fd = -1;
            }
            return;
        } else {
            conn.state = ConnState::SENDING_BODY;
            conn.out_buf.clear();
            conn.out_sent = 0;
        }
    }

    if (conn.state == ConnState::SENDING_BODY) {
        char file_buf[FILE_CHUNK];

        if (conn.file_offset < conn.file_size) {
            ssize_t to_read = FILE_CHUNK;
            if (conn.file_size - conn.file_offset < to_read)
                to_read = conn.file_size - conn.file_offset;

            ssize_t r = ::read(conn.file_fd, file_buf, to_read);
            if (r > 0) {
                conn.file_offset += r;
                ssize_t sent_total = 0;
                while (sent_total < r) {
                    ssize_t n = ::send(conn.fd,
                                       file_buf + sent_total,
                                       r - sent_total,
                                       0);
                    if (n > 0) {
                        sent_total += n;
                    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        conn.out_buf.assign(file_buf + sent_total,
                                            r - sent_total);
                        conn.out_sent = 0;
                        return;
                    } else {
                        want_close = true;
                        conn.state = ConnState::CLOSING;
                        return;
                    }
                }
            } else {
                conn.file_offset = conn.file_size;
            }
        }

        if (conn.file_offset >= conn.file_size) {
            if (conn.file_fd >= 0) {
                ::close(conn.file_fd);
                conn.file_fd = -1;
            }
            want_close = !conn.keep_alive;
            conn.state = want_close ? ConnState::CLOSING
                                    : ConnState::READING_REQUEST;
            conn.in_buf.clear();
            conn.out_buf.clear();
            conn.out_sent = 0;
        }
    }
}
