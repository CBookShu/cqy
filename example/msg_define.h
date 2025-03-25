 #pragma once
 #include "msgpack/v3/object_fwd_decl.hpp"
#include "ylt/reflection/user_reflect_macro.hpp"
#include <cstdint>
#include <msgpack.hpp>
#include <vector>

namespace game_def {
  enum MsgId {
    MsgId_Invalid_0,
    Login = 1,
    
    EnterSingleScene = 24,

    MsgOnlie = 40,
    FromGame = 41,
    FromWorld = 42,
    BuildingCenter = 43,
    PlaySound = 44,
  };

  enum ObjectType {
    Object_Invalid_0,


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
    MsgHead head;
    enum Error{
      Ok,
      Busy,
      PwdErr,
      NameErr,
      VerionErr,
    };
    Error result;
    std::string tip;
    MSGPACK_DEFINE(head, result, tip);
  };

  
  struct MsgOnline {
    MsgHead head;
    uint16_t u16count;
    std::vector<std::string> vecPlayerNickName;
    MSGPACK_DEFINE(head, u16count, vecPlayerNickName);
  };

  struct MsgFromWorld {
    MsgHead head;
    std::vector<uint8_t> vecByte;
    MSGPACK_DEFINE(head, vecByte);
  };

  struct MsgFromGame {
    MsgHead head;
    std::vector<uint8_t> vecByte;
    MSGPACK_DEFINE(head, vecByte);
  };

  struct MsgEnterSingleScene {
    MsgHead head;
    SceneID id;
    MSGPACK_DEFINE(head, id);
  };

}

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
