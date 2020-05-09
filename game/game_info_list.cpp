#include "game_objects/objs_basic.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "game_info_list.hpp"
#include "level_ctr.hpp"

TeleportInfo::TeleportInfo(ETeleport& ent)
    : ent(ent), room(*[&]{
		auto r = ent.core.get_lc().get_room(ent.get_pos());
		if (!r) throw std::runtime_error("TeleportInfo:: no room");
		return r;
	}())
{}

size_t GameInfoList::find_teleport(ETeleport& ent) const {
	auto it = std::find_if(teleport_list.begin(), teleport_list.end(), [&](auto& v){return &v.ent == &ent;});
	if (it == teleport_list.end()) THROW_FMTSTR("GameInfoList::find_teleport() failed - {}", ent.dbg_id());
	return std::distance(teleport_list.begin(), it);
}
