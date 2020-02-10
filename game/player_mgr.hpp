#ifndef PLAYER_MGR_HPP
#define PLAYER_MGR_HPP

#include "entity.hpp"

class  PlayerController;
struct WeaponMsgReport;



class PlayerManager
{
public:
	bool cheat_ammo    = false;
	bool cheat_godmode = false;
	bool fastforward = false; ///< see usages
	
	static PlayerManager* create(std::shared_ptr<PlayerController> pc_ctr);
	virtual ~PlayerManager() = default;
	
	virtual Entity* get_ent() = 0; ///< If exists
	virtual bool is_player(Entity* ent) const = 0;
	
	virtual void render(TimeSpan passed, vec2i mou_pos) = 0; ///< May be called not at each cycle
	virtual void update_cheats() = 0; ///< Call if changed any of cheat flags
	
	/// Current AI activation and deactivation rects
	virtual std::pair<Rectfp, Rectfp> get_ai_rects() = 0;
	
	/// Increments objective count
	virtual void inc_objective() = 0;
	
	virtual bool is_game_finished() = 0;
	
protected:
	friend class GameCore_Impl;
	virtual void step() = 0;
};

#endif // PLAYER_MGR_HPP
