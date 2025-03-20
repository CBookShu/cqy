#include "cqy.h"
#include "ylt/easylog.hpp"
#include <cassert>
#include <format>
#include <string_view>

namespace cqy {
  struct node_pong : public cqy_ctx_t {
    std::string echo;
    bool doing = false;

    Lazy<size_t> rpc_pong(std::string_view s)  {
        co_return s.size();
    }

    Lazy<void> rpc_pong1(std::string_view s)  {
        co_return;
    }

    virtual bool on_init(std::string_view param) override {
      ELOG_INFO << "param: " << param;
      app->ctx_mgr.register_name("pong", id.id);

      echo = param;

      register_rpc_func<&node_pong::rpc_pong>();
      return true;
    }

    virtual Lazy<void> on_msg(cqy_msg_t* msg) override {
      assert(ex->currentThreadInExecutor());
      if (doing) {
        ELOG_ERROR << "parallel call";
      }
      doing = true;
      co_await coro_io::sleep_for(std::chrono::milliseconds(10));
      cqy_handle_t from = msg->from;
      auto buffer = msg->buffer();
      ELOG_INFO << std::format("from {:0x} msg:{}", from.id, msg->buffer());
      respone(msg, echo);
      doing = false;
      co_return;
    }
  };
}

int main() {
  using namespace std;
  easylog::set_min_severity(easylog::Severity::INFO);
  cqy::cqy_app app;
  app.reg_ctx<cqy::node_pong>("pong");
  app.load_config("config1.json");
  app.start();
  return 0;
}