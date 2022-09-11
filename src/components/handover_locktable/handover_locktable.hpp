#ifndef __DISTORE__HANDOVER_LOCKTABLE__HANDOVER_LOCKTABLE__
#define __DISTORE__HANDOVER_LOCKTABLE__HANDOVER_LOCKTABLE__

#include "memory/memory.hpp"
#include "memory/remote_memory/remote_memory.hpp"
#include "city/city.hpp"

#include "tbb/concurrent_queue.h"

#include <atomic>
#include <mutex>

namespace DiStore::Concurrency {
    enum class ConcurrencyContextType {
        Insert,
        Update,
        Delete,
    };
    
    struct ConcurrencyRequests {
        // used to point to strings
        const void *tag;
        const void *content;

        std::atomic<bool> is_done;
        bool succeed;

        ConcurrencyRequests()
            : tag(nullptr),
              content(nullptr),
              is_done(false),
              succeed(false) {}
    };

    struct ConcurrencyContext {
        ConcurrencyContextType type;
        void *user_context;
        std::atomic<int> max_depth;
        tbb::concurrent_queue<ConcurrencyRequests *> requests;

        ConcurrencyContext() {
            type = ConcurrencyContextType::Insert;
            max_depth = 4;
        }
    };
}
#endif
