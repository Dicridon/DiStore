#include "compute_node.hpp"
namespace DiStore::Cluster {
    auto ComputeNode::initialize(const std::string &compute_config,
                                 const std::string &memory_config)
        -> bool
    {
        std::ifstream file(compute_config);
        if (!file.is_open()) {
            Debug::error("Faild to open config file %s\n", compute_config.c_str());
            return false;
        }

        if (!NodeInfo::initialize(file, &self_info)) {
            Debug::error("Failed to initailize node\n");
            return false;
        }


        if (!initialize_erpc()) {
            return false;
        }

        if (!initialize_rdma_dev(file)) {
            return false;
        }

        remote_memory_allocator.parse_config_file(memory_config);
        if (!remote_memory_allocator.connect_memory_nodes(compute_ctx)) {
            return false;
        }

        auto self = self_info.tcp_addr.to_uri(self_info.tcp_port);

        remote_put = false;
        local_nodes[0] = new LinkedNode10;
        local_nodes[1] = new LinkedNode10;

        Debug::info("Compute node %s is intialized\n", self.c_str());

        std::thread([&] {
            while (true) {
                CalibrateContext *cal;
                if (update_queue.try_pop(cal)) {
                    this->slist.calibrate(cal->new_node, cal->level);
                }
            }
        }).detach();
        return true;
    }

    auto ComputeNode::register_thread() -> bool {
        // we always use the this_thread::get_id() for thread id;
        if (!remote_memory_allocator.setup_rdma_per_thread(rdma_dev.get())) {
            Debug::error("Failed to setup rdma for each thread\n");
            return false;
        }

        std::scoped_lock<std::mutex> _(local_mutex);
        cctx.insert({std::this_thread::get_id(),
                std::make_unique<Concurrency::ConcurrencyContext>()});

        return true;
    }

    auto ComputeNode::put(const std::string &key, const std::string &value,
                          Stats::Breakdown *breakdown)
        -> bool
    {
        if (!remote_put) {
            if (quick_put(key, value))
                return true;
            // allow remote put
        }

        SkipListNode *data_node = nullptr;
        // Compiler will eliminate this branch if nullptr of breakdown is given
        if (breakdown) {
            breakdown->begin(Stats::DiStoreBreakdownOps::SearchLayerSearch);
            data_node = slist.fuzzy_search(key);
            breakdown->end(Stats::DiStoreBreakdownOps::SearchLayerSearch);
        } else {
            data_node = slist.fuzzy_search(key);
        }

        if (!data_node) {
            std::runtime_error("Impossible to get nullptr from"
                               "slist since remote_put is enabled\n");
        }

        return put_dispatcher(data_node, key, value, breakdown);
    }

    auto ComputeNode::get(const std::string &key, Stats::Breakdown *breakdown)
        -> std::optional<std::string>
    {
        if (!remote_put) {
            std::scoped_lock<std::mutex> _(local_mutex);
            if (!local_anchors[1].empty() && key >= local_anchors[1]) {
                return local_nodes[1]->find(key);
            } else {
                return local_nodes[0]->find(key);
            }
        }

    retry:

        // drain pending requests that the last operation hasn't processed
        drain_pending();

        SkipListNode *node = nullptr;

        if (breakdown) {
            breakdown->begin(Stats::DiStoreBreakdownOps::SearchLayerSearch);
            node = slist.fuzzy_search(key);
            breakdown->end(Stats::DiStoreBreakdownOps::SearchLayerSearch);
        } else {
            node = slist.fuzzy_search(key);
        }

        // we don't have to find the corrent fetch_as type since remote memory is completely
        // exposed to us
        LinkedNode16 *buffer = nullptr;
        if (breakdown) {
            breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerFetch);
            buffer = remote_memory_allocator.fetch_as<LinkedNode16 *>(node->data_node,
                                                                      sizeof(LinkedNode16));
            breakdown->end(Stats::DiStoreBreakdownOps::DataLayerFetch);
        } else {
            buffer = remote_memory_allocator.fetch_as<LinkedNode16 *>(node->data_node,
                                                                      sizeof(LinkedNode16));
        }

        auto crc = crc_validate(buffer, node->type);
        if (crc != buffer->crc)
            goto retry;

        return buffer->find(key);
    }

    auto ComputeNode::update(const std::string &key, const std::string &value,
                             Stats::Breakdown *breakdown)
        -> bool
    {
    retry:
        if (!remote_put) {
            std::scoped_lock<std::mutex> _(local_mutex);
            if (key > local_anchors[1]) {
                return local_nodes[1]->update(key, value);
            } else {
                return local_nodes[0]->update(key, value);
            }
        }

        SkipListNode *node = nullptr;
        if (breakdown) {
            breakdown->begin(Stats::DiStoreBreakdownOps::SearchLayerSearch);
            node = slist.fuzzy_search(key);
            breakdown->end(Stats::DiStoreBreakdownOps::SearchLayerSearch);
        } else {
            node = slist.fuzzy_search(key);
        }

        if (node == nullptr)
            return false;

        auto ret = true;

        drain_pending();
        // we don't have to find the corrent fetch_as type since remote memory is completely
        // exposed to us
        auto [win, shared_ctx] = try_win<LinkedNode16>(node,
                                                       Concurrency::ConcurrencyContextType::Update,
                                                       breakdown);
        if (win) {
            LinkedNode16 *buffer = reinterpret_cast<LinkedNode16 *>(shared_ctx->user_context);
            ret = buffer->update(key, value);

            Concurrency::ConcurrencyRequests *req;
            while(shared_ctx->requests.try_pop(req)) {
                req->succeed = buffer->update(*reinterpret_cast<const std::string *>(req->tag),
                                              *reinterpret_cast<const std::string *>(req->content));
                req->retry = false;
                req->is_done = true;
            }

            buffer->crc = crc_validate(buffer, buffer->type);
            
            if (breakdown) {
                breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerWriteBack);
                remote_memory_allocator.write_to(node->data_node, DataLayer::sizeof_node(buffer->type));
                breakdown->end(Stats::DiStoreBreakdownOps::DataLayerWriteBack);
            } else {
                remote_memory_allocator.write_to(node->data_node, DataLayer::sizeof_node(buffer->type));
            }

            node->ctx = nullptr;
            shared_ctx->max_depth = 4;
        } else {
            if (shared_ctx->type != Concurrency::ConcurrencyContextType::Update)
                return false;

            if (auto [stat, retry] = failed_write(shared_ctx, key, value, breakdown);
                retry == true) {
                goto retry;
            } else {
                return stat;
            }
        }

        return ret;
    }

    auto ComputeNode::scan(const std::string &key, size_t count, Stats::Breakdown *breakdown)
        -> uint64_t
    {
        auto total = 0UL;
        auto first = slist.fuzzy_search(key);
        std::vector<std::string> ret;

        if (first == nullptr) {
            return {};
        }

        auto second = first->forwards[0];
        LinkedNode16 l[2], r[2];
        RDMAContext *rdma = nullptr;
        uint8_t flip = 0;
        if (second && key <= second->anchor) {
            fetch_two(first, second, l[flip], r[flip]);
        } else {
            auto n = remote_memory_allocator.fetch_as<LinkedNode16 *>(first->data_node, sizeof(LinkedNode16));
            total = n->scan(key, count, ret);
            return total;
        }

        do {
            first = second->forwards[0];
            if (first == nullptr) {
                poll_fetch_two_async(rdma, r[flip], l[flip]);
                total += r[flip].scan(key, count - total, ret);
                total += l[flip].scan(key, count - total, ret);
                return total;
            }

            second = first->forwards[0];
            if (second == nullptr) {
                total += r[flip].scan(key, count - total, ret);
                total += l[flip].scan(key, count - total, ret);
                auto n = remote_memory_allocator.fetch_as<LinkedNode16 *>(first->data_node, sizeof(LinkedNode16));
                n->scan(key, count, ret);
                return total;
            }

            rdma = fetch_two_async(first, second);
            total += r[flip].scan(key, count - total, ret);
            total += l[flip].scan(key, count - total, ret);
            flip = (flip + 1) & 0x1;
            poll_fetch_two_async(rdma, r[flip], l[flip]);
        } while (total < count);

        return total;
    }

    auto ComputeNode::allocate(size_t size) -> RemotePointer {
        auto remote = allocator.allocate(size);
        if (remote.is_nullptr()) {
            auto new_seg = remote_memory_allocator.offer_remote_segment();
            auto base = remote_memory_allocator.get_base_addr(new_seg.get_node());
            allocator.apply_for_memory(new_seg, base);

            remote = allocator.allocate(size);
        }

        return remote;
    }

    auto ComputeNode::preallocate() -> bool {
        auto new_seg = remote_memory_allocator.offer_remote_segment();
        auto base = remote_memory_allocator.get_base_addr(new_seg.get_node());
        allocator.apply_for_memory(new_seg, base);
        return true;
    }

    auto ComputeNode::free(RemotePointer p)  -> void {
        allocator.free(p);
    }

    auto ComputeNode::drain_pending() -> void {
        auto thread_cctx = cctx.find(std::this_thread::get_id());
        auto shared_ctx = thread_cctx->second.get();
        Concurrency::ConcurrencyRequests *req;
        while (shared_ctx->requests.try_pop(req)) {
            req->succeed = false;
            req->retry = true;
            req->is_done = true;
        }
    }

    auto ComputeNode::quick_put(const std::string &key, const std::string &value) -> bool {
        RemotePointer larger, smaller;
        static LinkedNode12 remote;
        std::scoped_lock<std::mutex> _(local_mutex);
        if (remote_put) {
            return false;
        }

        LinkedNode10 *to_target = quick_put_pick_node(key);
        LinkedNode10 *no_move = nullptr;

        if (to_target->store(key, value))
            return true;

        smaller = allocate(sizeof(LinkedNode10));
        larger = allocate(sizeof(LinkedNode12));

        // time to flush to remote
        if (to_target == local_nodes[0]) {
            remote.llink = nullptr;
            remote.rlink = smaller;
            no_move = local_nodes[1];
            no_move->llink = larger;
            no_move->rlink = nullptr;
        } else {
            remote.llink = smaller;
            remote.rlink = nullptr;
            no_move = local_nodes[0];
            no_move->llink = nullptr;
            no_move->rlink = larger;
        }

        memcpy(remote.fingerprints, to_target->fingerprints, sizeof(to_target->fingerprints));
        memcpy(remote.pairs, to_target->pairs, sizeof(to_target->pairs));
        remote.next = to_target->next;
        remote.store(key, value);


        remote.crc = crc_validate(reinterpret_cast<LinkedNode16 *>(&remote), remote.type);
        if (!remote_memory_allocator.write_to(larger, sizeof(LinkedNode12),
                                              reinterpret_cast<byte_ptr_t>(&remote))) {
            Debug::error("Failed to flush local nodes to remote at early stage\n");
            return false;
        }

        remote.crc = crc_validate(reinterpret_cast<LinkedNode16 *>(&no_move), no_move->type);
        if (!remote_memory_allocator.write_to(smaller, sizeof(LinkedNode10),
                                              reinterpret_cast<byte_ptr_t>(no_move))) {
            Debug::error("Failed to flush local nodes to remote at early stage\n");
            return false;
        }

        // should only update search layer after local nodes being flushed to remote
        if (to_target == local_nodes[0]) {
            slist.insert(local_anchors[0], larger, LinkedNodeType::Type12);
            slist.insert(local_anchors[1], smaller, LinkedNodeType::Type10);
        } else {
            slist.insert(local_anchors[0], smaller, LinkedNodeType::Type10);
            slist.insert(local_anchors[1], larger, LinkedNodeType::Type12);
        }

        remote_put = true;

        return true;
    }

    auto ComputeNode::quick_put_pick_node(const std::string &key) -> DataLayer::LinkedNode10 * {
        if (local_anchors[0].empty()) {
            local_anchors[0] = key;
            return local_nodes[0];
        }

        DataLayer::LinkedNode10 *to_target = nullptr;
        auto comp = key.compare(local_anchors[0]);
        // start stage
        // don't migrate, just use the second node as the smaller one
        if (comp < 0) {
            if (local_anchors[1].empty()) {
                std::swap(local_nodes[0], local_nodes[1]);
                std::swap(local_anchors[0], local_anchors[1]);
            }

            local_anchors[0] = key;
            to_target = local_nodes[0];
        } else {
            if (local_anchors[1].empty()) {
                local_anchors[1] = key;
                to_target = local_nodes[1];
            } else if (key > local_anchors[1]) {
                to_target = local_nodes[1];
            } else {
                to_target = local_nodes[0];
            }
        }

        return to_target;
    }

    auto ComputeNode::put_dispatcher(SkipListNode *data_node, const std::string &key,
                                     const std::string &value, Stats::Breakdown *breakdown)
        -> bool
    {
    retry:
        switch (data_node->type){
        case LinkedNodeType::Type10:
            if (auto [ret, retry] = put10(data_node, key, value, breakdown); retry == true) {
                goto retry;
            } else {
                return ret;
            }
        case LinkedNodeType::Type12:
            if (auto [ret, retry] = put12(data_node, key, value, breakdown); retry == true) {
                goto retry;
            } else {
                return ret;
            }
        case LinkedNodeType::Type14:
            if (auto [ret, retry] = put14(data_node, key, value, breakdown); retry == true) {
                goto retry;
            } else {
                return ret;
            }
        case LinkedNodeType::Type16:
            if (auto [ret, retry] = put16(data_node, key, value, breakdown); retry == true) {
                goto retry;
            } else {
                return ret;
            }
        default:
            /*
             * If we reach here, it's likely that a key smaller than any key in the
             * current dataset will be inserted, thus the fuzzy_search returns the
             * head of the skiplist. This can result in competition on the first
             * data node and we need to ensure the smallest key in the competition
             * keys are used to update the anchor key. We don't handle this case,
             * instead, we insert the smallest key in the benchmark before
             * benchmarking to avoid such case.
             */
            throw std::runtime_error("Varaible-sized node not supported");
        }
    }

    // 10 + 5 -> 16
    auto ComputeNode::put10(SkipListNode *data_node, const std::string &key, const std::string &value,
                            Stats::Breakdown *breakdown)
        -> std::pair<bool, bool>
    {
        bool ret = true;
        drain_pending();
        auto [win, shared_ctx] = try_win<LinkedNode10>(data_node,
                                                       Concurrency::ConcurrencyContextType::Insert,
                                                       breakdown);
        if (!win) {
            if (shared_ctx->type != Concurrency::ConcurrencyContextType::Insert)
                return {false, true};

            return failed_write(shared_ctx, key, value, breakdown);
        }

        auto [done, pendings] = try_put_to_existing_node<LinkedNode10>(shared_ctx,
                                                                       data_node,
                                                                       key, value,
                                                                       breakdown);

        LinkedNode16 *real = reinterpret_cast<LinkedNode16 *>(shared_ctx->user_context);
        if (pendings == 0) {
            ret = true;
        } else {
            if (!done)
                real->store(key, value);

            Concurrency::ConcurrencyRequests *req = nullptr;
            while (shared_ctx->requests.try_pop(req)) {
                // space is guaranteed to be sufficient
                req->succeed = real->store(*reinterpret_cast<const std::string *>(req->tag),
                                           *reinterpret_cast<const std::string *>(req->content));
                req->retry = false;
                req->is_done = true;
            }

            if (breakdown) {
                breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerMorph);
                real->type = morph_node(real);
                breakdown->end(Stats::DiStoreBreakdownOps::DataLayerMorph);
            } else {
                real->type = morph_node(real);
            }
            auto real_size = DataLayer::sizeof_node(real->type);
            auto remote = allocate(real_size);

            real->crc = crc_validate(real, real->type);

            bool sta = false;
            if (breakdown) {
                breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerWriteBack);
                sta = remote_memory_allocator.write_to(remote, real_size);
                breakdown->end(Stats::DiStoreBreakdownOps::DataLayerWriteBack);
            } else {
                sta = remote_memory_allocator.write_to(remote, real_size);
            }

            if (!sta) {
                Debug::error("Failed to put morphed node to remote\n");
                ret = false;
            }

            data_node->data_node = remote;
            data_node->type = real->type;
        }

        // leave
        // the order is important to avoid pening requests in the queue
        // e.g., winner resets shared_ctx and a peer thread notice this,
        // then a requests is enqueued, but this winner will not process
        // it.
        data_node->ctx.store(nullptr);
        shared_ctx->max_depth = 4;
        return {ret, false};
    }

    // 12 + 5 -> 10 + 10
    auto ComputeNode::put12(SkipListNode *data_node, const std::string &key, const std::string &value,
                            Stats::Breakdown *breakdown)
        -> std::pair<bool, bool>
    {
        bool ret = true;
        drain_pending();
        auto [win, shared_ctx] = try_win<LinkedNode12>(data_node,
                                                       Concurrency::ConcurrencyContextType::Insert,
                                                       breakdown);

        if (!win) {
            if (shared_ctx->type != Concurrency::ConcurrencyContextType::Insert)
                return {false, true};

            return failed_write(shared_ctx, key, value, breakdown);
        }

        auto [done, pendings] = try_put_to_existing_node<LinkedNode12>(shared_ctx, data_node, key,
                                                                       value, breakdown);
        if (pendings == 0) {
            ret = true;
        } else {
            // eager morphing to a Node16
            LinkedNode16 *real = reinterpret_cast<LinkedNode16 *>(shared_ctx->user_context);
            if (pendings <= 4) {
                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerMorph);
                    ret = eager_morph(data_node, shared_ctx, key, value, done);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerMorph);
                } else {
                    ret = eager_morph(data_node, shared_ctx, key, value, done);
                }
            } else {
                LinkedNode16 *left = nullptr, *right = nullptr;
                std::string ranchor;

                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerSplit);
                    std::tie(left, right, ranchor) = out_of_place_split_node(real, shared_ctx, 9,
                                                                             key, value, done);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerSplit);
                } else {
                    std::tie(left, right, ranchor) = out_of_place_split_node(real, shared_ctx, 9,
                                                                             key, value, done);
                }

                left->type = LinkedNodeType::Type10;
                right->type = LinkedNodeType::Type10;
                left->crc = crc_validate(left, left->type);
                right->crc = crc_validate(right, right->type);

                RemotePointer r;
                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerWriteBackTwo);
                    r = write_back_two<LinkedNode10, LinkedNode10>(data_node, left, right);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerWriteBackTwo);
                } else {
                    r = write_back_two<LinkedNode10, LinkedNode10>(data_node, left, right);
                }

                if (r.is_nullptr()) {
                    ret =  false;
                } else {
                    // update_queue.push({ranchor, LinkedNodeType::Type10, r});
                    async_update(data_node, ranchor, LinkedNodeType::Type10, r);
                    ret = true;
                }

                data_node->type = left->type;
            }
        }

        data_node->ctx.store(nullptr);
        shared_ctx->max_depth = 4;
        return {ret, false};
    }

    // 14 + 5 -> 10 + 12
    auto ComputeNode::put14(SkipListNode *data_node, const std::string &key, const std::string &value,
                            Stats::Breakdown *breakdown)
        -> std::pair<bool, bool>
    {
        bool ret = true;
        drain_pending();
        auto [win, shared_ctx] = try_win<LinkedNode14>(data_node,
                                                       Concurrency::ConcurrencyContextType::Insert,
                                                       breakdown);

        if (!win) {
            if (shared_ctx->type != Concurrency::ConcurrencyContextType::Insert)
                return {false, true};

            return failed_write(shared_ctx, key, value, breakdown);
        }

        auto [done, pendings] = try_put_to_existing_node<LinkedNode14>(shared_ctx,
                                                                       data_node,
                                                                       key, value,
                                                                       breakdown);
        if (pendings == 0) {
            ret =  true;
        } else {
            // eager morphing to a Node16
            LinkedNode16 *real = reinterpret_cast<LinkedNode16 *>(shared_ctx->user_context);
            if (pendings <= 2) {
                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerMorph);
                    ret = eager_morph(data_node, shared_ctx, key, value, done);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerMorph);
                } else {
                    ret = eager_morph(data_node, shared_ctx, key, value, done);
                }
            } else {
                LinkedNode16 *left = nullptr, *right = nullptr;
                std::string ranchor;

                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerSplit);
                    std::tie(left, right, ranchor) = out_of_place_split_node(real, shared_ctx, 8,
                                                                             key, value, done);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerSplit);
                } else {
                    std::tie(left, right, ranchor) = out_of_place_split_node(real, shared_ctx, 8,
                                                                             key, value, done);
                }

                left->type = LinkedNodeType::Type10;
                right->type = LinkedNodeType::Type12;
                left->crc = crc_validate(left, left->type);
                right->crc = crc_validate(right, right->type);

                RemotePointer r;
                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerWriteBackTwo);
                    r = write_back_two<LinkedNode10, LinkedNode10>(data_node, left, right);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerWriteBackTwo);
                } else {
                    r = write_back_two<LinkedNode10, LinkedNode10>(data_node, left, right);
                }

                if (r.is_nullptr()) {
                    ret = false;
                } else {
                    // update_queue.push({ranchor, LinkedNodeType::Type12, r});
                    async_update(data_node, ranchor, LinkedNodeType::Type12, r);
                    ret = true;
                }

                data_node->type = left->type;
            }
        }

        data_node->ctx.store(nullptr);
        shared_ctx->max_depth = 4;
        return {ret, false};
    }

    // 16 + 5 -> 12 + 12
    auto ComputeNode::put16(SkipListNode *data_node, const std::string &key, const std::string &value,
                            Stats::Breakdown *breakdown)
        -> std::pair<bool, bool>
    {
        bool ret = true;
        drain_pending();
        auto [win, shared_ctx] = try_win<LinkedNode16>(data_node,
                                                       Concurrency::ConcurrencyContextType::Insert,
                                                       breakdown);

        if (!win) {
            if (shared_ctx->type != Concurrency::ConcurrencyContextType::Insert)
                return {false, true};

            return failed_write(shared_ctx, key, value, breakdown);
        }

        auto [done, pendings] = try_put_to_existing_node<LinkedNode16>(shared_ctx,
                                                                       data_node,
                                                                       key, value,
                                                                       breakdown);
        if (pendings == 0) {
            ret = true;
        } else {
            LinkedNode16 *real = reinterpret_cast<LinkedNode16 *>(shared_ctx->user_context);
            std::string ranchor;
            LinkedNode16 *right, *left;
            RemotePointer r;
            if (pendings <= 2) {
                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerSplit);
                    std::tie(left, right, ranchor) = out_of_place_split_node(real, shared_ctx, 9,
                                                                             key, value, done);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerSplit);
                } else {
                    std::tie(left, right, ranchor) = out_of_place_split_node(real, shared_ctx, 9,
                                                                             key, value, done);
                }

                left->type = LinkedNodeType::Type10;
                right->type = LinkedNodeType::Type10;
                left->crc = crc_validate(left, left->type);
                right->crc = crc_validate(right, right->type);

                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerWriteBackTwo);
                    r = write_back_two<LinkedNode10, LinkedNode10>(data_node, left, right);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerWriteBackTwo);
                } else {
                    r = write_back_two<LinkedNode10, LinkedNode10>(data_node, left, right);
                }
            } else if(pendings <= 4) {
                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerSplit);
                    std::tie(left, right, ranchor) = out_of_place_split_node(real, shared_ctx, 9,
                                                                             key, value, done);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerSplit);
                } else {
                    std::tie(left, right, ranchor) = out_of_place_split_node(real, shared_ctx, 9,
                                                                             key, value, done);
                }

                left->type = LinkedNodeType::Type10;
                right->type = LinkedNodeType::Type12;
                left->crc = crc_validate(left, left->type);
                right->crc = crc_validate(right, right->type);

                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerWriteBackTwo);
                    r = write_back_two<LinkedNode10, LinkedNode12>(data_node, left, right);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerWriteBackTwo);
                } else {
                    r = write_back_two<LinkedNode10, LinkedNode12>(data_node, left, right);
                }
            } else {
                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerSplit);
                    std::tie(left, right, ranchor) = out_of_place_split_node(real, shared_ctx, 10,
                                                                             key, value, done);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerSplit);
                } else {
                    std::tie(left, right, ranchor) = out_of_place_split_node(real, shared_ctx, 10,
                                                                             key, value, done);
                }

                left->type = LinkedNodeType::Type12;
                right->type = LinkedNodeType::Type12;
                left->crc = crc_validate(left, left->type);
                right->crc = crc_validate(right, right->type);

                if (breakdown) {
                    breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerWriteBackTwo);
                    r = write_back_two<LinkedNode12, LinkedNode12>(data_node, left, right);
                    breakdown->end(Stats::DiStoreBreakdownOps::DataLayerWriteBackTwo);
                } else {
                    r = write_back_two<LinkedNode12, LinkedNode12>(data_node, left, right);
                }
            }

            if (r.is_nullptr())
                ret =  false;
            else
                // update_queue.push({ranchor, right->type, r});
                async_update(data_node, ranchor, right->type, r);

            data_node->type = left->type;
        }

        data_node->ctx.store(nullptr);
        shared_ctx->max_depth = 4;
        return {ret, false};
    }

    auto ComputeNode::failed_write(Concurrency::ConcurrencyContext *cctx,
                                   const std::string &key, const std::string &value,
                                   Stats::Breakdown *breakdown)
        -> std::pair<bool, bool>
    {
        auto depth = cctx->max_depth.fetch_sub(1);
        if (depth > 0) {
            if (breakdown) {
                breakdown->begin(Stats::DiStoreBreakdownOps::DataLayerContention);
            }
            auto req = new Concurrency::ConcurrencyRequests;
            req->tag = &key;
            req->content = &value;
            cctx->requests.emplace(req);

            while (!req->is_done)
                ;

            if (breakdown) {
                breakdown->end(Stats::DiStoreBreakdownOps::DataLayerContention);
            }
            return {req->succeed, req->retry};
        } else {
            // competition failed, should retry
            return {false, true};
        }
    }

    auto ComputeNode::eager_morph(SkipListNode *data_node,
                                  Concurrency::ConcurrencyContext *shared_ctx,
                                  const std::string &key, const std::string &value,
                                  bool done)
        -> bool
    {
        LinkedNode16 *real = reinterpret_cast<LinkedNode16 *>(shared_ctx->user_context);

        if (!done)
            real->store(key, value);
        real->type = LinkedNodeType::Type16;
        real->crc = crc_validate(real, real->type);

        Concurrency::ConcurrencyRequests *req = nullptr;
        while (shared_ctx->requests.try_pop(req)) {
            // space is guaranteed to be sufficient
            req->succeed = real->store(*reinterpret_cast<const std::string *>(req->tag),
                                       *reinterpret_cast<const std::string *>(req->content));
            req->retry = false;
            req->is_done = true;
        }

        auto remote = allocate(sizeof(LinkedNode16));
        if (!remote_memory_allocator.write_to(remote, sizeof(LinkedNode16))) {
            Debug::error("Failed to write back to remote after eager morphing\n");
            return false;
        }

        data_node->data_node = remote;
        data_node->type = LinkedNodeType::Type16;

        return true;
    }

    auto ComputeNode::inplace_split_node(LinkedNode16 *source_buffer, size_t left_cap)
            -> std::tuple<LinkedNode16 *, LinkedNode16 *, std::string>
    {
        auto total_records = source_buffer->next;
        auto left = source_buffer;
        // we have sufficient buffer
        auto right = left + 1;

        int reorder_map[16] = {-1};
        bool picked[16] = {false};

        construct_reorder_map(source_buffer, left_cap, reorder_map, picked);
        auto right_anchor = std::string((char *)source_buffer->pairs[reorder_map[left_cap]].key,
                                        DataLayer::Constants::KEYLEN);
        picked[reorder_map[left_cap]] = false;

        // partially sorted, start migrating
        right->next = 0;
        for (size_t i = 0; i < total_records; i++) {
            if (picked[i])
                continue;

            right->store(left->pairs[i].key, DataLayer::Constants::KEYLEN,
                         left->pairs[i].value, DataLayer::Constants::VALLEN);
        }

        // compact the old node and calibrate metadata
        for (size_t i = 0; i < total_records; i++) {
            if (picked[i])
                continue;

            for (size_t j = i; j < total_records; j++) {
                if (picked[j]) {
                    source_buffer->fingerprints[i] = source_buffer->fingerprints[j];
                    memcpy(source_buffer->pairs[i].key, source_buffer->pairs[j].key,
                           DataLayer::Constants::KEYLEN);
                    memcpy(source_buffer->pairs[i].value, source_buffer->pairs[j].value,
                           DataLayer::Constants::KEYLEN);
                    picked[i] = true;
                    picked[j] = false;
                }
            }
        }
        left->next = left_cap;

        return {left, right, right_anchor};
    }

    auto ComputeNode::out_of_place_split_node(LinkedNode16 *source_buffer,
                                              Concurrency::ConcurrencyContext *shared_ctx,
                                              size_t left_cap, const std::string &key,
                                              const std::string &value, bool done)
        -> std::tuple<LinkedNode16 *, LinkedNode16 *, std::string>
    {
        BufferNode tmp_node;
        tmp_node.next = source_buffer->next;
        memcpy(tmp_node.fingerprints, source_buffer->fingerprints, sizeof(source_buffer->fingerprints));
        memcpy(tmp_node.pairs, source_buffer->pairs, sizeof(source_buffer->pairs));

        if (!done)
            tmp_node.store(key, value);

        Concurrency::ConcurrencyRequests *req = nullptr;
        while (shared_ctx->requests.try_pop(req)) {
            // space is guaranteed to be sufficient
            req->succeed = tmp_node.store(*reinterpret_cast<const std::string *>(req->tag),
                                          *reinterpret_cast<const std::string *>(req->content));
            req->retry = false;
            req->is_done = true;
        }

        int reorder_map[32] = {-1};
        bool picked[32] = {false};

        // construct_reorder_map(source_buffer, left_cap, reorder_map, picked);
        construct_reorder_map(&tmp_node, left_cap, reorder_map, picked);
        auto right_anchor = std::string((char *)tmp_node.pairs[reorder_map[left_cap]].key,
                                        DataLayer::Constants::KEYLEN);
        // left node should not take the anchor key of right node
        picked[reorder_map[left_cap]] = false;

        auto left = source_buffer;
        auto right = source_buffer + 1;

        left->next = 0;
        right->next = 0;

        for (size_t i = 0; i < tmp_node.next; i++) {
            if (picked[i]) {
                left->fingerprints[left->next] = tmp_node.fingerprints[i];
                memcpy(&left->pairs[left->next], &tmp_node.pairs[i], sizeof(DataLayer::KV));
                ++left->next;
            } else {
                right->fingerprints[right->next] = tmp_node.fingerprints[i];
                memcpy(&right->pairs[right->next], &tmp_node.pairs[i], sizeof(DataLayer::KV));
                ++right->next;
            }
        }

        return {left, right, right_anchor};
    }

    auto ComputeNode::async_update(SkipListNode *data_node, const std::string &anchor,
                                   LinkedNodeType t, RemotePointer r)
        -> void
    {
        auto [new_node, level] = SkipList::make_new_node(anchor, r, t);

        new_node->forwards[0] = data_node->forwards[0];
        data_node->forwards[0] = new_node;
        new_node->backward = data_node;

        if (level == 1) {
            return;
        }

        auto req = new CalibrateContext;
        req->level = level;
        req->new_node = new_node;

        update_queue.push(req);
    }


    // for debug
    auto ComputeNode::report_cluster_info() const noexcept -> void {
        Debug::info("Node Reporting cluster info\n");
        self_info.dump();
        remote_memory_allocator.dump();
    }

    auto ComputeNode::dump_list() noexcept -> void {
        slist.dump();

        auto walker = slist.iter();

        while (walker->forwards[0]) {
            walker = walker->forwards[0];
            auto buffer = remote_memory_allocator.fetch_as<LinkedNode16 *>(walker->data_node,
                                                                           sizeof(LinkedNode16));

            std::cout << ">> Anchor: " << walker->anchor << "\n";
            buffer->dump();
        }
     }

    auto ComputeNode::check_list() noexcept -> void {
        slist.dump();

        auto walker = slist.iter();

        while (walker->forwards[0]) {
            walker = walker->forwards[0];
            auto buffer = remote_memory_allocator.fetch_as<LinkedNode16 *>(walker->data_node,
                                                                           sizeof(LinkedNode16));

            std::cout << ">> Anchor: " << walker->anchor << "\n";
            buffer->check();
        }
    }

    auto ComputeNode::report_search_layer_stats() const -> void {
        Debug::info("Reporting search layer stats\n");
        slist.show_levels();
    }

    auto ComputeNode::report_data_layer_stats() -> void {
        Debug::info("Reporting search layer stats\n");
        Debug::info("Collecting stats via network, which will take some time\n");
        auto iter = slist.iter();

        while(iter->forwards[0]) {
            auto buffer = remote_memory_allocator.fetch_as<LinkedNode16 *>(iter->forwards[0]->data_node,
                                                                           sizeof(LinkedNode16));
            data_layer_stats[buffer->type].push_back(buffer->usage());
            iter = iter->forwards[0];
        }

        auto total = 0.0;
        for (auto &[k, v] : data_layer_stats) {
            std::sort(v.begin(), v.end());
            total += v.size();
            std::cout << ">> Type " << k << " usage: "
                      << "avg: " << Misc::avg(v) << ", "
                      << "p50: " << Misc::p50(v) << ", "
                      << "p90: " << Misc::p90(v) << ", "
                      << "p99: " << Misc::p99(v) << "\n";
        }

        Debug::info("Node type distribution\n");
        for (auto &[k, v] : data_layer_stats) {
            std::cout << ">> Type " << k << ": " << v.size() / total << "\n";
        }
    }
}
