 #pragma once
 #include "msgpack/adaptor/define_decl.hpp"
#include "msgpack/v3/object_fwd_decl.hpp"
#include "ylt/reflection/user_reflect_macro.hpp"
#include <cstdint>
#include <msgpack.hpp>
#include <vector>

namespace game_def {
  struct Position
  {
    float x = 0;
    float z = 0;
    MSGPACK_DEFINE(x, z);
    bool operator==(const Position& refRight)const
    {
      return x == refRight.x && z == refRight.z;
    }
    void operator+=(const Position& refRight)
    {
      x += refRight.x;
      z += refRight.z;
    }
    Position operator-(const Position& refRight)const
    {
      Position pos(*this);
      pos.x -= refRight.x;
      pos.z -= refRight.z;
      return pos;
    }
    Position operator+(const Position& refRight)const
    {
      Position pos(*this);
      pos.x += refRight.x;
      pos.z += refRight.z;
      return pos;
    }
    bool DistanceLessEqual(const Position& refPos, float fDistance)const;

    float DistancePow2(const Position& refPos)const;
    float Distance(const Position& refPos)const;
  };
  struct Rect
  {
    Position poslefttop;
    Position posrightbottom;
    float width()const
    {
      return posrightbottom.x - poslefttop.x;
    }
    int32_t width_i()const
    {
      return (int32_t)width();
    }
    float height()const
    {
      return posrightbottom.z - poslefttop.z;
    }
    int32_t height_i()const
    {
      return (int32_t)height();
    }
    bool contain(const Position& pos)const
    {
      return
        poslefttop.x < pos.x && pos.x < posrightbottom.x &&
        poslefttop.z < pos.z && pos.z < posrightbottom.z;
    }
  };
  enum MsgId {
    MsgId_Invalid_0,
    Login = 1,
    Move = 2,
    AddRoleRet = 3,
    NotifyPos = 4,

    Say = 6,

    EnterScene = 23,
    EnterSingleScene = 24,

    LeaveSpace = 26,
    EntityDes = 27,

    PlotDialogue = 38,    // 剧情对话
    PlotEnd = 39,         // 剧情结束

    MsgOnlie = 40,
    FromGame = 41,
    FromWorld = 42,
    BuildingCenter = 43,
    PlaySound = 44,
  };

  enum ObjectType {
    Object_Invalid_0,

    Effect = 1,   // 特效
    ViewWindow = 2,
  };

  enum SceneID
  {
    SingleScene_MIN,
    TrainScene = 1,   //训练战,
    DefendScene = 2,  //防守战,
    AttackScene = 3,  //攻坚战,
    SingleScnen_MAX,

    MultiScene_MIN = 100,
    SquareScene,    //四方对战,
    MultiScene_MAX = 200, //多人ID_非法_MAX,

    MultiOnlineScene,
  };

  struct MsgHead {
    MsgId id;
    uint32_t sn;
    uint32_t rpcSnId;   // abandon
    MSGPACK_DEFINE(id, sn, rpcSnId);
  };

  struct MsgLogin {
    MsgHead head;
    std::string name;
    std::string pwd;
    uint32_t uVer;
    MSGPACK_DEFINE(head, name, pwd, uVer);
  };

  struct MsgLoginResponce {
    MsgHead head{.id=MsgId::Login};
    enum Error{
      Ok,
      Busy,
      PwdErr,
      NameErr,
      VerionErr,
    };
    Error result = Ok;
    std::string tip;
    MSGPACK_DEFINE(head, result, tip);
  };

  
  struct MsgOnline {
    MsgHead head{.id = MsgId::MsgOnlie};
    uint16_t u16count;
    std::vector<std::string> vecPlayerNickName;
    MSGPACK_DEFINE(head, u16count, vecPlayerNickName);
  };

  struct MsgFromWorld {
    MsgHead head{.id = MsgId::FromWorld};
    std::vector<uint8_t> vecByte;
    MSGPACK_DEFINE(head, vecByte);
  };

  struct MsgFromGame {
    MsgHead head{.id = MsgId::FromGame};
    std::vector<uint8_t> vecByte;
    MSGPACK_DEFINE(head, vecByte);
  };

  struct MsgEnterSingleScene {
    MsgHead head{.id = MsgId::EnterSingleScene};
    SceneID id;
    MSGPACK_DEFINE(head, id);
  };

  struct MsgEnterSpace {
    MsgHead head{.id = MsgId::EnterScene};
    uint32_t idSpace;
    MSGPACK_DEFINE(head, idSpace);
  };

  // unit 
  struct UnitConfig {
    std::string strName;
    std::string strPrefabName;
    std::string strChooseSound;
    MSGPACK_DEFINE(strName, strPrefabName, strChooseSound);
  };

  struct MsgAddRoleRet {
    MsgHead head{.id = AddRoleRet};
    uint64_t entitiId;
    std::string nickName;
    std::string entityName;
    std::string prefabName;
    int32_t i32HpMax;
    ObjectType type;
    MSGPACK_DEFINE(head, entitiId, nickName, entityName, prefabName, i32HpMax, type);
  };

  struct MsgNotifyPos {
    MsgHead msg{.id = NotifyPos};
    uint64_t entityId;
    float x;
    float z;
    int eulerAnglesY;
    int hp;
    MSGPACK_DEFINE(msg, entityId, x, z, eulerAnglesY, hp);
  };

  // 描述
  struct MsgEntityDes {
    MsgHead msg {.id = EntityDes};
    uint64_t entityId;
    std::string strDes;
    MSGPACK_DEFINE(msg, entityId, strDes);
  };

  // 剧情对话
  struct MsgPlotDialogue
  {
      MsgHead msg{ .id = PlotDialogue }; 
      std::string leftAvatar;      // 头像左
      std::string leftName;        // 名字左
      std::string rightAvatar;     // 头像右
      std::string rightName;       // 名字右
      std::string dialogueText;    // 对话内容
      bool showExitSceneButton;    // 显示退出场景按钮
      MSGPACK_DEFINE(msg, leftAvatar, leftName, rightAvatar, rightName, dialogueText, showExitSceneButton);
  };

  struct MsgPlotEnd {
    MsgHead msg{ .id = PlotEnd }; 
    MSGPACK_DEFINE(msg);
  };

  struct MsgPlaySound {
    MsgHead msg{ .id = PlaySound }; 
    std::string strSound;
    std::string strText;
    MSGPACK_DEFINE(msg, strSound, strText);
  };

  enum SayChannel
  {
    SYSTEM,   // 系统
    CHAT,     // 聊天
    TASK_TIP, // 任务提示
  };

  struct MsgSay {
    MsgHead msg {.id = Say};
    std::string content;
    SayChannel channel = SayChannel::SYSTEM;
    MSGPACK_DEFINE(msg, content, channel);
  };

}

MSGPACK_ADD_ENUM(game_def::SayChannel);
MSGPACK_ADD_ENUM(game_def::MsgId);
MSGPACK_ADD_ENUM(game_def::ObjectType);
MSGPACK_ADD_ENUM(game_def::SceneID);
MSGPACK_ADD_ENUM(game_def::MsgLoginResponce::Error);

struct msg_code_t {
	msgpack::object_handle oh;
	msgpack::object obj;

	void unpack(std::string_view data) {
    oh = msgpack::unpack(data.data(), data.size());
    obj = oh.get();
  }
  void unpack(uint8_t* p, size_t len) {
    oh = msgpack::unpack((char*)p, len);
    obj = oh.get();
  }

	game_def::MsgHead head(){
    if(obj.type != msgpack::type::ARRAY) {
      return {};
    }
    if (obj.via.array.size < 1) {
      return {};
    }
    return obj.via.array.ptr[0].as<game_def::MsgHead>();
  }

	template <typename T>
	T as() {
			return obj.as<T>();
	}

  template <typename Msg>
  static std::string pack(Msg&& msg) {
      std::stringstream ss;
      msgpack::pack(ss, std::forward<Msg>(msg));
      return std::move(ss).str();
  }
};
