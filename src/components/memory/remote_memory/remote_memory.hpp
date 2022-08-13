#ifndef __DISTORE__MEMORY__REMOTE_MEMORY__REMOTE_MEMORY__
#define __DISTORE__MEMORY__REMOTE_MEMORY__REMOTE_MEMORY__
#include "../memory.hpp"
#include "../../rdma_util/rdma_util.hpp"
namespace DiStore {
    namespace Memory {
        namespace Constants {
            static constexpr uint64_t uREMOTE_POINTER_MASK = ~0xffff000000000000UL;
            static constexpr uint64_t uREMOTE_POINTER_BITS_MASK = 0xc000000000000000UL;
            static constexpr uint64_t uREMOTE_POINTER_BITS = 0x2UL;
        }

        namespace Enums {

        }

        /* !!!NEVER INHERIT FROM ANY OTHER STRUCT OR CLASS!!! */
        /*
         * RemotePointer is a pointer with node infomation embeded in the highest 16 bits.
         * To get the correct address in x86, the 16 bits should be the same as bit 47 of
         * the original pointer, i.e., it is a canonical pointer.
         *
         * Current RemotePointer layout is as follows
         *
         * 63 62 61 60 59 58 57 56 55              48             0
         * --------------------------------------------------------
         * |  A  |       B      |  |       C       |              |
         * --------------------------------------------------------
         * A: remote pointer bits, 0b'10 indicates a remote pointer
         * B: node ID (64 machines at most)
         * C: filling hint
         */
        class RemotePointer {
        public:
            inline static auto is_remote_pointer(const byte_ptr_t &ptr) -> bool {
                auto bits = ((reinterpret_cast<uint64_t>(ptr) & Constants::uREMOTE_POINTER_BITS_MASK) >> 62);
                return bits == Constants::uREMOTE_POINTER_BITS;
            }

            inline static auto make_remote_pointer(uint64_t node, uint64_t address) -> RemotePointer {
                auto value = address & Constants::uREMOTE_POINTER_MASK;
                auto meta = (Constants::uREMOTE_POINTER_BITS << 6) | (node & (0x3fUL));
                auto tmp = (meta << 56) | value;

                // dangerous operation, copy(move) assignment is necessary
                return *reinterpret_cast<RemotePointer *>(&tmp);
            }

            inline static auto make_remote_pointer(uint64_t node, const byte_ptr_t &address) -> RemotePointer {
                auto value = reinterpret_cast<uint64_t>(address) & Constants::uREMOTE_POINTER_MASK;
                auto meta = (Constants::uREMOTE_POINTER_BITS << 6) | (node & (~0xc0UL));
                auto tmp = (meta << 56) | value;

                // dangerous operation, copy(move) assignment is necessary
                return *reinterpret_cast<RemotePointer *>(&tmp);
            }

            RemotePointer() = default;
            RemotePointer(std::nullptr_t nu) : ptr(nu) {};
            RemotePointer(byte_ptr_t &p) : ptr(p) {};

            ~RemotePointer() = default;
            RemotePointer(const RemotePointer &) = default;
            RemotePointer(RemotePointer &&) = default;
            auto operator=(const RemotePointer &) -> RemotePointer & = default;
            auto operator=(RemotePointer &&) -> RemotePointer & = default;

            template<typename T, typename = typename std::enable_if_t<std::is_pointer_v<T>>>
            auto get_as() const noexcept -> T {
                auto copy = ptr;
                auto cursor = reinterpret_cast<byte_ptr_t>(&copy);
                cursor[7] = cursor[6];
                return reinterpret_cast<T>(copy);
            }

            inline auto get_node() const noexcept -> int {
                auto value = reinterpret_cast<uint64_t>(ptr);
                return (value >> 56) & (0x3fUL);
            }

            inline auto raw_ptr() const noexcept -> byte_ptr_t {
                return ptr;
            }

            inline auto void_ptr() const noexcept -> void * {
                return (void *)ptr;
            }

            inline auto is_nullptr() const noexcept -> bool {
                return ptr == nullptr;
            }
        private:
            byte_ptr_t ptr;
        };

        class PolymorphicPointer {
        public:
            PolymorphicPointer() = default;
            PolymorphicPointer(const RemotePointer &re) {
                ptr.remote = re;
            }
            PolymorphicPointer(std::nullptr_t nu) {
                ptr.local = nu;
            }
            template<typename T, typename = typename std::enable_if_t<std::is_pointer_v<T>>>
            PolymorphicPointer(const T &lo) {
                ptr.local = lo;
            }
            ~PolymorphicPointer() = default;
            PolymorphicPointer(const PolymorphicPointer &) = default;
            PolymorphicPointer(PolymorphicPointer &&) = default;
            auto operator=(const PolymorphicPointer &) -> PolymorphicPointer & = default;
            auto operator=(PolymorphicPointer &&) -> PolymorphicPointer & = default;
            auto operator=(const RemotePointer &re) -> PolymorphicPointer & {
                ptr.remote = re;
                return *this;
            }
            auto operator=(const byte_ptr_t &lo) -> PolymorphicPointer & {
                ptr.local = lo;
                return *this;
            }
            auto operator=(const std::nullptr_t &nu) -> PolymorphicPointer {
                ptr.local = nu;
                return *this;
            }

            inline auto operator==(const PolymorphicPointer &rhs) -> bool {
                return ptr.local == rhs.ptr.local;
            }

            inline auto operator!=(const PolymorphicPointer &rhs) -> bool {
                return ptr.local != rhs.ptr.local;
            }

            inline auto operator==(std::nullptr_t nu) -> bool {
                return ptr.local == nu;
            }

            inline auto operator!=(std::nullptr_t nu) -> bool {
                return ptr.local != nu;
            }

            template<typename T, typename = std::enable_if_t<std::is_pointer_v<T>>>
            static auto make_polymorphic_pointer(const T &t) -> PolymorphicPointer {
                PolymorphicPointer ret;
                ret.ptr.local = reinterpret_cast<byte_ptr_t>(t);
                return ret;
            }

            static auto make_polymorphic_pointer(const RemotePointer &t) -> PolymorphicPointer {
                PolymorphicPointer ret;
                ret.ptr.remote = t;
                return ret;
            }

            inline auto is_remote() const noexcept -> bool {
                return RemotePointer::is_remote_pointer(ptr.remote.raw_ptr());
            }

            inline auto is_local() const noexcept -> bool {
                return !RemotePointer::is_remote_pointer(ptr.remote.raw_ptr());
            }

            inline auto is_nullptr() const noexcept -> bool {
                return ptr.local == nullptr;
            }

            inline auto raw_ptr() const noexcept -> byte_ptr_t {
                return ptr.local;
            }

            inline auto remote_ptr() const noexcept -> RemotePointer {
                return ptr.remote;
            }

            inline auto local_ptr() const noexcept -> byte_ptr_t {
                return ptr.local;
            }

            template<typename T, typename = typename std::enable_if_t<std::is_pointer_v<T>>>
            auto get_as() const noexcept -> T {
                if (is_local()) {
                    return reinterpret_cast<T>(ptr.local);
                } else {
                    return ptr.remote.get_as<T>();
                }
            }

        private:
            union {
                RemotePointer remote;
                byte_ptr_t local;
            } ptr;
        };

    }
}
#endif
