#pragma once
#include "entity.h"
#include "game_demo.h"
#include "msg_define.h"
#include <string_view>
#include <vector>

struct PlayerConn {
  uint64_t connid = 0;
  game_def::MsgPlotDialogue msgLastMsg;
  uint32_t gas = 0;   // 气体
  uint32_t mineral;   // 晶体
};

struct PlayerNickNameComponent {
  std::string strNickName;
};

struct ResourceCompoent {
  game_def::ObjectType type = game_def::Object_Invalid_0;
  int num;    // 采集数量
};

struct OTypeComponent {
  game_def::ObjectType type = game_def::Object_Invalid_0;
};

struct UnitConfigComponent {
  game_def::UnitConfig config;
};

struct DefenceCompoent {
  int m_hp = 20;
  const int32_t m_i32HpMax;
  std::map<uint64_t, int> m_mapHurt;  // m_map对我伤害
};

struct PosComponent {
  game_def::Position pos;
  int m_eulerAnglesY = 0;
};

struct BuildingComponent {
  int process = 0;
  static const int MAX_PERCENT = 100;
  bool compelete () {
    return process >= MAX_PERCENT;
  }
};

// 临时阻挡
struct TempObstacleComponent {
  uint32_t m_u32DtObstacleRef = 0;
};

struct AoiComponent {
  int viewRange = 0;    // 视野范围
  std::vector<entity_id_t> view_self;     // 自己可以看到的 id
  std::vector<entity_id_t> view_other;    // 别人可以看到自己 id
};

struct static_interface {
  // 剧情对话
  static void npc1_say(scene_t& S,
    std::string_view player,
    std::string leftAvatar, 
    std::string leftName,
    std::string rightAvatar, 
    std::string rightName,
    std::string dialogueText,
    bool bShowQuit = false
  );

  static void play_sound(scene_t& S,std::string_view player, std::string sound, std::string text);

  static void plotend(scene_t& S,std::string_view player);

  static void say(scene_t& S, std::string_view player, std::string content, game_def::SayChannel channel);
};