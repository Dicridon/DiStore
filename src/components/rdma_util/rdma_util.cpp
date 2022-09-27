#include "rdma_util.hpp"
namespace DiStore::RDMAUtil {
    auto decode_rdma_status(const Enums::Status& status) -> std::string {
        switch(status){
        case Enums::Status::Ok:
            return "Ok";
        case Enums::Status::NoRDMADeviceList:
            return "NoRDMADeviceList";
        case Enums::Status::DeviceNotFound:
            return "DeviceNotFound";
        case Enums::Status::NoGID:
            return "NoGID";
        case Enums::Status::CannotOpenDevice:
            return "CannotOpenDevice";
        case Enums::Status::CannotAllocPD:
            return "CannotAllocPD";
        case Enums::Status::CannotCreateCQ:
            return "CannotCreateCQ";
        case Enums::Status::CannotRegMR:
            return "CannotRegMR";
        case Enums::Status::CannotCreateQP:
            return "CannotCreateQP";
        case Enums::Status::CannotQueryPort:
            return "CannotQueryPort";
        case Enums::Status::InvalidGIDIdx:
            return "InvalidGIDIdx";
        case Enums::Status::InvalidIBPort:
            return "InvalidIBPort";
        case Enums::Status::InvalidArguments:
            return "InvalidArguments";
        case Enums::Status::CannotInitQP:
            return "CannotInitQP";
        case Enums::Status::QPRTRFailed:
            return "QPRTRFailed";
        case Enums::Status::QPRTSFailed:
            return "QPRTSFailed";
        case Enums::Status::DeviceNotOpened:
            return "DeviceNotOpened";
        case Enums::Status::ReadError:
            return "ReadError";
        case Enums::Status::WriteError:
            return "WriteError";
        default:
            return "Unknown status";
        }
    }

    auto RDMAContext::default_connect(int socket) -> int {
        if (auto status = exchange_certificate(socket); status != Status::Ok) {
            Debug::error("Failed to exchange RDMA, error code: %s\n",
                         decode_rdma_status(status).c_str());
            return -1;
        }

        Debug::info("Certificate exchanged\n");

        auto init_attr = RDMADevice::get_default_qp_init_state_attr();
        if (auto [status, err] = modify_qp(*init_attr, RDMADevice::get_default_qp_init_state_attr_mask()); status != Status::Ok) {
            Debug::error("Modify QP to Init failed, error code: %d\n", err);
            return err;
        }

        auto rtr_attr = RDMADevice::get_default_qp_rtr_attr(remote, device->get_ib_port(), device->get_gid_idx());
        if (auto [status, err] = modify_qp(*rtr_attr, RDMADevice::get_default_qp_rtr_attr_mask()); status != Status::Ok) {
            Debug::error("Modify QP to RTR failed, error code: %d\n", err);
            return err;
        }

        auto rts_attr = RDMADevice::get_default_qp_rts_attr();
        if (auto [status, err] = modify_qp(*rts_attr, RDMADevice::get_default_qp_rts_attr_mask()); status != Status::Ok) {
            Debug::error("Modify QP to RTS failed, error code: %d\n", err);
            return err;
        }
        return 0;
    }


    auto RDMAContext::modify_qp(struct ibv_qp_attr &attr, int mask) noexcept -> StatusPair {
        if (auto ret =  ibv_modify_qp(qp, &attr, mask); ret == 0) {
            return std::make_pair(Status::Ok, ret);
        } else {
            return std::make_pair(Status::CannotInitQP, ret);
        }
    }

    auto RDMAContext::exchange_certificate(int sockfd) noexcept -> Status {
        const int normal = sizeof(connection_certificate);
        connection_certificate tmp;
        tmp.addr = htonll(local.addr);
        tmp.rkey = htonl(local.rkey);
        tmp.qp_num = htonl(local.qp_num);
        tmp.lid = htons(local.lid);
        memcpy(tmp.gid, local.gid, 16);
        if (write(sockfd, &tmp, normal) != normal) {
            return Status::WriteError;
        }

        if (read(sockfd, &tmp, normal) != normal) {
            return Status::ReadError;
        }

        remote.addr = ntohll(tmp.addr);
        remote.rkey = ntohl(tmp.rkey);
        remote.qp_num = ntohl(tmp.qp_num);
        remote.lid = ntohs(tmp.lid);
        memcpy(remote.gid, tmp.gid, 16);
        return Status::Ok;
    }

    auto RDMAContext::post_send_helper(const uint8_t *msg, size_t msg_len,
                                       enum ibv_wr_opcode opcode,
                                       size_t local_offset, size_t remote_offset)
        -> StatusPair
    {
        struct ibv_sge sg;
        struct ibv_send_wr sr;
        struct ibv_send_wr *bad_wr;
        auto byte_buf = reinterpret_cast<byte_ptr_t>(buf) + local_offset;

        if (msg) {
            memcpy(byte_buf, msg, msg_len);
        }

        memset(&sg, 0, sizeof(sg));
        sg.addr	  = reinterpret_cast<uint64_t>(byte_buf);
        sg.length = msg_len;
        sg.lkey	  = mr->lkey;

        memset(&sr, 0, sizeof(sr));
        sr.wr_id      = 0;
        sr.sg_list    = &sg;
        sr.num_sge    = 1;
        sr.opcode     = opcode;
        sr.send_flags = IBV_SEND_SIGNALED;

        if (opcode != IBV_WR_SEND) {
            sr.wr.rdma.remote_addr = remote.addr + remote_offset;
            sr.wr.rdma.rkey = remote.rkey;
        }

        if (auto ret = ibv_post_send(qp, &sr, &bad_wr); ret != 0) {
            return {Status::PostFailed, ret};
        }
        return {Status::Ok, 0};
    }

    auto RDMAContext::post_send_helper(const byte_ptr_t &ptr, const uint8_t *msg,
                                       size_t msg_len, enum ibv_wr_opcode opcode,
                                       size_t local_offset)
        -> StatusPair
    {
        auto remote_offset = reinterpret_cast<uint64_t>(ptr) - remote.addr;
        return post_send_helper(msg, msg_len, opcode, local_offset, remote_offset);
    }

    auto RDMAContext::post_send(const uint8_t *msg, size_t msg_len, size_t local_offset)
        -> StatusPair
    {
        return post_send_helper(msg, msg_len, IBV_WR_SEND, local_offset, 0);
    }

    auto RDMAContext::post_send(const byte_ptr_t &ptr, uint8_t *msg, size_t msg_len,
                                size_t local_offset)
        -> StatusPair
    {
        return post_send_helper(ptr, msg, msg_len, IBV_WR_SEND, local_offset);
    }

    auto RDMAContext::post_read(size_t msg_len, size_t local_offset, size_t remote_offset)
        -> StatusPair
    {
        return post_send_helper(nullptr, msg_len, IBV_WR_RDMA_READ, local_offset,
                                remote_offset);
    }

    auto RDMAContext::post_read(const byte_ptr_t &ptr, size_t msg_len, size_t local_offset)
        -> StatusPair
    {
        return post_send_helper(ptr, nullptr, msg_len, IBV_WR_RDMA_READ, local_offset);
    }

    auto RDMAContext::post_write(const uint8_t *msg, size_t msg_len, size_t local_offset,
                                 size_t remote_offset)
        -> StatusPair
    {
        return post_send_helper(msg, msg_len, IBV_WR_RDMA_WRITE, local_offset, remote_offset);
    }

    auto RDMAContext::post_write(const byte_ptr_t &ptr, const uint8_t *msg, size_t msg_len,
                                 size_t local_offset)
        -> StatusPair
    {
        return post_send_helper(ptr, msg, msg_len, IBV_WR_RDMA_WRITE, local_offset);
    }

    auto RDMAContext::post_recv_to(size_t msg_len, size_t offset) -> StatusPair {
        struct ibv_sge sg;
        struct ibv_recv_wr wr;
        struct ibv_recv_wr *bad_wr;

        auto tmp = (uint8_t *)buf + offset;
        memset(&sg, 0, sizeof(sg));
        sg.addr	  = (uintptr_t)tmp;
        sg.length = msg_len;
        sg.lkey	  = mr->lkey;

        memset(&wr, 0, sizeof(wr));
        wr.wr_id      = 0;
        wr.sg_list    = &sg;
        wr.num_sge    = 1;

        if (auto ret = ibv_post_recv(qp, &wr, &bad_wr); ret != 0) {
            return std::make_pair(Status::RecvFailed, ret);
        }
        return std::make_pair(Status::Ok, 0);
    }

    auto RDMAContext::generate_sge(const byte_ptr_t msg, size_t msg_len, size_t offset) -> struct ibv_sge {
        auto addr = fill_buf(msg, msg_len, offset);
        struct ibv_sge s;
        s.addr = uint64_t(addr);
        s.length = msg_len;
        s.lkey = mr->lkey;
        return s;
    }

    auto RDMAContext::post_batch_write(std::vector<struct ibv_sge> sges) -> StatusPair {
        struct ibv_send_wr *wrs = new struct ibv_send_wr[sges.size() + 1];
        struct ibv_send_wr *bad_wr;

        size_t i;
        for (i = 0; i < sges.size(); i++) {
            memset(wrs + i, 0, sizeof(struct ibv_send_wr));
            wrs[i].wr_id = i;
            wrs[i].next = &wrs[i + 1];
            wrs[i].sg_list = &sges[i];
            wrs[i].num_sge = 1;
            wrs[i].opcode = IBV_WR_RDMA_WRITE;
        }

        wrs[i - 1].next = nullptr;
        wrs[i - 1].send_flags = IBV_SEND_SIGNALED;

        if (auto ret = ibv_post_send(qp, wrs, &bad_wr); ret != 0) {
            Debug::error("posting wr %d failed\n", bad_wr->wr_id);
            return std::make_pair(Status::WriteError, ret);
        }

        return std::make_pair(Status::Ok, 0);
    }

    auto RDMAContext::poll_completion_once(bool send) noexcept -> int {
        struct ibv_wc wc;
        int ret;
        auto cq = send ? out_cq : in_cq;
        do {
            ret = ibv_poll_cq(cq, 1, &wc);
        } while (ret == 0);

        return ret;
    }

    auto RDMAContext::poll_one_completion(bool send) noexcept
        -> std::pair<std::unique_ptr<struct ibv_wc>, int>
    {
        auto wc = std::make_unique<struct ibv_wc>();
        int ret;
        auto cq = send ? out_cq : in_cq;
        do {
            ret = ibv_poll_cq(cq, 1, wc.get());
        } while (ret == 0);

        return {std::move(wc), ret};
    }

    auto RDMAContext::poll_multiple_completions(size_t no, bool send) noexcept
        -> std::pair<std::unique_ptr<struct ibv_wc[]>, int>
    {
        auto wc = std::make_unique<struct ibv_wc[]>(no);
        int ret;
        auto cq = send ? out_cq : in_cq;
        do {
            ret = ibv_poll_cq(cq, no, wc.get());
        } while (ret == 0);

        return {std::move(wc), ret};
    }

    auto RDMAContext::fill_buf(uint8_t *msg, size_t msg_len, size_t offset) -> byte_ptr_t {
        if (msg && msg_len != 0)
            memcpy((uint8_t *)buf + offset, msg, msg_len);
        return (byte_ptr_t)buf + offset;
    }


    auto RDMADevice::open(void *membuf, size_t memsize, size_t cqe, int mr_access,
                          struct ibv_qp_init_attr &attr)
        -> std::pair<std::unique_ptr<RDMAContext>, Status>
    {
        auto rdma_ctx = RDMAContext::make_rdma_context();
        rdma_ctx->ctx = ctx;
        if (!membuf || !cqe) {
            return {nullptr, Status::InvalidArguments};
        }

        if (!(rdma_ctx->pd = ibv_alloc_pd(ctx))) {
            return {nullptr, Status::CannotAllocPD};
        }

        if (!(rdma_ctx->in_cq = ibv_create_cq(ctx, cqe, nullptr, nullptr, 0))) {
            return {nullptr, Status::CannotCreateCQ};
        }

        if (!(rdma_ctx->out_cq = ibv_create_cq(ctx, cqe, nullptr, nullptr, 0))) {
            return {nullptr, Status::CannotCreateCQ};
        }

        if (!(rdma_ctx->mr = ibv_reg_mr(rdma_ctx->pd, membuf, memsize, mr_access))) {
            return {nullptr, Status::CannotRegMR};
        }

        attr.send_cq = rdma_ctx->out_cq;
        attr.recv_cq = rdma_ctx->in_cq;
        if (!(rdma_ctx->qp = ibv_create_qp(rdma_ctx->pd, &attr))) {
            return {nullptr, Status::CannotCreateQP};
        }

        union ibv_gid my_gid;
        if (gid_idx >= 0) {
            if (ibv_query_gid(ctx, ib_port, gid_idx, &my_gid)) {
                return {nullptr, Status::NoGID};
            }
            memcpy(rdma_ctx->local.gid, &my_gid, 16);
        }
        rdma_ctx->local.addr = (uint64_t)membuf;
        rdma_ctx->local.rkey = rdma_ctx->mr->rkey;
        rdma_ctx->local.qp_num = rdma_ctx->qp->qp_num;

        struct ibv_port_attr pattr;
        if (ibv_query_port(ctx, ib_port, &pattr)) {
            return {nullptr, Status::CannotQueryPort};
        }
        rdma_ctx->local.lid = pattr.lid;

        rdma_ctx->buf = membuf;
        rdma_ctx->device = this;
        return {std::move(rdma_ctx), Status::Ok};
    }

    auto RDMADevice::get_default_qp_init_attr() -> std::unique_ptr<struct ibv_qp_init_attr> {
        auto at = std::make_unique<ibv_qp_init_attr>();
        memset(at.get(), 0, sizeof(struct ibv_qp_init_attr));

        at->qp_type = IBV_QPT_RC;
        // post_send_helper will set signals accordingly
        at->sq_sig_all = 0;
        at->cap.max_send_wr = Constants::MAX_QP_DEPTH;
        at->cap.max_recv_wr = Constants::MAX_QP_DEPTH;
        at->cap.max_send_sge = 1;
        at->cap.max_recv_sge = 1;
        return at;
    }

    auto RDMADevice::get_default_qp_init_state_attr(const int ib_port)
        -> std::unique_ptr<struct ibv_qp_attr>
    {
        auto attr = std::make_unique<struct ibv_qp_attr>();
        memset(attr.get(), 0, sizeof(struct ibv_qp_attr));

        attr->qp_state = IBV_QPS_INIT;
        attr->port_num = ib_port;
        attr->pkey_index = 0;
        attr->qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
            IBV_ACCESS_REMOTE_READ |
            IBV_ACCESS_REMOTE_WRITE;
        return attr;
    }

    auto RDMADevice::get_default_qp_rtr_attr(const connection_certificate &remote,
                                             const int ib_port = 1,
                                             const int sgid_idx = -1)
        -> std::unique_ptr<struct ibv_qp_attr>
    {
        auto attr = std::make_unique<struct ibv_qp_attr>();
        memset(attr.get(), 0, sizeof(struct ibv_qp_attr));

        attr->qp_state = IBV_QPS_RTR;
        attr->path_mtu = IBV_MTU_256;
        attr->dest_qp_num = remote.qp_num;
        attr->rq_psn = 0;
        attr->max_dest_rd_atomic = 1;
        attr->min_rnr_timer = 0x12;

        attr->ah_attr.is_global = 0;
        attr->ah_attr.dlid = remote.lid;
        attr->ah_attr.sl = 0;
        attr->ah_attr.src_path_bits = 0;
        attr->ah_attr.port_num = ib_port;

        if (sgid_idx >= 0) {
            attr->ah_attr.is_global = 1;
            attr->ah_attr.port_num = 1;
            memcpy(&attr->ah_attr.grh.dgid, remote.gid, 16);
            attr->ah_attr.grh.flow_label = 0;
            attr->ah_attr.grh.hop_limit = 1;
            attr->ah_attr.grh.sgid_index = sgid_idx;
            attr->ah_attr.grh.traffic_class = 0;
        }
        return attr;
    }

    auto RDMADevice::get_default_qp_rts_attr() -> std::unique_ptr<struct ibv_qp_attr> {
        auto attr = std::make_unique<struct ibv_qp_attr>();
        memset(attr.get(), 0, sizeof(struct ibv_qp_attr));

        attr->qp_state = IBV_QPS_RTS;
        attr->timeout = 0x12; // 18
        attr->retry_cnt = 6;
        attr->rnr_retry = 0;
        attr->sq_psn = 0;
        attr->max_rd_atomic = 1;
        return attr;
    }
}
