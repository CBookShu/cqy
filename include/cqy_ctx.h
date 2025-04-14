#pragma once
#include "cqy_app.h"
#include "cqy_finally.h"
#include "cqy_utils.h"
#include <memory>
#include <string>
#include <string_view>

namespace cqy {

struct cqy_msg_t;
class cqy_ctx;
class cqy_app;

class rpc_func_router {
  friend class cqy_ctx;
  using rpc_call_t = std::function<Lazy<rpc_result_t>(std::string_view)>;
  std::unordered_map<std::string, rpc_call_t, string_hash, std::equal_to<>>
      cpp_rpc_router;
};

class cqy_ctx : public move_only, public std::enable_shared_from_this<cqy_ctx> {
  struct cqy_ctx_t;
  cqy_ctx_t *s_;
  rpc_func_router router_;

public:
  cqy_ctx();
  ~cqy_ctx();

  void attach_init(cqy_app *app, uint32_t id);
  uint32_t getid();

  virtual bool on_init(std::string_view param) { return true; }
  virtual Lazy<void> on_msg(cqy_str &msg) { co_return; }
  virtual void on_stop() {}

  uint32_t dispatch(uint32_t to, uint8_t t, std::string_view data);
  uint32_t dispatch(std::string_view nodectx, uint8_t t, std::string_view data);
  void response(cqy_msg_t *msg, std::string_view);

  template <typename... Args>
  uint32_t dispatch_pack(std::string_view nodectx, uint8_t t, Args &&...args);
  template <typename... Args>
  uint32_t dispatch_pack(uint32_t id, uint8_t t, Args &&...args);

  template <typename... fArgs, typename... Args>
  Lazy<rpc_result_t> ctx_call(uint32_t to, std::string_view func_name,
                              Args &&...args);

  template <typename... fArgs, typename... Args>
  Lazy<rpc_result_t> ctx_call_name(std::string_view nodectx,
                                   std::string_view func_name, Args &&...args);

  template <typename... fArgs, typename... Args>
  Lazy<rpc_result_t> ctx_call_nolock(uint32_t to, std::string_view func_name,
                                     Args &&...args);

  template <typename... fArgs, typename... Args>
  Lazy<rpc_result_t> ctx_call_name_nolock(std::string_view nodectx,
                                          std::string_view func_name,
                                          Args &&...args);

  template <typename... Args> auto unpack(std::string_view msg);

  cqy_app *get_app();
  void async_call(Lazy<void> task);

  template <typename T> Lazy<T> sync_call(Lazy<T> task);

  template <auto F, bool muli = false>
  void register_rpc_func(std::string_view name = "");

protected:
  void register_name(std::string name);

private:
  friend class cqy_app;
  Lazy<void> wait_msg_spawn(sptr<cqy_ctx> self);
  void node_push_msg(cqy_str msg);
  Lazy<bool> rpc_on_call(std::string_view func_name,
                         std::string_view param_data, rpc_result_t &result);
  void shutdown();
  cqy::coro_spinlock &ctx_lock();
  Lazy<void> coro_async_wrapper(Lazy<void> task);
};

template <auto F, bool muli>
void cqy_ctx::register_rpc_func(std::string_view name) {
  if (name.empty()) {
    name = coro_rpc::get_func_name<F>();
  }
  router_.cpp_rpc_router[std::string(name.data(), name.size())] =
      [this](std::string_view data) -> Lazy<rpc_result_t> {
    using class_type_t = util::class_type_t<decltype(F)>;
    if constexpr (muli) {
      co_return co_await rpc_call_func<F>(data,
                                          static_cast<class_type_t *>(this));
    } else {
      auto guard = co_await ctx_lock().coScopedLock();
      co_return co_await rpc_call_func<F>(data,
                                          static_cast<class_type_t *>(this));
      ;
    }
  };
}

template <typename... Args> auto cqy_ctx::unpack(std::string_view msg) {
  using Tp = std::tuple<Args...>;
  static_assert(std::tuple_size_v<Tp> > 0);
  if constexpr (std::tuple_size_v<Tp> == 1) {
    std::tuple_element_t<0, Tp> arg{};
    auto ec = struct_pack::deserialize_to(arg, msg);
    if (ec) {
      throw std::runtime_error(
          std::format("struct deser err:{}", ec.message()));
    }
    return arg;
  } else {
    Tp args{};
    auto ec = struct_pack::deserialize_to(args, msg);
    if (ec) {
      throw std::runtime_error(
          std::format("struct deser err:{}", ec.message()));
    }
    return args;
  }
}

template <typename... Args>
uint32_t cqy_ctx::dispatch_pack(std::string_view nodectx, uint8_t t,
                                Args &&...args) {
  std::string msg;
  struct_pack::serialize_to(msg, std::forward<Args>(args)...);
  return dispatch(nodectx, t, msg);
}

template <typename... Args>
uint32_t cqy_ctx::dispatch_pack(uint32_t id, uint8_t t, Args &&...args) {
  std::string msg;
  struct_pack::serialize_to(msg, std::forward<Args>(args)...);
  return dispatch(id, t, msg);
}

template <typename... fArgs, typename... Args>
Lazy<rpc_result_t> cqy_ctx::ctx_call_name(std::string_view nodectx,
                                          std::string_view func_name,
                                          Args &&...args) {
  ctx_lock().unlock();
  finally { ctx_lock().lock(); };
  co_return co_await ctx_call_name_nolock<fArgs...>(
      nodectx, func_name, std::forward<Args>(args)...);
}

template <typename... fArgs, typename... Args>
Lazy<rpc_result_t> cqy_ctx::ctx_call(uint32_t to, std::string_view func_name,
                                     Args &&...args) {
  ctx_lock().unlock();
  finally { ctx_lock().lock(); };
  co_return co_await ctx_call_nolock<fArgs...>(to, func_name,
                                               std::forward<Args>(args)...);
}

template <typename... fArgs, typename... Args>
Lazy<rpc_result_t> cqy_ctx::ctx_call_nolock(uint32_t to,
                                            std::string_view func_name,
                                            Args &&...args) {
  cqy_handle_t h(to);
  rpc_result_t result{};
  auto *app = get_app();
  auto &config = app->get_config();
  if (h.nodeid == config.nodeid) {
    std::string param;
    rpc_encode<fArgs...>(param, std::forward<Args>(args)...);

    co_return co_await app->rpc_ctx_call(to, func_name, param);
  } else {
    auto n = app->get_node().get_node(h.node());
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

template <typename... fArgs, typename... Args>
Lazy<rpc_result_t> cqy_ctx::ctx_call_name_nolock(std::string_view nodectx,
                                                 std::string_view func_name,
                                                 Args &&...args) {
  rpc_result_t result{};
  auto *app = get_app();
  auto p = app->get_handle(nodectx);
  if (!p) {
    result.status = -1;
    result.res = std::format("nodectx:{} miss", nodectx);
    co_return result;
  }
  auto nodeid = p->first.node();
  auto ctx_name = p->second;
  auto &config = app->get_config();
  if (nodeid == config.nodeid) {
    std::string param;
    rpc_encode<fArgs...>(param, std::forward<Args>(args)...);

    co_return co_await app->rpc_ctx_call_name(nodectx, func_name, param);
  } else {
    auto n = app->get_node().get_node(nodeid);
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

template <typename T> Lazy<T> cqy_ctx::sync_call(Lazy<T> task) {
  ctx_lock().unlock();
  finally { ctx_lock().lock(); };
  co_return co_await std::move(task);
}

} // namespace cqy