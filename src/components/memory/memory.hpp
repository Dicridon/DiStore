#ifndef __DISTORE__MEMORY__MEMORY__
#define __DISTORE__MEMORY__MEMORY__
#include "config/config.hpp"

#include <cinttypes>
#include <cstddef>
#include <optional>
namespace DiStore {
    namespace Memory {
        using byte_t = uint8_t;
        using byte_ptr_t = uint8_t *;
        using const_byte_ptr_t = const uint8_t *;

        namespace Constants {
            const byte_t byte_masks[8] = {
                    1, 2, 4, 8, 16, 32, 64, 128,
            };

            static constexpr uint64_t REMOTE_POINTER_MASK = ~0xffff000000000000UL;
            static constexpr uint64_t REMOTE_POINTER_BITS_MASK = 0xc000000000000000UL;
            static constexpr uint64_t REMOTE_POINTER_BITS = 0x2UL;
#ifndef __DEBUG__
            static constexpr size_t SEGMENT_SIZE = 1 << 30UL;
            static constexpr size_t PAGEGROUP_NO = 8;
            static constexpr size_t MEMORY_PAGE_SIZE = 4096;
            static constexpr uint64_t PAGE_MASK = 0xfffffffffffff000;
#else
            static constexpr size_t SEGMENT_SIZE = 1 << 12UL;
            static constexpr size_t PAGEGROUP_NO = 4;
            static constexpr size_t MEMORY_PAGE_SIZE = 128;
            static constexpr uint64_t PAGE_MASK = 0xffffffffffffff80;
#endif
        }

        struct Bitmap {
            static auto make_bitmap(byte_ptr_t region, size_t count) -> Bitmap * {
                auto bitmap = reinterpret_cast<Bitmap *>(region);

                size_t num_bytes = count / 8;
                bitmap->bytes = num_bytes;
                for (size_t i = 0; i < num_bytes; i++) {
                    bitmap->map[i] = 0;
                }

                return bitmap;
            }


            // find an empty slot and return its numbering
            auto find_empty() -> std::optional<size_t> {
                for (size_t i = 0; i < bytes; i++) {
                    for (int j = 0; j < 8; j++) {
                        if ((map[i] & Constants::byte_masks[j]) == 0) {
                            return i * 8 + j;
                        }
                    }
                }

                return {};
            }

            auto get_empty() -> std::optional<size_t> {
                for (size_t i = 0; i < bytes; i++) {
                    for (int j = 0; j < 8; j++) {
                        if ((map[i] & Constants::byte_masks[j]) == 0) {
                            map[i] = map[i] | Constants::byte_masks[j];
                            return i * 8 + j;
                        }
                    }
                }

                return {};
            }

            auto unset(size_t pos) {
                auto i = pos / 8;
                auto j = pos % 8;
                map[i] = map[i] & (~Constants::byte_masks[j]);
            }

            size_t bytes;
            byte_t map[];
        };
    }
}
#endif
