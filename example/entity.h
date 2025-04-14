#pragma once 
#include <vector>
#include <cstdint>
#include <atomic>
#include <cqy_utils.h>
#include <ranges>

struct entity_id_t {
  union {
    uint64_t id;
    struct {
      uint32_t ver;
      uint32_t idx;
    };
  };

  entity_id_t(uint64_t id_ = 0):id(id_) {}

  entity_id_t(uint32_t idx_, uint32_t ver_) {
    idx = idx_;
    ver = ver_;
  }

  operator uint64_t() {
    return id;
  }

  friend bool operator == (const entity_id_t& l, const entity_id_t&r) {
    return l.id == r.id;
  }
};

struct entity_id_t_hash {
  size_t operator()(const entity_id_t& id) const {
    return std::hash<uint64_t>()(id.id);
  }
};

struct component_t {
  virtual ~component_t() {}
  virtual bool test() = 0;
  virtual void* ptr() = 0;

  template <typename T>
  T* cast() {
    return reinterpret_cast<T*>(ptr());
  }

  static std::atomic_size_t gID;

  template <typename T>
  static size_t component_id() {
    static size_t id = gID.fetch_add(1, std::memory_order::relaxed);
    return id;
  }
};

template <typename T>
struct component_opt : component_t {
  cqy::optv<T> v;
  virtual bool test() override {
    return v.has_value();
  }
  virtual void* ptr() override {
    return std::addressof(v.value());
  }
};

struct entity_mgr_t;
struct entity_t {
  entity_id_t id;
  entity_mgr_t* mgr = nullptr;

  template<typename T, typename...Args>
  T* add(Args&&...args);

  template<typename...T>
  auto component() -> std::tuple<std::add_pointer_t<T>...>;

  void destroy();

  bool valid();
};

struct entity_mgr_t {
  using compool = std::vector<cqy::uptr<component_t>>;
  uint32_t index_counter_{0};

  std::vector<uint32_t> free_list_;
  std::vector<cqy::uptr<compool>> pools_;
  std::vector<uint32_t> entity_version_;

  entity_t create();
  void destroy(entity_id_t id);
  entity_t get(entity_id_t id);
  bool check(entity_id_t id);

  template <typename T>
  T* component(entity_id_t id);

  template<typename...T, typename C>
  std::vector<entity_id_t> entities_with_components(C& entytys);
};

template<typename T, typename...Args>
T* entity_t::add(Args&&...args) {
  auto cid = component_t::component_id<T>();
  if (mgr->pools_.size() <= cid) {
    mgr->pools_.resize(cid + 1);
  }
  if (!mgr->pools_[cid]) {
    mgr->pools_[cid] = std::make_unique<entity_mgr_t::compool>();
    mgr->pools_[cid]->resize(mgr->index_counter_);
  }
  auto& p = (*mgr->pools_[cid])[id.idx];
  if (!p) {
    (*mgr->pools_[cid])[id.idx] = std::make_unique<component_opt<T>>();
  }
  component_opt<T>* pp = static_cast<component_opt<T>*>(p.get());
  pp->v.emplace(std::forward<Args>(args)...);
  return pp->template cast<T>();
}

template <typename T>
T* entity_mgr_t::component(entity_id_t id) {
  auto cid = component_t::component_id<T>();
  if (pools_.size() <= cid) {
    return nullptr;
  }
  if (!pools_[cid]) {
    return nullptr;
  }
  auto& p = (*pools_[cid])[id.idx];
  if (!p) {
    return nullptr;
  }
  component_opt<T>* pp = static_cast<component_opt<T>*>(p.get());
  if (!pp->test()) {
    return nullptr;
  }
  return pp->template cast<T>();
}

template<typename...T, typename C>
std::vector<entity_id_t> entity_mgr_t::entities_with_components(C& entitys) {
  std::vector<entity_id_t> res;
  auto f_check = [](cqy::uptr<compool>& p, size_t idx) {
    return p && (*p)[idx] && (*p)[idx]->test();
  };
  for(auto& eid:entitys) {
    if (!check(eid)) {
      continue;
    }
    auto b = true && (f_check(pools_[component_t::component_id<T>()], eid.idx) && ...);
    if(b) {
      res.push_back(eid);
    }
  }
  return res;
}

template<typename... T>
auto entity_t::component() ->std::tuple<std::add_pointer_t<T>...> {
  return std::make_tuple(
    mgr->component<T>(id)...
  );
}