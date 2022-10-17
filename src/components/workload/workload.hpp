#ifndef __DISTORE__WORKLOAD__WORKLOAD__
#define __DISTORE__WORKLOAD__WORKLOAD__
#include "zipf/zipf.hpp"

#include <random>
#include <memory>

namespace DiStore::Workload {
    enum class WorkloadType {
        Uniform,
        Zipf,
    };

    class WorkloadGenerator {
    public:
        virtual ~WorkloadGenerator() = default;
        virtual auto next() -> uint64_t = 0;
    };

    class ZipfGenerator : public WorkloadGenerator {
    public:
        ZipfGenerator(uint64_t range, double theta, uint64_t rand_seed = 0) {
            state = new struct zipf_gen_state;
            mehcached_zipf_init(state, range, theta, rand_seed);
        }
        ~ZipfGenerator() {
            delete state;
        }

        inline auto next() -> uint64_t override final {
            return mehcached_zipf_next(state);
        }
    private:
        struct zipf_gen_state *state;
    };

    class UniformGenerator : public WorkloadGenerator {
    public:
        UniformGenerator(uint64_t range) {
            rd = new std::random_device;
            gen = new std::mt19937((*rd)());
            dist = new std::uniform_int_distribution<>(0, range);
        }

        ~UniformGenerator() {
            delete dist;
            delete gen;
            delete rd;
        }

        inline auto next() -> uint64_t override final {
            return (*dist)(*gen);
        }
    private:
        std::random_device *rd;
        std::mt19937 *gen;
        std::uniform_int_distribution<> *dist;
    };

    class BenchmarkWorkload {
    public:
        static auto make_bench_workload(uint64_t num, uint64_t range,
                                        WorkloadType t = WorkloadType::Uniform,
                                        double theta = 0.99, uint64_t rand_seed = 0)
            -> std::unique_ptr<BenchmarkWorkload>
        {
            auto ret = std::make_unique<BenchmarkWorkload>(num, range, t, theta, rand_seed);
            if (ret->generator == nullptr) {
                return nullptr;
            }

            return ret;
        }

        BenchmarkWorkload(uint64_t num, uint64_t range, WorkloadType t = WorkloadType::Uniform,
                          double theta = 0.99, uint64_t rand_seed = 0)
        {
            switch (t) {
            case WorkloadType::Uniform:
                generator = std::make_unique<UniformGenerator>(range);
                break;
            case WorkloadType::Zipf:
                generator = std::make_unique<ZipfGenerator>(range, theta, rand_seed);
                break;                
            default:
                generator = nullptr;
            }

            num_ops = num;
        }

        ~BenchmarkWorkload() = default;

        inline auto next() -> uint64_t {
            auto ret = generator->next();
            workload.push_back(ret);
            return ret;
        }

        inline auto get_all() -> const std::vector<uint64_t> & {
            if (workload.size() < num_ops) {
                workload.push_back(next());
            }

            return workload;
        }

    private:
        std::unique_ptr<WorkloadGenerator> generator;
        uint64_t num_ops;
        std::vector<uint64_t> workload;
    };
}
#endif
