#ifndef PLAYER_MGR_HPP
#define PLAYER_MGR_HPP

#include "entity.hpp"

class PlayerInput;
class PlayerUI;

class File;



struct PlayerNetworkHUD {
	uint8_t shlc_state; // ShieldControl::State
	float shlc_t; // log.shlc.get_ft()->get_hp().t_state()
	TimeSpan shlc_time;
	uint16_t shlc_hp, shlc_hpmax;
	
	float hlc_t; // hlc.get_hp().t_state()
	uint16_t hlc_hp, hlc_hpmax;
	
	bool t_accel_enabled;
	float t_accel; // -1 if infinite; log.pmov.get_t_accel()
	
	float arm_t;
	uint16_t arm_hp, arm_hpmax;
	bool arm_atmax; // arm_t >= log.armor->maxmod_t
	
	float pers_t;
	uint16_t pers_hp, pers_hpmax;
	bool pers_shld_alive; // log.pers_shld->get_hp().is_alive()
	
	struct WpnInfo {
		uint8_t icon;
		uint16_t ammo, ammomax;
	};
	std::vector<WpnInfo> wpns;
	uint8_t wpncur;
	
	bool wpn_reload_show;
	float wpn_reload; // -1 if none
	
	float wpn_overheat; // -1 if unused
	bool wpn_overheat_ok;
	
	float wpn_charge; // -1 if unused
	
	
	void write(Entity& ent, File& f);
	void read(File& f);
};



class PlayerManager
{
public:
	bool cheat_ammo    = false;
	bool cheat_godmode = false;
	bool is_superman   = false;
	bool fastforward   = false; ///< see usages
	bool debug_ai_rect = false;
	
	std::optional<std::pair<EntityIndex, PlayerNetworkHUD>> nethack_client;
	std::vector<std::pair<EntityIndex, PlayerInput*>> nethack_server;
	
	static PlayerManager* create(GameCore& core);
	virtual ~PlayerManager() = default;
	
	virtual Entity* get_ent() = 0; ///< If exists
	virtual Entity& ref_ent() = 0; ///< Throws if doesn't exist
	virtual bool is_player(Entity& ent) const = 0;
	virtual PlayerUI* get_pui() = 0;
	
	virtual void render(TimeSpan passed, vec2i cursor_pos) = 0;
	
	/// Call it if changed any of cheat flags
	virtual void update_cheats() = 0;
	
	/// Current AI activation and deactivation rects
	virtual std::pair<Rectfp, Rectfp> get_ai_rects() = 0;
	
	/// Expected to be called only once
	virtual void set_pui(std::unique_ptr<PlayerUI> pui) = 0;
	
	///
	virtual vec2fp get_last_pos() = 0;
	
protected:
	friend class GameCore_Impl;
	virtual void step() = 0;
};

#endif // PLAYER_MGR_HPP
