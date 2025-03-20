#include "ylt/coro_io/coro_io.hpp"
#include <optional>
#include <stop_token>
#include <thread>
#include <type_traits>
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>
#include "cqy.h"

int main(int argc, char** argv) { 
  return doctest::Context(argc, argv).run(); 
}

TEST_CASE("cqy_handle_t") {
  using namespace cqy;
  cqy_handle_t h(0x12345678);
  CHECK(h.nodeid == 0x12);
  CHECK(h.ctxid() == 0x345678);
  h = 0x12345679;
  CHECK(h.nodeid == 0x12);
  CHECK(h.ctxid() == 0x345679);

  cqy_handle_t h1;
  h1.nodeid = 1;
  h1.set_ctxid(1);
  CHECK(std::format("0x{:08x}", h1.id) == "0x01000001");
  CHECK(h1.nodeid == 1);
  CHECK(h1.ctxid() == 1);
}

TEST_CASE("algo::split") {
  using namespace cqy;
  auto vs = algo::split("hello world !", " ");
  CHECK(vs.size() == 3);
  CHECK(vs[0] == "hello");
  CHECK(vs[1] == "world");
  CHECK(vs[2] == "!");

  vs = algo::split("hello world ! ", " ");
  CHECK(vs.size() == 3);
  CHECK(vs[0] == "hello");
  CHECK(vs[1] == "world");
  CHECK(vs[2] == "!");
}

TEST_CASE("algo:split_one") {
  using namespace cqy;
  auto [a, b] = algo::split_one("hello world !", " ");
  CHECK(a == "hello");
  CHECK(b == "world !");

  std::string_view sv = "hello";
  auto [a1, b1] = algo::split_one(sv, " ");
  CHECK(a1 == "hello");
  CHECK(b1 == "");
}

TEST_CASE("algo::to_n") {
  using namespace cqy;
  CHECK(algo::to_n<int>("123") == std::make_optional(123));
  CHECK(algo::to_n<int>("123a") == std::make_optional(123));
  CHECK(algo::to_n<int>("1a123") == std::make_optional(1));
  CHECK(algo::to_n<int>("a123") == std::nullopt);
}

TEST_CASE("algo::to_underlying") {
  using namespace cqy;
  enum class E { A = 1, B = 2, C = 3 };
  CHECK(algo::to_underlying(E::A) == 1);
  CHECK(algo::to_underlying(E::B) == 2);
  CHECK(algo::to_underlying(E::C) == 3);
}

TEST_CASE("algo::deleter") {
  using namespace cqy;
  int i = 1;
  struct T {
    int &h;
    T(int &i):h(i) {}
    ~T() { h = 0; }
  };
  T* p = new T(i);
  algo::deleter(p);
  CHECK(i == 0);
}

TEST_CASE("algo::random_bernoulli") {
  using namespace cqy;
  int n = 0;
  for (int i = 0; i < 1000; i++) {
    if (algo::random_bernoulli(50)) {
      n++;
    }
  }
  CHECK(n > 400);
  CHECK(n < 600);

  n = 0;
  for (int i = 0; i < 1000; i++) {
    if (algo::random_bernoulli(0)) {
      n++;
    }
  }
  CHECK(n == 0);

  n = 0;
  for (int i = 0; i < 1000; i++) {
    if (algo::random_bernoulli(100)) {
      n++;
    }
  }
  CHECK(n == 1000);
}

TEST_CASE("cqy_ctx_mgr_t") {
  using namespace cqy;
  cqy_ctx_mgr_t mgr;
  sptr<cqy_ctx_t> p(new cqy_ctx_t{}, &algo::deleter<cqy_ctx_t>);
  p->id = cqy_handle_t(mgr.new_id());
  mgr.add_ctx(p);
  CHECK(mgr.find_name("ping") == 0);
  mgr.register_name("ping", p->id);
  CHECK(mgr.find_name("ping") == p->id.id);

  sptr<cqy_ctx_t> p1(new cqy_ctx_t{}, &algo::deleter<cqy_ctx_t>);
  p1->id = cqy_handle_t(mgr.new_id());
  mgr.add_ctx(p1);
  mgr.register_name("pong", p1->id);
  CHECK(mgr.find_name("pong") == p1->id.id);

  try  {
    mgr.register_name("ping", p->id);
  } catch (std::exception& e) {
    CHECK(std::string(e.what()) == "name already exists");
  } 
  CHECK(mgr.find_name("ping") == p->id.id);
  mgr.del_ctx(p->id);
  CHECK(mgr.find_name("ping") == 0);
}

TEST_CASE("config:load_config") {
  using namespace cqy;
  cqy_app app;
  app.load_config("config_test.json");
  auto& config = app.config;
  CHECK(config.thread == std::thread::hardware_concurrency());
  CHECK(config.nodeid == 1);
  CHECK(config.bootstrap == "pong world");

  CHECK(config.nodes.size() == 2);
  CHECK(config.nodes[0].nodeid == 1);
  CHECK(config.nodes[0].name == "n1");
  CHECK(config.nodes[0].ip == "127.0.0.1");
  CHECK(config.nodes[0].port == 8888);

  CHECK(config.nodes[1].nodeid == 2);
  CHECK(config.nodes[1].name == "n2");
  CHECK(config.nodes[1].ip == "127.0.0.1");
  CHECK(config.nodes[1].port == 8889);

  auto info = app.get_nodeinfo(1);
  CHECK(info->nodeid == 1);
  CHECK(info->name == "n1");
  CHECK(info->ip == "127.0.0.1");
  CHECK(info->port == 8888);

  auto info1 = app.get_nodeinfo("n1");
  CHECK(info1 == info);

  info = app.get_nodeinfo("n2");
  CHECK(info->nodeid == 2);
  CHECK(info->name == "n2");
  CHECK(info->ip == "127.0.0.1");
  CHECK(info->port == 8889);
}

TEST_CASE("app::start stop") {
  using namespace cqy;
  using namespace std;
  cqy_app app;
  std::jthread t1([&app] {
    app.config.nodeid = 1;
    app.config.nodes.push_back({
      .name = "n1", .ip = "127.0.0.1", .nodeid = 1,  .port = 8888}
    );
    app.config.nodes.push_back({
      .name = "n2", .ip = "127.0.0.1", .nodeid = 2,  .port = 8888}
    );
    app.start();
  });
  std::jthread t2([&app] {
    std::this_thread::sleep_for(2s);
    app.close_server();
  });
  t1.join();
  app.stop();
}

TEST_CASE("app:reg ctx") {
  using namespace cqy;
  using namespace std;

  cqy_app app;
  struct ctx_test : public cqy_ctx_t {
    virtual bool on_init(std::string_view param) override {
      CHECK(param == "hello");
      delay_stop().via(ex).detach();
      return true;
    }
    Lazy<void> delay_stop() {
      co_await coro_io::sleep_for(2s);
      app->close_server();
    } 
  };
  app.reg_ctx<ctx_test>("ctx_test");
  app.config.nodeid = 1;
  app.config.nodes.push_back({
    .name = "n1", .ip = "127.0.0.1", .nodeid = 1,  .port = 8888}
  );
  app.config.bootstrap = "ctx_test hello";
  app.start();
  app.stop();
}

TEST_CASE("app:thread") {
  using namespace cqy;
  using namespace std;
  cqy_app app1;
  std::jthread t([&app1] {
    std::this_thread::sleep_for(1s);
    app1.close_server();
  });
  app1.config.thread = 3;
  app1.config.nodeid = 1;
  app1.config.nodes.push_back({
    .name = "n1", .ip = "127.0.0.1", .nodeid = 1,  .port = 8888}
  );
  app1.config.nodes.push_back({
    .name = "n2", .ip = "127.0.0.1", .nodeid = 2,  .port = 8889}
  );
  app1.config.bootstrap = "ctx_test1 hello";
  app1.start();
  app1.stop();
}

// dispatch will retry to send message if the node is not ready
// so if you want msg not drop when the node is not ready, you can use dispatch
TEST_CASE("ctx:dispatch") {
  using namespace cqy;
  using namespace std;
  cqy_app app1;
  cqy_app app2;
  struct ctx_test1 : public cqy_ctx_t {
    uint32_t send_id = 0;
    virtual bool on_init(std::string_view param) override {
      send_id = dispatch("n2.ctx_test2", 0, std::string(param));
      return true;
    }
    virtual Lazy<void> on_msg(cqy_msg_t* msg) override {
      CHECK(msg->buffer() == "world");
      CHECK(msg->type == 1);    // response
      CHECK(msg->session == send_id);
      app->close_server();
      co_return;
    }
  };
  app1.reg_ctx<ctx_test1>("ctx_test1");
  std::jthread t1([&app1,&app2] {
    app1.config.thread = 3;
    app1.config.nodeid = 1;
    app1.config.nodes.push_back({
      .name = "n1", .ip = "127.0.0.1", .nodeid = 1,  .port = 8888}
    );
    app1.config.nodes.push_back({
      .name = "n2", .ip = "127.0.0.1", .nodeid = 2,  .port = 8889}
    );
    app1.config.bootstrap = "ctx_test1 hello";
    app1.start();
    app2.close_server();
  });

  struct ctx_test2 : public cqy_ctx_t {
    virtual bool on_init(std::string_view param) override {
      register_name("ctx_test2");
      return true;
    }
    virtual Lazy<void> on_msg(cqy_msg_t* msg) override {
      CHECK(msg->buffer() == "hello");
      CHECK(msg->type == 0);
      respone(msg, "world");
      co_return;
    }
  };
  app2.reg_ctx<ctx_test2>("ctx_test2");
  std::jthread t2([&app2] {
    app2.config.thread = 3;
    app2.config.nodeid = 2;
    app2.config.nodes.push_back({
      .name = "n1", .ip = "127.0.0.1", .nodeid = 1,  .port = 8888}
    );
    app2.config.nodes.push_back({
      .name = "n2", .ip = "127.0.0.1", .nodeid = 2,  .port = 8889}
    );
    app2.config.bootstrap = "ctx_test2";
    app2.start();
  });

  t1.join();
  t2.join();

  app2.stop();
  app1.stop();
}

// if the node is not ready, the message may not be sent
TEST_CASE("ctx:rpc") {
  using namespace cqy;
  using namespace std;
  cqy_app app1;
  cqy_app app2;
  struct ctx_test1 : public cqy_ctx_t {
    uint32_t send_id = 0;
    virtual bool on_init(std::string_view param) override {
      test_rpc().via(ex).detach();
      return true;
    }
    Lazy<void> test_rpc() {
      auto r = co_await app->ctx_call_name<int, std::string>("n2.ctx_test2", "get_rpc_test_21", 1, "hello");
      CHECK(r.as<int>() == 6);
      r = co_await app->ctx_call_name<int, std::string>("n2.ctx_test2", "get_rpc_test_20", 1, "hello");
      CHECK(!r.has_error());
      r = co_await app->ctx_call_name<int>("n2.ctx_test2", "get_rpc_test_11", 1);
      CHECK(r.as<int>() == 1);
      r = co_await app->ctx_call_name<int>("n2.ctx_test2", "get_rpc_test_10", 1);
      CHECK(!r.has_error());
      app->close_server();
      co_return;
    }
  };
  app1.reg_ctx<ctx_test1>("ctx_test1");
  std::jthread t1([&app1,&app2] {
    app1.config.thread = 3;
    app1.config.nodeid = 1;
    app1.config.nodes.push_back({
      .name = "n1", .ip = "127.0.0.1", .nodeid = 1,  .port = 8888}
    );
    app1.config.nodes.push_back({
      .name = "n2", .ip = "127.0.0.1", .nodeid = 2,  .port = 8889}
    );
    app1.config.bootstrap = "ctx_test1 hello";
    app1.start();
    app2.close_server();
  });

  struct ctx_test2 : public cqy_ctx_t {
    virtual bool on_init(std::string_view param) override {
      register_name("ctx_test2");
      register_rpc_func<&ctx_test2::get_rpc_test_21>("get_rpc_test_21");
      register_rpc_func<&ctx_test2::get_rpc_test_20>("get_rpc_test_20");
      register_rpc_func<&ctx_test2::get_rpc_test_11>("get_rpc_test_11");
      register_rpc_func<&ctx_test2::get_rpc_test_10>("get_rpc_test_10");
      return true;
    }
    // 2 params; 1 return
    Lazy<int> get_rpc_test_21(int a, std::string s) {
      co_return a + s.size();
    }
    // 2 params; 0 return
    Lazy<void> get_rpc_test_20(int a, std::string s) {
      CHECK(a == 1);
      CHECK(s == "hello");
      co_return;
    }
    // 1 params; 1 return
    Lazy<int> get_rpc_test_11(int a) {
      co_return a;
    }
    // 1 params; 0 return
    Lazy<void> get_rpc_test_10(int a) {
      CHECK(a == 1);
      co_return;
    }
  };
  app2.reg_ctx<ctx_test2>("ctx_test2");
  std::jthread t2([&app2] {
    app2.config.thread = 3;
    app2.config.nodeid = 2;
    app2.config.nodes.push_back({
      .name = "n1", .ip = "127.0.0.1", .nodeid = 1,  .port = 8888}
    );
    app2.config.nodes.push_back({
      .name = "n2", .ip = "127.0.0.1", .nodeid = 2,  .port = 8889}
    );
    app2.config.bootstrap = "ctx_test2";
    app2.start();
  });

  t1.join();
  t2.join();

  app2.stop();
  app1.stop();
}