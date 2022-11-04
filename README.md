# DiStore

# Hints
- Current due to the limitation eRPC, we preallocate all needed memory at startup time. This eliminates runtime memory segment allocation overhead. Since the overhead is negligible, the pre-allocation does not change the results

- If the preallocated memory use consumed and working threads need to require more remote memory, ePRC can fail due to mismatch thread-local contexts. To run larger workload, please use large segment size in `src/components/memory/memory.hpp`
