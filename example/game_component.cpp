#include "game_component.h"
#include "msg_define.h"
#include <utility>

void static_interface::npc1_say(scene_t& S, 
  std::string_view player,
  std::string leftAvatar, 
  std::string leftName,
  std::string rightAvatar, 
  std::string rightName,
  std::string dialogueText,
  bool bShowQuit
) {
  game_def::MsgPlotDialogue ntf = {
    .leftAvatar = std::move(leftAvatar),
    .leftName = std::move(leftName),
    .rightAvatar = std::move(rightAvatar),
    .rightName = std::move(rightName),
    .dialogueText = dialogueText
  };

  auto data = msg_code_t::pack(ntf);
  auto connid = S.game->get_player_connid(player);
  S.game->write_data(connid, data);
  auto e = S.geteid_fromconnid(connid);
  auto p = e.component<PlayerConn>();
  if (p) {
    p->msgLastMsg = ntf;
  }
}

void static_interface::play_sound(scene_t& S,std::string_view player, std::string sound, std::string text) {
  auto eids = S.entity_mgr.entities_with_components<PlayerConn>(S.entitys);
  game_def::MsgPlaySound  msg{
    .strSound = std::move(sound),
    .strText = std::move(text)
  };
  auto connid = S.game->get_player_connid(player);
  S.game->write_notify(connid, msg);
}

void static_interface::plotend(scene_t& S,std::string_view player) {
  auto eids = S.entity_mgr.entities_with_components<PlayerConn>(S.entitys);
  game_def::MsgPlotEnd msg;
  auto connid = S.game->get_player_connid(player);
  S.game->write_notify(connid, msg);
}

void static_interface::say(scene_t& S,std::string_view player, std::string content, game_def::SayChannel channel) {
  game_def::MsgSay msg {
    .content = std::move(content),
    .channel = channel
  };
  auto connid = S.game->get_player_connid(player);
  S.game->write_notify(connid, msg);
}