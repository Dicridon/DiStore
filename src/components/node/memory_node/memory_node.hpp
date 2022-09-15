#ifndef __DISTORE__NODE__MEMORY_NODE__MEMORY_NODE__
#define __DISTORE__NODE__MEMORY_NODE__MEMORY_NODE__
#include "node/node.hpp"
#include "memory/memory_node/memory_node.hpp"
#include "erpc_wrapper/erpc_wrapper.hpp"
#include "rdma_util/rdma_util.hpp"
#include "misc/misc.hpp"
#include "debug/debug.hpp"

namespace DiStore::Cluster {
    using namespace RPCWrapper;
    class MemoryNode {
    public:
        /*
         * RPC request format
         * 1. allocate:
         *    |             first byte          | following bytes
         *    | RPCOperations::RemoteAllocation |
         *
         * 2. deallocate:
         *    |             first byte            | following bytes
         *    | RPCOperations::RemoteDeallocation | RemotePointer segment
         *
         * RPC response format
         * 1. allocate:
         *    |  first 8 bytes | following bytes
         *    |  RemotePointer |
         * 2. deallocate:
         *    |  first byte  | following bytes
         *    |  true/false  |
         *
         */
        static auto allocation_handler(erpc::ReqHandle *req_handle, void *ctx) -> void;
        static auto deallocation_handler(erpc::ReqHandle *req_handle, void *ctx) -> void;

        static auto make_memory_node(const std::string &config) -> std::unique_ptr<MemoryNode> {
            auto ret = std::make_unique<MemoryNode>();
            if (!ret->initialize(config)) {
                Debug::error("Failed to initialize a memory node\n");
                return nullptr;
            }

            return ret;
        }

        /*
         * config file format is the similar to that in memory/compute_node.hpp
         * #        tcp              roce            erpc
         * node1: 127.0.0.1:1234, 127.0.0.1:4321, 127.0.0.1:3124
         * mem_cap: 1024
         * rdma_device: mlx5_0
         * rdma_port: 1
         * gid_idx: 2
         */
        auto initialize(const std::string &config) -> bool;

        // should first call launch erpc_thread to initialize erpc info
        auto launch_erpc_thread() -> std::optional<std::thread>;
        // then can launch_tcp_thread be called to deliver erpc info to the remote
        auto launch_tcp_thread() -> std::optional<std::thread>;
        auto launch_rdma_thread() -> std::optional<std::thread>;

        MemoryNode() = default;
        MemoryNode(const MemoryNode &) = delete;
        MemoryNode(MemoryNode &&) = delete;
        auto operator=(const MemoryNode &) = delete;
        auto operator=(MemoryNode &&) = delete;
    private:
        // do not use smart pointer here
        Memory::MemoryNodeAllocator *allocator;
        ServerRPCContext memory_ctx;
        MemoryNodeInfo self_info;
        std::unique_ptr<RDMAUtil::RDMAContext> rdma_ctx;

        auto initialize_addresses(std::ifstream &config) -> void {
            NodeInfo::initialize(config, &self_info);
        }

        auto initialize_erpc() -> bool {
            auto uri = self_info.erpc_addr.to_uri(self_info.erpc_port);
            if (!memory_ctx.initialize_nexus(self_info.erpc_addr, self_info.erpc_port)) {
                Debug::error("Failed to initialize nexus at %s\n", uri.c_str());
                return false;
            }

            memory_ctx.register_req_func(Enums::RPCOperations::RemoteAllocation, allocation_handler);
            memory_ctx.register_req_func(Enums::RPCOperations::RemoteDeallocation, deallocation_handler);

            // memory_ctx will be passed to an eRPC instance upon create_new_rpc
            memory_ctx.user_context = this;
            Debug::info("Memory node eRPC initialized at %s\n", uri.c_str());
            return true;
        }

        auto initialize_memory(std::ifstream &config) -> bool {
            std::stringstream buf;
            buf << config.rdbuf();
            auto content = buf.str();

            std::regex rmem("mem_cap: (\\d+)");
            std::smatch vmem;
            if (!std::regex_search(content, vmem, rmem)) {
                Debug::error(">> Failed to read memory capacity\n");
                return false;
            }

            auto cap = atoi(vmem[1].str().c_str());
            auto mem = new Memory::byte_t[cap];
            auto off = Memory::Constants::MEMORY_PAGE_SIZE;

            self_info.cap = cap - off;
            self_info.base_addr = Memory::RemotePointer::make_remote_pointer(self_info.node_id, mem + off);
            allocator = Memory::MemoryNodeAllocator::make_allocator(mem, cap);

            config.clear();
            config.seekg(0);
            Debug::info("Memory node memory intialized with capacity being %ld\n", cap);
            return true;
        }

        // rdma_device: mlx5_0
        // rdma_port: 1
        // gid_idx: 2
        auto initialize_rdma(std::ifstream &config) -> bool {
            std::stringstream buf;
            buf << config.rdbuf();
            auto content = buf.str();

            std::regex rdevice("rdma_device:\\s+(\\S+)");
            std::regex rport("rdma_port:\\s+(\\d+)");
            std::regex rgid("gid_idx:\\s+(\\d+)");

            std::smatch vdevice;
            std::smatch vport;
            std::smatch vgid;

            if (!std::regex_search(content, vdevice, rdevice)) {
                Debug::error("Failed to read RDMA device info\n");
                return false;
            }
            auto device = vdevice[1].str();

            if (!std::regex_search(content, vport, rport)) {
                Debug::error("Failed to read RDMA port info\n");
                return false;
            }
            auto port = atoi(vport[1].str().c_str());

            if (!std::regex_search(content, vgid, rgid)) {
                Debug::error("Failed to read RDMA gid info\n");
                return false;
            }
            auto gid = atoi(vgid[1].str().c_str());

            auto [rdma_dev, status] = RDMAUtil::RDMADevice::make_rdma(device, port, gid);
            if (status != RDMAUtil::Enums::Status::Ok) {
                Debug::error("Failed to create RDMA device due to %s\n",
                             RDMAUtil::decode_rdma_status(status).c_str());
                return false;
            }

            auto [rdma_ctx, s] = rdma_dev->open(self_info.base_addr.get_as<void *>(),
                                                self_info.cap, 1,
                                                RDMAUtil::RDMADevice::get_default_mr_access(),
                                                *RDMAUtil::RDMADevice::get_default_qp_init_attr());
            if (s != RDMAUtil::Enums::Status::Ok) {
                Debug::error(">> Failed to open RDMA device due to %s\n",
                             RDMAUtil::decode_rdma_status(s).c_str());
                return false;
            }

            self_info.rdma_ctx = std::move(rdma_ctx);
            auto uri = self_info.tcp_addr.to_uri(self_info.tcp_port);
            Debug::info("RDMA is initialized for memory node %s\n", uri.c_str());
            return true;
        }
    };
}
#endif
