 #ifndef __DISTORE__NODE__MEMORY_NODE__MEMORY_NODE__
#define __DISTORE__NODE__MEMORY_NODE__MEMORY_NODE__
#include "node/node.hpp"
#include "memory/memory_node/memory_node.hpp"
#include "erpc_wrapper/erpc_wrapper.hpp"
#include "rdma_util/rdma_util.hpp"
#include "misc/misc.hpp"

namespace DiStore {
    using namespace RPCWrapper;
    namespace Cluster {
        class MemoryNode {
        public:
            static auto allocation_handler(erpc::ReqHandle *req_handle, void *ctx) -> void;
            static auto deallocation_handler(erpc::ReqHandle *req_handle, void *ctx) -> void;

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
            auto launch_tcp_thread() -> std::optional<std::thread>;
            auto launch_erpc_thread() -> std::optional<std::thread>;
            auto launch_rdma_thread() -> std::optional<std::thread>;

            MemoryNode() = default;
            MemoryNode(const MemoryNode &) = delete;
            MemoryNode(MemoryNode &&) = delete;
            auto operator=(const MemoryNode &) = delete;
            auto operator=(MemoryNode &&) = delete;
        private:
            ServerRPCContext memory_ctx;
            MemoryNodeInfo self_info;
            std::unique_ptr<RDMAUtil::RDMAContext> rdma_ctx;

            auto initialize_addresses(std::ifstream &config) -> void {
                std::string buffer;
                std::regex node_info("node(\\d+):\\s*(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d+)");
                while(std::getline(config, buffer)) {
                    auto iter = std::sregex_iterator(buffer.begin(), buffer.end(), node_info);
                    std::smatch match = *iter;
                    // can throw exception here
                    self_info.node_id = atoi(match[1].str().c_str());

                    ++iter;
                    match = *iter;
                    self_info.tcp_addr = Cluster::IPV4Addr::make_ipv4_addr(match[2].str()).value();
                    self_info.tcp_port = atoi(match[3].str().c_str());

                    ++iter;
                    match = *iter;
                    self_info.roce_addr = Cluster::IPV4Addr::make_ipv4_addr(match[2].str()).value();
                    self_info.roce_port = atoi(match[3].str().c_str());

                    ++iter;
                    match = *iter;
                    self_info.erpc_addr = Cluster::IPV4Addr::make_ipv4_addr(match[2].str()).value();
                    self_info.erpc_port = atoi(match[3].str().c_str());
                }
                config.clear();
                config.seekg(0);
            }

            auto initialize_erpc() -> bool {
                if (!memory_ctx.initialize_nexus(self_info.erpc_addr, self_info.erpc_port)) {
                    auto uri = self_info.erpc_addr.to_string() + ":" + std::to_string(self_info.erpc_port);
                    std::cerr << ">> Failed to initialize nexus at " << uri << "\n";
                    return false;
                }

                return true;
            }

            auto initialize_memory(std::ifstream &config) -> bool {
                std::stringstream buf;
                buf << config.rdbuf();
                auto content = buf.str();

                std::regex rmem("mem_cap: (\\d+)");
                std::smatch vmem;
                if (!std::regex_search(content, vmem, rmem)) {
                    std::cout << ">> Failed to read memory capacity\n";
                    return false;
                }

                auto cap = atoi(vmem[1].str().c_str());
                auto mem = new Memory::byte_t[cap];
                self_info.cap = cap;
                self_info.base_addr = Memory::RemotePointer::make_remote_pointer(self_info.node_id, mem);

                config.clear();
                config.seekg(0);
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
                    std::cerr << ">> Failed to read RDMA device info\n";
                    return false;
                }
                auto device = vdevice[1].str();

                if (!std::regex_search(content, vport, rport)) {
                    std::cerr << ">> Failed to read RDMA port info\n";
                    return false;
                }
                auto port = atoi(vport[1].str().c_str());

                if (!std::regex_search(content, vgid, rgid)) {
                    std::cerr << ">> Failed to read RDMA gid info\n";
                    return false;
                }
                auto gid = atoi(vgid[1].str().c_str());

                auto [rdma_dev, status] = RDMAUtil::RDMADevice::make_rdma(device, port, gid);
                if (status != RDMAUtil::Enums::Status::Ok) {
                    std::cerr << ">> Failed to create RDMA device due to "
                              << RDMAUtil::decode_rdma_status(status);
                    return false;
                }

                auto [rdma_ctx, s] = rdma_dev->open(self_info.base_addr.get_as<void *>(),
                                                    self_info.cap, 1,
                                                    RDMAUtil::RDMADevice::get_default_mr_access(),
                                                    *RDMAUtil::RDMADevice::get_default_qp_init_attr());
                if (s != RDMAUtil::Enums::Status::Ok) {
                    std::cerr << ">> Failed to open RDMA device due to "
                              << RDMAUtil::decode_rdma_status(s);
                    return false;
                }

                self_info.rdma_ctx = std::move(rdma_ctx);
                return true;
            }
        };
    }
}
#endif
