#pragma once
#include "ylt/util/type_traits.h"
#include <deque>
#include <format>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <ylt/struct_pack.hpp>

#include <ylt/thirdparty/async_simple/coro/ConditionVariable.h>
#include <ylt/thirdparty/async_simple/coro/Dispatch.h>
#include <ylt/thirdparty/async_simple/coro/Mutex.h>
#include <ylt/thirdparty/async_simple/coro/SharedMutex.h>
#include <ylt/thirdparty/async_simple/coro/SpinLock.h>
#include <ylt/util/type_traits.h>

#include "cqy_gen.h"

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

struct move_only {
  move_only &operator=(move_only &) = delete;
  move_only &operator=(move_only &&) = delete;

  move_only(move_only &) = delete;

  move_only() = default;
  move_only(move_only &&) {}
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

template <typename T> struct cqy_cv_queue_t {
  std::deque<T> items;
  async_simple::coro::Mutex lock;
  async_simple::coro::ConditionVariable<async_simple::coro::Mutex> cv;
  std::atomic<bool> stop{false};

  bool shutdown() {
    if (stop.exchange(true)) {
      return false;
    }
    cv.notifyAll();
    return true;
  }

  async_simple::coro::Lazy<size_t> size() {
    auto guard = co_await lock.coScopedLock();
    co_return items.size();
  }

  async_simple::coro::Lazy<T> pop() {
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

  async_simple::coro::Lazy<std::deque<T>> pop_all() {
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

template <typename T> using Lazy = async_simple::coro::Lazy<T>;

template <typename T> using Try = async_simple::Try<T>;

using coro_spinlock = async_simple::coro::SpinLock;

using coro_sharedlock = async_simple::coro::SharedMutex;

using coro_mutex = async_simple::coro::Mutex;

template <typename M> using coro_cv = async_simple::coro::ConditionVariable<M>;

template <typename T> using coro_gen = cqy::coro::Generator<T>;

template <typename T> using sptr = std::shared_ptr<T>;

template <typename T> using wptr = std::weak_ptr<T>;

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
} // namespace cqy