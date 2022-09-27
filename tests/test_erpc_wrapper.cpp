#include "erpc_wrapper/erpc_wrapper.hpp"
#include "cmd_parser/cmd_parser.hpp"
#include "misc/misc.hpp"

using namespace DiStore;
using namespace RPCWrapper;
using namespace CmdParser;
auto remote_handler(erpc::ReqHandle *req_handle, void *ctx) -> void {
    auto rpc_ctx = reinterpret_cast<ServerRPCContext *>(ctx);

    auto &resp = req_handle->pre_resp_msgbuf;

    rpc_ctx->info->rpc->resize_msg_buffer(&resp, sizeof(Memory::RemotePointer));

    auto remote = 1234321UL;
    memcpy(resp.buf, &remote, sizeof(uint64_t));
    rpc_ctx->info->rpc->enqueue_response(req_handle, &resp);
}


static auto memory_continuation(void *ctx, void *tag) -> void {
    UNUSED(tag);
    auto info = reinterpret_cast<RPCWrapper::RPCConnectionInfo *>(ctx);
    info->done = true;
}


auto main() -> int {
    Parser parser;
    parser.add_option("--host", "-h");
    parser.add_option<int>("--host_port", "-p", 31851);
    parser.add_option("--self", "-s");
    parser.add_option<int>("--self_port", "-P", 31851);

    auto host = parser.get_as<std::string>("--host");
    auto self = parser.get_as<std::string>("--self");

    auto self_ip = Cluster::IPV4Addr::make_ipv4_addr(self.value()).value();
    auto self_port = parser.get_as<int>("--self_port").value();
    if (host.has_value()) {
        auto host_ip = Cluster::IPV4Addr::make_ipv4_addr(host.value()).value();
        auto host_port = parser.get_as<int>("--host_port").value();

        ClientRPCContext ctx;
        ctx.initialize_nexus(self_ip, self_port);
        ctx.connect_remote(0, host_ip, host_port, 0);

        auto info = ctx.select_first_info(0);

        info->done = false;
        info->rpc->resize_msg_buffer(&info->req_buf, 8);
        *reinterpret_cast<uint64_t *>(info->req_buf.buf) = 0UL;

        info->rpc->enqueue_request(info->session, Cluster::Enums::RPCOperations::RemoteAllocation,
                                   &info->req_buf, &info->resp_buf, memory_continuation, nullptr);
        while(!info->done){
            info->rpc->run_event_loop_once();
        }

        auto remote = *reinterpret_cast<uint64_t *>(info->resp_buf.buf);
        std::cout << "Got remote value " << remote << " from remote memory\n";
    } else {
        ServerRPCContext ctx;
        ctx.initialize_nexus(self_ip, self_port);
        ctx.register_req_func(0, remote_handler);

    }
    return 0;
}
