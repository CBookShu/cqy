#include "cqy_ctx_mgr.h"
#include "cqy_ctx.h"

using namespace cqy;

namespace  {
  struct cqy_name_id_t {
    std::string name;
    uint32_t id;
  };
}

struct cqy_ctx_mgr::cqy_ctx_mgr_t {
  std::atomic_uint32_t allocid{0};
  std::shared_mutex lock;
  std::unordered_map<uint32_t, sptr<cqy_ctx>> ctxs;
  std::vector<cqy_name_id_t> names;

  using creatro_func_t = sptr<cqy_ctx> (*)();
  std::unordered_map<std::string, creatro_func_t, string_hash, std::equal_to<>>
      ctx_creator;
};

cqy_ctx_mgr::cqy_ctx_mgr() {
  s_ = new cqy_ctx_mgr::cqy_ctx_mgr_t{};
}
cqy_ctx_mgr::~cqy_ctx_mgr() {
  if (s_) {
    delete s_;
  }
}

uint32_t cqy_ctx_mgr::new_id() {
  return 1 + s_->allocid.fetch_add(1, std::memory_order::relaxed);
}

sptr<cqy_ctx> cqy_ctx_mgr::get_ctx(uint32_t id) {
  std::shared_lock guard(s_->lock);
  if (auto it = s_->ctxs.find(id); it != s_->ctxs.end()) {
    return it->second;
  }
  return nullptr;
}

void cqy_ctx_mgr::add_ctx(sptr<cqy_ctx> p) {
  std::lock_guard guard(s_->lock);
  auto it = s_->ctxs.try_emplace(p->getid(), p);
  assert(it.second);
}

void cqy_ctx_mgr::del_ctx(uint32_t id) {
  std::lock_guard guard(s_->lock);
  s_->ctxs.erase(id);
  auto it = std::ranges::find(s_->names, id, &cqy_name_id_t::id);
  if (it != s_->names.end()) {
    s_->names.erase(it);
  }
}

std::vector<uint32_t> cqy_ctx_mgr::collect_ctxids(){
  std::shared_lock guard(s_->lock);
  std::vector<uint32_t> res;
  res.reserve(s_->ctxs.size());
  for (auto &p : s_->ctxs) {
    res.emplace_back(p.first);
  }
  return res;
}

void cqy_ctx_mgr::register_name(std::string name, uint32_t id) {
  std::lock_guard guard(s_->lock);
  if (std::ranges::binary_search(s_->names, name, {}, &cqy_name_id_t::name)) {
    throw std::runtime_error("name already exists");
  }
  s_->names.emplace_back(std::move(name), id);
  std::ranges::sort(s_->names, {}, &cqy_name_id_t::name);
}

uint32_t cqy_ctx_mgr::find_name(std::string_view name) {
  std::shared_lock guard(s_->lock);
  auto it = std::ranges::lower_bound(s_->names, name, {}, &cqy_name_id_t::name);
  if (it != s_->names.end() && it->name == name) {
    return it->id;
  }
  return 0;
}
