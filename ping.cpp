#include "cqy.h"
#include "iguana/json_reader.hpp"
#include "ylt/easylog.hpp"
#include "ylt/struct_pack.hpp"
#include <cassert>
#include <filesystem>
#include <format>
#include <thread>

namespace cqy {
  struct node_ping : public cqy_ctx_t {
    virtual bool on_init(std::string_view param) override {
      ELOG_INFO << "param " << param;
      app->ctx_mgr.register_name("ping", id);

      loop_send()
          .via(this->ex)
          .detach();
      return true;
    }

    virtual Lazy<void> on_msg(cqy_msg_t* msg) override {
      cqy_handle_t from = msg->from;
      ELOG_INFO << std::format("from {:0x} msg:{}", from.id, msg->buffer());

      co_return;
    }

    Lazy<void> loop_send() {
        using namespace std::chrono_literals;
        //for(;;) {
            co_await async_simple::coro::sleep(1s);
            //dispatch(1, "pong", 1, std::string("timer"));
            //dispatch(1, "pong", 1, std::string("timer"));
        //}

            auto result = co_await app->ctx_call_name<std::string_view>("n1.pong", "cqy::node_pong::rpc_pong", "hello world");
            auto len = result.as<size_t>();
            ELOG_INFO << std::format("pong::rpc_pong res:{}", len);
    }
  };
}

int main() {
    easylog::set_min_severity(easylog::Severity::INFO);
  using namespace std;
  cqy::cqy_app app;
  app.load_config("config2.json");
  app.reg_ctx<cqy::node_ping>("ping");
  app.start();
  return 0;
}