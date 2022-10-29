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

    /*
     * About some concurrency issue
     * 0. thread compete to be the winner
     * 1. tag and content are used for waiting threads to submit their
     *    requests to the winner thread
     * 2. is_done is used by the winner to notify waiting threads
     * 3. expected is used by the winner to check whether the node
     *    that the winner is currently operating on is the node expected
     *    by waiting threads (this guarantee update correctness)
     * 4. next is a guard for the winner to determine whether node split
     *    occurs and let waiting threads to retry if so.
     */
    struct ConcurrencyRequests {
        // used to point to strings
        const void *tag;
        const void *content;

        std::atomic<bool> is_done;
        bool succeed;
        bool retry;

        ConcurrencyRequests()
            : tag(nullptr),
              content(nullptr),
              is_done(false),
              succeed(false),
              retry(false) {}
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
