#include "cqy.h"
#include "iguana/json_reader.hpp"
#include "ylt/coro_io/coro_io.hpp"
#include "ylt/easylog.hpp"
#include "ylt/struct_pack.hpp"
#include <cassert>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <thread>

namespace cqy {
struct node_ping : public cqy_ctx_t {
  uint32_t last_session = 0;

  virtual bool on_init(std::string_view param) override {
    CQY_INFO("param:{}", param);
    app->ctx_mgr.register_name("ping", id);

    test().via(this->ex).detach();
    return true;
  }

  virtual Lazy<void> on_msg(cqy_msg_t *msg) override {
    assert(msg->session == last_session);
    assert(msg->response);
    CQY_INFO("from {:0x} msg:{}", msg->from, msg->buffer());
    co_await coro_io::sleep_for(std::chrono::seconds(1));
    app->close_server();
    co_return;
  }

  Lazy<void> test() {
    using namespace std::chrono_literals;

    try {
      /*
      n1.pong:
      n1 -> config.json 中nodeid=1 的name
      pong -> node_pong::on_init 中注册的name,register_name("pong");
      func_name -> 是 node_pong register_rpc_func<&node_pong::rpc_pong>("rpc_pong");
      所以下面是在远程调用 node_pong::rpc_pong 函数，并把结果返回
      */
      auto r = co_await app->ctx_call_name<std::string_view>(
          "n1.pong", "rpc_pong", "hello");
      CQY_INFO("rpc_pong res:{}", r.as<size_t>());

      r = co_await app->ctx_call_name<std::string_view>("n1.pong", "rpc_pong1",
                                                        "hello");
      assert(!r.has_error());

      /*
        dispatch type:0, msg:hello 给 n1的pong
        注意: dispatch 一定会成功，对面不在线，会一遍一遍尝试。
        dispatch 和 rpc 调用不使用同一个通道，dispatch 会一直尝试，直到对面上线。
        dispatch 中 type 和 msg 都是可定制的
        此外，对方如果response的话，返回的话，可以在msg.response 进行判断，并且session跟dispatch返回值一致
      */
      last_session = dispatch("n1.pong", 0, "hello");
    } catch (std::exception &e) {
      CQY_WARN("exception:{}", e.what());
    }
  }
};
} // namespace cqy

int main() {
  easylog::set_min_severity(easylog::Severity::WARN);
  using namespace std;
  cqy::cqy_app app;
  app.load_config("config2.json");
  app.reg_ctx<cqy::node_ping>("ping");
  app.start();
  app.stop();
  return 0;
}