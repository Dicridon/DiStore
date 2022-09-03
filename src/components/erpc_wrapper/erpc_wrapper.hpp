#ifndef __DISTORE__ERPC_WRAPPER__ERPC_WRAPPER__
#define __DISTORE__ERPC_WRAPPER__ERPC_WRAPPER__
#include "node/node.hpp"
#include "debug/debug.hpp"

#include "rpc.h"
namespace DiStore::RPCWrapper {
    static auto ghost_sm_handler(int, erpc::SmEventType, erpc::SmErrType, void *) -> void {};

    struct RPCConnectionInfo {
        // endpoint for communication, its id is stored in the following self_id field
        std::unique_ptr<erpc::Rpc<erpc::CTransport>> rpc;

        // this endpoint's own rpc id
        int self_id = -1;

        // remote node id
        int node_id = -1;

        // remote rpc id
        int remote_id = -1;

        // current session id
        int session = -1;
        erpc::MsgBuffer req_buf;
        erpc::MsgBuffer resp_buf;
        std::atomic_bool done;
    };


    // ServerContext and ClientContext have the same structure but different functionalities
    struct RPCContext {
        int current_id = 0;
        erpc::Nexus *nexus;
        // users can use this context in their eRPC handlers
        void *user_context = nullptr;

        auto initialize_nexus(const Cluster::IPV4Addr &ip, const int port) -> bool;
    };

    struct ServerRPCContext : RPCContext {
        std::unique_ptr<RPCConnectionInfo> info;

        auto register_req_func(uint8_t req_type, erpc::erpc_req_func_t req_func,
                               erpc::ReqFuncType req_func_type = erpc::ReqFuncType::kForeground)
            -> int;
        auto loop(size_t timeout_ms) -> void;
        auto loop_thread() -> std::thread;
        auto create_new_rpc(size_t buffer_size = 64) -> std::unique_ptr<RPCConnectionInfo> {
            auto rpc_info = std::make_unique<RPCConnectionInfo>();

            rpc_info->rpc = std::make_unique<erpc::Rpc<erpc::CTransport>>(nexus,
                                                                          this,
                                                                          current_id,
                                                                          ghost_sm_handler);
            rpc_info->self_id = current_id++;
            rpc_info->resp_buf = rpc_info->rpc->alloc_msg_buffer_or_die(buffer_size);
            return rpc_info;
        }
    };

    struct ClientRPCContext : RPCContext {
        std::vector<std::unique_ptr<RPCConnectionInfo>> infos;

        auto create_new_rpc(int node_id, int rpc_id, size_t buffer_size = 64)
            -> std::unique_ptr<RPCConnectionInfo> &
        {
            auto rpc_info = std::make_unique<RPCConnectionInfo>();

            rpc_info->rpc = std::make_unique<erpc::Rpc<erpc::CTransport>>(nexus,
                                                                          rpc_info.get(),
                                                                          current_id,
                                                                          ghost_sm_handler);
            rpc_info->self_id = current_id++;
            rpc_info->node_id = node_id;
            rpc_info->remote_id = rpc_id;

            rpc_info->req_buf = rpc_info->rpc->alloc_msg_buffer_or_die(buffer_size);
            rpc_info->resp_buf = rpc_info->rpc->alloc_msg_buffer_or_die(buffer_size);
            infos.push_back(std::move(rpc_info));
            return infos.back();
        }

        auto connect_remote(int node_id, Cluster::IPV4Addr &remote_ip, int remote_port,
                            int rpc_id) noexcept
            -> bool;
        auto select_info(int node_id, int remote_id, int session) const noexcept
            -> RPCConnectionInfo *;
        auto select_first_info(int node_id) const noexcept -> RPCConnectionInfo *;
        auto selecnt_all_info(int node_id) const noexcept -> std::vector<RPCConnectionInfo *>;
        // we do not implement send_request for the wrapper because the augument list is too long
    };
}
#endif
