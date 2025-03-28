#include "game_demo.h"
#include "cqy_logger.h"
#include "msg_define.h"
#include <cassert>
#include <functional>
#include <memory>
#include <openssl/ct.h>
#include <typeindex>
#include <utility>

bool ws_server_t::on_init(std::string_view param) {
  using namespace cinatra;

  register_name("ws_server");
  auto params = cqy::algo::split(param, ",");
  if (params.size() != 5) {
    CQY_ERROR("ws_server init param error:{}", param);
    CQY_ERROR("ws_server param: thread,port,cert_path,key_path,passwd", param);
    return false;
  }
  register_rpc_func<&ws_server_t::rpc_set_allocer>("set_allocer");
  register_rpc_func<&ws_server_t::rpc_sub>("sub");

  try {
    auto thread = cqy::algo::to_n<size_t>(params[0]);
    auto port = cqy::algo::to_n<uint16_t>(params[1]);
    server = std::make_unique<coro_http::coro_http_server>(thread.value(),
                                                           port.value());
    server->init_ssl(std::string(params[2].data(), params[2].size()),
                     std::string(params[3].data(), params[3].size()),
                     std::string(params[4].data(), params[4].size()));

    server->set_http_handler<cinatra::GET>(
        "/",
        [this](coro_http_request &req,
               coro_http_response &resp) -> cqy::Lazy<void> {
          co_return co_await co_ws_handle(req, resp);
        });
    auto start_f = server->async_start();
    if (start_f.hasResult()) {
      auto ec = std::move(start_f).get();
      CQY_ERROR("ws_server start error:{}", ec.message());
      return false;
    }
    co_ws_start(std::move(start_f)).via(get_coro_exe()).detach();
    CQY_INFO("ws_server init success:{}", param);
  } catch (const std::exception &e) {
    CQY_ERROR("ws_server init err:{} param:{}", e.what(), param);
    return false;
  }
  return true;
}

cqy::Lazy<void> ws_server_t::on_msg(cqy::cqy_msg_t *msg) { 
  try {
    if (msg->type == 1) {
      // write
      auto [connid, str] = unpack<uint64_t, std::string>(msg->buffer());
      async_call(co_conn_write(connid, std::move(str)));
    } else if(msg->type == 2) {
      // close
      auto connid = unpack<uint64_t>(msg->buffer());
      auto conn = get_con(connid);
      if (conn) {
        if (auto s = conn->conn.lock(); s) {
          s->close();
        }
      }
    }
  } catch(...) {

  }
  co_return; 
}

void ws_server_t::on_stop() {
  if (server) {
    server->stop();
  }
}

cqy::Lazy<bool> ws_server_t::rpc_set_allocer(uint32_t id, std::string func_new_alloc) {
  auto guard = lock.coLock();
  new_alloc_nodectx = id;
  new_alloc_func = func_new_alloc;
  co_return true;
}

cqy::Lazy<bool> ws_server_t::rpc_sub(uint32_t subid, std::string func_on_start,
                        std::string func_on_read, std::string func_on_stop) {
  auto guard = lock.coLock();
  sub_ctxs[subid] = {
    .func_on_start = std::move(func_on_start),
    .func_on_read = std::move(func_on_read),
    .func_on_stop = std::move(func_on_stop)
  };
  co_return true;
}

cqy::Lazy<uint64_t> ws_server_t::co_get_connid() {
  try {
    uint32_t to = 0;
    std::string func;
    {
      auto guard = lock.coLock();
      if (new_alloc_nodectx == 0 || new_alloc_func.empty()) {
        CQY_ERROR("allocer func not init");
        co_return 0;
      }
      to = new_alloc_nodectx;
      func = new_alloc_func;
    }
    auto r = co_await get_app()->ctx_call<>(to, func);
    co_return r.as<uint64_t>();
  } catch (std::exception &e) {
    CQY_ERROR("ws alloc func error:{},{}", new_alloc_nodectx, new_alloc_func);
    CQY_ERROR("ws alloc error:{}", e.what());
  }
  co_return 0;
}

cqy::Lazy<void> ws_server_t::co_ws_handle(cinatra::coro_http_request &req,
                             cinatra::coro_http_response &resp) {
  uint64_t connid = co_await co_get_connid();
  if (connid == 0) {
    co_return;
  }
  auto ws_con = std::make_shared<ws_conn_t>();
  ws_con->conn = req.get_conn()->weak_from_this();
  {
    auto guard = lock.coLock();
    conns[connid] = ws_con;
  }
  pub_conn_start(connid);
  co_await co_conn_read(connid, req);
  pub_conn_stop(connid);
  {
    auto guard = lock.coLock();
    conns.erase(connid);
  }
  co_return;
}

cqy::sptr<ws_server_t::ws_conn_t> ws_server_t::get_con(uint64_t connid) {
  auto guard = lock.coLock();
  if (auto it = conns.find(connid); it != conns.end()) {
    return it->second;
  }
  return nullptr;
}

void ws_server_t::pub_conn_start(uint64_t connid) { 
  auto guard = lock.coLock();
  for(auto& sub:sub_ctxs) {
    if (!sub.second.func_on_start.empty()) {
      get_app()->ctx_call<uint32_t, uint64_t>(
        sub.first, sub.second.func_on_start, getid(), connid
      ).start([](auto&&tr){});
    }
  }
}

void ws_server_t::pub_conn_read(uint64_t connid, std::string_view msg) { 
  auto guard = lock.coLock();
  for(auto& sub:sub_ctxs) {
    if (!sub.second.func_on_read.empty()) {
      get_app()->ctx_call<uint64_t, std::string_view>(
        sub.first, sub.second.func_on_read, connid, msg
      ).start([](auto&&tr){});
    }
  }
}

void ws_server_t::pub_conn_stop(uint64_t connid) {
  auto guard = lock.coLock();
  for(auto& sub:sub_ctxs) {
    if (!sub.second.func_on_stop.empty()) {
      get_app()->ctx_call<uint64_t>(
        sub.first, sub.second.func_on_stop, connid
      ).start([](auto&&tr){});
    }
  }
}

cqy::Lazy<void> ws_server_t::co_conn_read(uint64_t connid,
                             cinatra::coro_http_request &req) {
  using namespace cinatra;
  auto conn = req.get_conn();
  websocket_result result{};
  while (true) {
    result = co_await conn->read_websocket();
    if (result.ec) {
      break;
    }

    if (result.type == ws_frame_type::WS_CLOSE_FRAME) {
      break;
    }

    if (result.type == ws_frame_type::WS_TEXT_FRAME ||
        result.type == ws_frame_type::WS_BINARY_FRAME) {
      pub_conn_read(connid,result.data);
    } else if (result.type == ws_frame_type::WS_PING_FRAME ||
               result.type == ws_frame_type::WS_PONG_FRAME) {
      continue;
    } else {
      break;
    }
  }
}

cqy::Lazy<void> ws_server_t::co_conn_write(uint64_t connid, std::string data) {
  try {
    auto conn = get_con(connid);
    if (!conn) {
      co_return;
    }
    auto wscon = conn->conn.lock();
    if (!wscon) {
      co_return;
    }
    co_await async_simple::coro::dispatch(wscon->get_executor());
    auto guard = co_await conn->lock.coScopedLock();
    co_await wscon->write_websocket(data, cinatra::opcode::binary);
  } catch (std::exception &e) {
    CQY_ERROR("ws connid:{} write error:{}",connid, e.what());
  }
}

cqy::Lazy<void> ws_server_t::co_ws_start(async_simple::Future<std::error_code> f) {
  auto ec = co_await std::move(f);
  if (ec) {
    CQY_ERROR("ws_server start error:{}", ec.message());
  }
  co_return;
}

bool gate_t::on_init(std::string_view param) {
  register_name("gate");
  register_rpc_func<&gate_t::rpc_get_allocid, true>("get_allocid");
  register_rpc_func<&gate_t::rpc_on_conn_start>("on_conn_start");
  register_rpc_func<&gate_t::rpc_on_msg>("on_msg");
  register_rpc_func<&gate_t::rpc_on_conn_stop>("on_conn_stop");
  get_app()->create_ctx("ws_server", param);
  world_id = get_app()->create_ctx("world", "");
  assert(world_id != 0);
  game_id = get_app()->create_ctx("game", "");
  assert(game_id != 0);

  async_call(init_set_allocer());
  async_call(init_sub());

  return true;
}

cqy::Lazy<void> gate_t::on_msg(cqy::cqy_msg_t *msg) { 
  try {
    if (msg->type == write) {
      auto [connid, str] = unpack<uint64_t, std::string_view>(msg->buffer());
      if (msg->from == world_id) {
        game_def::MsgFromWorld w;
        std::ranges::copy(str, std::back_inserter(w.vecByte));
        write_rsp(connid, w);
      } else if(msg->from == game_id){
        game_def::MsgFromGame w;
        std::ranges::copy(str, std::back_inserter(w.vecByte));
        write_rsp(connid, w);
      }
    } else if(msg->type == broad) {
      auto str = unpack<std::string>(msg->buffer());
      for(auto& it:players) {
        if (msg->from == world_id) {
          game_def::MsgFromWorld w;
          std::ranges::copy(str, std::back_inserter(w.vecByte));
          write_rsp(it.first, w);
        } else if(msg->from == game_id){
          game_def::MsgFromGame w;
          std::ranges::copy(str, std::back_inserter(w.vecByte));
          write_rsp(it.first, w);
        }
      }
    }
  } catch(...) {

  }
  co_return; 
}

cqy::Lazy<void> gate_t::init_set_allocer() {
  auto r = co_await get_app()->ctx_call_name<uint32_t, std::string>(
    ".ws_server",
    "set_allocer",
    getid(),"get_allocid");
  if (r.has_error()) {
    CQY_ERROR("ws_server set_allocer error:{}", r.res);
  }
  co_return;
}

cqy::Lazy<void> gate_t::init_sub() {
  auto r = co_await get_app()->ctx_call_name<uint32_t, std::string, std::string, std::string>(
    ".ws_server",
    "sub",
    getid(), "on_conn_start", "on_msg", "on_conn_stop"
  );
  if (r.has_error()) {
    CQY_ERROR("ws_server sub error:{}", r.res);
  }
  co_return;
}

cqy::Lazy<uint64_t> gate_t::rpc_get_allocid() { co_return ++conn_alloc; }

void gate_t::close_con(uint64_t connid) {
  if (auto it = players.find(connid); it != players.end()) {
    write_pack(connid, it->second.from, write_type::close);
  }
}

cqy::Lazy<void> gate_t::rpc_on_conn_start(uint32_t from, uint64_t connid) { 
  CQY_INFO("conn id:{} start", connid);
  players[connid] = {
    .login = false,
    .from = from,
    .connid = connid,
    .send_sign = 0,
    .recv_sign = 0,
  };
  co_return; 
}

cqy::Lazy<void> gate_t::rpc_on_msg(uint64_t connid, std::string_view msg) { 
  CQY_INFO("conn id:{} msg:{}", connid, msg);
  try {
    msg_code_t codec;
    codec.unpack(msg);
    auto head = codec.head();
    if (head.id == game_def::Login) {
      co_return co_await co_login(connid, codec, msg);
    } else {
      co_await get_app()->ctx_call_name<uint64_t, std::string_view>(
        ".game", "on_client", connid, msg
      );
    }
  } catch(std::exception& e) {
    CQY_ERROR("connid:{} exception:{}", connid, e.what());
    close_con(connid);
  }
  co_return; 
}

cqy::Lazy<void> gate_t::rpc_on_conn_stop(uint64_t connid) { 
  CQY_INFO("conn id:{} stop", connid);
  auto& p = players[connid];
  dispatch_pack(world_id, close, connid);
  dispatch_pack(game_id, close, connid);
  players.erase(connid);
  co_return; 
}

cqy::Lazy<void> gate_t::co_login(uint64_t connid, msg_code_t& codec, std::string_view msg) {
  auto& p = players.at(connid);
  if (p.login) {
    // already login
    game_def::MsgLoginResponce rsp;
    write_rsp(connid, rsp);
    co_return;
  }

  auto head = codec.head();
  p.send_sign++;
  if(p.send_sign != head.sn) {
    // sn error close direct
    CQY_ERROR("connid:{} send sign error", connid);
    close_con(connid);
    co_return;
  }

  auto login = codec.as<game_def::MsgLogin>();
  // check version
  if (login.uVer < nVerMin || login.uVer > nVerMax) {
    game_def::MsgLoginResponce rsp;
    rsp.result = game_def::MsgLoginResponce::VerionErr;

    write_rsp(connid, rsp);
    close_con(connid);
    co_return;
  }

  // send to login ctx
  auto r = co_await get_app()->ctx_call_name<uint64_t,game_def::MsgLogin>(
    ".world", "player_login", connid, login
  );

  auto rsp = r.as<game_def::MsgLoginResponce>();
  write_rsp(connid, rsp);
  if (rsp.result == game_def::MsgLoginResponce::Ok) {
    p.login = true;

    co_await get_app()->ctx_call_name<uint64_t,game_def::MsgLogin>(
      ".game", "add_player", connid, login
    );

  } else {
    // login error
    close_con(connid);
  }
  co_return;
}

bool world_t::on_init(std::string_view param) { 
  register_name("world");
  register_rpc_func<&world_t::rpc_player_login>("player_login");
  return true; 
}

cqy::Lazy<void> world_t::on_msg(cqy::cqy_msg_t *msg) { 
  try {
    if (msg->type == gate_t::close) {
      auto connid = unpack<uint64_t>(msg->buffer());
      if (auto it = logins.find(connid); it != logins.end()) {
        name2conid.erase(it->second.name);
        logins.erase(it);
      }
    }
  } catch(...) {

  }
  co_return; 
}
void world_t::on_stop() {}

cqy::Lazy<game_def::MsgLoginResponce> world_t::rpc_player_login(
  uint64_t connid, game_def::MsgLogin login
) {
  game_def::MsgLoginResponce rsp{};
  try {
    if (auto it = name2conid.find(login.name); it != name2conid.end()) {
      // 重复登录，踢掉老的那个
      dispatch_pack(".gate", gate_t::close, it->second);
      name2conid.erase(it);
    }

    name2conid[login.name] = connid;
    logins[connid] = {
      .name = std::move(login.name),
      .connid = connid
    };

    // notify onlie 
    game_def::MsgOnline online;
    online.u16count = logins.size();
    for(auto& u:logins) {
      online.vecPlayerNickName.push_back(u.second.name);
    }
    broad_notify(online);
    co_return rsp;
  } catch(std::exception& e) {
    rsp.result = game_def::MsgLoginResponce::PwdErr;
    rsp.tip = std::format("login error:{}", e.what());
  }
  co_return rsp;
}

bool game_t::on_init(std::string_view param) { 
  register_name("game");
  register_rpc_func<&game_t::rpc_add_player>("add_player");
  register_rpc_func<&game_t::rpc_on_client>("on_client");

  scene_configs[game_def::SceneID::TrainScene] = scene_config_t{
    game_def::SceneID::TrainScene, 
    "all_tiles_tilecache.bin", 
    "scene战斗", 
    "https://www.rtsgame.online/music/Suno_Edge_of_Collapse.mp3",
    &game_t::scene1_task
  };

  return true; 
}

cqy::Lazy<void> game_t::on_msg(cqy::cqy_msg_t *msg) { 
  try {
    if (msg->type == gate_t::close) {
      auto connid = unpack<uint64_t>(msg->buffer());
      if(auto it = players.find(connid); it != players.end()) {
        name2id.erase(it->second.name);
        players.erase(it);
      }
    }
  } catch(...) {
    
  }
  co_return; 
}
void game_t::on_stop() {}

cqy::Lazy<void> game_t::rpc_add_player(uint64_t connid, game_def::MsgLogin login) {
  if (auto it = name2id.find(login.name); it != name2id.end()) {
    players.erase(it->second);
    name2id.erase(it);
  }
  players[connid] = {
    .connid = connid,
    .name = login.name
  };
  name2id[login.name] = connid;
  co_return;
}

cqy::Lazy<void> game_t::rpc_on_client(uint64_t connid, std::string_view msg) {
  try {
    msg_code_t codec;
    codec.unpack(msg);
    auto head = codec.head();
    if(head.id == game_def::EnterSingleScene) {
      on_recv(connid, codec.as<game_def::MsgEnterSingleScene>());
    } else {
      CQY_INFO("connid msgid:{}", cqy::algo::to_underlying(head.id));
    }
  } catch(...) {

  }
  co_return;
}

void game_t::on_recv(uint64_t connid, game_def::MsgEnterSingleScene msg) {
  auto& p = players.at(connid);
  auto& c = scene_configs.at(msg.id);
  if (auto it = scene1_map.find(p.name); it != scene1_map.end()) {
    if (it->second->config->id != msg.id) {
      destroy_scene(*it->second);
    }
  }

  auto [bNew, Scene] = get_scene1(p.name, c);
  enter_scene(p, Scene);
  if (bNew) {
    async_call(std::invoke(c.func, this, std::ref(*Scene), p.name));
  }
}

auto game_t::get_scene1(std::string_view player, scene_config_t& c)  ->std::pair<bool, cqy::sptr<scene_t>>{
  auto it = scene1_map.find(player);
  if (it != scene1_map.end()) {
    return std::make_pair(false, it->second);
  }

  auto s = std::make_shared<scene_t>();
  s->config = &c;
  s->tp = scene_t::sys_clock_t::now();
  return std::make_pair(true, s);
}

void game_t::destroy_scene(scene_t& s) {
  // TODO:
}

void game_t::enter_scene(player& p, cqy::sptr<scene_t>& s) {
  write_notify(p.connid, game_def::MsgEnterSpace{.idSpace = 1});
}

cqy::Lazy<void> game_t::scene1_task(scene_t& s, const std::string& player) {
  co_return;
}

std::atomic_size_t component_t::gID = 0;

void entity_t::destroy() {
  mgr->destroy(id);
  id.id = 0;
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

int main() {
  cqy::cqy_app app;
  app.reg_ctx<ws_server_t>("ws_server");
  app.reg_ctx<gate_t>("gate");
  app.reg_ctx<world_t>("world");
  app.reg_ctx<game_t>("game");
  app.load_config("config_game.json");
  auto& config = app.get_config();
  config.bootstrap = "gate 8,12349,server.crt,server.key,";
  app.start();
  app.stop();
  return 0;
}

static void test_entity() {
  entity_mgr_t mgr;
  auto e = mgr.create();
  auto* p1 = e.add<int>(1);
  auto* p2 = e.add<std::string>("hello world");
  CQY_INFO("p1:{}", *p1);
  CQY_INFO("p2:{}", *p2);

  auto [p11] = e.component<int>();
  auto [p21] = e.component<std::string>();
  assert(p1 == p11);
  assert(p2 == p21);

  auto [p12, p22] = e.component<int, std::string>();
  assert(p12 == p11);
  assert(p22 == p21);

  auto e1 = mgr.create();
  e1.add<std::string>();

  auto entitys1 = mgr.entities_with_components<std::string>();
  assert(entitys1.size() == 2);
  assert(entitys1[0] == e.id);
  assert(entitys1[1] == e1.id);

  auto entitis2 = mgr.entities_with_components<std::string, int>();
  assert(entitis2.size() == 1);
  assert(entitis2.front() == e.id);
}