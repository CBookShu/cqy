#include "game_demo.h"
#include "async_simple/coro/Generator.h"
#include "cqy_algo.h"
#include "cqy_logger.h"
#include "cqy_utils.h"
#include "game_component.h"
#include "msg_define.h"
#include <cassert>
#include <format>
#include <functional>
#include <memory>
#include <openssl/ct.h>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

bool ws_server_t::on_init(std::string_view param) {
  using namespace cinatra;

  register_name("ws_server");
  auto params = cqy::algo::split(param, ",");
  if (params.size() != 5) {
    CQY_ERROR("ws_server init param error:{}", param);
    CQY_ERROR("ws_server param: thread,port,cert_path,key_path,passwd", param);
    return false;
  }

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
    co_ws_start(std::move(start_f)).start([](auto&&){});
    CQY_INFO("ws_server init success:{}", param);
  } catch (const std::exception &e) {
    CQY_ERROR("ws_server init err:{} param:{}", e.what(), param);
    return false;
  }
  return true;
}

cqy::Lazy<void> ws_server_t::on_msg(cqy::cqy_str& s) { 
  auto msg = s.msg();
  try {
    if (msg->type == cqy::algo::to_underlying(gate_msg_type_t::write)) {
      // write
      auto [connid, str] = unpack<uint64_t, std::string>(msg->buffer());
      async_call(co_conn_write(connid, std::move(str)));
    } else if(msg->type == cqy::algo::to_underlying(gate_msg_type_t::close)) {
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

cqy::Lazy<uint64_t> ws_server_t::co_get_connid() {
  try {
    auto r = co_await ctx_call_name_nolock<>(".gate", "get_allocid");
    co_return r.as<uint64_t>();
  } catch (std::exception &e) {
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
  dispatch_pack(".gate", cqy::algo::to_underlying(gate_msg_type_t::ws_start), connid);
}

void ws_server_t::pub_conn_read(uint64_t connid, std::string_view msg) { 
  dispatch_pack(".gate", cqy::algo::to_underlying(gate_msg_type_t::ws_msg), connid, msg);
}

void ws_server_t::pub_conn_stop(uint64_t connid) {
  dispatch_pack(".gate", cqy::algo::to_underlying(gate_msg_type_t::ws_stop), connid);
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
  register_rpc_func<&gate_t::on_conn_start>("on_conn_start");
  register_rpc_func<&gate_t::on_conn_msg>("on_msg");
  register_rpc_func<&gate_t::on_conn_stop>("on_conn_stop");
  get_app()->create_ctx("ws_server", param);
  world_id = get_app()->create_ctx("world", "");
  assert(world_id != 0);
  game_id = get_app()->create_ctx("game", "");
  assert(game_id != 0);

  return true;
}

cqy::Lazy<void> gate_t::on_msg(cqy::cqy_str& s) { 
  auto msg = s.msg();
  try {
    if(msg->type == cqy::algo::to_underlying(gate_msg_type_t::ws_start)) {
      async_call([](gate_t* self, cqy::cqy_str s) ->cqy::Lazy<void>{
        auto msg = s.msg();
        auto connid = self->unpack<uint64_t>(msg->buffer());
        co_await self->on_conn_start(msg->from, connid);
        co_return;
      }(this, std::move(s)));
    }
    else if(msg->type == cqy::algo::to_underlying(gate_msg_type_t::ws_msg)) {
      async_call([](gate_t* self, cqy::cqy_str s) mutable ->cqy::Lazy<void>{
        auto msg = s.msg();
        auto [connid, data] = self->unpack<uint64_t, std::string_view>(msg->buffer());
        co_await self->on_conn_msg(connid, data);
        co_return;
      }(this, std::move(s)));
    }
    else if(msg->type == cqy::algo::to_underlying(gate_msg_type_t::ws_stop)) {
      async_call([](gate_t* self, cqy::cqy_str s) mutable ->cqy::Lazy<void>{
        auto msg = s.msg();
        auto connid = self->unpack<uint64_t>(msg->buffer());
        co_await self->on_conn_stop(connid);
        co_return;
      }(this, std::move(s)));
    }
    else if (msg->type == cqy::algo::to_underlying(gate_msg_type_t::write)) {
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
    } else if(msg->type == cqy::algo::to_underlying(gate_msg_type_t::broad)) {
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

cqy::Lazy<uint64_t> gate_t::rpc_get_allocid() { co_return ++conn_alloc; }

void gate_t::close_con(uint64_t connid) {
  if (auto it = players.find(connid); it != players.end()) {
    write_pack(connid, it->second.from, cqy::algo::to_underlying(gate_msg_type_t::close));
  }
}

cqy::Lazy<void> gate_t::on_conn_start(uint32_t from, uint64_t connid) { 
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

cqy::Lazy<void> gate_t::on_conn_msg(uint64_t connid, std::string_view msg) { 
  CQY_INFO("conn id:{} msg:{}", connid, msg);
  try {
    msg_code_t codec;
    codec.unpack(msg);
    auto head = codec.head();
    if (head.id == game_def::Login) {
      co_return co_await co_login(connid, codec, msg);
    } else {
      co_await ctx_call_name<uint64_t, std::string_view>(
        ".game", "on_client", connid, msg
      );
    }
  } catch(std::exception& e) {
    CQY_ERROR("connid:{} exception:{}", connid, e.what());
    close_con(connid);
  }
  co_return; 
}

cqy::Lazy<void> gate_t::on_conn_stop(uint64_t connid) { 
  CQY_INFO("conn id:{} stop", connid);
  auto& p = players[connid];
  dispatch_pack(world_id, cqy::algo::to_underlying(gate_msg_type_t::close), connid);
  dispatch_pack(game_id, cqy::algo::to_underlying(gate_msg_type_t::close), connid);
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
  auto r = co_await ctx_call_name<uint64_t,game_def::MsgLogin>(
    ".world", "player_login", connid, login
  );

  auto rsp = r.as<game_def::MsgLoginResponce>();
  write_rsp(connid, rsp);
  if (rsp.result == game_def::MsgLoginResponce::Ok) {
    p.login = true;

    co_await ctx_call_name<uint64_t,game_def::MsgLogin>(
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

cqy::Lazy<void> world_t::on_msg(cqy::cqy_str& s) { 
  auto msg = s.msg();
  try {
    if (msg->type == cqy::algo::to_underlying(gate_msg_type_t::close)) {
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
      dispatch_pack(".gate", cqy::algo::to_underlying(gate_msg_type_t::close), it->second);
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

cqy::Lazy<void> game_t::on_msg(cqy::cqy_str& s) { 
  auto msg = s.msg();
  try {
    if (msg->type == cqy::algo::to_underlying(gate_msg_type_t::close)) {
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
    }
    else if (head.id == game_def::PlotEnd) {
      on_recv(connid, codec.as<game_def::MsgPlotEnd>());
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
    Scene->gen = std::invoke(c.func, this, Scene, p.name).begin();
  }
}

void game_t::on_recv(uint64_t connid, game_def::MsgPlotEnd msg) {
  auto& p = players.at(connid);
  if(auto it = scene1_map.find(p.name); it != scene1_map.end()) {
    auto S = it->second;
    if (S->gen) {
      ++S->gen;
    }
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
  s->game = this;
  scene1_map[std::string(player.data(), player.size())] = s;
  return std::make_pair(true, s);
}

void game_t::destroy_scene(scene_t& s) {
  // TODO:
}

void game_t::enter_scene(player& p, cqy::sptr<scene_t>& s) {
  write_notify(p.connid, game_def::MsgEnterSpace{.idSpace = 1});

  for(auto& eid:s->entitys) {
    auto e = s->entity_mgr.get(eid);
    write_notify(p.connid, s->pack_addRoleRet(eid));
    write_notify(p.connid, s->pack_notifyPos(eid));

    auto c = e.component<BuildingComponent>();
    if(c && !c->compelete()) {
      write_notify(p.connid,
        game_def::MsgEntityDes{
          .entityId = eid.id,
          .strDes = std::format("建造进度{}%", c->process)
        });
    }
  }
  auto viewe = s->create_entity();
  s->connid2eid[p.connid] = viewe.id.id;
  viewe.add<PlayerConn>(p.connid);
  viewe.add<PosComponent>();
  viewe.add<OTypeComponent>(game_def::ViewWindow);
  viewe.add<UnitConfigComponent>()->config = {"视口", "smoke", ""};
  viewe.add<AoiComponent>(500);   // 视野默认500
}

cqy::coro_gen<size_t> game_t::scene1_task(cqy::sptr<scene_t> S, std::string player) {
  auto connid = S->game->get_player_connid(player);
  auto e = S->geteid_fromconnid(connid);

  #define WAIT_TYPE(t)  co_yield std::type_index(typeid(t)).hash_code();

  auto f_lingyun_say = [&](const std::string& strMsg){
    static_interface::npc1_say(*S, player,
      "图片/女教官白色海军服", "总教官：凌云", "", "", "    " + strMsg
    );
    static_interface::play_sound(*S, player, "音效/BUTTON", "");
  };
  auto f_user_say = [&](const std::string& msg, bool bQuit = false) {
    static_interface::npc1_say(*S, player,
      "", "", "图片/指挥学员学员青灰色军服", "玩家：" + player, "    " + msg, bQuit
    );
    static_interface::play_sound(*S, player, "音效/BUTTON", "");
  };
  f_lingyun_say("即时战略游戏的操作并不复杂，老年间便有四句童谣：\n"
    "\t\t\t工程车，造基地；\n"
    "\t\t\t基地又产工程车。\n"
    "\t\t\t工程车，造兵营，\n"
    "\t\t\t兵营产兵欢乐多！"
  );
  WAIT_TYPE(game_def::MsgPlotEnd);

  f_user_say("听说工程车还可以造地堡和炮台，我也要试试。");
  WAIT_TYPE(game_def::MsgPlotEnd);

  f_lingyun_say(
    "造完基地应该先安排工程车去采集晶体矿和燃气矿，这是一切生产建造的基础。此外建造民房可以提升活动单位上限。\n加油！"
  );
  WAIT_TYPE(game_def::MsgPlotEnd);

  f_user_say("我只想单手操作，拖动视口 和 选中多个单位 如何操作呢？");
  WAIT_TYPE(game_def::MsgPlotEnd);

  f_lingyun_say("\t\t拖动地面就可以移动视口，此外设置（齿轮图标）界面还有视口镜头投影切换、放大、缩小按钮。当然也支持双指缩放视口。\n"
    "\t\t先点击右边“框选”按钮，然后在屏幕中拖动，即可框选多个单位。在设置（齿轮图标）界面中可以切换菱形框选或方形框选。\n"
    "\t\t全程只要单手握持手机单指操作即可。\n"
  );
  WAIT_TYPE(game_def::MsgPlotEnd);

  f_user_say("只用一只手就能玩的RTS即时战略游戏，那岂不是跟刷短视频一样轻松？我一定要体验一下！");
  WAIT_TYPE(game_def::MsgPlotEnd);

  f_lingyun_say("走你!"); 
  WAIT_TYPE(game_def::MsgPlotEnd);

  static_interface::plotend(*S, player);
  static_interface::say(*S, player,"欢迎来到RTS即时战略游戏，现在您要接受基础的训练", game_def::SayChannel::SYSTEM);

  auto p = e.component<PlayerConn>();
  assert(p);
  p->mineral += 100;
}

entity_t scene_t::create_entity() {
  auto e = entity_mgr.create();
  entitys.insert(e.id);
  return e;
}

entity_t scene_t::geteid_fromconnid(uint64_t connid) {
  if (auto it = connid2eid.find(connid); it != connid2eid.end()) {
    return entity_mgr.get(entity_id_t{it->second});
  }
  return entity_mgr.get({});
}

const std::string& scene_t::headName(entity_id_t id) {
  auto e = entity_mgr.get(id);
  auto [c1, c2, c3] = e.component<
    PlayerNickNameComponent,ResourceCompoent,OTypeComponent
  >();
  if (c1) {
    return c1->strNickName;
  }
  if(c2) {
    static const std::string str("资源");
    return str;
  }
  if(c3 && c3->type == game_def::Effect) {
    static const std::string str("");
    return str;
  }
  {
    static const std::string str("敌人");
    return str;
  }
}

game_def::MsgAddRoleRet scene_t::pack_addRoleRet(entity_id_t id) {
  auto e = entity_mgr.get(id);
  auto [c1, c2, c3] = e.component<
    UnitConfigComponent, DefenceCompoent, OTypeComponent
  >();
  assert(c1 && c2 && c3);
  game_def::MsgAddRoleRet msg;
  msg.entitiId = id;
  msg.nickName = headName(id);
  msg.entityName = c1->config.strName;
  msg.prefabName = c1->config.strPrefabName;
  msg.i32HpMax = c2 ? c2->m_i32HpMax : 0;
  msg.type = c3->type;
  return msg;
}

game_def::MsgNotifyPos scene_t::pack_notifyPos(entity_id_t id) {
  auto e = entity_mgr.get(id);
  auto [c1, c2] = e.component<PosComponent, DefenceCompoent>();
  game_def::MsgNotifyPos msg{};
  msg.entityId = id;
  msg.x = c1->pos.x;
  msg.z = c1->pos.z;
  msg.eulerAnglesY = c1->m_eulerAnglesY;
  if(c2) {
    msg.hp = c2->m_hp;
  }
  return msg;
}

int main() {
  cqy::cqy_app app;
  app.reg_ctx<ws_server_t>("ws_server");
  app.reg_ctx<gate_t>("gate");
  app.reg_ctx<world_t>("world");
  app.reg_ctx<game_t>("game");
  app.load_config("config_game.json");
  auto& config = app.get_config();
  config.bootstrap = "gate 8,12348,server.crt,server.key,";
  app.start();
  app.stop();
  return 0;
}
