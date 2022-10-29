#include "node/memory_node/memory_node.hpp"
#include "node/compute_node/compute_node.hpp"
#include "cmd_parser/cmd_parser.hpp"
#include "workload/workload.hpp"
#include "stats/stats.hpp"
#include <chrono>

using namespace CmdParser;
using namespace DiStore;

size_t total = 0;

auto launch_compute_ycsb(const std::string &config, const std::string &memory_nodes,
                         int threads, Workload::YCSBWorkloadType workload_type) -> void {
    auto node = Cluster::ComputeNode::make_compute_node(config, memory_nodes);

    if (node == nullptr) {
        Debug::error("Wow you can do a really bad job\n");
        return;
    }

    if (!node->register_thread()) {
        Debug::error("Failed to register a thread\n");
        return;
    }

    auto guard = std::to_string(0);
    // guard.append(DataLayer::Constants::KEYLEN - guard.size(), 'x');

    auto ycsb = Workload::YCSBWorkload::make_ycsb_workload(total,
                                                           total / 10, /* ensure skewness*/
                                                           workload_type);
    Stats::Breakdown b(1000000);
    Debug::info("Populating\n");
    for (size_t i = 0; i < total / 10; i++) {
        auto k = std::to_string(i);
        k.insert(0, Workload::Constants::KEY_SIZE - k.size(), '0');

        if (!node->put(k, k, &b)) {
            Debug::error("Putting key %s failed\n", k.c_str());
            return;
        }
    }
    b.clear();

    // start benching
    const size_t sample_batch = 1000;
    std::vector<std::thread> workers;
    workers.reserve(threads);

    std::mutex print_lock;

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < threads; i++) {
        workers.emplace_back([&](int tid) {
            if (!node->register_thread()) {
                Debug::error("Failed to register a thread\n");
                return;
            }

            auto total_counter = 0;

            Stats::Breakdown breakdown(sample_batch);

            for (size_t i = 0; i < total / threads; i++) {
                auto op = ycsb->next();
                switch (op.first) {
                case Workload::YCSBOperation::Insert:
                    breakdown.begin(Stats::DiStoreBreakdownOps::Put);
                    if (!node->put(op.second, op.second, &breakdown)) {
                        Debug::error("Putting %s failed\n", op.second.c_str());
                        return;
                    }
                    breakdown.end(Stats::DiStoreBreakdownOps::Put);
                    break;
                case Workload::YCSBOperation::Update:
                    breakdown.begin(Stats::DiStoreBreakdownOps::Update);
                    if (!node->update(op.second, op.second, &breakdown)) {
                        Debug::error("Updating %s failed\n", op.second.c_str());
                        return;
                    }
                    breakdown.end(Stats::DiStoreBreakdownOps::Update);
                    break;
                case Workload::YCSBOperation::Search:
                    breakdown.begin(Stats::DiStoreBreakdownOps::Get);
                    if (auto v = node->get(op.second, &breakdown); !v.has_value()) {
                        Debug::error("Searching %s failed\n", op.second.c_str());
                        return;
                    }
                    breakdown.end(Stats::DiStoreBreakdownOps::Get);
                    break;
                case Workload::YCSBOperation::Scan:
                    return;
                default:
                    Debug::error("Unkown operation");
                    return;
                }

                if (++total_counter == sample_batch) {
                    print_lock.lock();
                    std::cout << "Thread " << tid << " reporting\n";
                    breakdown.report();
                    print_lock.unlock();
                    total_counter = 0;
                    breakdown.clear();
                }
            }

        }, i);
    }

    for (auto &t : workers) {
        t.join();
    }
    auto end = std::chrono::steady_clock::now();

    double time = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    Debug::info("Throughput: %fKOPS\n", total / time / 1000);
    node->report_search_layer_stats();
    node->report_data_layer_stats();
    Debug::info("You may see segfault due to the destruction of RDMAContext,"
                "but this program has fulfilled its duty:)\n");
}

auto launch_compute(const std::string &config, const std::string &memory_nodes, int threads) -> void {
    auto node = Cluster::ComputeNode::make_compute_node(config, memory_nodes);

    if (node == nullptr) {
        Debug::error("Wow you can do a really bad job\n");
        return;
    }

    if (!node->register_thread()) {
        Debug::error("Failed to register a thread\n");
        return;
    }

    auto total = 10000000UL;
    auto guard = std::to_string(0);
    // guard.append(DataLayer::Constants::KEYLEN - guard.size(), '0');
    Stats::Breakdown breakdown(1000);
    std::cout << "Populating\n";
    for (size_t i = 0; i < total; i++) {
        auto k = std::to_string(total + i);
        k.insert(0, DataLayer::Constants::KEYLEN - k.size(), '0');

        if (!node->put(k, k, &breakdown)) {
            Debug::error("Failed to insert %s\n", k.c_str());
            return;
        }
    }

    std::cout << "Double checking\n";
    for (size_t i = 0; i < total; i++) {
        auto k = std::to_string(total + i);
        k.append(DataLayer::Constants::KEYLEN - k.size(), '0');
        auto r = node->get(k, &breakdown);
        if (!r.has_value()) {
            std::cout << k << " is missing\n";
            std::cout << "Double checking failed\n";
            break;
        }
    }

    std::cout << "Updating\n";
    for (size_t i = 0; i < total; i++) {
        auto k = std::to_string(total + i);
        k.append(DataLayer::Constants::KEYLEN - k.size(), '0');
        if (!node->update(k, k, &breakdown)) {
            std::cout << "Updaing " << k << " failed\n";
            break;
        }
    }

    breakdown.report();
    std::cout << "Validation passed, good job\n";
}

auto launch_memory(const std::string &config) -> void {
    auto node = Cluster::MemoryNode::make_memory_node(config);

    if (node == nullptr) {
        Debug::error("Wow you can do a really bad job\n");
        return;
    }

    node->launch_erpc_thread().value().detach();
    node->launch_tcp_thread().value().detach();
    node->launch_rdma_thread().value().detach();

    while (true)
        ;
}

auto main(int argc, char *argv[]) -> int {
    Parser parser;
    parser.add_option("--type", "-t", "compute");
    parser.add_option("--config", "-c");
    parser.add_option("--memory_nodes", "-m");
    parser.add_option<int>("--threads", "-T", 1);
    parser.add_option<size_t>("--size", "-s", 10000000);
    parser.add_option<std::string>("--workload", "-w", "C");

    parser.parse(argc, argv);

    auto type = parser.get_as<std::string>("--type");
    auto config = parser.get_as<std::string>("--config");
    auto memory_nodes = parser.get_as<std::string>("--memory_nodes");
    auto threads = parser.get_as<int>("--threads").value();
    total = parser.get_as<size_t>("--size").value();
    auto workload = parser.get_as<std::string>("--workload").value();

    if (type == "compute") {
        if (!config.has_value()) {
            Debug::error("Please offer a configuration file to configure current node\n");
            return -1;
        }

        if (!memory_nodes.has_value()) {
            Debug::error("Please offer a configuration file about the memory nodes\n");
            return -1;
        }
        Workload::YCSBWorkloadType workload_type;
        if (workload == "A") {
            workload_type = Workload::YCSBWorkloadType::YCSB_A;
        } else if (workload == "B") {
            workload_type = Workload::YCSBWorkloadType::YCSB_B;
        } else if (workload == "C") {
            workload_type = Workload::YCSBWorkloadType::YCSB_C;
        } else if (workload == "L") {
            workload_type = Workload::YCSBWorkloadType::YCSB_L;
        } else {
            Debug::error("Other YCSB workloads are not supported\n");
            return -1;
        }
        launch_compute_ycsb(config.value(), memory_nodes.value(), threads, workload_type);
    } else if (type == "memory") {
        if (!config.has_value()) {
            Debug::error("Please offer a configuration file to configure current node\n");
            return -1;
        }

        launch_memory(config.value());
    } else {
        Debug::error("Unknown node type %s\n", type.value().c_str());
        return -1;
    }
}
