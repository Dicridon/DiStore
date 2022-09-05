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
        static auto make_compute_node(const std::string &compute_config, const std::string &memory_config)
            -> std::unique_ptr<ComputeNode>
        {
            auto ret = std::make_unique<ComputeNode>();
            if (!ret->initialize(compute_config, memory_config)) {
                Debug::error("Failed to initialize compute node\n");
                return nullptr;
            }

            return ret;
        }
        /*
         * format of compute_config
         * #       tcp            roce         erpc
         * node: 1.1.1.1:123, 2.2.2.2:123, 3.3.3.3:123
         * rdma_device: mlx5_0
         * rdma_port: 1
         * gid_idx: 4
         */
        auto initialize(const std::string &compute_config, const std::string &memory_config) -> bool;

        auto connect_memory_nodes() -> bool;

        auto put(const std::string &key, const std::string &value) -> bool;
        auto get(const std::string &key) -> std::string;
        auto update(const std::string &key, const std::string &value) -> bool;
        auto remove(const std::string &key) -> bool;
        auto scan(const std::string &key, size_t count) -> std::vector<Value>;

        // for debug
        auto report_cluster_info() const noexcept -> void;

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
            auto uri = self_info.erpc_addr.to_uri(self_info.erpc_port);
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
