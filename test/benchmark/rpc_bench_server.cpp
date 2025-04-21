#include "cqy_logger.h"
#include "cqy_utils.h"
#include <atomic>
#include <cqy.h>
#include <string_view>
#include <ylt/thirdparty/async_simple/coro/Latch.h>
#include "rpc_bench_def.h"

using namespace cqy;

struct bench_server : public cqy_ctx {
  std::atomic_uint32_t count{0};

  virtual bool on_init(std::string_view param) override {
    CQY_WARN("bench_server muli thread");
    register_name("bench_server");
    register_rpc_func<&bench_server::rpc_test, true>("rpc_test");
    return true;
  }
  Lazy<param_t> rpc_test(param_t&& p) {
    auto n = count.fetch_add(1, std::memory_order_relaxed);
    CQY_WARN("rpc_test: {} {}, count:{}", p.s, p.a, n + 1);
    co_return p;
  }
};

// 单线程rpc
struct bench_server_single : public cqy_ctx {
  std::atomic_uint32_t count{0};

  virtual bool on_init(std::string_view param) override {
    CQY_WARN("bench_server signle thread");
    register_name("bench_server");
    register_rpc_func<&bench_server::rpc_test>("rpc_test");
    return true;
  }
  Lazy<param_t> rpc_test(param_t&& p) {
    auto n = count.fetch_add(1, std::memory_order_relaxed);
    CQY_WARN("rpc_test: {} {}, count:{}", p.s, p.a, n + 1);
    co_return p;
  }
};

int main(int argc, char** argv) {
  easylog::set_min_severity(easylog::Severity::WARN);
  // easylog::init_log<1>(easylog::Severity::TRACE, "bench_server.log", false, true, 0, 0, true);

  cqy_app app;
  app.reg_ctx<bench_server>("bench_server");
  app.reg_ctx<bench_server_single>("bench_server_single");
  auto& config = app.get_config();
  config.nodeid = 2;
  config.nodes.push_back({
    .name = "n1", .ip = "127.0.0.1", .nodeid = 1,  .port = 8888}
  );
  config.nodes.push_back({
    .name = "n2", .ip = "127.0.0.1", .nodeid = 2,  .port = 8889}
  );

  config.bootstrap = "bench_server";
  if (argc > 1) {
    std::string_view arg = argv[1];
    if (arg == "single") {
      config.bootstrap = "bench_server_single";
    }
  }
  app.start();
  return 0;
}