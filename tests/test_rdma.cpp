#include "rdma_util/rdma_util.hpp"
#include "cmd_parser/cmd_parser.hpp"
#include "misc/misc.hpp"

using namespace DiStore;
using namespace DiStore::RDMAUtil;
using namespace CmdParser;

auto main(int argc, char *argv[]) -> int {
    Parser parser;
    parser.add_option("--server", "-s");
    parser.add_option<std::string>("--device", "-d", "mlx5_0");
    parser.add_option<int>("--gid", "-g", 0);
    parser.add_option<int>("--port", "-p", 1);

    parser.parse(argc, argv);
    auto server = parser.get_as<std::string>("--server");
    auto device = parser.get_as<std::string>("--device").value();
    auto gid = parser.get_as<int>("--gid").value();
    auto port = parser.get_as<int>("--port").value();

    
    auto [dev, _] = RDMADevice::make_rdma(device, port, gid);
    auto buffer = new byte_t[1024];

    if (server.has_value()) {
        auto ctx = dev->open(buffer, 1024, 1, RDMADevice::get_default_mr_access(),
                             *RDMADevice::get_default_qp_init_attr()).first;

        auto socket = Misc::socket_connect(false, 2333, server.value().c_str());
        ctx->default_connect(socket);

        // auto number = 0xabcddcbaUL;
        // auto ptr = (uint64_t *)buffer;
        // ptr[0] = number;
        // ptr[1] = 0x12344321UL;
        // 
        // auto sge1 = ctx->generate_sge(nullptr, sizeof(number), 0);
        // auto sge2 = ctx->generate_sge(nullptr, sizeof(number), sizeof(number));
        // 
        // ctx->post_write((uint8_t *)&number, sizeof(number));
        // ctx->post_batch_write({sge1, sge2});
        // ctx->poll_completion_once();
        // 
        // ctx->post_read(sizeof(number));
        // auto n = *(uint64_t *)ctx->get_buf();
        // std::cout << "read " << n << "\n";
        // 
        // ctx->post_read(sizeof(number), 0, sizeof(number));
        // n = *(uint64_t *)ctx->get_buf();
        // std::cout << "read " << n << "\n";
        ctx->post_batch_write_test();
    } else {
        auto ctx = dev->open(buffer, 1024, 1, RDMADevice::get_default_mr_access(),
                             *RDMADevice::get_default_qp_init_attr()).first;

        auto socket = Misc::socket_connect(true, 2333);
        ctx->default_connect(socket);

        sleep(100);
    }
}
