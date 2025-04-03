#include "cqy_node.h"
#include "cqy_app.h"
#include "cqy_logger.h"
#include "cqy_utils.h"
#include <cstdint>
#include <memory>
#include <vector>

using namespace cqy;

struct cqy_node::cqy_node_t {
  uint8_t self_nodeid = 0;
  sptr<rpc_server> server;
  std::vector<sptr<node_t>> nodes;
};

cqy_node::cqy_node() { s_ = new cqy_node::cqy_node_t{}; }
cqy_node::~cqy_node() {
  if (s_) {
    delete s_;
  }
}

uint8_t cqy_node::self_nodeid() { return s_->self_nodeid; }
bool cqy_node::check_self(uint8_t nodeid) {
  return s_->self_nodeid == nodeid;
}

sptr<cqy_node::node_t> cqy_node::get_node(uint8_t nodeid) {
  for (auto &n : s_->nodes) {
    if (n->info.nodeid == nodeid) {
      return n;
    }
  }
  return nullptr;
}

sptr<cqy_node::node_t> cqy_node::get_node(std::string_view name) {
  if(name.empty()) {
    return get_node(s_->self_nodeid);
  }
  for (auto &n : s_->nodes) {
    if (n->info.name == name) {
      return n;
    }
  }
  return nullptr;
}

void cqy_node::create_client(node_info &info) {
  sptr<node_t> node = std::make_shared<node_t>();
  node->info = info;
  node->rpc_client = rpc_client_pool::create(
      std::format("{}:{}", info.ip, info.port),
      {.max_connection = std::thread::hardware_concurrency()});
  auto id = info.nodeid;
  s_->nodes.push_back(node);

  node_mq_spawn(id).start([id,s = node->rpc_client](Try<void> tr) {
    if (tr.hasError()) {
      try {
        throw tr.getException();
      } catch (std::exception &e) {
        CQY_ERROR("node:{} connect exception:{}", id, e.what());
      }
    } else {
      CQY_INFO("node:{} connect stop", id);
    }
  });
}

cqy_node::rpc_server& cqy_node::create_rpc_server(uint32_t thread, node_info& info) {
  s_->server = std::make_unique<rpc_server>(
    thread, info.port
  );
  s_->self_nodeid = info.nodeid;

  sptr<node_t> node = std::make_shared<node_t>();
  node->info = info;
  s_->nodes.push_back(node);
  return *s_->server;
}

async_simple::Future<coro_rpc::err_code> cqy_node::rpc_server_start() {
  return s_->server->async_start();
}

void cqy_node::rpc_server_close() {
  if(s_->server) {
    s_->server->stop();
  }
}

void cqy_node::shutdown() {
  for (auto &n : s_->nodes) {
    n->coro_queue.shutdown();
    if (n->rpc_client) {
      n->rpc_client.reset();
    }
  }
}

Lazy<void> cqy_node::node_mq_spawn(uint8_t id) {
  using namespace std::chrono_literals;

  auto n = get_node(id);
  std::deque<cqy_str> msgs;

  size_t warn_size = 1024;
  while (!n->coro_queue.stop) {
    auto cur_size = co_await n->coro_queue.size();
    if (cur_size >= warn_size) {
      warn_size = warn_size * 2;
      CQY_WARN("node:{} queue size:{}", id, cur_size);
    } else {
      warn_size = std::clamp<size_t>(warn_size / 2, 1024,
                                     std::numeric_limits<size_t>::max());
    }

    if (msgs.empty()) {
      msgs = co_await n->coro_queue.pop_all();
    }

    if (msgs.empty()) {
      break;
    }

    auto r = co_await n->rpc_client->send_request(
        [&](coro_rpc::coro_rpc_client &client)
            -> Lazy<coro_rpc::rpc_result<void>> {
          co_return co_await client.call<&cqy_app::rpc_on_mq>(msgs);
        });
    if (!r) {
      // conn error
      co_await coro_io::sleep_for(2s);
      continue;
    }
    if (!r.value()) {
      // remote call error
      co_await coro_io::sleep_for(2s);
      continue;
    }
    msgs.clear();
  }
  co_return;
}