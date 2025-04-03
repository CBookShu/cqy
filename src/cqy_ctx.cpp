#include "cqy_ctx.h"
#include "async_simple/coro/Dispatch.h"
#include "cqy_app.h"
#include "cqy_utils.h"
#include <exception>
#include <memory>
#include <latch>
#include <cqy_msg.h>
#include <utility>

using namespace cqy;

struct cqy_ctx::cqy_ctx_t {
  friend class cqy_app;
  cqy_app *app = nullptr;
  cqy_handle_t id;
  cqy::coro_spinlock ctx_lock;
  std::atomic_uint32_t session = 0;
  cqy_cv_queue_t<cqy_str> msg_queue;

  std::unordered_map<std::string,
                     std::function<Lazy<rpc_result_t>(std::string_view)>,
                     string_hash, std::equal_to<>>
      cpp_rpc_router;
};

cqy_ctx::cqy_ctx() {
  s_ = new cqy_ctx::cqy_ctx_t();
}

cqy_ctx::~cqy_ctx() {
  on_stop();
  if (s_) {
    delete s_;
  }
}

uint32_t cqy_ctx::getid() {
  return s_->id;
}

cqy_app* cqy_ctx::get_app() {
  return s_->app;
}

uint32_t cqy_ctx::dispatch(uint32_t to, uint8_t t, std::string_view data) {
  cqy_str s;
  auto sessionid = ++s_->session;
  s.make(s_->id, to, sessionid, t, false, data);
  s_->app->node_mq_push(std::move(s));
  return sessionid;
}

uint32_t cqy_ctx::dispatch(std::string_view nodectx, uint8_t t,
                             std::string_view data) {
  auto p = s_->app->get_handle(nodectx);
  if (!p) {
    return -1;
  }

  auto nodeid = p->first.nodeid;
  auto ctxname = p->second;

  auto self_nodeid = s_->app->self_nodeid();

  if (nodeid == self_nodeid || p->first.node() == 0) {
    auto id = s_->app->get_ctxid(ctxname);
    return dispatch(id, t, std::move(data));
  }

  cqy_str s;
  assert(ctxname.size() < std::numeric_limits<uint8_t>::max());
  auto sessionid = ++s_->session;
  s.make(s_->id, nodeid, ctxname, sessionid, t, false, data);
  s_->app->node_mq_push(std::move(s));
  return sessionid;
}

void cqy_ctx::response(cqy_msg_t *msg, std::string_view data) {
  cqy_str s;
  s.make(s_->id, msg->from, msg->session, msg->type,
                             true, data);
  s_->app->node_mq_push(std::move(s));
}

void cqy_ctx::async_call(Lazy<void> task) {
  coro_async_wrapper(std::move(task))
    .via(coro_io::get_global_block_executor())
    .start([s = shared_from_this()](auto&& t){
    });
}

void cqy_ctx::register_name(std::string name) {
  s_->app->register_name(std::move(name), s_->id);
}

Lazy<void> cqy_ctx::wait_msg_spawn(sptr<cqy_ctx> self) {
  auto wrapper_on_msg = [this](cqy_str msg) -> Lazy<void> {
    auto guard = co_await ctx_lock().coScopedLock();
    co_await on_msg(msg);
  };
  while (!s_->msg_queue.stop.load(std::memory_order_relaxed)) {
    try {
      auto msg = co_await s_->msg_queue.pop();
      if (auto *cmsg = msg.parse(true); cmsg) {
        wrapper_on_msg(std::move(msg))
          .via(coro_io::get_global_block_executor())
          .start([s = shared_from_this()](auto&&) {});
      }
    } catch (...) {
    }
  }
}

void cqy_ctx::attach_init(cqy_app* app, uint32_t id) {
  s_->app = app;
  s_->id = id;
}

void cqy_ctx::node_push_msg(cqy_str msg) {
  s_->msg_queue.push(std::move(msg));
}

Lazy<bool> cqy_ctx::rpc_on_call(
  std::string_view func_name,std::string_view param_data, rpc_result_t& result) {
  if (auto it = router_.cpp_rpc_router.find(func_name); it != router_.cpp_rpc_router.end()) {
    result = co_await it->second(param_data);
    co_return true;
  }
  co_return false;
}

void cqy_ctx::shutdown() {
  s_->msg_queue.shutdown();
}

cqy::coro_spinlock& cqy_ctx::ctx_lock() {
  return s_->ctx_lock;
}

Lazy<void> cqy_ctx::coro_async_wrapper(Lazy<void> task) {
  auto guard = co_await s_->ctx_lock.coScopedLock();
  try {
    co_await std::move(task);
  } catch(std::exception&) {

  }
}