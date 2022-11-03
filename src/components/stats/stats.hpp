#ifndef __DISTORE__STATS__STATS__
#define __DISTORE__STATS__STATS__
#include "misc/misc.hpp"
#include "debug/debug.hpp"

#include <queue>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <unordered_map>
namespace DiStore::Stats {
    struct LatStats {
        double avg;
        double p50;
        double p90;
        double p99;

        LatStats() = default;
        LatStats(double avg_, double p50_, double p90_, double p99_)
            : avg(avg_),
              p50(p50_),
              p90(p90_),
              p99(p99_) {}
        ~LatStats() = default;
    };

    class StatsCollector {
    public:
        StatsCollector() = default;
        ~StatsCollector() = default;

        inline auto submit(const std::string &op, LatStats &&stats) -> void {
            lats[op].push_back(std::move(stats));
        }

        auto get_summarized() const noexcept -> std::vector<std::pair<std::string, LatStats>> {
            std::vector<std::pair<std::string, LatStats>> ret;
            for (const auto &[k, v] : lats) {
                std::vector<double> avg_tmp;
                std::vector<double> p50_tmp;
                std::vector<double> p90_tmp;
                std::vector<double> p99_tmp;
                LatStats stats;
                avg_tmp.reserve(v.size());
                p50_tmp.reserve(v.size());
                p90_tmp.reserve(v.size());
                p99_tmp.reserve(v.size());

                for (const auto &n : v) {
                    avg_tmp.push_back(n.avg);
                    p50_tmp.push_back(n.p50);
                    p90_tmp.push_back(n.p90);
                    p99_tmp.push_back(n.p99);                    
                }

                stats.avg = Misc::avg(avg_tmp);
                stats.p50 = Misc::avg(p50_tmp);
                stats.p90 = Misc::avg(p90_tmp);
                stats.p99 = Misc::avg(p99_tmp);

                ret.push_back({k, stats});
            }

            return ret;
        }

        auto summarize() const noexcept -> void {
            auto ret = get_summarized();

            for (const auto &v : ret) {
                std::cout << v.first << ": ";
                std::cout << "avg: " << v.second.avg << ", ";
                std::cout << "p50: " << v.second.p50 << ", ";
                std::cout << "p90: " << v.second.p90 << ", ";
                std::cout << "p99: " << v.second.p99 << "\n";
            }
        }
    private:
        std::unordered_map<std::string, std::vector<LatStats>> lats;
    };
}
#endif
