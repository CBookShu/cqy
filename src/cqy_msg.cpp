#include "cqy_msg.h"
#include <ylt/struct_json/json_reader.h>
#include <ylt/standalone/iguana/detail/string_resize.hpp>
#include "cqy_handle.h"

using namespace cqy;


std::string_view cqy_msg_t::name() {
  if (!route) {
    return std::string_view();
  }
  uint8_t *b = (uint8_t *)(this + 1);
  return std::string_view((const char *)b + 1, *b);
}

uint8_t *cqy_msg_t::end() {
  uint8_t *b = (uint8_t *)(this + 1);
  b += len;
  return b;
}

std::string_view cqy_msg_t::buffer() {
  uint8_t *b = (uint8_t *)(this + 1);
  if (route) {
    return std::string_view((const char *)b + 1 + *b, len - 1 - *b);
  }
  return std::string_view((const char *)b, len);
}

cqy_msg_t *cqy_str::parse(bool check) {
  cqy_msg_t *cqy_msg = (cqy_msg_t *)data();
  if (check) {
    size_t sz = size();
    if (sz < sizeof(cqy_msg_t)) {
      return nullptr;
    }
    sz -= sizeof(cqy_msg_t);
    if (sz < cqy_msg->len) {
      return nullptr;
    }
    if (cqy_msg->route) {
      if (sz < 1) {
        return nullptr;
      }
      uint8_t *b = (uint8_t *)(cqy_msg + 1);
      uint8_t name_sz = *b;
      if (sz < name_sz + 1) {
        return nullptr;
      }
    }
  }
  return cqy_msg;
}

cqy_msg_t *cqy_str::make(uint32_t source, uint32_t to,
                           uint32_t session, uint8_t t, bool rsp,
                           std::string_view msgdata) {
  iguana::detail::resize(*this, sizeof(cqy_msg_t) + msgdata.size());
  std::ranges::copy(msgdata, data() + sizeof(cqy_msg_t));
  auto *cmsg = parse(false);
  *cmsg = {};
  cmsg->from = source;
  cmsg->to = to;
  cmsg->session = session;
  cmsg->len = size() - sizeof(cqy_msg_t);
  cmsg->response = rsp;
  cmsg->type = t;
  return cmsg;
}

cqy_msg_t *cqy_str::make(uint32_t source, uint8_t nodeto,
                           std::string_view name, uint32_t session, uint8_t t,
                           bool rsp, std::string_view msgdata) {
  /*
  cqy_msg_t           + size      + name            + data
  sizeof(cqy_msg_t)   uint8_t     name.size()       data.size()
  */
  iguana::detail::resize(*this, sizeof(cqy_msg_t) + sizeof(uint8_t) + name.size() + msgdata.size());
  uint8_t *p = (uint8_t *)(data() + sizeof(cqy_msg_t));
  *p = name.size();
  std::ranges::copy(name, p + 1);
  std::ranges::copy(msgdata, p + name.size() + 1);

  cqy_handle_t h;
  h.nodeid = nodeto;
  auto *cmsg = parse(false);
  *cmsg = {};
  cmsg->from = source;
  cmsg->to = h.id;
  cmsg->session = session;
  cmsg->type = t;
  cmsg->route = 1;
  cmsg->response = rsp;
  cmsg->len = size() - sizeof(cqy_msg_t);
  return cmsg;
}