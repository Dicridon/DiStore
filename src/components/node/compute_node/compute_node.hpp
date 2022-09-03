#ifndef __DISTORE__NODE__COMPUTE_NODE__COMPUTE_NODE__
#define __DISTORE__NODE__COMPUTE_NODE__COMPUTE_NODE__
#include "node/node.hpp"
#include "memory/memory.hpp"
#include "memory/compute_node/compute_node.hpp"
#include "kv/kv.hpp"
#include "erpc_wrapper/erpc_wrapper.hpp"
#include "debug/debug.hpp"


namespace DiStore::Cluster {
    using namespace RPCWrapper;
    class ComputeNode {
    public:
        auto initialize(const std::string &compute_config, const std::string &memory_config) -> bool;

        auto connect_memory_nodes() -> bool;

        auto put(const std::string &key, const std::string &value) -> bool;
        auto get(const std::string &key) -> std::string;
        auto update(const std::string &key, const std::string &value) -> bool;
        auto remove(const std::string &key) -> bool;
        auto scan(const std::string &key, size_t count) -> std::vector<Value>;

        ComputeNode() = default;
        ComputeNode(const ComputeNode &) = delete;
        ComputeNode(ComputeNode &&) = delete;
        auto operator=(const ComputeNode &) = delete;
        auto operator=(ComputeNode &&) = delete;
    private:
        ComputeNodeInfo self_info;
        ClientRPCContext compute_ctx;
        Memory::ComputeNodeAllocator allocator;
        Memory::RemoteMemoryManager remote_memory_allocator;
        std::unique_ptr<RDMADevice> rdma_dev;

        auto initialize_erpc() -> bool {
            auto uri = self_info.erpc_addr.to_string() + ":" + std::to_string(self_info.erpc_port);
            if (!compute_ctx.initialize_nexus(self_info.erpc_addr, self_info.erpc_port)) {
                Debug::error("Failed to initialize nexus at compute node %s\n", uri.c_str());
                return false;
            }

            compute_ctx.user_context = this;
            Debug::info("eRPC on %s is initialized\n", uri.c_str());
            return true;
        }

        auto initialize_rdma_dev(std::ifstream &config) -> bool {
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

            auto [dev, status] = RDMAUtil::RDMADevice::make_rdma(device, port, gid);
            if (status != RDMAUtil::Enums::Status::Ok) {
                Debug::error("Failed to create RDMA device due to %s\n",
                             RDMAUtil::decode_rdma_status(status).c_str());
                return false;
            }

            Debug::info("RDMA %s device initialized with port %d and gidx %d",
                        device.c_str(), port, gid);
            rdma_dev = std::move(dev);
            return true;
        }
    };
}
#endif
