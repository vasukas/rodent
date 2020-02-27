#include "client/ec_render.hpp"
#include "game/game_core.hpp"
#include "game_ai/ai_algo.hpp"
#include "utils/noise.hpp"
#include "objs_basic.hpp"
#include "objs_creature.hpp"
#include "weapon_all.hpp"



void AtkPat_Sniper::shoot(Entity& target, float, Entity& self)
{
	float t = 0;
	if (auto ui = self.ref_eqp().get_wpn().get_ui_info())
		t = ui->charge_t.value_or(0.f);
	
	bool charged  = t > 0.99;
	
	has_tar = true;
	if (!charged)
		p_tar = target.get_pos();
}
void AtkPat_Sniper::idle(Entity& self)
{
	AI_Drone& drone = self.ref_ai_drone();
	EC_Equipment& eqp = self.ref_eqp();
	
	float t = 0;
	if (auto ui = eqp.get_wpn().get_ui_info())
		t = ui->charge_t.value_or(0.f);
	
	bool charged  = t > 0.99;
	bool shooting = t > 0.f || has_tar;
	bool visible = has_tar;
	has_tar = false;

	//
	
	if (auto mov = drone.mov)
		mov->locked = charged;
	
	auto& rotover = drone.get_rot_ctl().speed_override;
	if (charged && visible) rotover = rot_speed;
	else rotover.reset();
	
	if (shooting) {
		eqp.shoot(p_tar, true, false);
		if (eqp.get_wpn().get_reload_timeout())
			shooting = false;
	}
	
	//
	
	auto laser = &self.ensure<EC_LaserDesigRay>();
	laser->enabled = shooting;
	if (laser->enabled) 
	{
		if (visible) {
			laser->clr = charged ? 0xffffffff : 0xffff00ff;
//			laser->find_target( p_tar - self.get_pos() );
		}
		else {
			laser->clr = charged ? 0xffffffff : 0x00ff00ff;
//			laser->find_target( self.ref_pc().get_norm_dir() );
		}
		laser->find_target( self.ref_pc().get_norm_dir() );
	}
}
void AtkPat_Sniper::reset(Entity& self)
{
	auto laser = &self.ensure<EC_LaserDesigRay>();
	laser->enabled = false;
	self.ref_ai_drone().get_rot_ctl().speed_override.reset();
}



void AtkPat_Burst::shoot(Entity& target, float, Entity& self)
{
	if (shooting)
		self.ref_eqp().shoot(target.get_pos(), true, false);
}
void AtkPat_Burst::idle(Entity& self)
{
	if (crowd)
	{
		if (crowd_tmo.is_positive()) crowd_tmo -= GameCore::step_len;
		else {
			crowd_bots = -1; // exclude this
			area_query(self.core, self.get_pos(), crowd->radius, [&](auto&){ ++crowd_bots; return true; });
		}
	}
	
	if (tmo.is_positive()) tmo -= GameCore::step_len;
	else {
		shooting = !shooting;
		
		if    (shooting) tmo = burst;
		else if (!crowd) tmo = wait;
		else {
			float t = clampf_n(float(crowd_bots) / crowd->max_bots);
			tmo = lerp(wait, crowd->max_wait, t);
		}
		tmo += TimeSpan::seconds( self.core.get_random().range(0, 0.2) );
	}
}
void AtkPat_Burst::reset(Entity&)
{
	crowd_tmo = tmo = {};
	shooting = false;
}



static std::shared_ptr<AI_DroneParams> pars_turret()
{
	static std::shared_ptr<AI_DroneParams> pars;
	if (!pars) {
		pars = std::make_shared<AI_DroneParams>();
		pars->dist_suspect = pars->dist_visible = 24;
		pars->dist_battle = 40;
	}
	return pars;
}
ETurret::ETurret(GameCore& core, vec2fp at, size_t team)
	:
	Entity(core),
	phy(*this, bodydef(at, false)),
	hlc(*this, 400),
    eqp(*this),
    logic(*this, pars_turret(), {}, {}),
    team(team)
{
	ui_descr = "Turret";
	add_new<EC_RenderModel>(MODEL_BOX_SMALL, FColor(1, 0, 1, 1), EC_RenderModel::DEATH_AND_EXPLOSION);
	phy.add(FixtureCreate::circle( fixtdef(0.2, 0), GameConst::hsz_box_small, 0 ));
	eqp.add_wpn(std::make_unique<WpnMinigunTurret>());
}



static AI_Drone::IdleState init_idle(EEnemyDrone::Init& init)
{
	if (init.is_worker) return AI_Drone::IdleResource{{ AI_SimResource::T_ROCK, false, 0, 20 }};
	if (init.patrol.size() > 1) return AI_Drone::IdlePatrol{ std::move(init.patrol) };
	return {};
}
EEnemyDrone::EEnemyDrone(GameCore& core, vec2fp at, Init init)
	:
	Entity(core),
	phy(*this, bodydef(at, true)),
	hlc(*this, 70),
	eqp(*this),
	logic(*this, init.pars, init_idle(init), std::unique_ptr<AI_AttackPattern>(init.atk_pat)),
	mov(logic),
	drop_value(init.drop_value)
{
	ui_descr = "Drone";
	add_new<EC_RenderModel>(init.model, FColor(1, 0, 0, 1), EC_RenderModel::DEATH_AND_EXPLOSION);
	phy.add(FixtureCreate::circle( fixtdef(0.3, 0.4), GameConst::hsz_drone * 1.4, 25 ));  // sqrt2 - diagonal
	
	hlc.add_filter(std::make_unique<DmgShield>(100, 20, TimeSpan::seconds(5)));
//	hlc.ph_thr = 100;
//	hlc.ph_k = 0.2;
//	hlc.hook(phy);
	
	eqp.add_wpn( std::unique_ptr<Weapon>(init.wpn) );
}
EEnemyDrone::~EEnemyDrone()
{
	if (!core.is_freeing() && core.spawn_drop)
		EPickable::create_death_drop(core, get_pos(), drop_value);
}
