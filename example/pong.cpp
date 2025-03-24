#include "cqy.h"
#include "ylt/coro_io/coro_io.hpp"
#include "ylt/easylog.hpp"
#include <cassert>
#include <chrono>
#include <format>
#include <string_view>

namespace cqy {
  struct node_pong : public cqy_ctx {
    std::string echo;

    Lazy<size_t> rpc_pong(std::string_view s)  {
        co_return s.size();
    }

    Lazy<void> rpc_pong1(std::string_view s)  {
        co_return;
    }

    virtual bool on_init(std::string_view param) override {
      CQY_INFO("param:{}", param);
      register_name("pong");

      echo = param;

      register_rpc_func<&node_pong::rpc_pong>("rpc_pong");
      register_rpc_func<&node_pong::rpc_pong1>("rpc_pong1");
      return true;
    }

    virtual Lazy<void> on_msg(cqy_msg_t* msg) override {
      assert(get_coro_exe()->currentThreadInExecutor());
      CQY_INFO("from {:0x} msg:{}", msg->from, msg->buffer());
      response(msg, echo);
      co_await coro_io::sleep_for( std::chrono::seconds(1));
      get_app()->stop();
      co_return;
    }
  };
}

int main() {
  using namespace std;
  easylog::set_min_severity(easylog::Severity::WARN);
  cqy::cqy_app app;
  app.reg_ctx<cqy::node_pong>("pong");
  app.load_config("config1.json");
  app.start();
  app.stop();
  return 0;
}