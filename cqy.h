#pragma once
#include "asio/executor_work_guard.hpp"
#include "asio/io_context.hpp"
#include "async_simple/Try.h"
#include "async_simple/coro/Sleep.h"
#include "iguana/detail/string_resize.hpp"
#include "iguana/json_reader.hpp"
#include "ylt/coro_io/io_context_pool.hpp"
#include "ylt/coro_rpc/impl/coro_rpc_client.hpp"
#include "ylt/coro_rpc/impl/coro_rpc_server.hpp"
#include "ylt/coro_rpc/impl/default_config/coro_rpc_config.hpp"
#include "ylt/coro_rpc/impl/errno.h"
#include "ylt/coro_rpc/impl/protocol/coro_rpc_protocol.hpp"
#include "ylt/easylog.hpp"
#include "ylt/struct_pack.hpp"
#include <algorithm>
#include <atomic>
#include <ranges>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <random>
#include <ylt/coro_io/coro_io.hpp>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>
#include <ylt/thirdparty/async_simple/coro/ConditionVariable.h>
#include <ylt/coro_io/coro_file.hpp>
#include <ylt/thirdparty/async_simple/coro/SharedMutex.h>
#include <ylt/coro_io/client_pool.hpp>
#include <ylt/thirdparty/async_simple/coro/Dispatch.h>

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

  bool has_error() {
    return status != 0;
  }

  bool has_value() {
    return status == 0;
  }

  template <typename R> R as() {
    if (!has_value()) {
      throw std::runtime_error(
            std::format("as type:{} error:{} msg:{}", typeid(R).name(), status, res));
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
  static constexpr std::vector<std::string_view>
  split(std::string_view s, std::string_view div = " ") {
    std::vector<std::string_view> vs;
    int pos = 0;
    do {
      auto d = s.find(div, pos);
      if (d == std::string::npos) {
        if (pos != s.size()) {
          vs.push_back(s.substr(pos));
        }
        break;
      }
      vs.push_back(s.substr(pos, d - pos));
      pos = d + 1;
    } while (true);
    return vs;
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

  static char *str_malloc(std::string_view s) {
    if (s.empty())
      return nullptr;
    char *str = (char *)std::malloc(s.size() + 1);
    std::copy(s.begin(), s.end(), str);
    str[s.size()] = 0;
    return str;
  }

  template <typename T> static void safe_free(T *buf) {
    if (buf) {
      std::free((void *)buf);
    }
  }

  template<typename T>
  static std::optional<T> to_n(std::string_view s) {
    T n{};
    auto r = std::from_chars(s.begin(), s.end(), n);
    auto ec = std::make_error_code(r.ec);
    if (ec) {
      return std::nullopt;
    }
    return std::make_optional(n);
  }

  template <typename E>
  static constexpr auto to_underlying(E e) {
    return static_cast<std::underlying_type_t<E>>(e);
  }

  template <typename T>
  static void deleter(T* t) {
    delete t;
  }

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
  static_assert(util::is_specialization_v<return_type, async_simple::coro::Lazy>);
  
  rpc_result_t result{};
  auto& out = result.res;
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
      struct_pack::serialize_to(out,
                                co_await std::apply(func, std::forward_as_tuple(*self)));
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
            out, co_await std::apply(func, std::tuple_cat(std::forward_as_tuple(*self),
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

  cqy_handle_t(uint32_t id_ = 0):id(id_) {}

  uint32_t hid() {
    return 0x00ffffff & id;
  }

  operator uint32_t&(){
    return id;
  }

  cqy_handle_t& operator=(uint32_t hid) {
    id = (id & 0xff000000) | (hid & 0x00ffffff);
    return *this;
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
    uint8_t route         : 1;
    uint8_t reserved6     : 7;
  };
  uint8_t reserved2[2];

  std::string_view buffer() {
    uint8_t* b = (uint8_t*)(this + 1);
    if (route) {
      return std::string_view(
        (const char*)b + 1 + *b,
        len - 1 - *b
      );
    }
    return std::string_view((const char*)b, len);
  }

  std::string_view name() {
    if (!route) {
      return std::string_view();
    }
    uint8_t* b = (uint8_t*)(this + 1);
    return std::string_view((const char*)b+1, *b);
  }

  uint8_t* end() {
    uint8_t* b = (uint8_t*)(this + 1);
    b += len;
    return b;
  }

  static cqy_msg_t* parse(std::string_view s, bool check) {
    cqy_msg_t* cqy_msg = (cqy_msg_t*)s.data();
    if(check) {
      size_t sz = s.size();
      if (sz < sizeof(cqy_msg_t)) {
        return nullptr;
      }
      sz -= sizeof(cqy_msg_t);
      if(sz < cqy_msg->len) {
        return nullptr;
      }
      if(cqy_msg->route) {
        if (sz < 1) {
          return nullptr;
        }
        uint8_t* b = (uint8_t*)(cqy_msg + 1);
        uint8_t name_sz = *b;
        if (sz < name_sz + 1) {
          return nullptr;
        }
      }
    }
    return cqy_msg;
  }

  static cqy_msg_t* make(
      std::string& s, uint32_t source, uint32_t to, uint32_t session, uint8_t t, std::string_view data) {
    iguana::detail::resize(s, sizeof(cqy_msg_t) + data.size());
    std::ranges::copy(data, s.data() + sizeof(cqy_msg_t));
    auto* cmsg = parse(s, false);
    *cmsg = {};
    cmsg->from = source;
    cmsg->to = to;
    cmsg->session = session;
    cmsg->len = s.size() - sizeof(cqy_msg_t);
    cmsg->type = t;
    return cmsg;
  }

  static cqy_msg_t* make(
    std::string& s, 
      uint32_t source, uint8_t nodeto, std::string_view name, uint32_t session, uint8_t t, std::string_view data
  ) {
      /*
      cqy_msg_t           + size      + name            + data
      sizeof(cqy_msg_t)   uint8_t     name.size()       data.size()
    */
      iguana::detail::resize(s,
          sizeof(cqy_msg_t) + sizeof(uint8_t) + name.size() + data.size()
      );
      uint8_t* p = (uint8_t*)(s.data() + sizeof(cqy_msg_t));
      *p = name.size();
      std::ranges::copy(name, p + 1);
      std::ranges::copy(data, p + name.size() + 1);

      cqy_handle_t h;h.nodeid = nodeto;
      auto* cmsg = cqy_msg_t::parse(s, false);
      *cmsg = {};
      cmsg->from = source;
      cmsg->to = h;
      cmsg->session = ++session;
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
    if(stop.exchange(true)) {
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
    if (items.empty()) {
      throw std::runtime_error("queue stop");
    }
    auto front = std::move(items.front());
    items.pop_front();
    co_return front;
  }

  bool try_pop(T& t) {
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
    if(stop.load()) {
        return ;
    }
    {
      while (!lock.tryLock()) {}
      std::unique_lock guard(lock, std::adopt_lock);
      items.emplace_back(std::move(t));
    }
    cv.notify();
  }
};

using cqy_coro_queue_t = cqy_cv_queue_t<std::string>;

template<typename T>
struct cqy_queue_t {
  coro_spinlock lock;
  std::deque<T> queue;
  bool in_global;

  cqy_queue_t():in_global(true) {}
  
  size_t size() {
    std::lock_guard guard(lock);
    return queue.size();
  }

  void push(T t, auto cb) {
    std::lock_guard guard(lock);
    queue.emplace_back(std::move(t));
    if (!in_global) {
      in_global = true;
      cb();
    }
  }

  bool pop(T& t) {
    std::lock_guard guard(lock);
    if (queue.empty()) {
      in_global = false;
      return false;
    }
    t = (std::move(queue.front()));
    queue.pop_front();
    return true;
  }
};

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
  asio::io_context poll;

  sptr<mq_node_t> get_mq_node(uint8_t id) {
    std::lock_guard guard(mq_lock);
    for(auto& n:mqs) {
      if (n->id == id) {
        return n;
      }
    }
    return nullptr;
  }
};

struct cqy_app;
struct cqy_ctx_t {
  cqy_app* app = nullptr;
  coro_io::ExecutorWrapper<>* ex = nullptr;
  cqy_handle_t id;
  std::atomic_uint32_t session = 0;
  cqy_coro_queue_t msg_queue;
  std::unordered_map<std::string,
      std::function<Lazy<rpc_result_t>(std::string_view)>,
      string_hash,
      std::equal_to<>> cpp_rpc_router;
  
  /*virtual ~cqy_ctx_t() {};*/
  virtual bool on_init(std::string_view param) {return true;}
  virtual Lazy<void> on_msg(cqy_msg_t* msg) {
    co_return;
  }

  // tools
  Lazy<void> wait_msg_spawn();

  // rpc
  template <auto F> void register_rpc_func();

  void dispatch(uint32_t to, uint8_t t, std::string data);
  void dispatch(uint8_t nodeto, std::string_view name, uint8_t t, std::string data);
  void dispatch(std::string_view nodename, std::string_view ctxname, uint8_t t, std::string data);
  void respone(cqy_msg_t* msg, std::string);
};

struct cqy_ctx_mgr_t {
  std::atomic_uint32_t allocid{0};
  std::shared_mutex lock;
  std::unordered_map<uint32_t, sptr<cqy_ctx_t>> ctxs;
  std::vector<cqy_name_id_t> names;

  using creatro_func_t = sptr<cqy_ctx_t> (*)();
  std::unordered_map<std::string, creatro_func_t, string_hash, std::equal_to<>> ctx_creator;

  uint32_t new_id() {
    return 1 + allocid.fetch_add(1, std::memory_order::relaxed);
  }

  sptr<cqy_ctx_t> get_ctx(uint32_t id) {
    std::shared_lock guard(lock);
    if(auto it = ctxs.find(id); it != ctxs.end()) {
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

  void resiger_name(std::string name, uint32_t id) {
    std::lock_guard guard(lock);
    names.emplace_back(std::move(name), id);
    assert(!std::ranges::binary_search(names, name, {}, &cqy_name_id_t::name));
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

  template<typename T>
  void reg_ctx_creator(std::string name) {
    static_assert(std::is_base_of_v<cqy_ctx_t, T>);
    ctx_creator[std::move(name)] = +[]() -> sptr<cqy_ctx_t>{
      sptr<T> sptr(new T{}, &algo::deleter<T>);
      return std::static_pointer_cast<cqy_ctx_t>(sptr);
    };
  }

  sptr<cqy_ctx_t> create(std::string_view name) {
    if(auto it = ctx_creator.find(name); it != ctx_creator.end()) {
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

using ctx_queue_t = cqy_cv_queue_t<uint32_t>;
struct cqy_app {
  cqy_config_t config;
  cqy_node_t node;
  cqy_ctx_mgr_t ctx_mgr;

  uptr<coro_io::io_context_pool> pool;

  template<typename T>
  void reg_ctx(std::string name) {
    return ctx_mgr.reg_ctx_creator<T>(std::move(name));
  }

  void create_ctx(std::string_view name, std::string_view param);

  void load_config(std::string_view file);
  void start();
  void stop();

  auto get_nodeinfo(uint32_t id) -> cqy_node_info_t*;
  auto get_nodeinfo() -> cqy_node_info_t*;
  auto get_nodeinfo(std::string_view name) -> cqy_node_info_t*;

  Lazy<void> rpc_on_mq(std::deque<std::string> msgs);
  Lazy<rpc_result_t> rpc_ctx_call(uint32_t to, std::string_view func_name, std::string_view param_data);
  Lazy<rpc_result_t> rpc_ctx_call_name(uint8_t nodeid, std::string_view ctx_name, std::string_view func_name, std::string_view param_data);

  template <typename...fArgs, typename...Args>
  Lazy<rpc_result_t> ctx_call(uint32_t to, std::string_view func_name, Args&&...args);

  template <typename...fArgs, typename...Args>
  Lazy<rpc_result_t> ctx_call_name(uint8_t nodeid, std::string_view ctx_name, std::string_view func_name, Args&&...args);

  void node_mq_push(std::string msg);
private:
  void create_client();
  void create_rpc_server();
  void rpc_server_start();

  Lazy<void> node_mq_spawn(uint8_t id);

  Lazy<sptr<cqy_ctx_t>> pop_msg_ctx(ctx_queue_t& q);
  sptr<cqy_ctx_t> try_pop_msg_ctx(ctx_queue_t& q);
};

void cqy_app::start() {
  auto thread = config.thread;
  pool = std::make_unique<coro_io::io_context_pool>(thread);

  create_rpc_server();
  create_client();
  
  if (!config.bootstrap.empty()) {
    auto p = algo::split_one(config.bootstrap, " ");
    create_ctx(p.first, p.second);
  }

  rpc_server_start();
  std::jthread poll_th = std::jthread([&] {
    auto w = asio::make_work_guard(node.poll);
    node.poll.run();
  });


  pool->run();
  poll_th.join();
}

inline void cqy_app::stop()
{   
    node.poll.stop();
    if(node.rpc_server) {
        node.rpc_server->stop();
    }
    if (pool) {
        pool->stop();
    }
}

void cqy_app::create_rpc_server() {
  auto info = get_nodeinfo();
  node.rpc_server = std::make_shared<cqy_rpc_server>(1, info->port);
  node.rpc_server->register_handler<&cqy_app::rpc_on_mq>(this);
  node.rpc_server->register_handler<&cqy_app::rpc_ctx_call>(this);
  node.rpc_server->register_handler<&cqy_app::rpc_ctx_call_name>(this);
}

void cqy_app::create_client() {
  for(auto& info : config.nodes) {
    auto mq_node = std::make_shared<mq_node_t>();
    mq_node->id = info.nodeid;
    node.mqs.emplace_back(std::move(mq_node));
  }
  for(auto n : node.mqs) {
    if(n->id == config.nodeid) {
      continue;   // self can`t connect self
    }

    auto id = n->id;
    auto node_info = get_nodeinfo(id);
    n->rpc_client = cqy_rpc_client_pool::create(
      std::format("{}:{}",node_info->ip, node_info->port),
       {
          .max_connection = std::thread::hardware_concurrency() 
       },
      *pool
    );
    node_mq_spawn(id).start([id](async_simple::Try<void> tr){
      if (tr.hasError()) {
        try {
          throw tr.getException();
        } catch(std::exception& e) {
          ELOG_ERROR << std::format("node:{} connect exception:{}", id, e.what());
        }
      } else {
        ELOG_INFO << std::format("node:{} connect stop", id);
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
  node.rpc_server->async_start();
}

auto cqy_app::get_nodeinfo(uint32_t id) -> cqy_node_info_t* {
  for(auto& n:config.nodes) {
    if (n.nodeid == id) {
      return &n;
    }
  }
  return nullptr;
}
auto cqy_app::get_nodeinfo() -> cqy_node_info_t* {
  return get_nodeinfo(config.nodeid);
}

auto cqy_app::get_nodeinfo(std::string_view name) -> cqy_node_info_t* {
    for (auto& n : config.nodes) {
        if (n.name == name) {
            return &n;
        }
    }
    return nullptr;
}

Lazy<void> cqy_app::rpc_on_mq(std::deque<std::string> msgs)
{
    for(auto& msg:msgs) {
        auto cqy_msg = cqy_msg_t::parse(msg, true);
        if (!cqy_msg) {
            ELOG_WARN << std::format("rpc_on_mq msg error");
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
            ELOG_WARN << std::format("rpc_on_mq to:{},{} can`t find", ch.nodeid, ch.hid());
        }
    }
}

template <typename...fArgs, typename...Args>
Lazy<rpc_result_t> cqy_app::ctx_call_name(uint8_t nodeid, std::string_view ctx_name, std::string_view func_name, Args&&...args) {
    rpc_result_t result{};
    if (nodeid == config.nodeid) {
        std::string param;
        rpc_encode<fArgs...>(param, std::forward<Args>(args)...);
        co_return co_await rpc_ctx_call_name(nodeid, ctx_name, func_name, param);
    } else {
        auto n = node.get_mq_node(nodeid);
        if (!n) {
            result.status = -2;
            result.res = std::format("nodeid:{} config miss", nodeid);
            co_return result;
        }
        std::string param;
        rpc_encode<fArgs...>(param, std::forward<Args>(args)...);

        auto r = co_await n->rpc_client->send_request([&](coro_rpc::coro_rpc_client& client)->Lazy<coro_rpc::rpc_result<rpc_result_t>>
        {
            co_return co_await client.call<&cqy_app::rpc_ctx_call_name>(nodeid, ctx_name, func_name, param);
        });
        if (!r) {
            result.status = -4;
            //result.res = r.error();
            co_return result;
        }

        if (!r.value()) {
            result.status = -4;
            //result.res = r.value().error();
            co_return result;
        }
        co_return std::move(r.value().value());
    }
}

template <typename...fArgs, typename...Args>
Lazy<rpc_result_t> cqy_app::ctx_call(uint32_t to, std::string_view func_name, Args&&...args) {
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
            [&](coro_rpc::coro_rpc_client& client)->Lazy<coro_rpc::rpc_result<rpc_result_t>>
        {
           co_return co_await client.call<&cqy_app::rpc_ctx_call>(to, func_name, param);
        });
        if(!r) {
            result.status = -4;
            /*result.res = r.error();*/
            co_return result;
        }

        if(!r.value()) {
            result.status = -4;
            //result.res = r.value().error();
            co_return result;
        }
        co_return std::move(r.value().value());
    }
}

Lazy<rpc_result_t> cqy_app::rpc_ctx_call_name(uint8_t nodeid, std::string_view ctx_name, std::string_view func_name, std::string_view param_data) {
    rpc_result_t result{};
    if (nodeid != config.nodeid) {
        // node err
        result.status = -1;
        result.res = std::format("nodeid:{} miss",nodeid);
        co_return result;
    }
    auto id = ctx_mgr.find_name(ctx_name);
    auto ctx = ctx_mgr.get_ctx(id);
    if (!ctx) {
        result.status = -1;
        result.res = std::format("nodeid:{} ctx:{} miss",nodeid, ctx_name);
        co_return result;
    }
    if(auto it = ctx->cpp_rpc_router.find(func_name); it != ctx->cpp_rpc_router.end()) {
        co_return co_await it->second(param_data);
    }
    result.status = -1;     // no this func_name
    result.res = std::format("func_name:{} miss", func_name);
    co_return result;
}

inline Lazy<rpc_result_t> cqy_app::rpc_ctx_call(uint32_t to, std::string_view func_name, std::string_view param_data)
{
    rpc_result_t result{};
    cqy_handle_t h(to);
    if (h.nodeid != config.nodeid) {
        // node err
        result.status = -1;
        result.res = std::format("id:{}, nodeid:{} miss",to, h.nodeid);
        co_return result;
    }

    auto ctx = ctx_mgr.get_ctx(to);
    if (!ctx) {
        result.status = -1;
        result.res = std::format("id:{}, nodeid:{} hid:{} miss",to, h.nodeid, h.hid());
        co_return result;
    }

    if(auto it = ctx->cpp_rpc_router.find(func_name); it != ctx->cpp_rpc_router.end()) {
        co_return co_await it->second(param_data);
    }
    result.status = -1;     // no this func_name
    result.res = std::format("func_name:{} miss", func_name);
    co_return result;
}

void cqy_app::node_mq_push(std::string msg) {
  auto* cmsg = cqy_msg_t::parse(msg, true);
  if (!cmsg) {
    return;
  }

  cqy_handle_t to(cmsg->to);
  if (to.nodeid == config.nodeid) {
    auto ctx = ctx_mgr.get_ctx(to);
    if(ctx) {
      ctx->msg_queue.push(std::move(msg));
    }
  } else {
    auto mq_node = node.get_mq_node(to.nodeid);
    if (mq_node) {
      mq_node->coro_queue.push(std::move(msg));
    } else {
      ELOG_WARN << std::format("node:{} no config msg drop", to.nodeid);
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
      if(cur_size >= warn_size) {
        warn_size = warn_size * 2;
        ELOG_WARN << std::format("node:{} queue size:{}", id, cur_size);
      } else {
        warn_size = std::clamp<size_t>(warn_size / 2, 1024, std::numeric_limits<size_t>::max());
      }

      if (msgs.empty()) {
          msgs = co_await n->coro_queue.pop_all();
      }

      if (msgs.empty()) {
          break;
      }

      auto r = co_await n->rpc_client->send_request([&](coro_rpc::coro_rpc_client& client)->Lazy<coro_rpc::rpc_result<void>>{
          co_return co_await client.call<&cqy_app::rpc_on_mq>(
              msgs
          );
      });
      if(!r) {
        // conn error
        co_await coro_io::sleep_for(2s);
        continue;
      }
      if(!r.value()) {
        // remote call error
          co_await coro_io::sleep_for(2s);
          continue;
      }
      msgs.clear();
  }
  co_return;
}

Lazy<sptr<cqy_ctx_t>> cqy_app::pop_msg_ctx(ctx_queue_t& q) {
  uint32_t to = co_await q.pop();
  co_return ctx_mgr.get_ctx(to);
}

sptr<cqy_ctx_t> cqy_app::try_pop_msg_ctx(ctx_queue_t& q) {
  uint32_t to = 0;
  if (q.try_pop(to)) {
    return ctx_mgr.get_ctx(to);
  }
  return nullptr;
}

void cqy_app::create_ctx(std::string_view name, std::string_view param) {
  auto ctx = ctx_mgr.create(name);
  if (!ctx) {
    ELOG_WARN << std::format("ctx:{} create error", name);
    return;
  }
  ctx->app = this;
  auto hid = ctx_mgr.new_id();
  ctx->id.nodeid = config.nodeid;
  ctx->id = hid;
  ctx->ex = pool->get_executor();
  if(!ctx->on_init(param)) {
    ELOG_WARN << std::format("ctx:{} init error", name);
    return;
  }
  
  // begin receive msg
  ctx_mgr.add_ctx(ctx);
  ctx->wait_msg_spawn()
      .via(ctx->ex)
      .detach();
}

void cqy_ctx_t::dispatch(uint8_t nodeto, std::string_view name, uint8_t t, std::string data) {
  if(nodeto == app->config.nodeid
  || nodeto == 0) {
    auto id = app->ctx_mgr.find_name(name);
    dispatch(id, t, std::move(data));
    return;
  }

  std::string s;
  assert(name.size() < std::numeric_limits<uint8_t>::max());
  cqy_msg_t::make(
    s, this->id, nodeto, name, session++, t, data
  );
  app->node_mq_push(std::move(s));
}

inline void cqy_ctx_t::dispatch(std::string_view nodename, std::string_view ctxname, uint8_t t, std::string data)
{
    auto info = app->get_nodeinfo(nodename);
    if (!info) {
        // err
        return;
    }
    dispatch(info->nodeid, ctxname, t, std::move(data));
}

Lazy<void> cqy_ctx_t::wait_msg_spawn() {
    static thread_local size_t pop_count = 0;
    size_t pop_max = app->pool->pool_size();
    assert(ex->currentThreadInExecutor());
    auto wrapper_on_msg = [this](std::string msg) -> Lazy<void> {
        co_await on_msg(cqy_msg_t::parse(msg, false));
    };
    while(!msg_queue.stop.load(std::memory_order_relaxed)) {
        try {
            auto msg = co_await msg_queue.pop();
            do {
                if (auto* cmsg = cqy_msg_t::parse(msg, true); cmsg) {
                    wrapper_on_msg(std::move(msg))
                        .via(ex)
                        .start([](auto&& tr){
                        pop_count--;
                    });
                    pop_count++;
                }
                if(pop_count < pop_max && !msg_queue.try_pop(msg)) {
                    break;
                }
            } while(true);
        } catch(...) {
            
        }
    }
}

template <auto F> 
void cqy_ctx_t::register_rpc_func() {
    constexpr auto name = coro_rpc::get_func_name<F>();
    cpp_rpc_router[std::string(name.data(),name.size())] = [this](std::string_view data) -> Lazy<rpc_result_t> {
        using class_type_t = util::class_type_t<decltype(F)>;
        auto* pre_ex = co_await async_simple::CurrentExecutor();
        auto in_ex = ex->currentThreadInExecutor();
        if (!in_ex) {
            co_await async_simple::coro::dispatch(ex);
        }
        auto r = co_await rpc_call_func<F>(data, static_cast<class_type_t*>(this));
        if (!in_ex) {
            co_await async_simple::coro::dispatch(pre_ex);
        }
        co_return r;
    };
}

void cqy_ctx_t::dispatch(uint32_t to, uint8_t t, std::string data) {
  std::string s;
  cqy_msg_t::make(
    s, this->id, to, this->session++, t, data
  );
  app->node_mq_push(std::move(s));
}

void cqy_ctx_t::respone(cqy_msg_t* msg, std::string data) {
  std::string s;
  auto rsp = cqy_msg_t::make(
    s, this->id, msg->from, msg->session, algo::to_underlying(cqy_msg_type_t::response), data
  );
  app->node_mq_push(std::move(s));
}
} // namespace cqy