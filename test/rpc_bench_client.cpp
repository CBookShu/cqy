#include "async_simple/coro/Lazy.h"
#include "ylt/coro_io/io_context_pool.hpp"
#include "ylt/easylog/record.hpp"
#include <exception>
#define DOCTEST_CONFIG_IMPLEMENT
#include "cqy_logger.h"
#include "cqy_utils.h"
#include "ylt/coro_io/client_pool.hpp"
#include "ylt/coro_io/coro_io.hpp"
#include <atomic>
#include <cstddef>
#include <set>
#include <doctest.h>
#include <cqy.h>
#include <ylt/thirdparty/async_simple/coro/Latch.h>
#include "rpc_bench_def.h"

using namespace cqy;

struct bench_client : public cqy_ctx {
  std::atomic_uint32_t call_count{0};
  virtual bool on_init(std::string_view param) override {
    // async_call(test_rpc());
    async_call(test_rpc1());
    return true;
  }
  Lazy<void> test_rpc() {
    auto now = std::chrono::high_resolution_clock::now();
    for(auto _: std::ranges::views::iota(0, 10000)) {
      auto r = co_await ctx_call_name<param_t>("n2.bench_server", "rpc_test", param_t{"hello", 1});
      auto p = r.as<param_t>();
      CHECK(p.s == "hello");
      CHECK(p.a == 1);
    }
    auto after = std::chrono::high_resolution_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(after - now).count();
    CQY_WARN("1thread rpc test: {}ms", diff);
    co_return;
  }

  Lazy<void> rpc_call(int idx, async_simple::coro::Latch& latch) {
    using namespace std::chrono_literals;
    for(;;) {
      // CQY_WARN("rpc call{} begin", idx);
      auto r = co_await ctx_call_name_nolock<param_t>("n2.bench_server", "rpc_test", param_t{"hello", 1});
      // CQY_WARN("rpc call{} end error:{}", idx, r.has_error());
      if (r.has_error()) {
        co_await coro_io::sleep_for(0s);
        continue;
      }
      // CQY_WARN("rpc call{} close", idx);
      auto p = r.as<param_t>();
      assert(p.s == "hello");
      assert(p.a == 1);
      break;
    }
    // auto n = call_count.fetch_add(1, std::memory_order_release);
    // CQY_WARN("rpc call{} count_count:{}", idx, n + 1);
    latch.count_down().via(coro_io::get_global_block_executor()).detach();
    co_return;
  }

  Lazy<void> test_rpc1() {
    auto now = std::chrono::high_resolution_clock::now();
    int count = 10000;
    async_simple::coro::Latch latch(count);
    auto f = [](bench_client* self, async_simple::coro::Latch& latch, int idx)
    mutable
    -> Lazy<void>
    {
      co_return co_await self->rpc_call(idx, latch);
    };
    for(auto idx: std::ranges::views::iota(0, count)) {
      f(this, latch, idx)
      .via(coro_io::get_global_block_executor())
      .start([idx](cqy::Try<void>&&tr){
        if (tr.hasError()) {
          try {
            std::rethrow_exception(tr.getException());
          } catch(std::exception& e) {
            CQY_WARN("rpc call{} exception:{}", idx, e.what());
          }
        }
      });
    }
    co_await latch.wait();
    auto after = std::chrono::high_resolution_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(after - now).count();
    CQY_WARN("rpc test: {}ms", diff);
    co_return;
  }
};

int main(int argc, char** argv) { 
  easylog::set_min_severity(easylog::Severity::WARN);
  easylog::set_async(false);
  // easylog::init_log<1>(easylog::Severity::TRACE, "bench_client.log", false, true, 0, 0, true);

  cqy_app app;
  app.reg_ctx<bench_client>("bench_client");
  auto& config = app.get_config();
  config.nodeid = 1;
  config.nodes.push_back({
    .name = "n1", .ip = "127.0.0.1", .nodeid = 1,  .port = 8888}
  );
  config.nodes.push_back({
    .name = "n2", .ip = "127.0.0.1", .nodeid = 2,  .port = 8889}
  );
  config.bootstrap = "bench_client";
  app.start();
  return 0;
}
