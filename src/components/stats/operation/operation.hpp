#ifndef __DISTORE__STATS__OPERATION__OPERATION__
#define __DISTORE__STATS__OPERATION__OPERATION__
#include "stats/stats.hpp"
#include "config/config.hpp"
#include "misc/misc.hpp"

namespace DiStore::Stats {
    enum class DiStoreOperationOps {
        Put,
        Get,
        Update,
        Scan,
        Delete
    };

    class Operation {
    public:
        friend StatsCollector;
        Operation(size_t batch_size)
            :batch(batch_size) {}
        ~Operation() = default;

        inline auto begin(DiStoreOperationOps op) noexcept -> void {
#ifdef __STATS__
            spans[op].first = std::chrono::steady_clock::now();
#endif
        }

        inline auto end(DiStoreOperationOps op) noexcept -> void {
#ifdef __STATS__
            auto pair = spans[op];
            pair.second = std::chrono::steady_clock::now();

            auto diff = pair.second - pair.first;
            auto span = double(std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count());
            auto &tmp_arr = tmp[op];
            tmp_arr.push_back(span);

            if (tmp_arr.size() == batch) {
                results[op].push_back(Misc::avg(tmp_arr));
                tmp_arr.clear();
            }
#endif
        }

        auto report() noexcept -> void {
#ifdef __STATS__
            for (auto &k : ops_table) {
                std::cout << ">> Operation " << decode_breakdown(k) << ": ";
                auto &arr = results[k];
                std::sort(arr.begin(), arr.end(), std::greater<>());
                std::cout << "avg: " << Misc::avg(arr) << "ns, ";
                std::cout << "p50: " << Misc::p50(arr) << "ns, ";
                std::cout << "p90: " << Misc::p90(arr) << "ns, ";
                std::cout << "p99: " << Misc::p99(arr) << "ns\n";
            }
#endif
        }

        auto submit(StatsCollector &collector) noexcept -> void {
            for (auto &k : ops_table) {
                auto &arr = results[k];
                std::sort(arr.begin(), arr.end(), std::greater<>());
                collector.submit(decode_breakdown(k),
                                 {Misc::avg(arr), Misc::p50(arr),
                                  Misc::p90(arr), Misc::p99(arr)});
            }            
        }

        auto clear() noexcept -> void {
            for (auto &k : ops_table) {
                results[k].clear();
                tmp[k].clear();
            }
        }
    private:
        using SteadyTimePoint = std::chrono::time_point<std::chrono::steady_clock>;
        using SteadyTimePair = std::pair<SteadyTimePoint, SteadyTimePoint>;

        constexpr static DiStoreOperationOps ops_table[] = {
            DiStoreOperationOps::Put,
            DiStoreOperationOps::Get,
            DiStoreOperationOps::Update,
            DiStoreOperationOps::Scan,
            DiStoreOperationOps::Delete,
        };

        const size_t batch;
        std::unordered_map<DiStoreOperationOps, std::vector<double>> results;
        std::unordered_map<DiStoreOperationOps, std::vector<double>> tmp;
        std::unordered_map<DiStoreOperationOps, SteadyTimePair> spans;

        auto decode_breakdown(DiStoreOperationOps op) -> std::string {
            switch (op) {
            case DiStoreOperationOps::Put:
                return "Put";
            case DiStoreOperationOps::Get:
                return "Get";
            case DiStoreOperationOps::Update:
                return "Update";
            case DiStoreOperationOps::Scan:
                return "Scan";
            case DiStoreOperationOps::Delete:
                return "Delete";
            default:
                return "Unknwon";
            }
        }
    };
}
#endif
