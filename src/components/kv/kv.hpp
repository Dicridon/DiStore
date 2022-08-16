#ifndef __DISTORE__KV__KV__
#define __DISTORE__KV__KV__

#include "memory/memory.hpp"

#include <cstring>
namespace DiStore {
    using namespace Memory;
    struct DiStoreStringHeader {
        uint16_t valid : 1;
        uint16_t length : 15;
    };
        
    /*
     * This is a simple compact string implementation
     * The length field should be copied to the index to avoid unnecessary PM accesses
     */
    struct DiStoreString {
        DiStoreStringHeader header;
        // not [0] so no warning. 
        byte_t content[1];

        static auto make_string(const byte_ptr_t &chunk, const_byte_ptr_t bytes, size_t size) -> DiStoreString & {
            auto ret = reinterpret_cast<DiStoreString *>(chunk);
            ret->header.length = size;
            memcpy(&ret->content, bytes, size);
            ret->header.valid = 1;
            return *ret;
        }

        static auto make_string(const byte_ptr_t &chunk, const char *bytes, size_t size) -> DiStoreString & {
            return make_string(chunk, reinterpret_cast<const_byte_ptr_t>(bytes), size);
        }

        auto compare(const char *rhs, size_t r_sz) const noexcept -> int {
            auto chars = raw_chars();
            auto bound = std::min(size(), r_sz);
            if (auto ret = strncmp(chars, rhs, bound); ret != 0) {
                return ret;
            } else {
                return size() - r_sz;
            }
        }

        auto operator==(const DiStoreString &rhs) const noexcept -> bool {
            if (header.length != rhs.header.length) {
                return false;
            }
                
            for (size_t i = 0; i < header.length; i++) {
                if (content[i] != rhs.content[i]) {
                    return false;                        
                }
            }
            return true;
        }

        auto operator!=(const DiStoreString &rhs) const noexcept -> bool {
            return !(*this == rhs);
        }

        auto operator<(const DiStoreString &rhs) const noexcept -> bool {
            for (size_t i = 0; i < std::min(header.length, rhs.header.length); i++) {
                if (content[i] > rhs.content[i]) {
                    return false;
                }

                if (content[i] < rhs.content[i]) {
                    return true;
                }
            }

            return header.length < rhs.header.length;
        }

        auto operator>(const DiStoreString &rhs) const noexcept -> bool {
            for (size_t i = 0; i < std::min(header.length, rhs.header.length); i++) {
                if (content[i] > rhs.content[i]) {
                    return true;
                }

                if (content[i] < rhs.content[i]) {
                    return false;
                }
            }

            return header.length > rhs.header.length;
        }

        auto operator<=(const DiStoreString &rhs) const noexcept -> bool {
            for (size_t i = 0; i < std::min(header.length, rhs.header.length); i++) {
                if (content[i] < rhs.content[i]) {
                    return true;
                }
                    
                if (content[i] > rhs.content[i]) {
                    return false;
                }
            }
            return header.length <= rhs.header.length;
        }

        auto operator>=(const DiStoreString &rhs) const noexcept -> bool {
            for (size_t i = 0; i < std::min(header.length, rhs.header.length); i++) {
                if (content[i] > rhs.content[i]) {
                    return true;
                }
                    
                if (content[i] < rhs.content[i]) {
                    return false;
                }
            }
            return header.length >= rhs.header.length;
        }            

        inline auto is_valid() const noexcept -> bool {
            return header.valid;
        }

        inline auto validate() noexcept -> void {
            header.valid = 1;
        }

        inline auto invalidate() noexcept -> void {
            header.valid = 0;
        }

        inline auto raw_bytes() const noexcept -> const_byte_ptr_t {
            return &content[0];
        }

        inline auto raw_chars() const noexcept -> const char * {
            return reinterpret_cast<const char *>(&content[0]);
        }

        inline auto to_string() const noexcept -> std::string {
            return std::string(raw_chars(), size());
        }

        inline auto size() const noexcept -> size_t {
            return header.length;
        }

        // size of the whole DiStoreString object including header and all content
        inline auto object_size() const noexcept -> size_t {
            return header.length + sizeof(header);
        }

        inline auto inplace_update(const_byte_ptr_t bytes, size_t size) noexcept -> bool {
            if (size > header.length)
                return false;

            memcpy(&content, bytes, size);
            header.length = size;
            return true;
        }
            
        inline auto inplace_update(const char *bytes, size_t size) noexcept -> bool {
            return inplace_update(reinterpret_cast<const_byte_ptr_t>(bytes), size);
        }
        DiStoreString() = delete;
        ~DiStoreString() = default;
        DiStoreString(const DiStoreString &) = delete;
        DiStoreString(DiStoreString &&) = delete;
        auto operator=(const DiStoreString &) = delete;
        auto operator=(DiStoreString &&) = delete;
    };

    using Key = DiStoreString;
    using Value = DiStoreString;
}
#endif
