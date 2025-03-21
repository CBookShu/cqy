#include "cqy.h"
#include "async_simple/Try.h"
#include "iguana/json_reader.hpp"
#include "ylt/coro_io/io_context_pool.hpp"
#include "ylt/coro_rpc/impl/errno.h"
#include "ylt/easylog.hpp"
#include <memory>
#include <thread>

using namespace cqy;

void cqy_app::start() {
  auto thread = config.thread;
  create_rpc_server();
  create_client();

  if (!config.bootstrap.empty()) {
    auto p = algo::split_one(config.bootstrap, " ");
    create_ctx(p.first, p.second);
  }
  rpc_server_start();
}

void cqy_app::stop() {
  if(bstop.exchange(true)) {
    return;
  }

  for(auto &n : node.mqs) {
    n->coro_queue.shutdown();
  }
  auto ctxids = ctx_mgr.collect_ctxids();
  for (auto id : ctxids) {
    auto ctx = ctx_mgr.get_ctx(id);
    if (ctx) {
      ctx->msg_queue.shutdown();
    }
  }
  for (auto id : ctxids) {
    auto ctx = ctx_mgr.get_ctx(id);
    if (ctx && ctx->wait_stop) {
      ctx->wait_stop->wait();
    }
  }
  for (auto id : ctxids) {
    auto ctx = ctx_mgr.get_ctx(id);
    if (ctx) {
      ctx->app = nullptr;
      ctx_mgr.del_ctx(id);
    }
  }
}

void cqy_app::close_server() {
  if (node.rpc_server) {
    std::thread thrd{[this] {
      node.rpc_server->stop();
    }};
    thrd.join();
  }
}

void cqy_app::create_rpc_server() {
  auto info = get_nodeinfo();
  node.rpc_server = std::make_shared<cqy_rpc_server>(config.thread, info->port);
  node.rpc_server->register_handler<&cqy_app::rpc_on_mq>(this);
  node.rpc_server->register_handler<&cqy_app::rpc_ctx_call>(this);
  node.rpc_server->register_handler<&cqy_app::rpc_ctx_call_name>(this);
}

void cqy_app::create_client() {
  for (auto &info : config.nodes) {
    auto mq_node = std::make_shared<mq_node_t>();
    mq_node->id = info.nodeid;
    node.mqs.emplace_back(std::move(mq_node));
  }
  for (auto n : node.mqs) {
    if (n->id == config.nodeid) {
      continue; // self can`t connect self
    }

    auto id = n->id;
    auto node_info = get_nodeinfo(id);
    n->rpc_client = cqy_rpc_client_pool::create(
        std::format("{}:{}", node_info->ip, node_info->port),
        {.max_connection = std::thread::hardware_concurrency()});
    node_mq_spawn(id).start([id](async_simple::Try<void> tr) {
      if (tr.hasError()) {
        try {
          throw tr.getException();
        } catch (std::exception &e) {
          CQY_ERROR("node:{} connect exception:{}", id,
                                    e.what());
        }
      } else {
        CQY_INFO("node:{} connect stop", id);
      }
    });
  }
}

void cqy_app::load_config(std::string_view file) {
  auto dir = std::filesystem::current_path();
  auto config_path = dir.append(file);

  iguana::from_json_file(config, config_path.string());
}

void cqy_app::rpc_server_start() { 
  auto ec = node.rpc_server->start();
  if (ec) {
    CQY_ERROR("rpc_server start error:{}", ec.message());
  }
}

auto cqy_app::get_nodeinfo(uint32_t id) -> cqy_node_info_t * {
  for (auto &n : config.nodes) {
    if (n.nodeid == id) {
      return &n;
    }
  }
  return nullptr;
}
auto cqy_app::get_nodeinfo() -> cqy_node_info_t * {
  return get_nodeinfo(config.nodeid);
}

auto cqy_app::get_nodeinfo(std::string_view name) -> cqy_node_info_t * {
  for (auto &n : config.nodes) {
    if (n.name == name) {
      return &n;
    }
  }
  return nullptr;
}

auto cqy_app::get_handle(std::string_view name) -> optv<std::pair<cqy_handle_t, std::string_view>> {
  auto p = algo::split_one(name, ".");
  if (p.second.empty()) {
    return std::nullopt;
  }
  auto node_info = get_nodeinfo(p.first);
  if (!node_info) {
    return std::nullopt;
  }
  cqy_handle_t h;h.nodeid = node_info->nodeid;
  return std::make_optional(std::make_pair(h, p.second));
}

Lazy<void> cqy_app::rpc_on_mq(std::deque<std::string> msgs) {
  for (auto &msg : msgs) {
    auto cqy_msg = cqy_msg_t::parse(msg, true);
    if (!cqy_msg) {
      CQY_ERROR("rpc_on_mq msg error");
      co_return;
    }
    auto to = cqy_msg->to;
    auto name = cqy_msg->name();
    if (!name.empty()) {
      to = ctx_mgr.find_name(name);
    }
    auto ctx = ctx_mgr.get_ctx(to);
    if (ctx) {
      ctx->msg_queue.push(std::move(msg));
    } else {
      cqy_handle_t ch(to);
      CQY_ERROR("rpc_on_mq to:{:0x} can`t find", ch.id);
    }
  }
}

Lazy<rpc_result_t> cqy_app::rpc_ctx_call_name(std::string_view nodectx,
                                              std::string_view func_name,
                                              std::string_view param_data) {
  rpc_result_t result{};
  auto p = get_handle(nodectx); 
  if (!p) {
    result.status = -1;
    result.res = std::format("nodectx:{} miss", nodectx);
    co_return result;
  }
  if (p->first.nodeid != config.nodeid) {
    // node err
    result.status = -1;
    result.res = std::format("nodectx:{} not self", nodectx);
    co_return result;
  }
  auto id = ctx_mgr.find_name(p->second);
  auto ctx = ctx_mgr.get_ctx(id);
  if (!ctx) {
    result.status = -1;
    result.res = std::format("nodectx:{} ctx miss", nodectx);
    co_return result;
  }
  if (auto it = ctx->cpp_rpc_router.find(func_name);
      it != ctx->cpp_rpc_router.end()) {
    co_return co_await it->second(param_data);
  }
  result.status = -1; // no this func_name
  result.res = std::format("func_name:{} miss", func_name);
  co_return result;
}

Lazy<rpc_result_t> cqy_app::rpc_ctx_call(uint32_t to,
                                                std::string_view func_name,
                                                std::string_view param_data) {
  rpc_result_t result{};
  cqy_handle_t h(to);
  if (h.nodeid != config.nodeid) {
    // node err
    result.status = -1;
    result.res = std::format("id:{:0x} miss", to, h.id);
    co_return result;
  }

  auto ctx = ctx_mgr.get_ctx(to);
  if (!ctx) {
    result.status = -1;
    result.res = std::format("id:{:0x} miss", to, h.id);
    co_return result;
  }

  if (auto it = ctx->cpp_rpc_router.find(func_name);
      it != ctx->cpp_rpc_router.end()) {
    co_return co_await it->second(param_data);
  }
  result.status = -1; // no this func_name
  result.res = std::format("func_name:{} miss", func_name);
  co_return result;
}

void cqy_app::node_mq_push(std::string msg) {
  auto *cmsg = cqy_msg_t::parse(msg, true);
  if (!cmsg) {
    return;
  }

  cqy_handle_t to(cmsg->to);
  if (to.nodeid == config.nodeid) {
    auto ctx = ctx_mgr.get_ctx(to.id);
    if (ctx) {
      ctx->msg_queue.push(std::move(msg));
    }
  } else {
    auto mq_node = node.get_mq_node(to.nodeid);
    if (mq_node) {
      mq_node->coro_queue.push(std::move(msg));
    } else {
      CQY_ERROR("node:{} no config msg drop", to.nodeid);
    }
  }
}

Lazy<void> cqy_app::node_mq_spawn(uint8_t id) {
  using namespace std::chrono_literals;

  auto n = node.get_mq_node(id);
  std::deque<std::string> msgs;

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

void cqy_app::create_ctx(std::string_view name, std::string_view param) {
  auto ctx = ctx_mgr.create(name);
  if (!ctx) {
    CQY_ERROR("ctx:{} create error", name);
    return;
  }
  ctx->app = this;
  auto ctx_id = ctx_mgr.new_id();
  ctx->id.nodeid = config.nodeid;
  ctx->id.set_ctxid(ctx_id);
  ctx->ex = coro_io::get_global_block_executor();
  if (!ctx->on_init(param)) {
    CQY_ERROR("ctx:{} init error", name);
    return;
  }

  // begin receive msg
  ctx->wait_stop = std::make_unique<std::latch>(1);
  ctx_mgr.add_ctx(ctx);
  ctx->wait_msg_spawn().via(ctx->ex).detach();
}

void cqy_ctx_t::register_name(std::string name) {
  app->ctx_mgr.register_name(std::move(name), id);
}

Lazy<void> cqy_ctx_t::wait_msg_spawn() {
  assert(ex->currentThreadInExecutor());
  auto wrapper_on_msg = [this](std::string msg) -> Lazy<void> {
    co_await on_msg(cqy_msg_t::parse(msg, false));
  };
  while (!msg_queue.stop.load(std::memory_order_relaxed)) {
    try {
      auto msg = co_await msg_queue.pop();
      if (auto *cmsg = cqy_msg_t::parse(msg, true); cmsg) {
        wrapper_on_msg(std::move(msg))
        .via(ex)
        .detach();
      }
    } catch (...) {
    }
  }
  wait_stop->count_down();
}

uint32_t cqy_ctx_t::dispatch(uint32_t to, uint8_t t, std::string data) {
  std::string s;
  auto sessionid = ++this->session;
  cqy_msg_t::make(s, this->id, to, sessionid, t, data);
  app->node_mq_push(std::move(s));
  return sessionid;
}

uint32_t cqy_ctx_t::dispatch(std::string_view nodectx, uint8_t t, std::string data) {
  auto p = app->get_handle(nodectx);
  if (!p) {
    return -1;
  }

  auto nodeid = p->first.nodeid;
  auto ctxname = p->second;

  if (nodeid == app->config.nodeid || p->first.node() == 0) {
    auto id = app->ctx_mgr.find_name(ctxname);
    return dispatch(id, t, std::move(data));
  }

  std::string s;
  assert(ctxname.size() < std::numeric_limits<uint8_t>::max());
  auto sessionid = ++this->session;
  cqy_msg_t::make(s, this->id, nodeid, ctxname, sessionid, t, data);
  app->node_mq_push(std::move(s));
  return sessionid;
}

void cqy_ctx_t::respone(cqy_msg_t *msg, std::string data) {
  std::string s;
  auto rsp =
      cqy_msg_t::make(s, this->id, msg->from, msg->session,
                      algo::to_underlying(cqy_msg_type_t::response), data);
  app->node_mq_push(std::move(s));
}