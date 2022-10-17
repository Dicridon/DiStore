#include "workload/workload.hpp"

#include <iostream>

using namespace DiStore;
using namespace DiStore::Workload;

auto main() -> int {
    auto bench = BenchmarkWorkload::make_bench_workload(1000, 100, WorkloadType::Zipf);

    for (int i = 0; i < 1000; i++) {
        std::cout << bench->next() << "\n";
    }
    return 0;
}
