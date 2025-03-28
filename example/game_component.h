#pragma once
#include "entity.h"
#include "msg_define.h"

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
};