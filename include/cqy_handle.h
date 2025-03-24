#pragma once
#include <cstdint>

namespace cqy {
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
}