#ifndef __DISTORE__DATA_LAYER__DATA_LAYER__
#define __DISTORE__DATA_LAYER__DATA_LAYER__
#include "memory/memory.hpp"
#include "memory/remote_memory/remote_memory.hpp"
#include "city/city.hpp"
#include "misc/misc.hpp"
#include "workload/workload.hpp"

#include <boost/crc.hpp>

namespace DiStore::DataLayer {
    using namespace Memory;
    namespace Constants {
        static constexpr size_t KEYLEN = Workload::Constants::KEY_SIZE;
        static constexpr size_t VALLEN = KEYLEN;
    }

    namespace Enums {
        // DataLayer includes the implementation of adaptive linked array
        enum LinkedNodeType : uint32_t {
            TypeHead = 1,
            Type10 = 10,
            Type12 = 12,
            Type14 = 14,
            Type16 = 16,
            TypeVar = 99, // variable
            NotSet = 0,
        };
    }


    // Layout is important for us to avoid the read-modify-write procedure
    struct KV {
        byte_t key[Constants::KEYLEN];
        byte_t value[Constants::VALLEN];
    };

    using namespace Enums;

    template <std::size_t M, std::size_t N>
    struct LinkedNode {
        RemotePointer llink;
        RemotePointer rlink;
        uint16_t crc;
        LinkedNodeType type;

        // next writtable slot
        uint32_t next;

        // here we leave extra space for fingerprints so that we do not need to
        // move data when morphing. Such space cost is marginal
        uint8_t fingerprints[M];
        KV pairs[N];

        LinkedNode()
            : llink(nullptr),
              rlink(nullptr),
              type(static_cast<LinkedNodeType>(N)),
              next(0)
        {
            memset(fingerprints, 0, sizeof(fingerprints));
            // memset(pairs, 0, sizeof(pairs));
        }

        auto available() const noexcept -> bool {
            return next < N;
        }

        // uninsert, not upsert
        auto store(const std::string &key, const std::string &value) -> bool {
            if (!available()) {
                return false;
            }

            if (find(key)) {
                return true;
            }

            fingerprints[next] = (uint8_t)CityHash64(key.c_str(), key.size());
            memcpy(pairs[next].key, key.c_str(), key.size());
            memcpy(pairs[next].value, value.c_str(), value.size());
            ++next;

            return true;
        }

        auto find(const std::string &key) -> std::optional<std::string> {
            auto finger = (uint8_t)CityHash64(key.c_str(), key.size());

            for (int i = 0; i < next; i++) {
                // fuck the type conversion
                if (finger != fingerprints[i])
                    continue;
                if (key.compare(0, key.size(), (char *)&pairs[i].key[0], key.size()) == 0) {
                    return std::string((char *)&pairs[i].value[0], Constants::VALLEN);
                }
            }

            return {};
        }

        auto update(const std::string &key, const std::string &value) -> bool {
            auto finger = (uint8_t)CityHash64(key.c_str(), key.size());

            for (int i = 0; i < next; i++) {
                if (finger != fingerprints[i])
                    continue;

                // fuck the type conversion
                if (key.compare(0, key.size(), (char *)&pairs[i].key[0], key.size()) == 0) {
                    memcpy(pairs[i].value, value.c_str(), value.size());
                    return true;
                }
            }

            return false;
        }

        auto store(const_byte_ptr_t key, size_t k_sz, const_byte_ptr_t val, size_t v_sz)
            -> bool
        {
            if (!available()) {
                return false;
            }

            fingerprints[next] = CityHash64((char *)key, k_sz);
            memcpy(pairs[next].key, key, k_sz);
            memcpy(pairs[next].value, val, v_sz);
            ++next;
            return true;
        }

        auto scan(const std::string &key, size_t ct, std::vector<std::string> &ret) -> uint64_t {
            auto total = 0UL;
            for (int i = 0; i < next; i++) {
                if (ct - total > 0 &&
                    key.compare(0, key.size(), (char *)&pairs[i].key[0], key.size()) <= 0) {
                    ret.emplace_back((char *)&pairs[i].value[0], Constants::VALLEN);
                    ++total;
                }
            }

            return total;
        }


        auto dump() const noexcept -> void {
            std::cout << ">> Type: " << type << "\n";
            std::cout << ">> Next: " << next << "\n";
            for (int i = 0; i < next; i++) {
                std::cout << (uint64_t)fingerprints[i] << " ";
            }
            std::cout << "\n";
            for (int i = 0; i < next; i++) {
                auto k = std::string((char *)&pairs[i].key[0], Constants::KEYLEN);
                std::cout << k << " ";
            }
            std::cout << "\n";
        }

        auto check() const noexcept -> void {
            std::cout << ">> Type: " << type << "\n";
            std::cout << ">> Next: " << next << "\n";
            assert(next <= 16);
        }

        auto usage() const noexcept -> double {
            switch (type) {
            case LinkedNodeType::Type10:
                return next / 10.0;
            case LinkedNodeType::Type12:
                return next / 12.0;
            case LinkedNodeType::Type14:
                return next / 14.0;
            case LinkedNodeType::Type16:
                return next / 16.0;
            default:
                return 0;
            }
        }
        // checking number of KVs in this node and change type accordingly
    };

    using LinkedNode10 = LinkedNode<16, 10>;
    using LinkedNode12 = LinkedNode<16, 12>;
    using LinkedNode14 = LinkedNode<16, 14>;
    using LinkedNode16 = LinkedNode<16, 16>;
    using BufferNode = LinkedNode<21, 21>;

    inline static auto sizeof_node(Enums::LinkedNodeType t) -> size_t {
        switch(t) {
        case LinkedNodeType::Type10:
            return sizeof(LinkedNode10);
        case LinkedNodeType::Type12:
            return sizeof(LinkedNode12);
        case LinkedNodeType::Type14:
            return sizeof(LinkedNode14);
        case LinkedNodeType::Type16:
            return sizeof(LinkedNode16);
        default:
            return 0;
        }
    }

    inline auto crc_validate(const LinkedNode16 *buffer, LinkedNodeType type) -> uint16_t {
        boost::crc_optimal<16, 0x1021, 0xFFFF, 0, false, false>  crcer;

        switch (type) {
        case Enums::LinkedNodeType::Type10: {
            crcer.process_bytes(reinterpret_cast<const void *>(buffer->pairs), 10 * sizeof(KV));
            return crcer.checksum();            
        }
        case Enums::LinkedNodeType::Type12: {
            crcer.process_bytes(reinterpret_cast<const void *>(buffer->pairs), 12 * sizeof(KV));
            return crcer.checksum();            
        }
        case Enums::LinkedNodeType::Type14: {
            crcer.process_bytes(reinterpret_cast<const void *>(buffer->pairs), 14 * sizeof(KV));
            return crcer.checksum();            
        }
        case Enums::LinkedNodeType::Type16: {
            crcer.process_bytes(reinterpret_cast<const void *>(buffer->pairs), 16 * sizeof(KV));
            return crcer.checksum();
        }
        default:
            return 0;
        }
    }




    // not used
    struct LinkedNodeVar {
        RemotePointer llink;
        RemotePointer rlink;
        LinkedNodeType type;

        // next writtable slot
        size_t total_size;
        uint8_t fingerprint;
        size_t key_size;
        size_t value_size;
        byte_t kv[0];

        static auto make_linkednode_var(byte_ptr_t buf, size_t total,
                                        size_t k_sz, const char *k,
                                        size_t v_sz, const char *v,
                                        RemotePointer llk = nullptr,
                                        RemotePointer rlk = nullptr)
            -> LinkedNodeVar *
        {
            FOR_FUTURE(v);
            FOR_FUTURE(v_sz);
            auto self = reinterpret_cast<LinkedNodeVar *>(buf);
            self->llink = llk;
            self->rlink = rlk;
            self->type = LinkedNodeType::TypeVar;
            self->total_size = total;
            self->fingerprint = CityHash64(k, k_sz);
            return self;
        }
    };
}
#endif
