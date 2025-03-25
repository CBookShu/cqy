#pragma once
#include "cqy_utils.h"
#include "ylt/coro_io/client_pool.hpp"
#include "ylt/coro_rpc/impl/coro_rpc_client.hpp"
#include "ylt/coro_rpc/impl/default_config/coro_rpc_config.hpp"
#include <cstdint>
#include <string_view>

namespace cqy {

  struct node_info {
    std::string name;
    std::string ip;
    uint32_t nodeid = 0;
    int port = 0;
  };

  class cqy_node : move_only{
    struct cqy_node_t;
    cqy_node_t* s_;
  public:
    using rpc_client = coro_rpc::coro_rpc_client;
    using rpc_client_pool = coro_io::client_pool<rpc_client>;
    using rpc_server = coro_rpc::coro_rpc_server;

    struct node_t {
      node_info info;
      cqy_coro_queue_t coro_queue;
      sptr<cqy_node::rpc_client_pool> rpc_client;
    };

    cqy_node();
    ~cqy_node();

    uint8_t self_nodeid();
    bool check_self(uint8_t nodeid);

    sptr<node_t> get_node(uint8_t nodeid);
    sptr<node_t> get_node(std::string_view name);

    void create_client(node_info& info);
    rpc_server& create_rpc_server(uint32_t thread, node_info& info);
    async_simple::Future<coro_rpc::err_code> rpc_server_start();
    void rpc_server_close();
    void shutdown();
  protected:
    Lazy<void> node_mq_spawn(uint8_t id);
  };
}