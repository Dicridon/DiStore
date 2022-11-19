#ifndef __DISTORE__WORKLOAD__WORKLOAD__
#define __DISTORE__WORKLOAD__WORKLOAD__
#include "zipf/zipf.hpp"

#include <random>
#include <memory>

namespace DiStore::Workload {
    namespace Constants {
        // Value size is not so important
        static constexpr size_t KEY_SIZE = 16;
    };

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

        inline auto next_unrecorded() -> uint64_t {
            return generator->next();
        }

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

    enum class YCSBWorkloadType {
        YCSB_A,
        YCSB_B,
        YCSB_C,
        YCSB_D,
        YCSB_E,
        YCSB_L,
        YCSB_R,
    };

    enum class YCSBOperation {
        Insert,
        Update,
        Search,
        Scan,
    };

    class YCSBWorkload {
    public:
        static auto make_ycsb_workload(uint64_t num, uint64_t range,
                                       YCSBWorkloadType t = YCSBWorkloadType::YCSB_C,
                                       double theta = 0.99, uint64_t rand_seed = 0)
            -> std::unique_ptr<YCSBWorkload>
        {
            return std::make_unique<YCSBWorkload>(num, range, t, theta, rand_seed);
        }


        YCSBWorkload(uint64_t num, uint64_t range, YCSBWorkloadType t = YCSBWorkloadType::YCSB_C,
                     double theta = 0.99, uint64_t rand_seed = 0)
        {
            load_generator = BenchmarkWorkload::make_bench_workload(num, range,
                                                                    WorkloadType::Zipf,
                                                                    theta, rand_seed);
            op_generator = BenchmarkWorkload::make_bench_workload(num, 99);
            num_ops = num;
            type = t;
        }

        ~YCSBWorkload() = default;

        inline auto next() -> std::pair<YCSBOperation, std::string> {
            auto k = std::to_string(load_generator->next_unrecorded());
            k.insert(0, Constants::KEY_SIZE - k.size(), '0');

            auto op = op_generator->next_unrecorded();

            switch (type) {
            case YCSBWorkloadType::YCSB_A:
                if (op < 49) {
                    return {YCSBOperation::Update, k};
                } else {
                    return {YCSBOperation::Search, k};
                }
            case YCSBWorkloadType::YCSB_B:
                if (op < 4) {
                    return {YCSBOperation::Update, k};
                } else {
                    return {YCSBOperation::Search, k};
                }
            case YCSBWorkloadType::YCSB_C:
                return {YCSBOperation::Search, k};
            case YCSBWorkloadType::YCSB_D:
                if (op < 4) {
                    return {YCSBOperation::Insert, k};
                } else {
                    return {YCSBOperation::Search, k};
                }
            case YCSBWorkloadType::YCSB_E:
                if (op < 4) {
                    return {YCSBOperation::Insert, k};
                } else {
                    return {YCSBOperation::Scan, k};
                }
            case YCSBWorkloadType::YCSB_L:
                return {YCSBOperation::Insert, k};
            case YCSBWorkloadType::YCSB_R:
                return {YCSBOperation::Scan, k};
            default:
                throw std::invalid_argument("Unkown YCSB workload type");
            }
        }
    private:
        std::unique_ptr<BenchmarkWorkload> load_generator;
        std::unique_ptr<BenchmarkWorkload> op_generator;
        uint64_t num_ops;
        YCSBWorkloadType type;
    };
}
#endif
