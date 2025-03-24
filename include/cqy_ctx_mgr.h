#pragma once
#include "cqy_utils.h"
#include "cqy_algo.h"

namespace cqy {
  class cqy_ctx;

  class cqy_ctx_mgr : move_only{
      struct cqy_ctx_mgr_t;
      cqy_ctx_mgr_t* s_;
    public:
      cqy_ctx_mgr();
      ~cqy_ctx_mgr();

      uint32_t new_id();

      sptr<cqy_ctx> get_ctx(uint32_t id);

      void add_ctx(sptr<cqy_ctx> p);

      void del_ctx(uint32_t id);

      std::vector<uint32_t> collect_ctxids();

      void register_name(std::string name, uint32_t id);

      uint32_t find_name(std::string_view name);
  };
}