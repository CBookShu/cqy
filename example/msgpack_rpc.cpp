#include "async_simple/coro/SyncAwait.h"
#include "cqy.h"
#include "msgpack_rpc_protocol1.hpp"
#include "msgpack_rpc_protocol.hpp"
#include "ylt/coro_rpc/coro_rpc_server.hpp"
#include "ylt/coro_rpc/impl/coro_rpc_client.hpp"
#include <format>
#include <string>
#include <thread>

using namespace coro_rpc;

std::string rpc_get(int a) {
  return std::to_string(a);
}

cqy::Lazy<void> test_client() {
  coro_rpc_client<coro_rpc::protocol::msgpack_rpc_protocol> c;
  co_await c.connect("127.0.0.1:8801");
  auto result = co_await c.call<rpc_get>(100);
  if(result) {
    std::cout << "rpc_get result:" << result.value() << std::endl;
  } else {
    std::cout << result.error().msg << std::endl;
  }
  co_return;
}

int main() {
  coro_rpc_server_base<config::msgpack_rpc_config> server(
    std::thread::hardware_concurrency(), 8801);
  std::thread t([&]{
    server.register_handler<rpc_get>();
    auto ec = server.start();
  });

  async_simple::coro::syncAwait(test_client());
  server.stop();
  return 0;
}