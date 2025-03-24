#pragma once
#include <cstdint>
#include <string_view>


namespace cqy {
  enum class cqy_msg_type_t : uint8_t {
    none = 0,
    // custom
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
      uint8_t response : 1;
      uint8_t reserved6 : 6;
    };
    uint8_t reserved2[2];
  
    std::string_view buffer();
  
    std::string_view name();
  
    uint8_t *end();
  
    static cqy_msg_t *parse(std::string_view s, bool check);
  
    static cqy_msg_t *make(std::string &s, uint32_t source, uint32_t to,
                           uint32_t session, uint8_t t, bool rsp,
                           std::string_view data);
  
    static cqy_msg_t *make(std::string &s, uint32_t source, uint8_t nodeto,
                           std::string_view name, uint32_t session, uint8_t t,
                           bool rsp, std::string_view data);
  };
}