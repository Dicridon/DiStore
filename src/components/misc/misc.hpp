#ifndef __DISTORE__MISC__MISC__
#define __DISTORE__MISC__MISC__
#define UNUSED(x) (void)(x)
#define FOR_FUTURE(x) (void)(x)
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>
#include <numeric>
#include <cmath>

#include <fcntl.h>

namespace DiStore::Misc {
    /*
     * Socket related functions. Here I don't use trailing return type because
     * these functions manipulate low-level OS interfaces.
     */
    // make a new socket file descriptor and listen
    int make_socket(bool is_server, int socket_port);
    int make_async_socket(bool is_server, int socket_port);
    int connect_socket(int sockfd, int socket_port, const char *server);
    int accept_blocking(int sockfd);
    int accept_nonblocking(int sockfd);

    int send_all(int sockfd, void *buf, size_t count);
    int recv_all(int sockfd, void *buf, size_t count);

    /*
     * This function serves as a shortcut for establishing blocking socket connection
     * if is_server == false, supply an IP
     */
    int socket_connect(bool is_server, int socket_port, const char *server = nullptr);
    bool syncop(int sockfd);

    // Don't know why I'm writing this, perhaps because pend(); is shorter than while(true);
    auto pend() -> void;

    auto file_as_string(const std::string &file_name) -> std::optional<std::string>;

    auto check_socket_read_write(ssize_t ret, bool is_read = true) -> void;

    // input vector should be sorted if p90, p99 and p999 are needed
    template<typename T,
             typename = std::enable_if_t<std::is_arithmetic_v<T>>>
    auto percentile(const std::vector<T> &sorted, double percent) -> T {
        if (sorted.size() == 0)
            return 0;
        auto partition = ceil(sorted.size() * (1 - percent / 100));
        return std::accumulate(sorted.begin(), sorted.begin() + partition, 0) / partition;
    }

    // input vector is not required to be sorted
    template<typename T,
             typename = std::enable_if_t<std::is_arithmetic_v<T>>>
    auto avg(const std::vector<T> &sorted) -> T {
        return percentile(sorted, 0);
    }

    template<typename T,
             typename = std::enable_if_t<std::is_arithmetic_v<T>>>
    auto p90(const std::vector<T> &sorted) -> T {
        return percentile(sorted, 90);
    }

    template<typename T,
             typename = std::enable_if_t<std::is_arithmetic_v<T>>>
    auto p99(const std::vector<T> &sorted) -> T {
        return percentile(sorted, 99);
    }

    template<typename T,
             typename = std::enable_if_t<std::is_arithmetic_v<T>>>
    auto p999(const std::vector<T> &sorted) -> T {
        return percentile(sorted, 99.9);
    }
}
#endif
