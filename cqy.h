#pragma once

#include "iguana/detail/string_resize.hpp"
#include "ylt/coro_io/io_context_pool.hpp"
#include "ylt/coro_rpc/impl/coro_rpc_client.hpp"
#include "ylt/coro_rpc/impl/default_config/coro_rpc_config.hpp"
#include "ylt/reflection/template_string.hpp"
#include "ylt/struct_pack.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <format>
#include <functional>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <ranges>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <ylt/coro_io/client_pool.hpp>
#include <ylt/coro_io/coro_file.hpp>
#include <ylt/coro_io/coro_io.hpp>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>
#include <ylt/thirdparty/async_simple/coro/ConditionVariable.h>
#include <ylt/thirdparty/async_simple/coro/Dispatch.h>
#include <ylt/thirdparty/async_simple/coro/SharedMutex.h>

namespace cqy {
struct string_hash {
  using is_transparent = void;
  [[nodiscard]] size_t operator()(const char *txt) const {
    return std::hash<std::string_view>{}(txt);
  }
  [[nodiscard]] size_t operator()(std::string_view txt) const {
    return std::hash<std::string_view>{}(txt);
  }
  [[nodiscard]] size_t operator()(const std::string &txt) const {
    return std::hash<std::string>{}(txt);
  }
};

struct rpc_result_t {
  char status;
  char reserved[3];
  std::string res;

  bool has_error() { return status != 0; }

  bool has_value() { return status == 0; }

  template <typename R> R as() {
    if (!has_value()) {
      throw std::runtime_error(std::format("as type:{} error:{} msg:{}",
                                           typeid(R).name(), (int)status, res));
    }
    R r{};
    auto e = struct_pack::deserialize_to(r, res);
    if (e) {
      throw std::runtime_error(e.message().data());
    }
    return r;
  }
};

template <typename T> class shared_mutex_type {
public:
  template <typename... Args>
  shared_mutex_type(Args &&...args) : data_(std::forward<Args>(args)...) {}

  class WriteGuard {
    std::lock_guard<std::shared_mutex> guard_;
    shared_mutex_type *p_;

  public:
    WriteGuard(shared_mutex_type *m) : guard_(m->smux_), p_(m) {}

    T *operator->() { return &(p_->data_); }

    T *get() { return &(p_->data_); }
  };

  class ReadGuard {
    std::shared_lock<std::shared_mutex> guard_;
    shared_mutex_type *p_;

  public:
    ReadGuard(shared_mutex_type *m) : guard_(m->smux_), p_(m) {}

    const T *operator->() { return &(p_->data_); }

    const T *get() { return &(p_->data_); }
  };

  WriteGuard write() { return WriteGuard(this); }

  ReadGuard read() { return ReadGuard(this); }

private:
  std::shared_mutex smux_;
  T data_;
};

struct algo {
  static std::vector<std::string_view> split(std::string_view s,
                                             std::string_view div = " ") {
    auto sp = s | std::views::split(div);
    std::vector<std::string_view> res;
    for (auto &&sub_range : sp) {
      if (sub_range.empty()) {
        continue;
      }
      res.emplace_back(sub_range.begin(), sub_range.end());
    }
    return res;
  }

  static constexpr std::pair<std::string_view, std::string_view>
  split_one(std::string_view s, std::string_view div = " ") {
    auto pos = s.find(div);
    if (pos == std::string_view::npos) {
      return std::make_pair(s, "");
    } else {
      return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
    }
  }

  template <typename T> static std::optional<T> to_n(std::string_view s) {
    T n{};
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    auto ec = std::make_error_code(r.ec);
    if (ec) {
      return std::nullopt;
    }
    return std::make_optional(n);
  }

  template <typename E> static constexpr auto to_underlying(E e) {
    return static_cast<std::underlying_type_t<E>>(e);
  }

  template <typename T> static void deleter(T *t) { delete t; }

  static decltype(auto) random_engine() {
    static std::default_random_engine e(std::random_device{}());
    return (e);
  }
  static bool random_bernoulli(double percent) {
    percent = std::clamp(percent, 0.0, 100.0);
    std::bernoulli_distribution u(percent / 100.0);
    return u(random_engine());
  }
};

template <typename T> using Lazy = async_simple::coro::Lazy<T>;

using coro_spinlock = async_simple::coro::SpinLock;

using coro_sharedlock = async_simple::coro::SharedMutex;

using coro_mutex = async_simple::coro::Mutex;

template <typename M> using coro_cv = async_simple::coro::ConditionVariable<M>;

template <typename T> using sptr = std::shared_ptr<T>;

template <typename T> using uptr = std::unique_ptr<T>;

template <typename T> using optv = std::optional<T>;

template <auto func, typename Self>
Lazy<rpc_result_t> rpc_call_func(std::string_view str, Self *self) {
  using T = decltype(func);
  using return_type = util::function_return_type_t<T>;
  using param_type = util::function_parameters_t<T>;
  static_assert(
      util::is_specialization_v<return_type, async_simple::coro::Lazy>);

  rpc_result_t result{};
  auto &out = result.res;
  result.status = 0;
  if constexpr (std::is_void_v<typename return_type::ValueType>) {
    if constexpr (std::is_void_v<param_type>) {
      co_await std::apply(func, std::forward_as_tuple(*self));
    } else {
      param_type args{};
      if constexpr (std::tuple_size_v<param_type> == 1) {
        if (struct_pack::deserialize_to(std::get<0>(args), str)) {
          result.status = -3; // param parse error
        }
      } else {
        if (struct_pack::deserialize_to(args, str)) {
          result.status = -3; // param parse error
        }
      }
      if (result.status == 0) {
        co_await std::apply(func, std::tuple_cat(std::forward_as_tuple(*self),
                                                 std::move(args)));
      }
    }
  } else {
    if constexpr (std::is_void_v<param_type>) {
      struct_pack::serialize_to(
          out, co_await std::apply(func, std::forward_as_tuple(*self)));
    } else {
      param_type args{};
      if constexpr (std::tuple_size_v<param_type> == 1) {
        if (struct_pack::deserialize_to(std::get<0>(args), str)) {
          result.status = -3;
        }
      } else {
        if (struct_pack::deserialize_to(args, str)) {
          result.status = -3;
        }
      }
      if (result.status == 0) {
        struct_pack::serialize_to(
            out, co_await std::apply(
                     func, std::tuple_cat(std::forward_as_tuple(*self),
                                          std::move(args))));
      }
    }
  }

  co_return result;
}

template <typename fArg, typename Arg>
inline decltype(auto) helper_wraper_value(Arg &arg) {
  if constexpr (std::is_same_v<std::decay_t<fArg>, std::decay_t<Arg>>) {
    return (arg);
  } else {
    return fArg{arg};
  }
}

template <typename... pArgs, typename... Args, typename Stream>
inline auto helper_pack_args(Stream &out, Args &&...args) {
  return struct_pack::serialize_to(out, helper_wraper_value<pArgs>(args)...);
}

template <typename... fArgs, typename Stream, typename... pArgs>
inline void rpc_encode(Stream &out, pArgs &&...args) {
  auto f = [](fArgs... fargs) { return 0; };
  using check_t = decltype(f(std::forward<pArgs>(args)...));

  if constexpr (sizeof...(pArgs) > 0) {
    helper_pack_args<fArgs...>(out, std::forward<pArgs>(args)...);
  }
}

using cqy_rpc_server = coro_rpc::coro_rpc_server;
using cqy_rpc_client = coro_rpc::coro_rpc_client;
using cqy_rpc_client_pool = coro_io::client_pool<cqy_rpc_client>;

enum class cqy_msg_type_t : uint8_t {
  none = 0,
  response = 1,
};

struct cqy_handle_t {
  union {
    struct {
      uint8_t h[3];
      uint8_t nodeid;
    };
    uint32_t id;
  };

  cqy_handle_t(uint32_t id_ = 0) : id(id_) {}

  uint32_t ctxid() { return 0x00ffffff & id; }

  cqy_handle_t &set_ctxid(uint32_t ctxid) {
    id = (id & 0xff000000) | (ctxid & 0x00ffffff);
    return *this;
  }

  uint8_t node() { return nodeid; }

  operator uint32_t() { return id; }

  static cqy_handle_t from(uint8_t nid, uint32_t ctxid) {
    cqy_handle_t h;
    h.nodeid = nid;
    h.set_ctxid(ctxid);
    return h;
  }
};

struct cqy_msg_t {
  // ctxid
  uint32_t from;
  // ctxid
  uint32_t to;
  // session
  uint32_t session;

  uint32_t len;

  uint8_t type;
  struct {
    uint8_t route : 1;
    uint8_t reserved6 : 7;
  };
  uint8_t reserved2[2];

  std::string_view buffer() {
    uint8_t *b = (uint8_t *)(this + 1);
    if (route) {
      return std::string_view((const char *)b + 1 + *b, len - 1 - *b);
    }
    return std::string_view((const char *)b, len);
  }

  std::string_view name() {
    if (!route) {
      return std::string_view();
    }
    uint8_t *b = (uint8_t *)(this + 1);
    return std::string_view((const char *)b + 1, *b);
  }

  uint8_t *end() {
    uint8_t *b = (uint8_t *)(this + 1);
    b += len;
    return b;
  }

  static cqy_msg_t *parse(std::string_view s, bool check) {
    cqy_msg_t *cqy_msg = (cqy_msg_t *)s.data();
    if (check) {
      size_t sz = s.size();
      if (sz < sizeof(cqy_msg_t)) {
        return nullptr;
      }
      sz -= sizeof(cqy_msg_t);
      if (sz < cqy_msg->len) {
        return nullptr;
      }
      if (cqy_msg->route) {
        if (sz < 1) {
          return nullptr;
        }
        uint8_t *b = (uint8_t *)(cqy_msg + 1);
        uint8_t name_sz = *b;
        if (sz < name_sz + 1) {
          return nullptr;
        }
      }
    }
    return cqy_msg;
  }

  static cqy_msg_t *make(std::string &s, uint32_t source, uint32_t to,
                         uint32_t session, uint8_t t, std::string_view data) {
    iguana::detail::resize(s, sizeof(cqy_msg_t) + data.size());
    std::ranges::copy(data, s.data() + sizeof(cqy_msg_t));
    auto *cmsg = parse(s, false);
    *cmsg = {};
    cmsg->from = source;
    cmsg->to = to;
    cmsg->session = session;
    cmsg->len = s.size() - sizeof(cqy_msg_t);
    cmsg->type = t;
    return cmsg;
  }

  static cqy_msg_t *make(std::string &s, uint32_t source, uint8_t nodeto,
                         std::string_view name, uint32_t session, uint8_t t,
                         std::string_view data) {
    /*
    cqy_msg_t           + size      + name            + data
    sizeof(cqy_msg_t)   uint8_t     name.size()       data.size()
  */
    iguana::detail::resize(s, sizeof(cqy_msg_t) + sizeof(uint8_t) +
                                  name.size() + data.size());
    uint8_t *p = (uint8_t *)(s.data() + sizeof(cqy_msg_t));
    *p = name.size();
    std::ranges::copy(name, p + 1);
    std::ranges::copy(data, p + name.size() + 1);

    cqy_handle_t h;
    h.nodeid = nodeto;
    auto *cmsg = cqy_msg_t::parse(s, false);
    *cmsg = {};
    cmsg->from = source;
    cmsg->to = h.id;
    cmsg->session = session;
    cmsg->type = t;
    cmsg->route = 1;
    cmsg->len = s.size() - sizeof(cqy_msg_t);
    return cmsg;
  }
};

struct cqy_ctx_rpc_result_t {
  std::string_view data;
};

template <typename T> struct cqy_cv_queue_t {
  std::deque<T> items;
  coro_mutex lock;
  coro_cv<coro_mutex> cv;
  std::atomic<bool> stop{false};

  void shutdown() {
    if (stop.exchange(true)) {
      return;
    }
    cv.notifyAll();
  }

  Lazy<size_t> size() {
    auto guard = co_await lock.coScopedLock();
    co_return items.size();
  }

  Lazy<T> pop() {
    auto scope = co_await lock.coScopedLock();
    co_await cv.wait(lock, [&]() { return items.size() > 0 || stop.load(); });
    if (items.empty() || stop.load()) {
      throw std::runtime_error("queue stop");
    }
    auto front = std::move(items.front());
    items.pop_front();
    co_return front;
  }

  bool try_pop(T &t) {
    if (!lock.tryLock()) {
      return false;
    }
    std::unique_lock guard(lock, std::adopt_lock);
    if (items.empty()) {
      return false;
    }
    t = std::move(items.front());
    items.pop_front();
    return true;
  }

  Lazy<std::deque<T>> pop_all() {
    auto scope = co_await lock.coScopedLock();
    co_await cv.wait(lock, [&]() { return items.size() > 0 || stop.load(); });
    std::deque<T> res(std::move(items));
    co_return res;
  }

  void push(T t) {
    if (stop.load()) {
      return;
    }
    {
      while (!lock.tryLock()) {
      }
      std::unique_lock guard(lock, std::adopt_lock);
      items.emplace_back(std::move(t));
    }
    cv.notify();
  }
};

using cqy_coro_queue_t = cqy_cv_queue_t<std::string>;

struct cqy_name_id_t {
  std::string name;
  uint32_t id;
};

struct mq_node_t {
  uint8_t id = 0;
  cqy_coro_queue_t coro_queue;
  sptr<cqy_rpc_client_pool> rpc_client;
};

struct cqy_node_t {
  coro_spinlock mq_lock;
  std::vector<sptr<mq_node_t>> mqs;
  sptr<cqy_rpc_server> rpc_server;

  sptr<mq_node_t> get_mq_node(uint8_t id) {
    std::lock_guard guard(mq_lock);
    for (auto &n : mqs) {
      if (n->id == id) {
        return n;
      }
    }
    return nullptr;
  }
};

struct cqy_app;
struct cqy_ctx_t {
  cqy_app *app = nullptr;
  coro_io::ExecutorWrapper<> *ex = nullptr;
  cqy_handle_t id;
  uptr<std::latch> wait_stop;
  std::atomic_uint32_t session = 0;
  cqy_coro_queue_t msg_queue;
  std::unordered_map<std::string,
                     std::function<Lazy<rpc_result_t>(std::string_view)>,
                     string_hash, std::equal_to<>>
      cpp_rpc_router;

  /*virtual ~cqy_ctx_t() {};*/
  virtual bool on_init(std::string_view param) { return true; }
  virtual Lazy<void> on_msg(cqy_msg_t *msg) { co_return; }

  uint32_t dispatch(uint32_t to, uint8_t t, std::string data);
  uint32_t dispatch(std::string_view nodectx, uint8_t t, std::string data);
  void respone(cqy_msg_t *msg, std::string);

protected:
  template <auto F> void register_rpc_func(std::string_view name = "");
  void register_name(std::string name);
private:
  friend struct cqy_app;
  Lazy<void> wait_msg_spawn();
};

struct cqy_ctx_mgr_t {
  std::atomic_uint32_t allocid{0};
  std::shared_mutex lock;
  std::unordered_map<uint32_t, sptr<cqy_ctx_t>> ctxs;
  std::vector<cqy_name_id_t> names;

  using creatro_func_t = sptr<cqy_ctx_t> (*)();
  std::unordered_map<std::string, creatro_func_t, string_hash, std::equal_to<>>
      ctx_creator;

  uint32_t new_id() {
    return 1 + allocid.fetch_add(1, std::memory_order::relaxed);
  }

  sptr<cqy_ctx_t> get_ctx(uint32_t id) {
    std::shared_lock guard(lock);
    if (auto it = ctxs.find(id); it != ctxs.end()) {
      return it->second;
    }
    return nullptr;
  }

  void add_ctx(sptr<cqy_ctx_t> p) {
    std::lock_guard guard(lock);
    auto it = ctxs.try_emplace(p->id, p);
    assert(it.second);
  }

  void del_ctx(uint32_t id) {
    std::lock_guard guard(lock);
    ctxs.erase(id);
    auto it = std::ranges::find(names, id, &cqy_name_id_t::id);
    if (it != names.end()) {
      names.erase(it);
    }
  }

  std::vector<uint32_t> collect_ctxids() {
    std::shared_lock guard(lock);
    std::vector<uint32_t> res;
    res.reserve(ctxs.size());
    for (auto &p : ctxs) {
      res.emplace_back(p.first);
    }
    return res;
  }

  void register_name(std::string name, uint32_t id) {
    std::lock_guard guard(lock);
    if (std::ranges::binary_search(names, name, {}, &cqy_name_id_t::name)) {
      throw std::runtime_error("name already exists");
    }
    names.emplace_back(std::move(name), id);
    std::ranges::sort(names, {}, &cqy_name_id_t::name);
  }

  uint32_t find_name(std::string_view name) {
    std::shared_lock guard(lock);
    auto it = std::ranges::lower_bound(names, name, {}, &cqy_name_id_t::name);
    if (it != names.end() && it->name == name) {
      return it->id;
    }
    return 0;
  }

  template <typename T> void reg_ctx_creator(std::string name) {
    static_assert(std::is_base_of_v<cqy_ctx_t, T>);
    ctx_creator[std::move(name)] = +[]() -> sptr<cqy_ctx_t> {
      sptr<T> sptr(new T{}, &algo::deleter<T>);
      return std::static_pointer_cast<cqy_ctx_t>(sptr);
    };
  }

  sptr<cqy_ctx_t> create(std::string_view name) {
    if (auto it = ctx_creator.find(name); it != ctx_creator.end()) {
      return it->second();
    }
    return nullptr;
  }
};

struct cqy_node_info_t {
  std::string name;
  std::string ip;
  uint32_t nodeid;
  int port;
};

struct cqy_config_t {
  uint8_t nodeid = 1;
  uint32_t thread = std::thread::hardware_concurrency();
  std::vector<cqy_node_info_t> nodes;
  std::string bootstrap;
};

struct cqy_app {
  cqy_config_t config;
  cqy_node_t node;
  cqy_ctx_mgr_t ctx_mgr;
  std::atomic_bool bstop{false};

  ~cqy_app() {
    stop();
  }

  template <typename T> void reg_ctx(std::string name = "") {
    if (name.empty()) {
      name = ylt::reflection::type_string<T>();
    }
    return ctx_mgr.reg_ctx_creator<T>(std::move(name));
  }

  void create_ctx(std::string_view name, std::string_view param);

  void load_config(std::string_view file);
  void start();
  void close_server();
  void stop();

  auto get_nodeinfo(uint32_t id) -> cqy_node_info_t *;
  auto get_nodeinfo() -> cqy_node_info_t *;
  auto get_nodeinfo(std::string_view name) -> cqy_node_info_t *;

  /*
    format
    nodename.ctxname
    result:
    pair<handle,ctxname>
  */
  auto get_handle(std::string_view name) -> optv<std::pair<cqy_handle_t, std::string_view>>;

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

private:
  void create_client();
  void create_rpc_server();
  void rpc_server_start();

  Lazy<void> node_mq_spawn(uint8_t id);
};

template <auto F> void cqy_ctx_t::register_rpc_func(std::string_view name) {
  if (name.empty()) {
    name = coro_rpc::get_func_name<F>();
  }
  cpp_rpc_router[std::string(name.data(), name.size())] =
      [this](std::string_view data) -> Lazy<rpc_result_t> {
    using class_type_t = util::class_type_t<decltype(F)>;
    auto *pre_ex = co_await async_simple::CurrentExecutor();
    auto in_ex = ex->currentThreadInExecutor();
    if (!in_ex) {
      co_await async_simple::coro::dispatch(ex);
    }
    auto r = co_await rpc_call_func<F>(data, static_cast<class_type_t *>(this));
    if (!in_ex) {
      co_await async_simple::coro::dispatch(pre_ex);
    }
    co_return r;
  };
}

template <typename... fArgs, typename... Args>
Lazy<rpc_result_t>
cqy_app::ctx_call_name(std::string_view nodectx,
                       std::string_view func_name, Args &&...args) {
  rpc_result_t result{};
  auto p = get_handle(nodectx);
  if (!p) {
    result.status = -1;
    result.res = std::format("nodectx:{} miss", nodectx);
    co_return result;
  }
  auto nodeid = p->first.node();
  auto ctx_name = p->second;
  if (nodeid == config.nodeid) {
    std::string param;
    rpc_encode<fArgs...>(param, std::forward<Args>(args)...);
    co_return co_await rpc_ctx_call_name(nodectx, func_name, param);
  } else {
    auto n = node.get_mq_node(nodeid);
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
  if (h.nodeid == config.nodeid) {
    std::string param;
    rpc_encode<fArgs...>(param, std::forward<Args>(args)...);
    co_return co_await rpc_ctx_call(to, func_name, param);
  } else {
    auto n = node.get_mq_node(h.nodeid);
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