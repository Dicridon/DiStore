#ifndef __DISTORE__STATS__BREAKDOWN__BREAKDOWN__
#define __DISTORE__STATS__BREAKDOWN__BREAKDOWN__
#include "config/config.hpp"
#include "stats/stats.hpp"
#include "misc/misc.hpp"

#include <vector>
#include <unordered_map>

namespace DiStore::Stats {
    enum class DiStoreBreakdownOps {
        SearchLayerSearch,
        SearchLayerUpdate,

        DataLayerFetch,
        DataLayerWriteBack,
        DataLayerWriteMorphed,
        DataLayerWriteSplitted,
        DataLayerMorph,
        DataLayerSplit,
        DataLayerContention,

        MemoryAllocation,
        RemoteMemoryAllocation,
    };

    class Breakdown {
    public:
        friend StatsCollector;
        Breakdown(size_t batch_size)
            :batch(batch_size) {}
        ~Breakdown() = default;

        inline auto begin(DiStoreBreakdownOps op) noexcept -> void {
#ifdef __BREAKDOWN__
            spans[op].first = std::chrono::steady_clock::now();
#endif
        }

        inline auto end(DiStoreBreakdownOps op) noexcept -> void {
#ifdef __BREAKDOWN__
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
#ifdef __BREAKDOWN__
            for (auto &k : ops_table) {
                std::cout << ">> Breakdown " << decode_breakdown(k) << ": ";
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
                auto tmp_arr = tmp[k];
                if (tmp_arr.size() != 0) {
                    results[k].push_back(Misc::avg(tmp_arr));
                    tmp_arr.clear();
                }
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

        constexpr static DiStoreBreakdownOps ops_table[] = {
            DiStoreBreakdownOps::SearchLayerSearch,
            DiStoreBreakdownOps::SearchLayerUpdate,
            DiStoreBreakdownOps::DataLayerWriteBack,
            DiStoreBreakdownOps::DataLayerFetch,
            DiStoreBreakdownOps::DataLayerWriteMorphed,
            DiStoreBreakdownOps::DataLayerWriteSplitted,
            DiStoreBreakdownOps::DataLayerMorph,
            DiStoreBreakdownOps::DataLayerSplit,
            DiStoreBreakdownOps::DataLayerContention,

            DiStoreBreakdownOps::MemoryAllocation,
            DiStoreBreakdownOps::RemoteMemoryAllocation,
        };

        const size_t batch;
        std::unordered_map<DiStoreBreakdownOps, std::vector<double>> results;
        std::unordered_map<DiStoreBreakdownOps, std::vector<double>> tmp;
        std::unordered_map<DiStoreBreakdownOps, SteadyTimePair> spans;

        auto decode_breakdown(DiStoreBreakdownOps op) -> std::string {
            switch (op) {
            case DiStoreBreakdownOps::SearchLayerSearch:
                return "SearchLayerSearch";
            case DiStoreBreakdownOps::SearchLayerUpdate:
                return "SearchLayerUpdate";
            case DiStoreBreakdownOps::DataLayerFetch:
                return "DataLayerFetch";
            case DiStoreBreakdownOps::DataLayerWriteBack:
                return "DataLayerWriteBack";
            case DiStoreBreakdownOps::DataLayerWriteMorphed:
                return "DataLayerWriteMorphed";
            case DiStoreBreakdownOps::DataLayerWriteSplitted:
                return "DataLayerWriteWriteSplitted";
            case DiStoreBreakdownOps::DataLayerMorph:
                return "DataLayerMorph";
            case DiStoreBreakdownOps::DataLayerSplit:
                return "DataLayerSplit";
            case DiStoreBreakdownOps::DataLayerContention:
                return "DataLayerContention";
            case DiStoreBreakdownOps::MemoryAllocation:
                return "MemoryAllocation";
            case DiStoreBreakdownOps::RemoteMemoryAllocation:
                return "RemoteMemoryAllocation";
            default:
                return "Unknwon";
            }
        }
    };
}
#endif
