#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "client/presenter.hpp"
#include "utils/time_utils.hpp"
#include "game/entity.hpp"
#include "game/physics.hpp"
#include "game/weapon.hpp"

class PlayerController;



struct PlayerRender : ECompRender
{
	struct Attach
	{
		ModelType model = MODEL_NONE;
		Transform at;
		FColor clr;
	};
	std::array<Attach, ATT_TOTAL_COUNT> atts;
	
	float angle = 0.f; // override
	
	bool show_ray = false; // laser
	std::string dbg_info;
	
	
	PlayerRender(Entity* ent);
	void set_ray_tar(vec2fp new_tar);
	
private:
	vec2fp tar_ray = {};
	std::optional<std::pair<vec2fp, vec2fp>> tar_next;
	float tar_next_t;
	
	std::string dbg_info_real;
	
	void on_destroy() override;
	void step() override;
	void proc(PresCommand c) override;
	void sync() override;
};



struct PlayerMovement : EComp
{
	bool accel_inf = false; ///< Hack - if true, acceleration is always available
	
	PlayerMovement(Entity* ent);
	void upd_vel(vec2fp dir, bool is_accel, vec2fp look_pos); ///< Must be called exactly once per step
	
	/// Returns if acceleration can be enabled and current charge value
	std::pair<bool, float> get_t_accel() const {return {acc_flag, clampf_n(acc_val)};}
	
private:
	const float spd_shld = 7; // with shield
	const float spd_norm = 10; // default
	const float spd_accel = 20;
	const float max_mov_speed = spd_accel;
	
	vec2fp prev_dir = {}; // last used move direction
	SmoothSwitch accel_ss;
	
	bool acc_flag = true;
	float acc_val = 1;
	const float acc_incr = 0.1; // per second
	const float acc_decr = 0.4;
	const float acc_on_thr = 0.6;
	
	vec2fp tar_dir = {};
	float inert_t = 0;
	
	void step() override;
};



struct ShieldControl
{
	ShieldControl(Entity& root);
	void enable();
	void disable();
	
	DmgShield* get_ft() {return sh.get();}
	bool is_enabled() {return !is_dead && sh->enabled;}
	std::optional<TimeSpan> get_dead_tmo();
	
	/// Gets switch status, returns new switch status
	bool step(bool sw_state);
	
private:
	Entity& root;
	size_t armor_index;
	const Transform tr;
	std::shared_ptr<DmgShield> sh;
	b2Fixture* fix = nullptr;
	
	bool is_dead = true;
	TimeSpan tmo;
	TimeSpan inact_time;
};



struct PlayerLogic : EComp
{
	ShieldControl shlc;
	
	PlayerLogic(Entity* ent, std::shared_ptr<PlayerController> ctr_in);
	
private:
	struct FI_Sensor : FixtureInfo {};
	
	EVS_SUBSCR;
	std::shared_ptr<PlayerController> ctr;
	
	const float min_tar_dist = 1.2; // minimal target distance
	vec2fp prev_tar; // used if current dist < minimal
	
	const float col_dmg_radius = 1; // additional radius for collision sensor
	const int   col_dmg_val = 60; // base collision damage
	const float col_dmg_spd_min = 15; // minimal speed for collision damage
	const float col_dmg_spd_mul = 1 / 10.f; // speed -> damage increase multiplier
	const int   col_dmg_restore = 120; // base shield restore
	
	float shld_restore_left = 0;
	float shld_restore_rate = 100; // per second
	
	void on_cnt(const CollisionEvent& ev);
	void step() override;
};



class PlayerEntity final : public Entity
{
public:
	EC_Physics     phy;
	PlayerRender   ren;
	PlayerMovement mov;
	EC_Health      hlc;
	EC_Equipment   eqp;
	PlayerLogic    log;
	float rot = 0.f; // rotation override
	
	std::shared_ptr<DmgShield> pers_shld;
	std::shared_ptr<DmgArmor> armor;
	
	PlayerEntity(vec2fp pos, std::shared_ptr<PlayerController> ctr);
	std::string ui_descr() const override {return "Player";}
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	EC_Equipment* get_eqp() override {return &eqp;}
	size_t get_team() const override {return TEAM_PLAYER;}
};

#endif // PLAYER_HPP
