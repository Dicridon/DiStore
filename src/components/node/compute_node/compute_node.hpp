#ifndef __DISTORE__NODE__COMPUTE_NODE__COMPUTE_NODE__
#define __DISTORE__NODE__COMPUTE_NODE__COMPUTE_NODE__
#include "memory/remote_memory/remote_memory.hpp"
#include "node/node.hpp"
#include "memory/memory.hpp"
#include "memory/compute_node/compute_node.hpp"
#include "kv/kv.hpp"
#include "erpc_wrapper/erpc_wrapper.hpp"
#include "debug/debug.hpp"
#include "search_layer/search_layer.hpp"
#include "data_layer/data_layer.hpp"
#include "handover_locktable/handover_locktable.hpp"
#include "stats/stats.hpp"
#include "stats/breakdown/breakdown.hpp"
#include "stats/operation/operation.hpp"
#include <chrono>
#include <infiniband/verbs.h>
#include <ratio>

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

        auto put(const std::string &key, const std::string &value, Stats::Breakdown *breakdown)
            -> bool;
        auto get(const std::string &key, Stats::Breakdown *breakdown) -> std::optional<std::string>;
        auto update(const std::string &key, const std::string &value, Stats::Breakdown *breakdown)
            -> bool;
        auto remove(const std::string &key, Stats::Breakdown *breakdown) -> bool;
        auto scan(const std::string &key, size_t count, Stats::Breakdown *breakdown) -> uint64_t;

        // always return non-null pointer as long as remote memory is not depleted
        auto allocate(size_t size) -> RemotePointer;
        // preallocate one segment;
        auto preallocate() -> bool;
        auto free(RemotePointer p) -> void;

        // for debug
        auto report_cluster_info() const noexcept -> void;
        auto dump_list() noexcept -> void;
        auto check_list() noexcept -> void;
        auto report_search_layer_stats() const -> void;

        // this method takes RDMA buffer's ownership, thus is not const
        auto report_data_layer_stats() -> void;

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

        // nodes are kept locally if total number of nodes is fewer than LOCAL_MAX_NODES
        bool remote_put;
        DataLayer::LinkedNode10 *local_nodes[Constants::LOCAL_MAX_NODES];
        std::string local_anchors[2];
        std::mutex local_mutex;

        std::map<LinkedNodeType, std::vector<double>> data_layer_stats;

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

        auto drain_pending() -> void;
        auto quick_put(const std::string &key, const std::string &value) -> bool;
        auto quick_put_pick_node(const std::string &key) -> DataLayer::LinkedNode10 *;

        auto put_dispatcher(SkipListNode *data_node, const std::string &key,
                            const std::string &value, Stats::Breakdown *breakdown)
            -> bool;
        auto put10(SkipListNode *data_node, const std::string &key, const std::string &value,
                   Stats::Breakdown *breakdown)
            -> std::pair<bool, bool>;
        auto put12(SkipListNode *data_node, const std::string &key, const std::string &value,
                   Stats::Breakdown *breakdown)
            -> std::pair<bool, bool>;
        auto put14(SkipListNode *data_node, const std::string &key, const std::string &value,
                   Stats::Breakdown *breakdown)
            -> std::pair<bool, bool>;
        auto put16(SkipListNode *data_node, const std::string &key, const std::string &value,
                   Stats::Breakdown *breakdown)
            -> std::pair<bool, bool>;


        // try to win the put and fetch remote memory to local
        // pair[0], win the competition
        // pair[1], pointer to winner's ConcurrencyContext
        template<typename NodeType>
        auto try_win_for_update(SkipListNode *data_node, Concurrency::ConcurrencyContextType t,
                                Stats::Breakdown *breakdown)
            -> std::pair<bool, Concurrency::ConcurrencyContext *>
        {

            auto thread_cctx = cctx.find(std::this_thread::get_id());
            if (thread_cctx == cctx.end()) {
                throw std::runtime_error("Threads should always register before running\n");
            }

            Concurrency::ConcurrencyContext *expect = nullptr;
            auto shared_ctx = thread_cctx->second.get();

            // context is not usable until the predecessor is also locked
            shared_ctx->type = t;

            auto r = data_node;

            if (r->ctx.compare_exchange_strong(expect, shared_ctx)) {
                // the spinning threads can now submit requests
                shared_ctx->max_depth = 4;

                // the only winner should remember to collect pending requests
                // first process winner's own request
                NodeType *buffer;
                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerFetch);
                    buffer = remote_memory_allocator.fetch_as<NodeType *>(data_node->data_node,
                                                                          sizeof(NodeType));

                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerFetch);
                } else {
                    buffer = remote_memory_allocator.fetch_as<NodeType *>(data_node->data_node,
                                                                          sizeof(NodeType));
                }
                shared_ctx->user_context = buffer;
                shared_ctx->max_depth = -1;

                return {true, shared_ctx};
            }

            return {false, expect};
        }

        template<typename NodeType>
        auto try_win_for_insert(SkipListNode *data_node, Concurrency::ConcurrencyContextType t,
                     Stats::Breakdown *breakdown)
            -> std::pair<bool, Concurrency::ConcurrencyContext *>
        {
            auto thread_cctx = cctx.find(std::this_thread::get_id());
            if (thread_cctx == cctx.end()) {
                throw std::runtime_error("Threads should always register before running\n");
            }

            Concurrency::ConcurrencyContext *expect = nullptr;
            auto shared_ctx = thread_cctx->second.get();

            // context is not usable until the predecessor is also locked
            shared_ctx->max_depth = -1;
            shared_ctx->type = t;

            auto r = data_node;
            auto l = data_node->backward;

            if (r->ctx.compare_exchange_strong(expect, shared_ctx)) {
                if (!l->ctx.compare_exchange_strong(expect, shared_ctx)) {
                    shared_ctx->max_depth = 0;
                    r->ctx = nullptr;
                    return {false, nullptr};
                }
                // the spinning threads can now submit requests
                shared_ctx->max_depth = 4;

                // the only winner should remember to collect pending requests
                // first process winner's own request
                LinkedNode16 *buffer;
                if (breakdown) {
                    // We stored the real node in the second LinkedNode16, the first is reserved for
                    // updating the predecessor's RLink after morphing or splitting
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerFetch);
                    // buffer = remote_memory_allocator.fetch_as<NodeType *>(data_node->data_node,
                    //                                                       sizeof(NodeType),
                    //                                                       sizeof(LinkedNode16));
                    // no idea why fetch_two is so slow
                    // buffer = fetch_two(l, r).first;

                    // we have only one MN, so the following code should work
                    // Fuck Mellanox that I have to use TWO contexts for authentic parallel read
                    auto rdma = remote_memory_allocator.get_rdma(l->data_node);
                    auto prdma = remote_memory_allocator.get_parallel_rdma(l->data_node);
                    rdma->post_read(r->data_node.get_as<byte_ptr_t>(), sizeof_node(r->type), sizeof(LinkedNode16));
                    prdma->post_read(l->data_node.get_as<byte_ptr_t>(), sizeof_node(l->type));
                    rdma->poll_one_completion();
                    prdma->poll_one_completion();
                    buffer = reinterpret_cast<LinkedNode16 *>(rdma->get_edible_buf());

                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerFetch);
                } else {
                    // buffer = remote_memory_allocator.fetch_as<NodeType *>(data_node->data_node,
                    //                                                       sizeof(NodeType),
                    //                                                       sizeof(LinkedNode16));
                    buffer = fetch_two(l, r).first;
                }

                shared_ctx->user_context = buffer;
                shared_ctx->max_depth = -1;

                return {true, shared_ctx};
            }

            return {false, expect};
        }

        auto help_pred(LinkedNode16 *buf, const std::string &k,
                       const std::string &v)
            -> bool
        {
            switch (buf->type) {
            case LinkedNodeType::Type16: {
                return buf->store(k, v);
            }
            case LinkedNodeType::Type14: {
                return reinterpret_cast<LinkedNode14 *>(buf)->store(k, v);
            }
            case LinkedNodeType::Type12: {
                return reinterpret_cast<LinkedNode12 *>(buf)->store(k, v);
            }
            case LinkedNodeType::Type10: {
                return reinterpret_cast<LinkedNode10 *>(buf)->store(k, v);
            }
            default:
                return false;
            }
        }

        template<typename NodeType>
        auto help_others(Concurrency::ConcurrencyContext *shared_ctx, SkipListNode *data_node,
                         LinkedNode16 *pred_buffer, NodeType *real_buffer)
            -> void
        {
            Concurrency::ConcurrencyRequests *req = nullptr;
            const std::string *k = nullptr;
            const std::string *v = nullptr;
            bool s = false;
            while (shared_ctx->requests.try_pop(req)) {
                k = reinterpret_cast<const std::string *>(req->tag);
                v = reinterpret_cast<const std::string *>(req->content);

                if (*k < data_node->anchor) {
                    s = help_pred(pred_buffer, *k, *v);
                    req->succeed = s;
                    req->retry = !s;
                    req->is_done = true;
                } else {
                    s = real_buffer->store(*k, *v);
                    if (!s) {
                        shared_ctx->requests.push(req);
                        break;
                    }
                    req->succeed = s;
                    req->retry = false;
                    req->is_done = true;
                }
            }
        }
        // pair[0], whether current key-value is put
        // pair[1], toal number of all pending requests including current key-value
        template<typename NodeType>
        auto try_put_to_existing_node(Concurrency::ConcurrencyContext *shared_ctx, SkipListNode *data_node,
                                      const std::string &key, const std::string &value,
                                      Stats::Breakdown *breakdown)
            -> std::pair<bool, size_t>
        {

            auto pred_buffer = reinterpret_cast<LinkedNode16 *>(shared_ctx->user_context);
            auto real_buffer = reinterpret_cast<NodeType *>(pred_buffer + 1);
            if (!real_buffer->store(key, value)) {
                // current key-value is not put
                return {false, shared_ctx->requests.unsafe_size() + 1};
            }

            help_others(shared_ctx, data_node, pred_buffer, real_buffer);

            if (shared_ctx->requests.unsafe_size() == 0) {
                bool ret = false;
                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerWriteBack);
                    ret = remote_memory_allocator.write_back_current(data_node->data_node, sizeof(NodeType));
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerWriteBack);
                } else {
                    ret = remote_memory_allocator.write_back_current(data_node->data_node, sizeof(NodeType));
                }
                if(ret) {
                    return {true, 0};
                } else {
                    Debug::error("Failed to write back to remote\n");
                    return {false, 0};
                }
            }
            return {true, shared_ctx->requests.unsafe_size()};

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
                          const std::string &value, Stats::Breakdown *breakdown)
            -> std::pair<bool, bool>;

        auto eager_morph(SkipListNode *data_node, LinkedNode16 *real,
                         Concurrency::ConcurrencyContext *shared_ctx,
                         const std::string &key, const std::string &value, bool done)
            -> bool;

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

        auto out_of_place_split_node(SkipListNode *data_node, LinkedNode16 *pred,
                                     LinkedNode16 *source_buffer,
                                     Concurrency::ConcurrencyContext *shared_ctx,
                                     size_t left_cap, const std::string &key,
                                     const std::string &value, bool done)
            -> std::tuple<LinkedNode16 *, LinkedNode16 *, std::string>;

        // return the address of newly allocated right
        auto write_back_morphed(SkipListNode *data_node, LinkedNode16 *pred,
                                 LinkedNode16 *morphed)
            -> RemotePointer
        {
            auto r = allocate(DataLayer::sizeof_node(morphed->type));
            auto rdma = remote_memory_allocator.get_rdma(r);
            pred->rlink = r;

            auto p_sge = rdma->generate_sge(nullptr, DataLayer::sizeof_node(pred->type), 0);
            auto r_sge = rdma->generate_sge(nullptr, DataLayer::sizeof_node(morphed->type),
                                            sizeof(LinkedNode16));

            auto wr_p = rdma->generate_send_wr(0, p_sge.get(), 1,
                                               data_node->backward->data_node.get_as<byte_ptr_t>(),
                                               nullptr);
            auto wr_r = rdma->generate_send_wr(1, r_sge.get(), 1, r.get_as<byte_ptr_t>(), nullptr);
            wr_p->send_flags = 0;
            wr_p->next = wr_r.get();

            rdma->post_batch_write(wr_p.get());

            if (auto [wc, _] = rdma->poll_one_completion(); wc != nullptr) {
                // data_node->data_node = l;
                return nullptr;
            }

            data_node->data_node = r;
            return r;
        }

        template<typename LNodeType, typename RNodeType>
        auto write_back_splitted(SkipListNode *data_node, LinkedNode16 *pred,
                                 LinkedNode16 *left, LinkedNode16 *right)
            -> RemotePointer
        {
            auto l = allocate(sizeof(LNodeType));
            auto r = allocate(sizeof(RNodeType));

            // actually we do not need a doubly-linked strucutre, the data layer is just like a
            // level in the blink tree.
            pred->rlink = l;
            right->rlink = left->rlink;
            left->rlink = r;

            // for implementation simplicity, we assume only one MN``
            auto rdma = remote_memory_allocator.get_rdma(l);
            auto p_sge = rdma->generate_sge(nullptr, DataLayer::sizeof_node(pred->type), 0);
            auto l_sge = rdma->generate_sge(nullptr, sizeof(LNodeType), sizeof(LinkedNode16));
            auto r_sge = rdma->generate_sge(nullptr, sizeof(RNodeType), 2 * sizeof(LinkedNode16));

            auto wr_p = rdma->generate_send_wr(0, p_sge.get(), 1,
                                               data_node->backward->data_node.get_as<byte_ptr_t>(),
                                               nullptr);
            auto wr_l = rdma->generate_send_wr(1, l_sge.get(), 1, l.get_as<byte_ptr_t>(), nullptr);
            auto wr_r = rdma->generate_send_wr(2, r_sge.get(), 1, r.get_as<byte_ptr_t>(), nullptr);

            wr_p->send_flags = 0;
            wr_p->next = wr_l.get();
            wr_l->send_flags = 0;
            wr_l->next = wr_r.get();

            rdma->post_batch_write(wr_p.get());

            if (auto [wc, _] = rdma->poll_one_completion(); wc != nullptr) {
                // data_node->data_node = l;
                return nullptr;
            }

            data_node->data_node = l;
            return r;
        }

        auto fetch_two(SkipListNode *left, SkipListNode *right)
            -> std::pair<LinkedNode16 *, LinkedNode16 *>
        {
            auto rdma = remote_memory_allocator.get_rdma(left->data_node);

            // auto l_sge = rdma->generate_sge(nullptr, sizeof_node(left->type), 0);
            // auto r_sge = rdma->generate_sge(nullptr, sizeof_node(right->type), sizeof(LinkedNode16));
            //
            // auto wr_l = rdma->generate_send_wr(0, l_sge.get(), 1, left->data_node.get_as<byte_ptr_t>(),
            //                                    nullptr, IBV_WR_RDMA_READ);
            // auto wr_r = rdma->generate_send_wr(0, r_sge.get(), 1, right->data_node.get_as<byte_ptr_t>(),
            //                                    nullptr, IBV_WR_RDMA_READ);

            // wr_l->send_flags = 0;
            // wr_l->next = wr_r.get();
            //
            // rdma->post_batch_read(wr_l.get());
            // if (auto [wc, _] = rdma->poll_one_completion(); wc != nullptr) {
            //     return {nullptr, nullptr};
            // }
            //

            auto bps = std::chrono::steady_clock::now();
            rdma->post_read(right->data_node.get_as<byte_ptr_t>(), sizeof_node(right->type), sizeof(LinkedNode16));
            rdma->post_read(left->data_node.get_as<byte_ptr_t>(), sizeof_node(left->type));
            auto bpe = std::chrono::steady_clock::now();
            std::cout << "batch post takes " << std::chrono::duration_cast<std::chrono::microseconds>(bpe - bps).count() << "\n";
            
            auto fs = std::chrono::steady_clock::now();
            rdma->poll_one_completion();
            auto fe = std::chrono::steady_clock::now();
            auto ss = std::chrono::steady_clock::now();
            rdma->poll_one_completion();
            auto se = std::chrono::steady_clock::now();
             
            std::cout << "First poll takes " << std::chrono::duration_cast<std::chrono::microseconds>(fe - fs).count() << "\n";
            std::cout << "Second poll takes " << std::chrono::duration_cast<std::chrono::microseconds>(se - ss).count() << "\n";

            auto buf = reinterpret_cast<LinkedNode16 *>(rdma->get_edible_buf());

            return {buf, buf + 1};
        }



        auto fetch_two_into_buffer(SkipListNode *left, SkipListNode *right,
                                   LinkedNode16 *l, LinkedNode16 *r)
            -> bool
        {
            auto [lb, rb] = fetch_two(left, right);

            if (!lb || !rb) {
                return false;
            }

            if (l) {
                memcpy(l, lb, sizeof(LinkedNode16));
            }

            if (r) {
                memcpy(r, rb, sizeof(LinkedNode16));
            }

            return true;
        }

        /*
         *
         * This method tries to fetch two data node from remote memory with batched requests
         * without waiting the completion
         *
         * The caller obtained the return value from this method own the RDMAContext and this
         * context is reserved for the batched requests posted from this method. No more
         * requests should be posted via this context before the polling, otherwise the RDMA
         * buffer would corrupt
         */
        auto fetch_two_async(SkipListNode *left, SkipListNode *right) -> RDMAContext * {
            auto rdma = remote_memory_allocator.get_rdma(left->data_node);

            auto l_sge = rdma->generate_sge(nullptr, sizeof_node(left->type), 0);
            auto r_sge = rdma->generate_sge(nullptr, sizeof_node(right->type), sizeof(LinkedNode16));

            auto wr_l = rdma->generate_send_wr(0, l_sge.get(), 1, left->data_node.get_as<byte_ptr_t>(),
                                               nullptr, IBV_WR_RDMA_READ);
            auto wr_r = rdma->generate_send_wr(0, l_sge.get(), 1, right->data_node.get_as<byte_ptr_t>(),
                                               nullptr, IBV_WR_RDMA_READ);

            wr_l->send_flags = 0;
            wr_l->next = wr_r.get();

            rdma->post_batch_read(wr_l.get());

            return rdma;
        }

        /*
         * This method polls the RDMAContext obtained from fet_two_async. The return values are
         * two pointers pointing the the underlying RDMA buffer of the RDMAContext. To reuse the
         * RDMA buffer while reserving the content in the buffer, please copy the content to other
         * places
         */
        auto poll_fetch_two_async(RDMAContext *rdma, LinkedNode16 &l, LinkedNode16 &r) -> bool {
            if (auto [wc, _] = rdma->poll_one_completion(); wc != nullptr) {
                return false;
            }

            auto buf = reinterpret_cast<const LinkedNode16 *>(rdma->get_buf());
            memcpy(&l, buf, sizeof(LinkedNode16));
            memcpy(&r, buf + 1, sizeof(LinkedNode16));

            return true;
        }


        auto async_update(SkipListNode *data_node, const std::string &anchor,
                          LinkedNodeType t, RemotePointer r) -> void;
    };
}
#endif
