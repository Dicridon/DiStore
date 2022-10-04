#ifndef __DISTORE__NODE__COMPUTE_NODE__COMPUTE_NODE__
#define __DISTORE__NODE__COMPUTE_NODE__COMPUTE_NODE__
#include "node/node.hpp"
#include "memory/memory.hpp"
#include "memory/compute_node/compute_node.hpp"
#include "kv/kv.hpp"
#include "erpc_wrapper/erpc_wrapper.hpp"
#include "debug/debug.hpp"
#include "search_layer/search_layer.hpp"
#include "data_layer/data_layer.hpp"
#include "handover_locktable/handover_locktable.hpp"

namespace DiStore::Cluster {
    using namespace RPCWrapper;
    using namespace SearchLayer;
    using namespace DataLayer;

    namespace Constants {
        // 2 local nodes will form a well-formed doubly-linked list
        static constexpr int LOCAL_MAX_NODES = 2;
    }

    struct CalibrateContext {
        int level;
        SkipListNode *new_node;
    };

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

        // must call this register_thread before threads actually do some stuff
        auto register_thread() -> bool;

        auto put(const std::string &key, const std::string &value) -> bool;
        auto get(const std::string &key) -> std::optional<std::string>;
        auto update(const std::string &key, const std::string &value) -> bool;
        auto remove(const std::string &key) -> bool;
        auto scan(const std::string &key, size_t count) -> std::vector<std::string>;

        // always return non-null pointer as long as remote memory is not depleted
        auto allocate(size_t size) -> RemotePointer;
        auto free(RemotePointer p) -> void;

        // for debug
        auto report_cluster_info() const noexcept -> void;

        ComputeNode() = default;
        ComputeNode(const ComputeNode &) = delete;
        ComputeNode(ComputeNode &&) = delete;
        auto operator=(const ComputeNode &) = delete;
        auto operator=(ComputeNode &&) = delete;
    private:
        SearchLayer::SkipList slist;

        ComputeNodeInfo self_info;
        ClientRPCContext compute_ctx;
        Memory::ComputeNodeAllocator allocator;
        Memory::RemoteMemoryManager remote_memory_allocator;
        std::unique_ptr<RDMADevice> rdma_dev;

        std::unordered_map<std::thread::id, std::unique_ptr<Concurrency::ConcurrencyContext>> cctx;

        tbb::concurrent_queue<CalibrateContext *> update_queue;

        // nodes are kept locally if total number of nodes is less than LOCAL_MAX_NODES
        bool remote_put;
        DataLayer::LinkedNode10 *local_nodes[2];
        std::string local_anchors[2];
        std::mutex local_mutex;


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

            Debug::info("RDMA %s device initialized with port %d and gidx %d\n",
                        device.c_str(), port, gid);
            rdma_dev = std::move(dev);
            return true;
        }

        auto quick_put(const std::string &key, const std::string &value) -> bool;
        auto quick_put_pick_node(const std::string &key) -> DataLayer::LinkedNode10 *;

        auto put_dispatcher(SkipListNode *data_node, const std::string &key,
                            const std::string &value) -> bool;
        auto put10(SkipListNode *data_node, const std::string &key, const std::string &value) -> bool;
        auto put12(SkipListNode *data_node, const std::string &key, const std::string &value) -> bool;
        auto put14(SkipListNode *data_node, const std::string &key, const std::string &value) -> bool;
        auto put16(SkipListNode *data_node, const std::string &key, const std::string &value) -> bool;

        // try to win the put and fetch remote memory to local
        // pair[0], win the competition
        // pair[1], pointer to winner's ConcurrencyContext
        template<typename NodeType>
        auto try_win(SkipListNode *data_node, Concurrency::ConcurrencyContextType t)
            -> std::pair<bool, Concurrency::ConcurrencyContext *>
        {

            auto thread_cctx = cctx.find(std::this_thread::get_id());
            if (thread_cctx == cctx.end()) {
                throw std::runtime_error("Threads should always register before running\n");
            }

            Concurrency::ConcurrencyContext *expect = nullptr;
            auto shared_ctx = thread_cctx->second.get();
            shared_ctx->type = t;
            if (data_node->ctx.compare_exchange_strong(expect, shared_ctx)) {
                // the only winner should remember to collect pending requests
                // first process winner's own request
                auto buffer = remote_memory_allocator.fetch_as<NodeType *>(data_node->data_node,
                                                                           sizeof(NodeType));

                shared_ctx->user_context = buffer;
                shared_ctx->max_depth = -1;

                return {true, shared_ctx};
            }

            return {false, expect};
        }

        // pair[0], whether current key-value is put
        // pair[1], toal number of all pending requests including current key-value
        template<typename NodeType>
        auto try_put_to_existing_node(Concurrency::ConcurrencyContext *shared_ctx, SkipListNode *data_node,
                                      const std::string &key, const std::string &value)
            -> std::pair<bool, size_t>
        {
            auto node_buffer = reinterpret_cast<NodeType *>(shared_ctx->user_context);

            if (node_buffer->store(key, value)) {
                Concurrency::ConcurrencyRequests *req = nullptr;
                while (shared_ctx->requests.try_pop(req)) {
                    auto s = node_buffer->store(*reinterpret_cast<const std::string *>(req->tag),
                                                *reinterpret_cast<const std::string *>(req->content));
                    if (!s) {
                        shared_ctx->requests.push(req);
                        break;
                    }
                }

                if (shared_ctx->requests.unsafe_size() == 0) {
                    if(remote_memory_allocator.write_to(data_node->data_node, sizeof(NodeType))) {
                        return {true, 0};
                    } else {
                        Debug::error("Failed to write back to remote\n");
                        return {false, 0};
                    }
                }
                return {true, shared_ctx->requests.unsafe_size()};
            }

            // current key-value is not put
            return {false, shared_ctx->requests.unsafe_size() + 1};
        }

        // find out the true type of a LinkedNode
        template<typename NodeType>
        auto morph_node(NodeType *buffer) -> LinkedNodeType {
            auto buf = reinterpret_cast<LinkedNode16 *>(buffer);
            auto ct = 0;
            for (int i = 0; i < 16; i++) {
                if (buf->fingerprints[i] != 0) {
                    ++ct;
                }
            }

            if (ct <= 10) {
                return LinkedNodeType::Type10;
            } else if (ct <= 12) {
                return LinkedNodeType::Type12;
            } else if (ct <= 14) {
                return LinkedNodeType::Type14;
            } else if (ct <= 16) {
                return LinkedNodeType::Type16;
            } else {
                return LinkedNodeType::NotSet;
            }
        }

        auto failed_write(Concurrency::ConcurrencyContext *cctx, const std::string &key,
                          const std::string &value) -> bool;

        auto eager_morph(SkipListNode *data_node, Concurrency::ConcurrencyContext *shared_ctx,
                         const std::string &key, const std::string &value, bool done) -> bool;

        template<typename NodeType>
        auto construct_reorder_map(NodeType *source_buffer, int left_cap,
                                   int *reorder_map, bool *picked) -> void
        {
            auto total_records = source_buffer->next;
            // int result = 0;
            // pick one more key as right's anchor key
            int target = 0;
            for (int i = 0; i < left_cap + 1; i++) {
                for (int j = 0; j < total_records; j++) {
                    if (!picked[j]) {
                        target = j;
                        break;
                    }
                }

                for (int j = 0; j < total_records; j++) {
                    if (picked[j])
                        continue;

                    if (memcmp(source_buffer->pairs[target].key, source_buffer->pairs[j].key,
                               DataLayer::Constants::KEYLEN) > 0) {
                        target = j;
                    }
                }

                picked[target] = true;
                reorder_map[i] = target;
            }
        }

        // the splitted node is still large enough to hold the remaining pairs
        auto inplace_split_node(LinkedNode16 *source_buffer, size_t left_cap)
            -> std::tuple<LinkedNode16 *, LinkedNode16 *, std::string>;

        auto out_of_place_split_node(LinkedNode16 *source_buffer, Concurrency::ConcurrencyContext *shared_ctx,
                                     size_t left_cap, const std::string &key, const std::string &value,
                                     bool done)
            -> std::tuple<LinkedNode16 *, LinkedNode16 *, std::string>;

        // return the address of newly allocated right
        template<typename LNodeType, typename RNodeType>
        auto write_back_two(SkipListNode *data_node, LinkedNode16 *left, LinkedNode16 *right) -> RemotePointer {
            auto l = allocate(sizeof(LNodeType));
            auto r = allocate(sizeof(RNodeType));

            // actually we do not need a doubly-linked strucutre, the data layer is just like a
            // level in the blink tree.
            right->rlink = left->rlink;
            left->rlink = r;

            if (l.get_node() == r.get_node()) {
                auto rdma = remote_memory_allocator.get_rdma(l);
                auto l_sge = rdma->generate_sge(nullptr, 0, 0);
                auto r_sge = rdma->generate_sge(nullptr, 0, sizeof(LinkedNode16));

                rdma->post_batch_write({l_sge, r_sge});
                if (auto [wc, _] = rdma->poll_one_completion(); wc != nullptr) {
                    data_node->data_node = l;
                    return nullptr;
                }
            }

            data_node->data_node = l;

            // TODO: add right and its anchor to update thread
            return r;
        }

        auto async_update(SkipListNode *data_node, const std::string &anchor,
                          LinkedNodeType t, RemotePointer r) -> void;
    };
}
#endif
