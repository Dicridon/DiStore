#include "memory_node.hpp"
namespace DiStore {
    namespace Cluster {
        auto MemoryNode::allocation_handler(erpc::ReqHandle *req_handle, void *ctx) -> void {
            auto rpc_ctx = reinterpret_cast<ServerRPCContext *>(ctx);
            auto mem_node = reinterpret_cast<MemoryNode *>(rpc_ctx->user_context);

            auto &resp = req_handle->pre_resp_msgbuf;

            rpc_ctx->info->rpc->resize_msg_buffer(&resp, sizeof(Memory::RemotePointer));

            auto buf = mem_node->allocator->allocate();
            auto rem_buf = Memory::RemotePointer::make_remote_pointer(mem_node->self_info.node_id, buf);

            memcpy(resp.buf, &rem_buf, sizeof(Memory::RemotePointer));
            rpc_ctx->info->rpc->enqueue_response(req_handle, &resp);
        }

        auto MemoryNode::deallocation_handler(erpc::ReqHandle *req_handle, void *ctx) -> void {
            auto rpc_ctx = reinterpret_cast<ServerRPCContext *>(ctx);
            auto mem_node = reinterpret_cast<MemoryNode *>(rpc_ctx->user_context);

            auto req = req_handle->get_req_msgbuf();
            auto buf = req->buf;
            auto off = sizeof(Enums::RPCOperations::RemoteDeallocation);
            auto addr = *reinterpret_cast<Memory::RemotePointer *>(buf + off);

            mem_node->allocator->deallocate(addr.get_as<Memory::byte_ptr_t>());

            auto &resp = req_handle->pre_resp_msgbuf;
            bool y = true;
            memcpy(resp.buf, &y, sizeof(y));
            rpc_ctx->info->rpc->enqueue_response(req_handle, &resp);
        }

        auto MemoryNode::initialize(const std::string &config) -> bool {
            std::ifstream file(config);
            if (!file.is_open()) {
                Debug::error("Failed to open config file %s\n", config.c_str());
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

            auto uri = self_info.tcp_addr.to_string() + std::to_string(self_info.tcp_port);
            Debug::info("Memory node %s is initialized\n", uri.c_str());
            return true;
        }

        auto MemoryNode::launch_tcp_thread() -> std::optional<std::thread> {
            auto socket = Misc::make_async_socket(true, self_info.tcp_port);
            if (socket == -1) {
                Debug::error("Failed to create TCP socket at port %d\n", self_info.tcp_port);
                return {};
            }
            auto rpc_id = this->memory_ctx.info->self_id;

            std::thread t([socket](Memory::RemotePointer base, int rpc_id) {
                while(true){
                    auto sock = Misc::accept_nonblocking(socket);
                    if (sock == -1) {
                        sleep(1);
                    }

                    Misc::send_all(sock, &base, sizeof(Memory::RemotePointer));

                    Misc::send_all(sock, &rpc_id, sizeof(rpc_id));
                }
            }, self_info.base_addr, rpc_id);

            Debug::info("TCP thread launched\n");
            return t;
        }

        auto MemoryNode::launch_erpc_thread() -> std::optional<std::thread> {
            return memory_ctx.loop_thread();
        }

        auto MemoryNode::launch_rdma_thread() -> std::optional<std::thread> {
            auto socket = Misc::make_async_socket(true, self_info.roce_port);
            if (socket == -1) {
                Debug::error("Failed to create RoCE socket at port %d\n", self_info.roce_port);
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

            Debug::info("RDMA thread launched\n");
            return t;
        }
    }
}
