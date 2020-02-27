#ifndef PLAYER_MGR_HPP
#define PLAYER_MGR_HPP

#include "entity.hpp"

class PlayerController;
class PlayerUI;



class PlayerManager
{
public:
	bool cheat_ammo    = false;
	bool cheat_godmode = false;
	bool is_superman   = false;
	bool fastforward = false; ///< see usages
	
	static PlayerManager* create(GameCore& core);
	virtual ~PlayerManager() = default;
	
	virtual Entity* get_ent() = 0; ///< If exists
	virtual Entity& ref_ent() = 0; ///< Throws if doesn't exist
	virtual bool is_player(Entity& ent) const = 0;
	
	virtual void render(TimeSpan passed, vec2i cursor_pos) = 0;
	
	/// Call it if changed any of cheat flags
	virtual void update_cheats() = 0;
	
	/// Current AI activation and deactivation rects
	virtual std::pair<Rectfp, Rectfp> get_ai_rects() = 0;
	
	/// Increments objective count
	virtual void inc_objective() = 0;
	
	///
	virtual bool is_game_finished() = 0;
	
	///
	virtual void set_ctr(std::shared_ptr<PlayerController> pc_ctr) = 0;
	
	/// Expected to be called only once
	virtual void set_pui(std::unique_ptr<PlayerUI> pui) = 0;
	
protected:
	friend class GameCore_Impl;
	virtual void step() = 0;
};

#endif // PLAYER_MGR_HPP
