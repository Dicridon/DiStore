#include "memory_node.hpp"
namespace DiStore {
    namespace Cluster {
        auto MemoryNode::initialize(const std::string &config) -> bool {
            std::ifstream file(config);
            if (!file.is_open()) {
                std::cerr << "Failed to open config file " << config << "\n";
                return false;
            }

            initialize_addresses(file);
            if (!initialize_erpc()) {
                return false;
            }
            if (!initialize_memory(file)) {
                return false;
            }
            if (!initialize_rdma(file)) {
                return false;
            }


            auto memory = new Memory::byte_t[4 * (1 << 3)];
            self_info.base_addr = Memory::RemotePointer::make_remote_pointer(self_info.node_id,
                                                                             memory);

            memory_ctx.register_req_func(Enums::RPCOperations::RemoteAllocation, allocation_handler);
            memory_ctx.register_req_func(Enums::RPCOperations::RemoteDeallocation, deallocation_handler);

        }

        auto MemoryNode::launch_tcp_thread() -> std::optional<std::thread> {
            auto socket = Misc::make_async_socket(true, self_info.tcp_port);
            if (socket == -1) {
                std::cerr << ">> Failed to create TCP socket at port " << self_info.tcp_port << "\n";
                return {};
            }

            std::thread t([socket](Memory::RemotePointer base) {
                while(true){
                    auto sock = Misc::accept_nonblocking(socket);
                    if (sock == -1) {
                        sleep(1);
                    }

                    Misc::send_all(sock, &base, sizeof(Memory::RemotePointer));
                }
            }, self_info.base_addr);

            return t;
        }

        auto MemoryNode::launch_erpc_thread() -> std::optional<std::thread> {
            return memory_ctx.loop_thread();
        }

        auto MemoryNode::launch_rdma_thread() -> std::optional<std::thread> {
            auto socket = Misc::make_async_socket(true, self_info.roce_port);
            if (socket == -1) {
                std::cerr << ">> Failed to create RoCE socket at port " << self_info.roce_port << "\n";
                return {};
            }

            std::thread t([socket](RDMAUtil::RDMAContext *ctx) {
                while(true){
                    auto sock = Misc::accept_nonblocking(socket);
                    if (sock == -1) {
                        sleep(1);
                    }

                    ctx->default_connect(sock);
                }
            }, self_info.rdma_ctx.get());
        }
    }
}
