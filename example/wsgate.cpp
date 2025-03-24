#include "msgpack/v3/object_fwd_decl.hpp"
#include "ylt/coro_http/coro_http_server.hpp"
#include "ylt/coro_io/coro_io.hpp"
#include "ylt/coro_io/io_context_pool.hpp"
#include "async_simple/coro/SyncAwait.h"
#include "cinatra/coro_http_connection.hpp"
#include "cinatra/define.h"
#include "cqy_ctx.h"
#include "cqy_app.h"
#include "cqy_logger.h"
#include "cqy_msg.h"
#include "ylt/struct_pack.hpp"
#include "ylt/thirdparty/async_simple/coro/Dispatch.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "msg_define.h"


struct ws_server_t : public cqy::cqy_ctx {
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
      co_ws_start(std::move(start_f)).via(get_coro_exe()).detach();
      CQY_INFO("ws_server init success:{}", param);
    } catch (const std::exception &e) {
      CQY_ERROR("ws_server init err:{} param:{}", e.what(), param);
      return false;
    }
    return true;
  }

  virtual cqy::Lazy<void> on_msg(cqy::cqy_msg_t *msg) override { 
    try {
      if (msg->type == 1) {
        // write
        auto [connid, str] = unpack<uint64_t, std::string>(msg->buffer());
        co_conn_write(connid, std::move(str)).start([](auto &&tr) {});
      } else if(msg->type == 2) {
        // close
        auto connid = unpack<uint64_t>(msg->buffer());
        auto conn = get_ws_con(connid);
        if (conn) {
          conn->close();
        }
      }
    } catch(...) {

    }
    co_return; 
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
      auto r = co_await get_app()->ctx_call<>(to, func);
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
        get_app()->ctx_call<uint64_t>(
          sub.first, sub.second.func_on_start, connid
        ).start([](auto&&tr){});
      }
    }
  }

  void pub_conn_read(uint64_t connid, std::string_view msg) { 
    auto guard = lock.coLock();
    for(auto& sub:sub_ctxs) {
      if (!sub.second.func_on_read.empty()) {
        get_app()->ctx_call<uint64_t, std::string_view>(
          sub.first, sub.second.func_on_read, connid, msg
        ).start([](auto&&tr){});
      }
    }
  }

  void pub_conn_stop(uint64_t connid) {
    auto guard = lock.coLock();
    for(auto& sub:sub_ctxs) {
      if (!sub.second.func_on_stop.empty()) {
        get_app()->ctx_call<uint64_t>(
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

struct gate_t : public cqy::cqy_ctx {
  std::atomic_int64_t conn_alloc = 0;

  struct player {
    bool login = false;
    uint64_t connid;
  };
  std::unordered_map<uint64_t, player> players;


  virtual bool on_init(std::string_view param) {
    register_name("gate");
    register_rpc_func<&gate_t::rpc_get_allocid, true>("get_allocid");
    register_rpc_func<&gate_t::rpc_on_conn_start>("on_conn_start");
    register_rpc_func<&gate_t::rpc_on_msg>("on_msg");
    register_rpc_func<&gate_t::rpc_on_conn_stop>("on_conn_stop");
    get_app()->create_ctx("ws_server", param);
    get_app()->create_ctx("login", "");

    get_app()->ctx_call_name<uint32_t, std::string>(
      ".ws_server",
      "set_allocer",
      getid(),"get_allocid").start([](auto&&){});
    get_app()->ctx_call_name<uint32_t, std::string, std::string, std::string>(
      ".ws_server",
      "sub",
      getid(), "on_conn_start", "on_msg", "on_conn_stop"
    ).start([](auto&&tr){});

    return true;
  }

  cqy::Lazy<uint64_t> rpc_get_allocid() { co_return ++conn_alloc; }

  void write(uint64_t connid, const std::string& msg) {
    std::string data;
    struct_pack::serialize_to(data, connid, msg);
    dispatch(".ws_server", 1, data);
  }

  cqy::Lazy<void> rpc_on_conn_start(uint64_t connid) { 
    CQY_INFO("conn id:{} start", connid);
    players[connid] = {
      .login = false,
      .connid = connid
    };
    co_return; 
  }

  cqy::Lazy<void> rpc_on_msg(uint64_t connid, std::string_view msg) { 
    CQY_INFO("conn id:{} msg:{}", connid, msg);
    bool ok = false;
    try {
      msg_code_t codec;
      codec.unpack(msg);
      auto head = codec.head();
      if (head.id == game_def::Login) {
        auto& p = players.at(connid);
        if (p.login) {
          // already login
          game_def::MsgLoginResponce rsp;
          rsp.head.id = game_def::MsgId::Login;
          rsp.result = game_def::MsgLoginResponce::Busy;
          dispatch_pack(".ws_server", 1, connid, msg_code_t::pack(rsp));
          ok = true;
        } else {
          auto r = co_await get_app()->ctx_call_name<uint64_t,std::string_view>(
            ".login", "player_login", connid, msg
          );
          auto rsp = r.as<game_def::MsgLoginResponce>();
          if (rsp.result == game_def::MsgLoginResponce::Ok) {
            p.login = true;
            ok = true;
          }else {
            ok = false;
          }
          dispatch_pack(".ws_server", 1, connid, msg_code_t::pack(rsp));
        }
      } else {
        dispatch(".game", 0, msg);
        ok = true;
      }
    } catch(std::exception& e) {
      
    }
    if(!ok) {
      // close this player
      dispatch_pack(".ws_server", 2, connid);
    }
    co_return; 
  }

  cqy::Lazy<void> rpc_on_conn_stop(uint64_t connid) { 
    CQY_INFO("conn id:{} stop", connid);
    auto& p = players[connid];
    dispatch_pack(".login", 2, connid);
    dispatch_pack(".game", 2, connid);
    co_return; 
  }
};

struct ctx_login : public cqy::cqy_ctx {
  struct login_t {
    std::string name;
    uint64_t connid;
  };
  std::unordered_map<uint64_t, login_t> logins;

  virtual bool on_init(std::string_view param) { 
    register_name("login");
    register_rpc_func<&ctx_login::rpc_player_login>("player_login");
    return true; 
  }
  virtual cqy::Lazy<void> on_msg(cqy::cqy_msg_t *msg) { co_return; }
  virtual void on_stop() {}

  cqy::Lazy<game_def::MsgLoginResponce> rpc_player_login(
    uint64_t connid, std::string_view msg
  ) {
    game_def::MsgLoginResponce rsp{};
    rsp.head.id = game_def::Login;
    rsp.result = game_def::MsgLoginResponce::Ok;
    try {
      msg_code_t codec;
      codec.unpack(msg);
      auto login = codec.as<game_def::MsgLogin>();
      logins[connid] = {
        .name = std::move(login.name),
        .connid = connid
      };
      co_return rsp;
    } catch(std::exception& e) {
      rsp.result = game_def::MsgLoginResponce::PwdErr;
      rsp.tip = std::format("login error:{}", e.what());
    }
    co_return rsp;
  }
};

int main() {
  cqy::cqy_app app;
  app.reg_ctx<ws_server_t>("ws_server");
  app.reg_ctx<gate_t>("gate");
  app.reg_ctx<ctx_login>("login");
  app.load_config("config_game.json");
  auto& config = app.get_config();
  config.bootstrap = "gate 8,12349,server.crt,server.key,";
  app.start();
  app.stop();
  return 0;
}