#include "entity.h"

std::atomic_size_t component_t::gID = 0;

void entity_t::destroy() {
  mgr->destroy(id);
  id.id = 0;
}

bool entity_t::valid() {
  return mgr && mgr->check(id);
}

entity_t entity_mgr_t::create() {
  uint32_t index,version;
  if (free_list_.empty()) {
    index = index_counter_++;
    if(entity_version_.size() <= index) {
      entity_version_.resize(index + 1);
      for(auto& pool : pools_) {
        if (pool) {
          pool->resize(index + 1);
        }
      }
    }
    version = entity_version_[index] = 1;
  } else {
    index = free_list_.back();
    free_list_.pop_back();
    version = entity_version_[index];
  }
  return entity_t(entity_id_t(index, version), this);
}

entity_t entity_mgr_t::get(entity_id_t id) {
  return entity_t{id, this};
}

bool entity_mgr_t::check(entity_id_t id) {
  if (entity_version_.size() <= id.idx) {
    return false;
  }
  return entity_version_[id.idx] == id.ver;
}

void entity_mgr_t::destroy(entity_id_t id) {
  auto index = id.idx;
  if (id.idx >= entity_version_.size()) {
    return;
  }
  if(entity_version_[id.idx] != id.ver) {
    return;
  }
  entity_version_[id.idx]++;
  free_list_.push_back(id.idx);
  for(auto& p : pools_) {
    if(p && p->size() >= id.idx) {
      (*p)[id.idx].reset();
    }
  }
}