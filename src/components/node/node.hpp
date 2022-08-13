#ifndef __DISTORE__NODE__NODE__
#define __DISTORE__NODE__NODE__
#include "../memory/memory.hpp"
#include "../memory/remote_memory/remote_memory.hpp"

#include <regex>
namespace DiStore {
    namespace Cluster {
        struct IPV4Addr {
            uint8_t content[4];

            static auto make_ipv4_addr(const std::string &in) -> std::optional<IPV4Addr> {
                std::regex ip_pattern("(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})");
                std::smatch result;
                if (!std::regex_match(in, result, ip_pattern)) {
                    return {};
                }

                IPV4Addr addr;
                for (int i = 0; i < 4; i++) {
                    addr.content[i] = atoi(result[i + 1].str().c_str());
                }
                return addr;
            }
            
            auto to_string() const -> std::string {
                std::stringstream stream;
                stream << std::to_string(content[0]);
                for (int i = 1; i < 4; i++) {
                    stream << "." << std::to_string(content[i]);
                }
                return stream.str();
            }
        } __attribute__((packed));
        
        struct NodeInfo {
            int node_id;
            IPV4Addr tcp_addr;
            int tcp_port;

            IPV4Addr roce_addr;
            int roce_port;

            IPV4Addr erpc_addr;
            IPV4Addr erpc_port;
        } __attribute__((packed));
        
        struct ComputeNodeInfo : NodeInfo {
            
        };
        
        struct MemoryNodeInfo : NodeInfo {
            Memory::RemotePointer base_addr;
        };
    }
}
#endif

