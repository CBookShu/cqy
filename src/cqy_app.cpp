#include "cqy_app.h"
#include "cqy_ctx.h"
#include "cqy_ctx_mgr.h"
#include "cqy_handle.h"
#include "cqy_logger.h"
#include "cqy_msg.h"
#include "cqy_node.h"
#include "iguana/json_reader.hpp"
#include <atomic>
#include <format>
#include <stdexcept>

using namespace cqy;

struct cqy_app::cqy_app_t {
  std::atomic_bool bstop{false};
  config_t config;
  cqy_ctx_mgr ctx_mgr;
  cqy_node node;
};

cqy_app::cqy_app() { s_ = new cqy_app::cqy_app_t{}; }

cqy_app::~cqy_app() {
  if (s_) {
    delete s_;
  }
}

config_t &cqy_app::get_config() { return s_->config; }

cqy_node &cqy_app::get_node() {
  return s_->node;
}

void cqy_app::load_config(std::string_view file) {
  auto dir = std::filesystem::current_path();
  auto config_path = dir.append(file);

  iguana::from_json_file(s_->config, config_path.string());
}

void cqy_app::start() {
  node_info *self = nullptr;
  for (auto &info : s_->config.nodes) {
    if (info.nodeid != s_->config.nodeid) {
      s_->node.create_client(info);
    } else {
      self = &info;
    }
  }
  if (!self) {
    throw std::runtime_error(
        std::format("self node:{} not config", s_->config.nodeid));
  }
  auto &rpc_server = s_->node.create_rpc_server(s_->config.thread, *self);
  rpc_server.register_handler<&cqy_app::rpc_on_mq>(this);
  rpc_server.register_handler<&cqy_app::rpc_ctx_call>(this);
  rpc_server.register_handler<&cqy_app::rpc_ctx_call_name>(this);

  if (!s_->config.bootstrap.empty()) {
    auto p = algo::split_one(s_->config.bootstrap, " ");
    create_ctx(p.first, p.second);
  }
  // will block here
  s_->node.rpc_server_start();
  // clear
  s_->node.shutdown();
  auto ctxids = s_->ctx_mgr.collect_ctxids();
  for (auto id : ctxids) {
    auto ctx = s_->ctx_mgr.get_ctx(id);
    if (ctx) {
      ctx->shutdown();
    }
  }
}

void cqy_app::stop() {
  if (s_->bstop.exchange(true)) {
    return;
  }
  std::thread thrd{[this] { s_->node.rpc_server_close(); }};
  thrd.join();
}

uint8_t cqy_app::self_nodeid() { return s_->config.nodeid; }

uint32_t cqy_app::get_ctxid(std::string_view name) {
  return s_->ctx_mgr.find_name(name);
}

void cqy_app::register_name(std::string name, uint32_t id) {
  s_->ctx_mgr.register_name(std::move(name), id);
}

auto cqy_app::get_handle(std::string_view name)
    -> optv<std::pair<cqy_handle_t, std::string_view>> {
  auto p = algo::split_one(name, ".");
  if (p.second.empty()) {
    return std::nullopt;
  }
  auto node_info = s_->node.get_node(p.first);
  if (!node_info) {
    return std::nullopt;
  }
  cqy_handle_t h;
  h.nodeid = node_info->info.nodeid;
  return std::make_optional(std::make_pair(h, p.second));
}

void cqy_app::node_mq_push(std::string msg) {
  auto *cmsg = cqy_msg_t::parse(msg, true);
  if (!cmsg) {
    return;
  }
  auto& config = get_config();
  cqy_handle_t to(cmsg->to);
  if (to.nodeid == config.nodeid) {
    auto ctx = s_->ctx_mgr.get_ctx(to.id);
    if (ctx) {
      ctx->node_push_msg(std::move(msg));
    }
  } else {
    auto mq_node = get_node().get_node(to.nodeid);
    if (mq_node) {
      mq_node->coro_queue.push(std::move(msg));
    } else {
      CQY_ERROR("node:{} no config msg drop", to.nodeid);
    }
  }
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
      to = s_->ctx_mgr.find_name(name);
    }
    auto ctx = s_->ctx_mgr.get_ctx(to);
    if (ctx) {
      ctx->node_push_msg(std::move(msg));
    } else {
      cqy_handle_t ch(to);
      CQY_ERROR("rpc_on_mq to:{:0x} can`t find", ch.id);
    }
  }
}

Lazy<rpc_result_t> cqy_app::rpc_ctx_call(uint32_t to,
                                         std::string_view func_name,
                                         std::string_view param_data) {
  rpc_result_t result{};
  cqy_handle_t h(to);
  if (h.nodeid != s_->config.nodeid) {
    // node err
    result.status = -1;
    result.res = std::format("id:{:0x} miss", to, h.id);
    co_return result;
  }

  auto ctx = s_->ctx_mgr.get_ctx(to);
  if (!ctx) {
    result.status = -1;
    result.res = std::format("id:{:0x} miss", to, h.id);
    co_return result;
  }

  auto ok = co_await ctx->rpc_on_call(func_name, param_data, result);
  if (ok) {
    co_return result;
  }
  result.status = -1; // no this func_name
  result.res = std::format("func_name:{} miss", func_name);
  co_return result;
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
  if (p->first.nodeid != s_->config.nodeid) {
    // node err
    result.status = -1;
    result.res = std::format("nodectx:{} not self", nodectx);
    co_return result;
  }
  auto id = s_->ctx_mgr.find_name(p->second);
  auto ctx = s_->ctx_mgr.get_ctx(id);
  if (!ctx) {
    result.status = -1;
    result.res = std::format("nodectx:{} ctx miss", nodectx);
    co_return result;
  }

  auto ok = co_await ctx->rpc_on_call(func_name, param_data, result);
  if (ok) {
    co_return result;
  }
  result.status = -1; // no this func_name
  result.res = std::format("func_name:{} miss", func_name);
  co_return result;
}

void cqy_app::create_ctx(std::string_view name, std::string_view param) {
  sptr<cqy_ctx> ctx;
  if (auto it = creator_.ctx_creator.find(name);
      it != creator_.ctx_creator.end()) {
    ctx = it->second();
  }
  if (!ctx) {
    CQY_ERROR("ctx:{} create error", name);
    return;
  }
  cqy_handle_t h;
  h.set_ctxid(s_->ctx_mgr.new_id());
  h.nodeid = s_->config.nodeid;
  auto ex = coro_io::get_global_block_executor();
  ctx->attach_init(this, h, ex);

  if (!ctx->on_init(param)) {
    CQY_ERROR("ctx:{} init error", name);
    return;
  }
  // begin receive msg
  s_->ctx_mgr.add_ctx(ctx);
  ctx->wait_msg_spawn().via(ex).detach();
}