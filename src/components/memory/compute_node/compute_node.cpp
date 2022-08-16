#include "compute_node.hpp"

namespace DiStore {
    namespace Memory {
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

        // think more about the detail
        auto PageGroup::allocate(AllocationClass ac) -> RemotePointer {
            for (auto &p : pages) {
                if (p->desc.allocation_class == ac) {
                    return p->allocate();
                } else if (p->desc.allocation_class == AllocationClass::ChunkUnknown) {
                    p->desc.initialize(ac);
                    return p->allocate();
                }
            }

            return nullptr;
        }

        auto PageGroup::available(AllocationClass ac) -> Enums::MemoryAllocationStatus {
            for (auto &p : pages) {
                if (p->desc.allocation_class == ac) {
                    if (p->available()) {
                        return Enums::MemoryAllocationStatus::Ok;
                    } else {
                        return Enums::MemoryAllocationStatus::EmptyPage;
                    }
                }
            }

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

            auto id = std::this_thread::get_id();

            auto group = thread_info.find(id);
            if (group == thread_info.end()) {
                if (refill(id) == false)
                    return nullptr;
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
                break;
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
                if (p->desc.allocation_class == ac) {
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
                std::cerr << "Failed to open config file " << config << "\n";
                return false;
            }


            std::string buffer;
            std::regex node_info("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d+)");
            auto counter = 0;
            while(std::getline(file, buffer)) {
                auto mem_node = std::make_unique<Cluster::MemoryNodeInfo>();
                auto iter = std::sregex_iterator(buffer.begin(), buffer.end(), node_info);
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

                mem_node->node_id = counter++;
                memory_nodes.push_back(std::move(mem_node));
            }
            // we set mem_node->base_addr in connect_memory_nodes();

            current = 0;
            return true;
        }

        auto RemoteMemoryManager::connect_memory_nodes() -> bool {
            for (const auto &n : memory_nodes) {
                auto socket = Misc::socket_connect(false, n->tcp_port, n->tcp_addr.to_string().c_str());

                if (socket == -1) {
                    std::cerr << "Failed to connect to memory node "
                              << n->tcp_addr.to_string() + std::to_string(n->tcp_port)
                              << "\n";
                }

                // here is a low level casting, be cautious
                auto s = Misc::recv_all(socket, &n->base_addr, sizeof(n->base_addr));
                if (s != sizeof(n->base_addr)) {
                    std::cerr << "Failed to receive base address of "
                              << n->tcp_addr.to_string() + std::to_string(n->tcp_port)
                              << "\n";
                }
            }

            return true;
        }

        auto RemoteMemoryManager::get_base_addr(int node_id) -> RemotePointer {
            return memory_nodes[node_id]->base_addr;
        }

        auto RemoteMemoryManager::offer_remote_segment() -> RemotePointer {
            auto &mem_node = memory_nodes[(current++) % memory_nodes.size()];
            // TODO: do the erpc job
        }

        auto RemoteMemoryManager::recycle_remote_segment(RemotePointer segment) -> bool {
            // TODO: do the erpc job
        }

        // For debug
        auto PageGroup::dump() const noexcept -> void {
            for (const auto &m : pages) {
                std::cout << "---->> page id: " << m->page_id << "\n";
                std::cout << "---->> page base: " << m->page_base.void_ptr() << "\n";
                std::cout << "---->> allocation class: " << m->desc.allocation_class << "\n";
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
                std::cout << ">> Thread " << t.first << " occupies following page group\n\n";
                t.second->dump();
            }
        }
    }
}
