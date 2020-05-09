#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "client/ec_render.hpp"
#include "game/weapon.hpp"
#include "utils/time_utils.hpp"

class PlayerInput;
class PlayerEntity;



struct PlayerMovement : EComp
{
	bool cheat_infinite = false; ///< Hack - if true, acceleration is always available
	
	
	PlayerMovement(PlayerEntity& ent);
	
	/// Returns if acceleration can be enabled and current charge value
	std::pair<bool, float> get_t_accel() const {return {acc_flag, clampf_n(acc_val)};}
	
	/// Resets speed gain
	void battle_trigger() {peace_tmo = c_peace_tmo;}
	
	///
	bool is_infinite_accel() const {return cheat_infinite || !peace_tmo.is_positive();}
	
private:
	const float spd_shld = 8; // with shield
	const float spd_norm = 12; // default (base)
	const float spd_accel = 24;
	const float max_mov_speed = spd_accel;
	
	vec2fp prev_dir = {}; // last used move direction
	SmoothSwitch accel_ss;
	
	static constexpr TimeSpan c_peace_tmo = TimeSpan::seconds(16);
	static constexpr TimeSpan c_peace_tmo_short = TimeSpan::seconds(5); // when no enemies around
	TimeSpan peace_tmo;
	bool peace_tmo_was_zero = true;
	std::unique_ptr<EC_ParticleEmitter::Channel> peace_parts;
	
	bool acc_flag = true;
	float acc_val = 1;
	const float acc_incr = 0.15; // per second
	const float acc_decr = 0.7;
	const float acc_on_thr = 0.4;
	
	vec2fp tar_dir = {};
	float inert_t = 0;
	
	float inert_reduce = 0;
	const float inert_reduce_decr = 0.5; // per second
	
	friend struct EC_PlayerLogic;
	void upd_vel(Entity& ent, vec2fp dir, bool is_accel, vec2fp look_pos); ///< Must be called exactly once per step
	void step() override;
};



struct ShieldControl
{
	static constexpr TimeSpan activate_time = TimeSpan::seconds(0.7);
	static constexpr TimeSpan dead_time     = TimeSpan::seconds(6);
	
	enum State
	{
		ST_DEAD,
		ST_DISABLED,
		ST_SWITCHING,
		ST_ACTIVE
	};
	
	ShieldControl(Entity& ent);
	
	DmgShield* get_ft() {return sh;}
	std::pair<State, TimeSpan> get_state() {return {state, tmo};}
	
private:
	Entity& ent;
	const Transform tr;
	DmgShield* sh;
	
	State state = ST_DISABLED;
	TimeSpan tmo;
	
	friend struct EC_PlayerLogic;
	bool step(bool sw_state); ///< Gets switch status, returns new switch status
};



struct EC_PlayerLogic : EComp
{
	PlayerMovement pmov;
	ShieldControl  shlc;
	
	DmgShield* pers_shld; // never null - set in spawn_player()
	DmgArmor*  armor;
	
	EC_PlayerLogic(PlayerEntity& ent);
	
private:
	EVS_SUBSCR;
	
	const float min_tar_dist = 1.2; // minimal target distance
	vec2fp prev_tar; // used if current dist < minimal
	
	const int   col_dmg_val = 90; // base collision damage
	const float col_dmg_spd_min = 20; // minimal speed for collision damage
	const int   col_dmg_restore = 90; // shield restore
	
	float shld_restore_left = 0;
	float shld_restore_rate = 120; // per second
	b2Fixture* ram_sensor;
	
	const vec2fp enemy_radius = {55.f, 45.f}; // radius in which enemies are detected
	
	void on_cnt(const CollisionEvent& ev); // ram
	void on_dmg(const DamageQuant& dq);
	
	friend class PlayerManager_Impl;
	void m_step();
};



class PlayerEntity final : public Entity
{
public:
	EC_Physics     phy;
	EC_Health      hlc;
	EC_Equipment   eqp;
	EC_PlayerLogic log;
	
	PlayerEntity(GameCore& core, vec2fp pos, bool is_superman);
	~PlayerEntity();
	size_t get_team() const override {return TEAM_PLAYER;}

	EC_Equipment* get_eqp() override {return &eqp;}
	EC_Health*    get_hlc() override {return &hlc;}
	EC_Position&  ref_pc()  override {return phy;}
};

#endif // PLAYER_HPP
