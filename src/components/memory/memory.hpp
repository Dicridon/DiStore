#ifndef __DISTORE__MEMORY__MEMORY__
#define __DISTORE__MEMORY__MEMORY__

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
                        if (map[i] & Constants::byte_masks[j]) {
                            return i * 8 + j;
                        }
                    }
                }

                return {};
            }

            auto get_empty() -> std::optional<size_t> {
                for (size_t i = 0; i < bytes; i++) {
                    for (int j = 0; j < 8; j++) {
                        if (map[i] & Constants::byte_masks[j]) {
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
