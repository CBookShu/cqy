#include "ylt/coro_http/coro_http_server.hpp"
#include "ylt/coro_io/coro_io.hpp"
#include "ylt/coro_io/io_context_pool.hpp"
#include "async_simple/coro/SyncAwait.h"
#include "cinatra/coro_http_connection.hpp"
#include "cinatra/define.h"
#include "cqy.h"
#include "ylt/thirdparty/async_simple/coro/Dispatch.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

cqy::Lazy<void> ws_start() {
  using namespace cinatra;
  using namespace std::chrono_literals;

  coro_http::coro_http_server server(1, 9001);
  auto cert_path = std::filesystem::current_path().append("server.crt");
  auto key_path = std::filesystem::current_path().append("server.key");
  server.init_ssl(cert_path.string(), key_path.string(), "");

  server.set_http_handler<cinatra::GET>(
      "/ws_echo",
      [](coro_http_request &req,
         coro_http_response &resp) -> async_simple::coro::Lazy<void> {
        assert(req.get_content_type() == content_type::websocket);
        websocket_result result{};
        while (true) {
          result = co_await req.get_conn()->read_websocket();
          if (result.ec) {
            break;
          }

          if (result.type == ws_frame_type::WS_CLOSE_FRAME) {
            std::cout << "close frame\n";
            break;
          }

          if (result.type == ws_frame_type::WS_TEXT_FRAME ||
              result.type == ws_frame_type::WS_BINARY_FRAME) {
            std::cout << result.data << "\n";
          } else if (result.type == ws_frame_type::WS_PING_FRAME ||
                     result.type == ws_frame_type::WS_PONG_FRAME) {
            // ping pong frame just need to continue, no need echo anything,
            // because framework has reply ping/pong msg to client
            // automatically.
            continue;
          } else {
            // error frame
            break;
          }

          auto ec = co_await req.get_conn()->write_websocket(result.data);
          if (ec) {
            break;
          }
        }
      });
  auto res = server.sync_start();
  CQY_INFO("server start:{}", res.message());
  co_return;
}

struct ws_server_t : public cqy::cqy_ctx_t {
  cqy::uptr<coro_http::coro_http_server> server;
  // guard by lock
  cqy::coro_spinlock lock;
  uint32_t new_alloc_nodectx = 0;
  std::string new_alloc_func;
  struct ws_conn_t {
    cqy::wptr<cinatra::coro_http_connection> conn;
    cqy::coro_spinlock lock;
  };
  std::unordered_map<uint64_t, cqy::sptr<ws_conn_t>> conns;
  struct sub_info_t {
    std::string func_on_start;
    std::string func_on_read;
    std::string func_on_stop;
  };
  std::unordered_map<uint32_t, sub_info_t> sub_ctxs;
  /*
    param format: thread,port,cert_path,key_path,passwd
  */
  virtual bool on_init(std::string_view param) override {
    using namespace cinatra;

    register_name("ws_server");
    auto params = cqy::algo::split(param, ",");
    if (params.size() != 5) {
      CQY_ERROR("ws_server init param error:{}", param);
      CQY_ERROR("ws_server param: thread,port,cert_path,key_path,passwd", param);
      return false;
    }
    register_rpc_func<&ws_server_t::rpc_set_allocer>("set_allocer");
    register_rpc_func<&ws_server_t::rpc_sub>("sub");
    register_rpc_func<&ws_server_t::rpc_sub>("write");

    try {
      auto thread = cqy::algo::to_n<size_t>(params[0]);
      auto port = cqy::algo::to_n<uint16_t>(params[1]);
      server = std::make_unique<coro_http::coro_http_server>(thread.value(),
                                                             port.value());
      server->init_ssl(std::string(params[2].data(), params[2].size()),
                       std::string(params[3].data(), params[3].size()),
                       std::string(params[4].data(), params[4].size()));

      server->set_http_handler<cinatra::GET>(
          "/",
          [this](coro_http_request &req,
                 coro_http_response &resp) -> cqy::Lazy<void> {
            co_return co_await co_ws_handle(req, resp);
          });
      auto start_f = server->async_start();
      if (start_f.hasResult()) {
        auto ec = std::move(start_f).get();
        CQY_ERROR("ws_server start error:{}", ec.message());
        return false;
      }
      co_ws_start(std::move(start_f)).via(ex).detach();
      CQY_INFO("ws_server init success:{}", param);
    } catch (const std::exception &e) {
      CQY_ERROR("ws_server init err:{} param:{}", e.what(), param);
      return false;
    }
    return true;
  }

  virtual void on_stop() override {
    if (server) {
      server->stop();
    }
  }

  cqy::Lazy<bool> rpc_set_allocer(uint32_t id, std::string func_new_alloc) {
    auto guard = lock.coLock();
    new_alloc_nodectx = id;
    new_alloc_func = func_new_alloc;
    co_return true;
  }

  cqy::Lazy<bool> rpc_sub(uint32_t subid, std::string func_on_start,
                          std::string func_on_read, std::string func_on_stop) {
    auto guard = lock.coLock();
    sub_ctxs[subid] = {
      .func_on_start = std::move(func_on_start),
      .func_on_read = std::move(func_on_read),
      .func_on_stop = std::move(func_on_stop)
    };
    co_return true;
  }

  cqy::Lazy<void> rpc_write(uint64_t connid, std::string msg) {
    co_conn_write(connid, std::move(msg)).start([](auto &&tr) {});
    co_return;
  }

  cqy::Lazy<uint64_t> co_get_connid() {
    try {
      uint32_t to = 0;
      std::string func;
      {
        auto guard = lock.coLock();
        if (new_alloc_nodectx == 0 || new_alloc_func.empty()) {
          CQY_ERROR("allocer func not init");
          co_return 0;
        }
        to = new_alloc_nodectx;
        func = new_alloc_func;
      }
      auto r = co_await app->ctx_call<>(to, func);
      co_return r.as<uint64_t>();
    } catch (std::exception &e) {
      CQY_ERROR("ws alloc func error:{},{}", new_alloc_nodectx, new_alloc_func);
      CQY_ERROR("ws alloc error:{}", e.what());
    }
    co_return 0;
  }

  cqy::Lazy<void> co_ws_handle(cinatra::coro_http_request &req,
                               cinatra::coro_http_response &resp) {
    uint64_t connid = co_await co_get_connid();
    if (connid == 0) {
      co_return;
    }
    auto ws_con = std::make_shared<ws_conn_t>();
    ws_con->conn = req.get_conn()->weak_from_this();
    {
      auto guard = lock.coLock();
      conns[connid] = ws_con;
    }
    pub_conn_start(connid);
    co_await co_conn_read(connid, req);
    pub_conn_stop(connid);
    {
      auto guard = lock.coLock();
      conns.erase(connid);
    }
    co_return;
  }

  cqy::sptr<cinatra::coro_http_connection> get_ws_con(uint64_t connid) {
    auto guard = lock.coLock();
    if (auto it = conns.find(connid); it != conns.end()) {
      return it->second->conn.lock();
    }
    return nullptr;
  }

  void pub_conn_start(uint64_t connid) { 
    auto guard = lock.coLock();
    for(auto& sub:sub_ctxs) {
      if (!sub.second.func_on_start.empty()) {
        app->ctx_call<uint64_t>(
          sub.first, sub.second.func_on_start, connid
        ).start([](auto&&tr){});
      }
    }
  }

  void pub_conn_read(uint64_t connid, std::string_view msg) { 
    auto guard = lock.coLock();
    for(auto& sub:sub_ctxs) {
      if (!sub.second.func_on_read.empty()) {
        app->ctx_call<uint64_t, std::string_view>(
          sub.first, sub.second.func_on_read, connid, msg
        ).start([](auto&&tr){});
      }
    }
  }

  void pub_conn_stop(uint64_t connid) {
    auto guard = lock.coLock();
    for(auto& sub:sub_ctxs) {
      if (!sub.second.func_on_stop.empty()) {
        app->ctx_call<uint64_t>(
          sub.first, sub.second.func_on_stop, connid
        ).start([](auto&&tr){});
      }
    }
  }

  cqy::Lazy<void> co_conn_read(uint64_t connid,
                               cinatra::coro_http_request &req) {
    using namespace cinatra;
    auto conn = req.get_conn();
    websocket_result result{};
    while (true) {
      result = co_await conn->read_websocket();
      if (result.ec) {
        break;
      }

      if (result.type == ws_frame_type::WS_CLOSE_FRAME) {
        break;
      }

      if (result.type == ws_frame_type::WS_TEXT_FRAME ||
          result.type == ws_frame_type::WS_BINARY_FRAME) {
        pub_conn_read(connid,result.data);
      } else if (result.type == ws_frame_type::WS_PING_FRAME ||
                 result.type == ws_frame_type::WS_PONG_FRAME) {
        continue;
      } else {
        break;
      }
    }
  }

  cqy::Lazy<void> co_conn_write(uint64_t connid, std::string data) {
    try {
      auto wscon = get_ws_con(connid);
      if (!wscon) {
        co_return;
      }
      co_await async_simple::coro::dispatch(wscon->get_executor());
      co_await wscon->write_websocket(data, cinatra::opcode::binary);
    } catch (std::exception &e) {
    }
  }

  cqy::Lazy<void> co_ws_start(async_simple::Future<std::error_code> f) {
    auto ec = co_await std::move(f);
    if (ec) {
      CQY_ERROR("ws_server start error:{}", ec.message());
    }
    co_return;
  }
};

struct gate_t : public cqy::cqy_ctx_t {
  uint64_t conn_alloc = 0;

  virtual bool on_init(std::string_view param) {
    register_name("gate");
    register_rpc_func<&gate_t::rpc_get_allocid>("get_allocid");
    register_rpc_func<&gate_t::rpc_on_conn_start>("on_conn_start");
    register_rpc_func<&gate_t::rpc_on_msg>("on_msg");
    register_rpc_func<&gate_t::rpc_on_conn_stop>("on_conn_stop");
    app->create_ctx("ws_server", param);

    app->ctx_call_name<uint32_t, std::string>(
      ".ws_server",
      "set_allocer",
      id,"get_allocid").start([](auto&&){});
    app->ctx_call_name<uint32_t, std::string, std::string, std::string>(
      ".ws_server",
      "sub",
      id, "on_conn_start", "on_msg", "on_conn_stop"
    ).start([](auto&&tr){});

    return true;
  }

  cqy::Lazy<uint64_t> rpc_get_allocid() { co_return ++conn_alloc; }

  cqy::Lazy<void> rpc_on_conn_start(uint64_t connid) { 
    CQY_INFO("conn id:{} start", connid);
    co_return; 
  }

  cqy::Lazy<void> rpc_on_msg(uint64_t connid, std::string_view msg) { 
    CQY_INFO("conn id:{} msg:{}", connid, msg);
    co_return; 
  }

  cqy::Lazy<void> rpc_on_conn_stop(uint64_t connid) { 
    CQY_INFO("conn id:{} stop", connid);
    co_return; 
  }
};

int main() {
  cqy::cqy_app app;
  app.reg_ctx<ws_server_t>("ws_server");
  app.reg_ctx<gate_t>("gate");
  app.config.bootstrap = "gate 1,9001,server.crt,server.key,";
  app.config.nodes.push_back(
      {.name = "n1", .ip = "127.0.0.1", .nodeid = 1, .port = 8888});
  app.start();
  app.stop();
  return 0;
}