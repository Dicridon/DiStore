#ifndef __DISTORE__DATA_LAYER__DATA_LAYER__
#define __DISTORE__DATA_LAYER__DATA_LAYER__
#include "memory/memory.hpp"
#include "memory/remote_memory/remote_memory.hpp"
#include "city/city.hpp"

namespace DiStore::DataLayer {
    using namespace Memory;
    namespace Constants {
        static constexpr size_t KEYLEN = 32;
        static constexpr size_t VALLEN = 32;
    }

    namespace Enums {
        // DataLayer includes the implementation of adaptive linked array
        enum LinkedNodeType :uint32_t {
            Type10,
            Type12,
            Type14,
            Type16,
            TypeVar, // variable
        };
    }
    // Layout is important for us to avoid the read-modify-write procedure

    struct KV {
        byte_t key[Constants::KEYLEN];
        byte_t value[Constants::VALLEN];
    };

    using namespace Enums;
    struct LinkedNode10 {
        RemotePointer llink;
        RemotePointer rlink;
        LinkedNodeType type;

        // next writtable slot
        uint32_t next;
        uint8_t fingerprints[10];
        KV paris[10];

        LinkedNode10()
            : llink(nullptr),
              rlink(nullptr),
              type(LinkedNodeType::Type10),
              next(24)
        {
            memset(fingerprints, 0, sizeof(fingerprints));
        }
    };

    struct LinkedNode12 {
        RemotePointer llink;
        RemotePointer rlink;
        LinkedNodeType type;

        // next writtable slot
        uint32_t next;
        uint8_t fingerprints[12];
        KV paris[12];

        LinkedNode12()
            : llink(nullptr),
              rlink(nullptr),
              type(LinkedNodeType::Type12),
              next(24)
        {
            memset(fingerprints, 0, sizeof(fingerprints));
        }
    };

    struct LinkedNode14 {
        RemotePointer llink;
        RemotePointer rlink;
        LinkedNodeType type;

        // next writtable slot
        uint32_t next;
        uint8_t fingerprints[14];
        KV paris[14];

        LinkedNode14()
            : llink(nullptr),
              rlink(nullptr),
              type(LinkedNodeType::Type14),
              next(24)
        {
            memset(fingerprints, 0, sizeof(fingerprints));
        }
    };

    struct LinkedNode16 {
        RemotePointer llink;
        RemotePointer rlink;
        LinkedNodeType type;

        // next writtable slot
        uint32_t next;
        uint8_t fingerprints[16];
        KV paris[16];

        LinkedNode16()
            : llink(nullptr),
              rlink(nullptr),
              type(LinkedNodeType::Type16),
              next(24)
        {
            memset(fingerprints, 0, sizeof(fingerprints));
        }
    };

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
