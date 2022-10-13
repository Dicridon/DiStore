#include "compute_node.hpp"

namespace DiStore::Memory {
    auto PageMirror::allocate() -> RemotePointer {
        auto base = page_base;
        --desc.empty_slots;
        return base.offset((desc.offset++) * allocation_class_size_map[desc.allocation_class]);
    }

    auto PageMirror::available() -> bool {
        auto total = Constants::MEMORY_PAGE_SIZE / allocation_class_size_map[desc.allocation_class];
        if (desc.offset >= total) {
            return false;
        }
        return true;
    }

    auto PageMirror::free(RemotePointer ptr) -> bool {
        if (page_base != ptr.page())
            return false;

        // TODO: add reclaimation update to memory node
        // Alternatively, we can trigger gloabl GC on memory node to recycle
        return ++desc.empty_slots;
    }

    auto PageGroup::allocate(AllocationClass ac) -> RemotePointer {
        for (auto &p : pages) {
            if (p->desc.allocation_class == ac && p->available()) {
                return p->allocate();
            } else if (p->desc.allocation_class == AllocationClass::ChunkUnknown) {
                p->desc.initialize(ac);
                return p->allocate();
            }
        }

        return nullptr;
    }

    auto PageGroup::available(AllocationClass ac) -> Enums::MemoryAllocationStatus {
        bool have_ac = false;
        for (size_t i = 0; i < Constants::PAGEGROUP_NO; i++) {
            auto p = pages[i];
            if (p->desc.allocation_class == AllocationClass::ChunkUnknown) {
                return Enums::MemoryAllocationStatus::Ok;
            }

            if (p->desc.allocation_class == ac) {
                if (p->available()) {
                    return Enums::MemoryAllocationStatus::Ok;
                } else {
                    have_ac = true;
                }
            }
        }

        if (have_ac)
            return Enums::MemoryAllocationStatus::EmptyPage;
        return Enums::MemoryAllocationStatus::EmptyPageGroup;
    }

    auto SegmentTracker::offer_page() -> PageMirror * {
        auto ret = new PageMirror;
        ret->desc.clear();
        auto page = current->seg.offset_by(current->offset * Constants::MEMORY_PAGE_SIZE);
        auto off = page - current->base_addr;
        ret->page_id = (off / Constants::MEMORY_PAGE_SIZE) - 1;
        ret->page_base = page;

        current->mirrors.insert({page, ret});

        --current->available_pages;
        ++current->offset;
        return ret;
    }

    auto SegmentTracker::offer_page_group() -> PageGroup * {
        auto group = new PageGroup;

        for (size_t i = 0; i < Constants::PAGEGROUP_NO; i++) {
            group->pages[i] = offer_page();
        }
        return group;
    }

    auto ComputeNodeAllocator::allocate(size_t sz) -> RemotePointer {
        if (sz == 0) {
            throw new std::runtime_error("Received 0 size in " + std::string(__FUNCTION__) + "\n");
        }

        if (sz > Constants::MEMORY_PAGE_SIZE) {
            throw new std::runtime_error("Size is larger than a page in " + std::string(__FUNCTION__) + "\n");
        }

        auto id = std::this_thread::get_id();

        auto group = thread_info.find(id);
        if (group == thread_info.end()) {
            if (refill(id) == false) {
                return nullptr;
            }

            group = thread_info.find(id);
        }

        auto ac = get_class(sz);
        switch(group->second->available(ac)) {
        case Enums::MemoryAllocationStatus::Ok:
            break;
        case Enums::MemoryAllocationStatus::EmptyPage:
            if (refill_single_page(id, ac) == false)
                return nullptr;
            break;
        case Enums::MemoryAllocationStatus::EmptyPageGroup:
            if (refill(id) == false)
                return nullptr;
            break;
        default:
            return nullptr;
        }

        return group->second->allocate(ac);
    }

    auto ComputeNodeAllocator::free(RemotePointer chunk) -> void {
        auto page = chunk.page();

        auto current = tracker.current;
        if (auto p = current->mirrors.find(page); p != current->mirrors.end()) {
            p->second->free(chunk);
        }

        for (auto &k : tracker.segments) {
            auto &mirror = k.second->mirrors;
            if (auto p = mirror.find(page); p != mirror.end()) {
                p->second->free(page);
            }
        }
    }

    auto ComputeNodeAllocator::refill(const std::thread::id &id) -> bool {
        std::scoped_lock<std::mutex> _(mutex);
        if (!tracker.available(Constants::PAGEGROUP_NO)) {
            return false;
        }

        auto group = thread_info.find(id);

        if (group == thread_info.end()) {
            thread_info.insert({id, new PageGroup});
            group = thread_info.find(id);
        }

        group->second = tracker.offer_page_group();
        return true;
    }

    auto ComputeNodeAllocator::refill_single_page(const std::thread::id &id, AllocationClass ac) -> bool {
        std::scoped_lock<std::mutex> _(mutex);
        if (!tracker.available(Constants::PAGEGROUP_NO)) {
            return false;
        }

        auto group = thread_info.find(id);
        for (auto &p : group->second->pages) {
            if (p->desc.allocation_class == ac && !p->available()) {
                p = tracker.offer_page();
                p->desc.initialize(ac);
            }
        }

        return true;
    }

    auto ComputeNodeAllocator::return_memory() -> bool {
        throw std::runtime_error(std::string(__FUNCTION__) + " not implemented");
        return false;
    }


    auto RemoteMemoryManager::parse_config_file(const std::string &config) -> bool {
        std::ifstream file(config);
        if (!file.is_open()) {
            Debug::error("Failed to open config file %s\n ", config.c_str());
            return false;
        }

        std::string buffer;
        std::regex uri("\\s*(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d+)");
        std::regex nid("node(\\d+)");
        while(std::getline(file, buffer)) {
            auto mem_node = std::make_unique<Cluster::MemoryNodeInfo>();

            std::smatch nid_v;
            if (std::regex_search(buffer, nid_v, nid)) {
                mem_node->node_id = atoi(nid_v[1].str().c_str());
            }

            auto iter = std::sregex_iterator(buffer.begin(), buffer.end(), uri);
            std::smatch match = *iter;
            // can throw exception here
            mem_node->tcp_addr = Cluster::IPV4Addr::make_ipv4_addr(match[1].str()).value();
            mem_node->tcp_port = atoi(match[2].str().c_str());

            ++iter;
            match = *iter;
            mem_node->roce_addr = Cluster::IPV4Addr::make_ipv4_addr(match[1].str()).value();
            mem_node->roce_port = atoi(match[2].str().c_str());

            ++iter;
            match = *iter;
            mem_node->erpc_addr = Cluster::IPV4Addr::make_ipv4_addr(match[1].str()).value();
            mem_node->erpc_port = atoi(match[2].str().c_str());

            memory_nodes.push_back(std::move(mem_node));
        }
        // we set mem_node->base_addr in connect_memory_nodes();
        Debug::info("Config file %s parsed\n", config.c_str());
        return true;
    }

    auto RemoteMemoryManager::connect_memory_nodes(RPCWrapper::ClientRPCContext &ctx) -> bool {
        // memory node and its rdma_ctx have the same indexes
        int successed = 0;
        rpc_ctx = &ctx;
        for (const auto &n : memory_nodes) {
            auto socket = Misc::socket_connect(false, n->tcp_port, n->tcp_addr.to_string().c_str());
            auto node = n->tcp_addr.to_uri(n->tcp_port);
            if (socket == -1) {
                Debug::error("Failed to connect to memory node %s\n", node.c_str());
                return false;
            }

            // here is a low level casting, be cautious
            auto s = Misc::recv_all(socket, &n->base_addr, sizeof(n->base_addr));
            if (s != sizeof(n->base_addr)) {
                Debug::error("Failed to receive base address of %s\n", node.c_str());
                return false;
            }


            int rpc_id = -1;
            Misc::recv_all(socket, &rpc_id , sizeof(rpc_id));
            if (rpc_id == -1) {
                Debug::error("Failed to receive rpc id of %s\n", node.c_str());
                return false;
            }

            if (!rpc_ctx->connect_remote(n->node_id, n->erpc_addr, n->erpc_port, rpc_id)) {
                Debug::error("Failed to establish eRPC connection with node %s at %s\n",
                             node.c_str(), n->erpc_addr.to_uri(n->erpc_port).c_str());
                return false;
            }
            Debug::info("Successfully connected to node %s\n", node.c_str());

            close(socket);
            ++successed;
        }

        Debug::info("%d out of %ld memory nodes connected\n", successed, memory_nodes.size());

        return true;
    }

    auto RemoteMemoryManager::setup_rdma_per_thread(RDMADevice *device) -> bool {
        auto id = std::this_thread::get_id();
        auto p = rdma_ctxs.find(id);
        if (p != rdma_ctxs.end())
            return true;

        std::vector<std::unique_ptr<RDMAContext>> rdma;

        auto common_buffer = new byte_t[4096];
        for (const auto &n : memory_nodes) {
            auto socket = Misc::socket_connect(false, n->roce_port, n->roce_addr.to_string().c_str());
            auto [rdma_ctx, status] = device->open(common_buffer, 4096, 1,
                                                   RDMADevice::get_default_mr_access(),
                                                   *RDMADevice::get_default_qp_init_attr());

            if (status != RDMAUtil::Enums::Status::Ok) {
                Debug::error(">> Failed to open device due to %s\n",
                             RDMAUtil::decode_rdma_status(status).c_str());
                return false;
            }

            if (rdma_ctx->default_connect(socket) != 0) {
                Debug::error("Failed to establish RDMA with node %d\n", n->node_id);
                return false;
            }

            Debug::info("RDMA with node %d established\n", n->node_id);
            rdma.push_back(std::move(rdma_ctx));
        }

        std::scoped_lock<std::mutex> _(init_mutex);
        rdma_ctxs.insert({id, std::move(rdma)});
        return true;
    }

    auto RemoteMemoryManager::get_rdma(RemotePointer remote) -> RDMAContext * {
        auto rdma = rdma_ctxs.find(std::this_thread::get_id());
        if (rdma == rdma_ctxs.end()) {
            Debug::warn("Do remember to setup_rdma_per_thread before running\n");
            return nullptr;
        }

        return rdma->second[remote.get_node()].get();
    }

    auto RemoteMemoryManager::get_base_addr(int node_id) -> RemotePointer {
        return memory_nodes[node_id]->base_addr;
    }

    auto RemoteMemoryManager::offer_remote_segment() -> RemotePointer {
        auto &mem_node = memory_nodes[(current++) % memory_nodes.size()];
        auto id = mem_node->node_id;
        auto info = rpc_ctx->select_first_info(id);

        info->done = false;
        info->rpc->resize_msg_buffer(&info->req_buf, 8);
        *reinterpret_cast<uint64_t *>(info->req_buf.buf) = 0UL;

        info->rpc->enqueue_request(info->session, Cluster::Enums::RPCOperations::RemoteAllocation,
                                   &info->req_buf, &info->resp_buf, memory_continuation, nullptr);
        while(!info->done){
            info->rpc->run_event_loop_once();
        }

        auto remote = *reinterpret_cast<RemotePointer *>(info->resp_buf.buf);
        if (remote.is_nullptr()) {
            // try another memory node until success
            Debug::info("Nested call of %s to get remote memory\n", __FUNCTION__);
            offer_remote_segment();
        }

        return remote;
    }

    auto RemoteMemoryManager::recycle_remote_segment(RemotePointer segment) -> bool {
        // TODO: do the erpc job
        UNUSED(segment);
        return false;
    }

    // For debug
    auto PageGroup::dump() const noexcept -> void {
        for (const auto &m : pages) {
            std::cout << "---->> page id: " << m->page_id << "\n";
            std::cout << "---->> page base: " << m->page_base.void_ptr() << "\n";
            auto ac = allocation_class_map[m->desc.allocation_class];
            std::cout << "---->> allocation class: " << dump_allocation_class(ac) << "\n";
            std::cout << "---->> empty slots: " << (int)m->desc.empty_slots << "\n";
            std::cout << "---->> offset: " << (int)m->desc.offset << "\n";
        }
    }

    auto Segment::dump() const noexcept -> void {
        std::cout << ">> Segment pointer: " << seg.void_ptr() << "\n";
        std::cout << "---->> offset: " << offset << "\n";
        std::cout << "---->> available pages: " << available_pages << "\n";
    }

    auto SegmentTracker::dump() const noexcept -> void {
        std::cout << ">> Current used segment: \n";
        current->dump();
    }

    auto ComputeNodeAllocator::dump() const noexcept -> void {
        tracker.dump();

        for (const auto &t : thread_info) {
            std::cout << ">> Thread " << t.first << " occupies following page group\n";
            t.second->dump();
        }
        std::cout << "\n";
    }

    auto RemoteMemoryManager::dump() const noexcept -> void {
        std::cout << ">> Currently using memory from node " << current <<"\n";
        std::cout << ">> reporting known memory nodes:\n";
        for (const auto &m : memory_nodes) {
            m->dump();
        }
    }
}
