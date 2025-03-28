#pragma once
#include "cqy.h"
#include "cqy_utils.h"
#include "ylt/coro_http/coro_http_server.hpp"
#include "msg_define.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <ranges>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

struct ws_server_t : public cqy::cqy_ctx {
  cqy::uptr<coro_http::coro_http_server> server;
  // guard by lock
  cqy::coro_spinlock lock;
  uint32_t new_alloc_nodectx = 0;
  std::string new_alloc_func;
  struct ws_conn_t {
    cqy::wptr<cinatra::coro_http_connection> conn;
    cqy::coro_mutex lock;   // write buf
  };
  std::unordered_map<uint64_t, cqy::sptr<ws_conn_t>> conns;
  struct sub_info_t {
    std::string func_on_start;
    std::string func_on_read;
    std::string func_on_stop;
  };
  std::unordered_map<uint32_t, sub_info_t> sub_ctxs;
  /*
    param format: thread,port,cert_path,key_path,passwd
  */
  virtual bool on_init(std::string_view param) override;

  virtual cqy::Lazy<void> on_msg(cqy::cqy_msg_t *msg) override;

  virtual void on_stop() override;

  cqy::Lazy<bool> rpc_set_allocer(uint32_t id, std::string func_new_alloc);

  cqy::Lazy<bool> rpc_sub(uint32_t subid, std::string func_on_start,
                          std::string func_on_read, std::string func_on_stop);

  cqy::Lazy<uint64_t> co_get_connid();

  cqy::Lazy<void> co_ws_handle(cinatra::coro_http_request &req,
                               cinatra::coro_http_response &resp);

  cqy::sptr<ws_conn_t> get_con(uint64_t connid);

  void pub_conn_start(uint64_t connid);

  void pub_conn_read(uint64_t connid, std::string_view msg);

  void pub_conn_stop(uint64_t connid);

  cqy::Lazy<void> co_conn_read(uint64_t connid,
                               cinatra::coro_http_request &req);

  cqy::Lazy<void> co_conn_write(uint64_t connid, std::string data);

  cqy::Lazy<void> co_ws_start(async_simple::Future<std::error_code> f);
};

struct gate_t : public cqy::cqy_ctx {
  enum write_type {
    none = 0,
    write = 1,
    close = 2,

    broad = 3,
  };

  std::atomic_int64_t conn_alloc = 0;

  struct player {
    bool login = false;
    uint32_t from;
    uint64_t connid;
    uint32_t send_sign;
    uint32_t recv_sign;
  };
  std::unordered_map<uint64_t, player> players;
  uint32_t world_id = 0;
  uint32_t game_id = 0;

  static constexpr int nVerMax = 8;
  static constexpr int nVerMin = 0;

  virtual bool on_init(std::string_view param) override;

  virtual cqy::Lazy<void> on_msg(cqy::cqy_msg_t *msg) override;

  cqy::Lazy<void> init_set_allocer();

  cqy::Lazy<void> init_sub();


  cqy::Lazy<uint64_t> rpc_get_allocid();

  template<typename... Args>
  void write_pack(uint64_t connid, uint32_t from, uint8_t t, Args&&... args) {
    dispatch_pack(from, t, connid, std::forward<Args>(args)...);    
  }

  template <typename Arg>
  void write_rsp(uint64_t connid, Arg&&arg) {
    if (auto it = players.find(connid); it != players.end()) {
      ++it->second.recv_sign;
      arg.head.sn = it->second.recv_sign;
      write_pack(connid, it->second.from, write_type::write, msg_code_t::pack(std::forward<Arg>(arg)));
    }
  }

  void close_con(uint64_t connid);


  cqy::Lazy<void> rpc_on_conn_start(uint32_t from, uint64_t connid);

  cqy::Lazy<void> rpc_on_msg(uint64_t connid, std::string_view msg);

  cqy::Lazy<void> rpc_on_conn_stop(uint64_t connid);

  // 
  cqy::Lazy<void> co_login(uint64_t connid, msg_code_t& codec, std::string_view msg);
};

struct world_t : public cqy::cqy_ctx {
  struct login_t {
    std::string name;
    uint64_t connid;
  };
  std::unordered_map<uint64_t, login_t> logins;
  std::unordered_map<std::string, uint64_t,cqy::string_hash, std::equal_to<>> name2conid;

  virtual bool on_init(std::string_view param) override;
  virtual cqy::Lazy<void> on_msg(cqy::cqy_msg_t *msg) override;
  virtual void on_stop() override;

  template <typename Arg>
  void write_notify(uint64_t connid, Arg&&arg) {
    dispatch_pack(".gate", gate_t::write, connid, msg_code_t::pack(std::forward<Arg>(arg)));
  }

  template <typename Arg>
  void broad_notify(Arg&&arg) {
    dispatch_pack(".gate", gate_t::broad, msg_code_t::pack(std::forward<Arg>(arg)));
  }

  cqy::Lazy<game_def::MsgLoginResponce> rpc_player_login(
    uint64_t connid, game_def::MsgLogin login
  );
};


struct game_t;
struct scene_t;
struct scene_config_t {
  using co_task_func = cqy::Lazy<void>(game_t::*)(scene_t& s, const std::string& player);
  game_def::SceneID id;
  std::string strCrowdPath;
  std::string strSceneName;
  std::string strHttpsMusic;
  co_task_func func;
};

struct entity_id_t {
  union {
    uint64_t id;
    struct {
      uint32_t ver;
      uint32_t idx;
    };
  };

  entity_id_t(uint32_t idx_, uint32_t ver_) {
    idx = idx_;
    ver = ver_;
  }

  operator uint64_t() {
    return id;
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

  template <typename T>
  T* component(entity_id_t id);

  template<typename...T>
  std::vector<entity_id_t> entities_with_components();
};

struct scene_t {
  using sys_clock_t = std::chrono::system_clock;

  scene_config_t* config = nullptr;
  entity_mgr_t entity_mgr;
  sys_clock_t::time_point tp;
};

struct game_t : public cqy::cqy_ctx {
  struct player {
    uint64_t connid;
    std::string name;
  };
  std::unordered_map<uint64_t, player> players;
  std::unordered_map<std::string, uint64_t, cqy::string_hash, std::equal_to<>> name2id;

  std::unordered_map<game_def::SceneID, scene_config_t> scene_configs;

  // 单人剧本
  std::unordered_map<std::string, cqy::sptr<scene_t>, cqy::string_hash, std::equal_to<>> scene1_map; 


  virtual bool on_init(std::string_view param) override;

  virtual cqy::Lazy<void> on_msg(cqy::cqy_msg_t *msg) override;
  virtual void on_stop() override;

  template <typename Arg>
  void write_notify(uint64_t connid, Arg&&arg) {
    dispatch_pack(".gate", gate_t::write, connid, msg_code_t::pack(std::forward<Arg>(arg)));
  }

  template <typename Arg>
  void broad_notify(Arg&&arg) {
    dispatch_pack(".gate", gate_t::broad, msg_code_t::pack(std::forward<Arg>(arg)));
  }

  cqy::Lazy<void> rpc_add_player(uint64_t connid, game_def::MsgLogin login);

  cqy::Lazy<void> rpc_on_client(uint64_t connid, std::string_view msg);

  void on_recv(uint64_t connid, game_def::MsgEnterSingleScene msg);

  auto get_scene1(std::string_view player, scene_config_t& c) ->std::pair<bool, cqy::sptr<scene_t>>;
  void destroy_scene(scene_t& s);
  void enter_scene(player& p, cqy::sptr<scene_t>& s);

  cqy::Lazy<void> scene1_task(scene_t& s, const std::string& player);
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

template<typename...T>
std::vector<entity_id_t> entity_mgr_t::entities_with_components() {
  std::vector<entity_id_t> res;
  auto f_check = [](cqy::uptr<compool>& p, size_t idx) {
    return p && (*p)[idx] && (*p)[idx]->test();
  };
  for(auto idx : std::ranges::views::iota(uint32_t(0), index_counter_)) {
    auto b = true && (f_check(pools_[component_t::component_id<T>()], idx) && ...);
    if(b) {
      res.push_back(entity_id_t{idx, entity_version_[idx]});
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