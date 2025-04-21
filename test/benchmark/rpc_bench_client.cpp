#include "async_simple/coro/Lazy.h"
#include "ylt/coro_io/io_context_pool.hpp"
#include "ylt/easylog/record.hpp"
#include <exception>
#include "cqy_logger.h"
#include "cqy_utils.h"
#include "ylt/coro_io/coro_io.hpp"
#include <atomic>
#include <cqy.h>
#include <ylt/thirdparty/async_simple/coro/Latch.h>
#include "rpc_bench_def.h"

using namespace cqy;

constexpr unsigned thread_cnt = 1920;
constexpr auto request_cnt = 100000;
std::atomic<uint64_t> working_echo = 0;
std::atomic<int32_t> qps = 0;
std::vector<std::chrono::microseconds> result;

struct bench_client : public cqy_ctx {
  std::atomic_uint32_t call_count{0};
  virtual bool on_init(std::string_view param) override {
    async_call(test());
    return true;
  }
  Lazy<void> test_rpc() {
    auto now = std::chrono::high_resolution_clock::now();
    for(auto _: std::ranges::views::iota(0, 10000)) {
      auto r = co_await ctx_call_name<param_t>("n2.bench_server", "rpc_test", param_t{"hello", 1});
      auto p = r.as<param_t>();
      assert(p.s == "hello");
      assert(p.a == 1);
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

  Lazy<std::vector<std::chrono::microseconds>> send() {
    std::vector<std::chrono::microseconds> latencys;
    latencys.reserve(request_cnt);
    auto tp = std::chrono::steady_clock::now();
    ++working_echo;
    int id = 0;
    for (int i = 0; i < request_cnt; ++i) {
      auto result = co_await ctx_call_name_nolock<param_t>("n2.bench_server", "rpc_test", param_t{"hello", 1});
      if (result.has_error()) {
        ELOG_ERROR << "get client form client pool failed: \n"
                   << result.status;
        continue;
      }
      qps.fetch_add(1, std::memory_order::release);
      auto old_tp = tp;
      tp = std::chrono::steady_clock::now();
      latencys.push_back(
          std::chrono::duration_cast<std::chrono::microseconds>(tp - old_tp));
    }
    co_return std::move(latencys);
  }
  

  Lazy<void> qps_watcher() {
    using namespace std::chrono_literals;
    auto n = get_app()->get_node().get_node("n2");
    do {
      co_await coro_io::sleep_for(1s);
      uint64_t cnt = qps.exchange(0);
      std::cout << "QPS:" << cnt
                << " free connection: " << n->rpc_client->free_client_count()
                << " working echo:" << working_echo << std::endl;
    } while (working_echo > 0);
  }

  void latency_watcher() {
    std::sort(result.begin(), result.end());
    auto arr = {0.1, 0.3, 0.5, 0.7, 0.9, 0.95, 0.99, 0.999, 0.9999, 0.99999, 1.0};
    for (auto e : arr) {
      std::cout
          << (e * 100) << "% request finished in:"
          << result[std::max<std::size_t>(0, result.size() * e - 1)].count() /
                 1000.0
          << "ms" << std::endl;
    }
  }

  Lazy<void> test_rpc2() {
    auto executor = coro_io::get_global_block_executor();
    for (int i = 0, lim = thread_cnt; i < lim; ++i) {
      send()
      .via(coro_io::get_global_executor())
      .start([executor](auto&& res){
        executor->schedule([res = std::move(res.value())]() mutable {
          result.insert(result.end(), res.begin(), res.end());
          --working_echo;
        });
      });
    }
    co_await qps_watcher();
    latency_watcher();
    co_return;
  }

  Lazy<void> test() {
    co_await test_rpc();
    co_await test_rpc1();
    co_await test_rpc2();
    
    get_app()->stop();
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
