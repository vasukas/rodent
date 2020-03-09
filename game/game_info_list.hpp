#ifndef OBJS_INFO_HPP
#define OBJS_INFO_HPP

#include "common_defs.hpp"

class  Entity;
class  ETeleport;
struct LevelCtrRoom;

struct TeleportInfo
{
	ETeleport& ent;
	const LevelCtrRoom& room;
	bool discovered = false;
	
	TeleportInfo(ETeleport& ent);
};

class GameInfoList
{
public:
	std::vector<TeleportInfo>& get_teleport_list() {return teleport_list;}
	size_t find_teleport(ETeleport& ent) const;
	
	std::optional<size_t> get_menu_teleport() const {return teleport_cur;}
	void set_menu_teleport(std::optional<size_t> i) {teleport_cur = i;}
	
private:
	std::vector<TeleportInfo> teleport_list;
	std::optional<size_t> teleport_cur;
	
	friend class GameCore_Impl;
	GameInfoList() = default;
	~GameInfoList() = default;
};

#endif // OBJS_INFO_HPP
