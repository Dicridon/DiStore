#ifndef __DISTORE__STATS__STATS__
#define __DISTORE__STATS__STATS__
#include "misc/misc.hpp"

#include <queue>
#include <chrono>
#include <algorithm>
#include <numeric>
namespace DiStore::Stats {
    class LatencyCatcher {
    public:
        LatencyCatcher() = default;
        ~LatencyCatcher() = default;

        inline auto begin() noexcept {
            s = std::chrono::steady_clock::now();
        }

        inline auto end() noexcept {
            e = std::chrono::steady_clock::now();
        }

        // report in nanosecond
        inline auto report() noexcept -> double {
            return double(std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count());
        }
    private:
        std::chrono::time_point<std::chrono::steady_clock> s;
        std::chrono::time_point<std::chrono::steady_clock> e;
    };

    /*
     * How to use:
     *     This class collects timespan over a certain number of operations
     *     and caculate mean latenc and percentiles out of these collected
     *     timespans
     * This class is not write thread-safe
     */
    class TimedLatency {
    public:
        TimedLatency(size_t batch_size)
            : batch(batch_size)
        {
            data.reserve(batch_size);
        }

        ~TimedLatency() = default;

        auto reset() -> void {
            processed = false;
            data.clear();
            sorted.clear();
        }

        inline auto record_time() -> void {
            data.push_back(std::chrono::steady_clock::now());
        }

        inline auto avg() -> double {
            process();
            return Misc::avg(sorted);
        }

        inline auto p50() -> double {
            process();
            return Misc::p50(sorted);
        }

        inline auto p90() -> double {
            process();
            return Misc::p90(sorted);
        }

        inline auto p99() -> double {
            process();
            return Misc::p99(sorted);
        }

        inline auto p999() -> double {
            process();
            return Misc::p999(sorted);
        }

        auto process() -> bool {
            if (processed)
                return true;

            if (data.size() <= batch) {
                return false;
            }

            auto s = data.cbegin() + 1;
            while (s != data.cend()) {
                double span = std::chrono::duration_cast<std::chrono::microseconds>(*s - *(s - 1)).count();
                sorted.push_back(span);
                ++s;
            }
            std::sort(sorted.begin(), sorted.end(), std::greater<>());
            return processed = true;
        }
    private:;
        size_t batch;
        bool processed = false;
        std::vector<std::chrono::time_point<std::chrono::steady_clock>> data;
        std::vector<double> sorted;

    };

    class TimedThroughput {
    public:
        TimedThroughput() {
            current = std::chrono::steady_clock::now();
        }

        ~TimedThroughput() = default;

        auto reset() -> void {
            processed = false;
            current = std::chrono::steady_clock::now();
        }

        auto report_throughput(size_t ops) -> double {
            auto s = std::chrono::steady_clock::now();
            double span = std::chrono::duration_cast<std::chrono::microseconds>(s - current).count();
            current = std::chrono::steady_clock::now();
            return ops / span;
        }

    private:
        bool processed = false;
        std::chrono::time_point<std::chrono::steady_clock> current;
    };
}
#endif
