#include "cqy_ctx.h"
#include "async_simple/coro/Dispatch.h"
#include "cqy_app.h"
#include <memory>
#include <latch>
#include <cqy_msg.h>

using namespace cqy;

struct cqy_ctx::cqy_ctx_t {
  friend class cqy_app;
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
};

cqy_ctx::cqy_ctx() {
  s_ = new cqy_ctx::cqy_ctx_t();
}

cqy_ctx::~cqy_ctx() {
  if (s_) {
    delete s_;
  }
}

uint32_t cqy_ctx::getid() {
  return s_->id;
}

coro_io::ExecutorWrapper<>* cqy_ctx::get_coro_exe() {
  return s_->ex;
}

cqy_app* cqy_ctx::get_app() {
  return s_->app;
}

uint32_t cqy_ctx::dispatch(uint32_t to, uint8_t t, std::string_view data) {
  std::string s;
  auto sessionid = ++s_->session;
  cqy_msg_t::make(s, s_->id, to, sessionid, t, false, data);
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

  std::string s;
  assert(ctxname.size() < std::numeric_limits<uint8_t>::max());
  auto sessionid = ++s_->session;
  cqy_msg_t::make(s, s_->id, nodeid, ctxname, sessionid, t, false, data);
  s_->app->node_mq_push(std::move(s));
  return sessionid;
}

void cqy_ctx::response(cqy_msg_t *msg, std::string_view data) {
  std::string s;
  auto rsp = cqy_msg_t::make(s, s_->id, msg->from, msg->session, msg->type,
                             true, data);
  s_->app->node_mq_push(std::move(s));
}

void cqy_ctx::async_call(Lazy<void> task) {
  std::move(task)
  .via(get_coro_exe())
  .start([s = shared_from_this()](auto&& t){
  });
}

Lazy<void> cqy_ctx::ctx_switch() {
  co_await async_simple::coro::dispatch(get_coro_exe());
}

void cqy_ctx::register_name(std::string name) {
  s_->app->register_name(std::move(name), s_->id);
}

Lazy<void> cqy_ctx::wait_msg_spawn(sptr<cqy_ctx> self) {
  assert(s_->ex->currentThreadInExecutor());
  auto wrapper_on_msg = [this](std::string msg) -> Lazy<void> {
    co_await on_msg(cqy_msg_t::parse(msg, false));
  };
  while (!s_->msg_queue.stop.load(std::memory_order_relaxed)) {
    try {
      auto msg = co_await s_->msg_queue.pop();
      if (auto *cmsg = cqy_msg_t::parse(msg, true); cmsg) {
        wrapper_on_msg(std::move(msg)).via(s_->ex).detach();
      }
    } catch (...) {
    }
  }
  on_stop();
  s_->wait_stop->count_down();
}

void cqy_ctx::attach_init(cqy_app* app, uint32_t id, coro_io::ExecutorWrapper<>* ex) {
  s_->app = app;
  s_->id = id;
  s_->ex = ex;
  s_->wait_stop = std::make_unique<std::latch>(1);
}

void cqy_ctx::node_push_msg(std::string msg) {
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

void cqy_ctx::shutdown(bool wait) {
  if(!s_->msg_queue.shutdown()) {
    return;
  }
  if(s_->wait_stop && wait) {
    s_->wait_stop->wait();
  }
}