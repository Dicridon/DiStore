#include "misc.hpp"
namespace DiStore {
    namespace Misc {
        int make_socket(bool is_server, int socket_port) {
            struct sockaddr_in seraddr;
            auto sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd == -1) {
                std::cout << ">> Error:" << "can not open socket\n";
                exit(-1);
            }

            if (!is_server) {
                return sockfd;
            }

            memset(&seraddr, 0, sizeof(struct sockaddr));
            seraddr.sin_family = AF_INET;
            seraddr.sin_port = htons(socket_port);
            seraddr.sin_addr.s_addr = INADDR_ANY;
            
            if (bind(sockfd, (struct sockaddr *)&seraddr, sizeof(struct sockaddr)) == -1) {
                std::cout << ">> Error: " << "can not bind socket on port " << socket_port << "\n";
                exit(-1);
            }

            if (listen(sockfd, 1) == -1) {
                std::cout << ">> Error: " << "can not listen socket\n";
                exit(-1);
            }

            return sockfd;
        }

        int make_async_socket(bool is_server, int socket_port) {
            auto sockfd = make_socket(is_server, socket_port);
            if (sockfd == -1) {
                return sockfd;
            }
            
            auto flags = fcntl(sockfd, F_GETFL);
            fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
            return sockfd;
        }        

        int connect_socket(int sockfd, int socket_port, const char *server) {
            struct sockaddr_in seraddr;
            memset(&seraddr, 0, sizeof(struct sockaddr));
            seraddr.sin_family = AF_INET;
            seraddr.sin_port = htons(socket_port);
            inet_pton(AF_INET, server, &seraddr.sin_addr);
        
            if (connect(sockfd, (struct sockaddr *)&seraddr, sizeof(seraddr)) == -1) {
                return -1;
            }
            return sockfd;
        }

        int accept_blocking(int sockfd) {
            return accept(sockfd, NULL, NULL);
        }

        int accept_nonblocking(int sockfd) {
            return accept4(sockfd, NULL, NULL, SOCK_NONBLOCK);
        }

        int send_all(int sockfd, void *buf, size_t count) {
            ssize_t sent = 0;
            ssize_t ret = 0;
            do {
                ret = write(sockfd, reinterpret_cast<char *>(buf) + sent, count - sent);
                if (ret < 0) {
                    if (sent == 0) {
                        return ret;
                    }
                    break;
                }
                sent += ret;
            } while(sent < static_cast<ssize_t>(count));
            return sent;
        }

        int recv_all(int sockfd, void *buf, size_t count) {
            ssize_t got = 0;
            ssize_t ret = 0;
            do {
                ret = read(sockfd, reinterpret_cast<char *>(buf) + got, count - got);
                if (ret < 0) {
                    if (got == 0) {
                        return ret;
                    }
                    break;
                }
                got+= ret;
            } while(got < static_cast<ssize_t>(count));
            return got;
        }
            

        int socket_connect(bool is_server, int socket_port, const char *server) {
            auto sockfd = make_socket(is_server, socket_port);
            if (!is_server) {
                if (!server) {
                    std::cout << ">> Error: server unspecified\n";
                    return -1;
                }
                return connect_socket(sockfd, socket_port, server);
            }
            
            auto ret = accept_blocking(sockfd);
            if (ret == -1) {
                std::cout << ">> Error: accepting connection failed\n";
                exit(-1);
            }
            return ret;
        }
 
        bool syncop(int sockfd) {
            char msg = 'a';
            if (write(sockfd, &msg, 1) != 1) {
                return false;
            }

            if (read(sockfd, &msg, 1) != 1) {
                return false;
            }
            return true;
        }

        auto pend() -> void {
            while(true);
        }

        auto file_as_string(const std::string &file_name) -> std::optional<std::string> {
            std::ifstream in(file_name);
            if (!in.is_open()) {
                return {};
            }

            std::stringstream buf;
            buf << in.rdbuf();
            return buf.str();
        }

        auto check_socket_read_write(ssize_t ret, bool is_read) -> void {
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::cout << "nonblocking read, try again\n";
                } else {
                    std::stringstream stream;
                    stream << ">> Invalid read/write, errno: " << errno << "\n";
                    throw std::runtime_error(stream.str());
                }
            } else if (ret == 0) {
                throw std::runtime_error(">> Seems no data");
            } else {
                if (is_read)
                    std::cout << "Reading " << ret << " bytes\n";
                else
                    std::cout << "Writing " << ret << " bytes\n";
            }
        }
    }
}
