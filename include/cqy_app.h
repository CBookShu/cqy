#pragma once
#include "async_simple/Executor.h"
#include "async_simple/coro/Dispatch.h"
#include "cqy_algo.h"
#include "cqy_handle.h"
#include "cqy_node.h"
#include "cqy_utils.h"
#include "ylt/coro_io/coro_io.hpp"
#include "ylt/reflection/template_string.hpp"
#include <chrono>
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

  void rpc_on_mq(std::deque<cqy_str> msgs);

  Lazy<uint32_t> rpc_find_ctx(std::string_view nodectx);

  Lazy<rpc_result_t> rpc_ctx_call(uint32_t to, std::string_view func_name,
                                  std::string_view param_data);
  Lazy<rpc_result_t> rpc_ctx_call_name(std::string_view nodectx,
                                       std::string_view func_name,
                                       std::string_view param_data);

  void node_mq_push(cqy_str msg);

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

  uint32_t create_ctx(std::string_view name, std::string_view param);
  void stop_ctx(std::string_view name);
  void stop_ctx(uint32_t id);
};
} // namespace cqy