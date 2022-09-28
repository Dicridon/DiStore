#include "erpc_wrapper.hpp"

namespace DiStore::RPCWrapper {
    auto RPCContext::initialize_nexus(const Cluster::IPV4Addr &ip, const int port) -> bool {
        auto uri = ip.to_string() + ":" + std::to_string(port);
        nexus = new erpc::Nexus(uri, 0, 0);

        if (nexus == nullptr) {
            Debug::error("Failed to create nexus for node %s\n", uri.c_str());
            return false;
        }
        return true;
    }

    auto ServerRPCContext::register_req_func(uint8_t req_type, erpc::erpc_req_func_t req_func,
                                             erpc::ReqFuncType req_func_type) -> int
    {
        return nexus->register_req_func(req_type, req_func, req_func_type);
    }

    auto ServerRPCContext::loop(size_t timeout_ms) -> void {
        info = create_new_rpc();
        info->rpc->run_event_loop(timeout_ms);
    }

    auto ServerRPCContext::loop_thread() -> std::thread {
        return std::thread([this]() {
            this->info = create_new_rpc();

            while(true) {
                this->info->rpc->run_event_loop(200);
            }
        });
    }

    auto ClientRPCContext::connect_remote(int node_id, Cluster::IPV4Addr &remote_ip,
                                          int remote_port, int rpc_id) noexcept
        -> bool
    {
        auto &info = create_new_rpc(node_id, rpc_id);

        auto remote_uri = remote_ip.to_string() + ":" + std::to_string(remote_port);

        auto s = info->rpc->create_session(remote_uri, rpc_id);

        if (s == -1) {
            Debug::error("Failed to connect %s\n", remote_uri.c_str());
            return false;
        }

        info->session = s;
        while (!info->rpc->is_connected(s)) {
            info->rpc->run_event_loop_once();
        }

        Debug::info("RPC Connected to remote %s with rpc id %d\n", remote_uri.c_str(), rpc_id);
        return true;
    }

    // we will not store many infos, thus linear searching is acceptable
    auto ClientRPCContext::select_info(int node_id, int remote_id, int session) const noexcept
        -> RPCConnectionInfo *
    {
        for (auto &i : infos) {
            if (i->node_id == node_id && i->remote_id == remote_id && i->session == session)
                return i.get();
        }

        return nullptr;
    }

    auto ClientRPCContext::select_first_info(int node_id) const noexcept
        -> RPCConnectionInfo *
    {
        for (auto &i : infos) {
            if (i->node_id == node_id)
                return i.get();
        }

        return nullptr;
    }

    auto ClientRPCContext::selecnt_all_info(int node_id) const noexcept
        -> std::vector<RPCConnectionInfo *>
    {
        std::vector<RPCConnectionInfo *> ret;
        for (auto &i : infos) {
            if (i->node_id == node_id)
                ret.push_back(i.get());
        }

        return ret;
    }
}
