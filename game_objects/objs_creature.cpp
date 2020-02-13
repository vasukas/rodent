#include "client/effects.hpp"
#include "game/game_core.hpp"
#include "game_ai/ai_algo.hpp"
#include "utils/noise.hpp"
#include "objs_creature.hpp"
#include "weapon_all.hpp"



AtkPat_Sniper::AtkPat_Sniper()
{
	laser = LaserDesigRay::create();
	laser->is_enabled = true;
}
AtkPat_Sniper::~AtkPat_Sniper()
{
	laser->destroy();
}
void AtkPat_Sniper::shoot(Entity& target, float, Entity& self)
{
	float t = 0;
	if (auto ui = self.get_eqp()->get_wpn().get_ui_info())
		t = ui->charge_t.value_or(0.f);
	
	bool charged  = t > 0.99;
	
	has_tar = true;
	if (!charged)
		p_tar = target.get_pos();
}
void AtkPat_Sniper::idle(Entity& self)
{
	float t = 0;
	if (auto ui = self.get_eqp()->get_wpn().get_ui_info())
		t = ui->charge_t.value_or(0.f);
	
	bool charged  = t > 0.99;
	bool shooting = t > 0.f || has_tar;
	bool visible = has_tar;
	has_tar = false;

	//
	
	if (auto mov = self.get_ai_drone()->mov) mov->locked = charged;
	
	auto& rotover = self.get_ai_drone()->get_ren_rot().speed_override;
	if (charged && visible) rotover = self.get_ai_drone()->get_pars().rot_speed * rotation_k;
	else rotover.reset();
	
	if (shooting) {
		self.get_eqp()->shoot(p_tar, true, false);
		if (self.get_eqp()->get_wpn().get_reload_timeout())
			shooting = false;
	}
	
	//
	
	laser->is_enabled = shooting;
	if (laser->is_enabled) 
	{
		laser->src_eid = self.index;
		if (visible) {
			laser->clr = charged ? 0xffffffff : 0xffff00ff;
			laser->find_target( p_tar - self.get_pos() );
		}
		else {
			laser->clr = charged ? 0xffffffff : 0x00ff00ff;
			laser->find_target( vec2fp{1,0}.fastrotate(self.get_face_rot()) );
		}
	}
}
void AtkPat_Sniper::reset(Entity& self)
{
	laser->src_eid = {};
	self.get_ai_drone()->get_ren_rot().speed_override.reset();
}



void AtkPat_Burst::shoot(Entity& target, float, Entity& self)
{
	if (shooting)
		self.get_eqp()->shoot(target.get_pos(), true, false);
}
void AtkPat_Burst::idle(Entity& self)
{
	if (crowd)
	{
		if (crowd_tmo.is_positive()) crowd_tmo -= GameCore::step_len;
		else {
			crowd_bots = -1; // exclude this
			area_query(self.get_pos(), crowd->radius, [&](auto&){ ++crowd_bots; return true; });
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
		tmo += TimeSpan::seconds( GameCore::get().get_random().range(0, 0.2) );
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
ETurret::ETurret(vec2fp at, size_t team)
	:
	phy(this, [&]{
		b2BodyDef bd;
		bd.position = conv(at);
		bd.fixedRotation = true;
		return bd;
	}()),
    ren(this, MODEL_BOX_SMALL, FColor(1, 0, 1, 1)),
    hlc(this, 400),
    eqp(this),
    logic(this, pars_turret(), {}, {}),
    team(team)
{
	b2FixtureDef fd;
	phy.add_circle(fd, GameConst::hsz_box_small, 1);
	
	eqp.add_wpn(new WpnMinigunTurret);
}



static AI_Drone::IdleState init_idle(EEnemyDrone::Init& init)
{
	if (init.is_worker) return AI_Drone::IdleResource{{ AI_SimResource::T_ROCK, false, 0, 20 }};
	if (init.patrol.size() > 1) return AI_Drone::IdlePatrol{ std::move(init.patrol) };
	return {};
}
EEnemyDrone::EEnemyDrone(vec2fp at, Init init)
	:
	phy(this, [&]{
        b2BodyDef def;
		def.position = conv(at);
		def.type = b2_dynamicBody;
		return def;
	}()),
	ren(this, init.model, FColor(1, 0, 0, 1)),
	hlc(this, 70),
	eqp(this),
    logic(this, init.pars, init_idle(init), std::unique_ptr<AI_AttackPattern>(init.atk_pat)),
	mov(logic),
	drop_value(init.drop_value)
{
	b2FixtureDef fd;
	fd.friction = 0.3;
	fd.restitution = 0.4;
	phy.add_circle(fd, GameConst::hsz_drone * 1.4, 25); // sqrt2 - diagonal
	
	hlc.add_filter(std::make_shared<DmgShield>(100, 20, TimeSpan::seconds(5)));
//	hlc.ph_thr = 100;
//	hlc.ph_k = 0.2;
//	hlc.hook(phy);
	
	eqp.add_wpn( init.wpn );
}
EEnemyDrone::~EEnemyDrone()
{
	if (!GameCore::get().is_freeing() && GameCore::get().spawn_drop)
		EPickable::death_drop(get_pos(), drop_value);
}
