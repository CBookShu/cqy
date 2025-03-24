#pragma once
#include "cqy_algo.h"
#include "cqy_handle.h"
#include "cqy_node.h"
#include "cqy_utils.h"
#include "ylt/reflection/template_string.hpp"
#include <string_view>

namespace cqy {
class cqy_ctx;

struct config_t {
  uint8_t nodeid = 1;
  uint32_t thread = std::thread::hardware_concurrency();
  std::vector<node_info> nodes;
  std::string bootstrap;
};

struct ctx_creator {
  friend class cqy_ctx_mgr;
  using creatro_func_t = sptr<cqy_ctx> (*)();
  std::unordered_map<std::string, creatro_func_t, string_hash, std::equal_to<>>
      ctx_creator;
};

class cqy_app : move_only {
  struct cqy_app_t;
  cqy_app_t *s_;
  ctx_creator creator_;

public:
  cqy_app();
  ~cqy_app();

  config_t &get_config();
  cqy_node &get_node();

  void load_config(std::string_view path);
  void start();
  void stop();

  uint8_t self_nodeid();
  uint32_t get_ctxid(std::string_view name);
  void register_name(std::string name, uint32_t id);

  auto get_handle(std::string_view name)
      -> optv<std::pair<cqy_handle_t, std::string_view>>;

  Lazy<void> rpc_on_mq(std::deque<std::string> msgs);
  Lazy<rpc_result_t> rpc_ctx_call(uint32_t to, std::string_view func_name,
                                  std::string_view param_data);
  Lazy<rpc_result_t> rpc_ctx_call_name(std::string_view nodectx,
                                       std::string_view func_name,
                                       std::string_view param_data);

  template <typename... fArgs, typename... Args>
  Lazy<rpc_result_t> ctx_call(uint32_t to, std::string_view func_name,
                              Args &&...args);

  template <typename... fArgs, typename... Args>
  Lazy<rpc_result_t> ctx_call_name(std::string_view nodectx,
                                   std::string_view func_name, Args &&...args);

  void node_mq_push(std::string msg);

public:
  // ctx creator reg and create by name
  template <typename T> void reg_ctx(std::string name = "") {
    static_assert(std::is_base_of_v<cqy_ctx, T>);
    if (name.empty()) {
      name = ylt::reflection::type_string<T>();
    }
    creator_.ctx_creator[std::move(name)] = +[]() -> sptr<cqy_ctx> {
      sptr<T> sptr(new T{}, &algo::deleter<T>);
      return std::static_pointer_cast<cqy_ctx>(sptr);
    };
  }

  void create_ctx(std::string_view name, std::string_view param);
};

template <typename... fArgs, typename... Args>
Lazy<rpc_result_t> cqy_app::ctx_call_name(std::string_view nodectx,
                                          std::string_view func_name,
                                          Args &&...args) {
  rpc_result_t result{};
  auto p = get_handle(nodectx);
  if (!p) {
    result.status = -1;
    result.res = std::format("nodectx:{} miss", nodectx);
    co_return result;
  }
  auto nodeid = p->first.node();
  auto ctx_name = p->second;
  auto& config = get_config();
  if (nodeid == config.nodeid) {
    std::string param;
    rpc_encode<fArgs...>(param, std::forward<Args>(args)...);
    co_return co_await rpc_ctx_call_name(nodectx, func_name, param);
  } else {
    auto n = get_node().get_node(nodeid);
    if (!n) {
      result.status = -2;
      result.res = std::format("nodeid:{} config miss", nodeid);
      co_return result;
    }
    std::string param;
    rpc_encode<fArgs...>(param, std::forward<Args>(args)...);

    auto r = co_await n->rpc_client->send_request(
        [&](coro_rpc::coro_rpc_client &client)
            -> Lazy<coro_rpc::rpc_result<rpc_result_t>> {
          co_return co_await client.call<&cqy_app::rpc_ctx_call_name>(
              nodectx, func_name, param);
        });
    if (!r) {
      result.status = -4;
      // result.res = r.error();
      co_return result;
    }

    if (!r.value()) {
      result.status = -4;
      // result.res = r.value().error();
      co_return result;
    }
    co_return std::move(r.value().value());
  }
}

template <typename... fArgs, typename... Args>
Lazy<rpc_result_t> cqy_app::ctx_call(uint32_t to, std::string_view func_name,
                                     Args &&...args) {
  cqy_handle_t h(to);
  rpc_result_t result{};
  auto& config = get_config();
  if (h.nodeid == config.nodeid) {
    std::string param;
    rpc_encode<fArgs...>(param, std::forward<Args>(args)...);
    co_return co_await rpc_ctx_call(to, func_name, param);
  } else {
    auto n = get_node().get_node(h.node());
    if (!n) {
      result.status = -2;
      result.res = std::format("nodeid:{} config miss", h.nodeid);
      co_return result;
    }
    std::string param;
    rpc_encode<fArgs...>(param, std::forward<Args>(args)...);

    auto r = co_await n->rpc_client->send_request(
        [&](coro_rpc::coro_rpc_client &client)
            -> Lazy<coro_rpc::rpc_result<rpc_result_t>> {
          co_return co_await client.call<&cqy_app::rpc_ctx_call>(to, func_name,
                                                                 param);
        });
    if (!r) {
      result.status = -4;
      /*result.res = r.error();*/
      co_return result;
    }

    if (!r.value()) {
      result.status = -4;
      // result.res = r.value().error();
      co_return result;
    }
    co_return std::move(r.value().value());
  }
}
} // namespace cqy