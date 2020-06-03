#ifndef OBJS_INFO_HPP
#define OBJS_INFO_HPP

#include "entity.hpp"

class  ETeleport;
struct LevelCtrRoom;

struct TeleportInfo
{
	ETeleport& ent;
	const LevelCtrRoom& room;
	bool discovered = false;
	
	TeleportInfo(ETeleport& ent);
};

struct BotAssemblerInfo
{
	EntityIndex eid;
	vec2fp prod_pos;
};

class GameInfoList
{
public:
	std::vector<TeleportInfo>& get_teleport_list() {return teleport_list;}
	size_t find_teleport(ETeleport& ent) const;
	
	std::optional<size_t> get_menu_teleport() const {return teleport_cur;}
	void set_menu_teleport(std::optional<size_t> i) {teleport_cur = i;}
	
	std::vector<BotAssemblerInfo>& get_assembler_list() {return assembler_list;}
	
	enum EventType {
		STAT_DAMAGE_RECEIVED_PLAYER,
		STAT_DAMAGE_RECEIVED_BOTS,
		STAT_DAMAGE_BLOCKED,
		STAT_DEATHS_PLAYER,
		STAT_DEATHS_BOTS,
		STAT_DEATHS_BOSSES,
		STAT_SPAWN_BOTS_ALL,
		STAT_MOVED_NORMAL,
		STAT_MOVED_ACCEL,
		STAT__TOTAL_COUNT
	};
	void stat_event(EventType ev, float increase = 1) {stat_event_vals[ev] += increase;}
	float get_stat_event(EventType ev) const {return stat_event_vals[ev];}
	
private:
	std::vector<TeleportInfo> teleport_list;
	std::optional<size_t> teleport_cur;
	std::vector<BotAssemblerInfo> assembler_list;
	std::array<float, STAT__TOTAL_COUNT> stat_event_vals = {};
	
	friend class GameCore_Impl;
	GameInfoList() = default;
	~GameInfoList() = default;
};

#endif // OBJS_INFO_HPP
