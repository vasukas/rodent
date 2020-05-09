#include "client/ec_render.hpp"
#include "game/game_core.hpp"
#include "game/game_mode.hpp"
#include "game/player_mgr.hpp"
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



void AtkPat_Boss::shoot(Entity& target, float, Entity& self)
{
	if (passed(self, t_reset)) set_stage(self, 0);
	
	seen_at = self.core.get_step_time();
	if (!pause && !stages[i_st].continious)
		self.ref_eqp().shoot(target.get_pos(), true, false);
}
void AtkPat_Boss::idle(Entity& self)
{
	if (passed(self, t_reset)) set_stage(self, 0);
	
	tmo -= self.core.step_len;
	if (pause)
	{
		if (tmo.is_negative())
			set_stage(self, i_st + 1);
	}
	else if (tmo.is_negative())
	{
		pause = true;
		tmo = stages[i_st].wait;
		self.ref_ai_drone().get_rot_ctl().speed_override.reset();
		
		auto& next = stages[(i_st + 1) % stages.size()];
		set_dist(self, next.opt_dist);
	}
	else if (stages[i_st].continious && !passed(self, t_stop))
	{
		vec2fp at;
		if (stages[i_st].targeted) {
			auto eid = std::get<AI_Drone::Battle>(self.ref_ai_drone().get_state()).grp->tar_eid;
			if (auto e = self.core.get_ent(eid)) at = e->get_pos();
			else {
				return;
			}
		}
		else {
			at = self.get_pos() + self.ref_pc().get_norm_dir() * 100; // don't shoot inside self
		}
		self.ref_eqp().shoot(at, true, false);
	}
}
void AtkPat_Boss::reset(Entity& self)
{
	self.ref_ai_drone().get_rot_ctl().speed_override.reset();
}
void AtkPat_Boss::set_dist(Entity& self, std::optional<float> dist)
{
	if (dist) self.ref_ai_drone().mut_pars().dist_optimal = *dist;
}
bool AtkPat_Boss::passed(Entity& self, TimeSpan t)
{
	return self.core.get_step_time() - seen_at > t;
}
void AtkPat_Boss::set_stage(Entity& self, size_t i)
{
	i_st = i % stages.size();	
	auto& stage = stages[i_st];
	
	pause = false;
	tmo = stage.len;
	self.ref_eqp().set_wpn(stage.i_wpn, true);
	
	self.ref_ai_drone().get_rot_ctl().speed_override = stage.rot_limit;
	set_dist(self, stage.opt_dist);
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




EEnemyDrone::Init EEnemyDrone::def_workr(GameCore& core)
{
	auto pars = []{
		static std::shared_ptr<AI_DroneParams> pars;
		if (!pars) {
			pars = std::make_shared<AI_DroneParams>();
			pars->set_speed(2, 3, 4);
			pars->dist_minimal = 3;
			pars->dist_optimal = 10;
			pars->dist_visible = 14;
			pars->dist_suspect = 16;
			pars->rot_speed = deg_to_rad(90);
			pars->helpcall = AI_DroneParams::HELP_LOW;
		}
		return pars;
	};
	
	Init init;
	init.pars = pars();
	init.model = MODEL_WORKER;
	
	static const auto cs = normalize_chances<Weapon*(*)(), 2>({{
		{[]()->Weapon*{return new WpnRocket;}, 1},
		{[]()->Weapon*{return new WpnSMG;}, 0.05}
	}});
	init.wpn.reset(core.get_random().random_el(cs)());
	
	init.atk_pat.reset(new AtkPat_Burst);
	init.drop_value = 0.4;
	init.is_worker = true;
	return init;
}
EEnemyDrone::Init EEnemyDrone::def_drone(GameCore& core)
{
	auto pars = []{
		static std::shared_ptr<AI_DroneParams> pars;
		if (!pars) {
			pars = std::make_shared<AI_DroneParams>();
			pars->set_speed(4, 7, 9);
			pars->dist_minimal = 8;
			pars->dist_optimal = 14;
			pars->dist_visible = 20;
			pars->dist_suspect = 22;
			pars->rot_speed = deg_to_rad(240);
			pars->fov = std::make_pair(deg_to_rad(45), deg_to_rad(90));
			pars->placement_prio = 5;
		}
		return pars;
	};
	
	Init init;
	init.pars = pars();
	init.model = MODEL_DRONE;
	
	static const auto cs = normalize_chances<Weapon*(*)(), 2>({{
		{[]()->Weapon*{return new WpnRocket;}, 0.6},
		{[]()->Weapon*{return new WpnSMG;}, 0.4}
	}});
	init.wpn.reset(core.get_random().random_el(cs)());
	
	init.drop_value = 0.7;
	return init;
}
EEnemyDrone::Init EEnemyDrone::def_campr(GameCore&)
{
	auto pars = []{
		static std::shared_ptr<AI_DroneParams> pars;
		if (!pars) {
			pars = std::make_shared<AI_DroneParams>();
			pars->set_speed(5, 6, 8);
			pars->dist_panic   = 6;
			pars->dist_minimal = 12;
			pars->dist_optimal = 18;
			pars->dist_visible = 24;
			pars->dist_suspect = 28;
			pars->dist_battle = 50;
			pars->rot_speed = deg_to_rad(180);
			pars->is_camper = true;
			pars->fov = {};
			pars->helpcall = AI_DroneParams::HELP_NEVER;
			pars->placement_prio = 30;
			pars->placement_freerad = 1;
		}
		return pars;
	};
	
	Init init;
	init.pars = pars();
	init.model = MODEL_CAMPER;
	init.wpn.reset(new WpnElectro(WpnElectro::T_CAMPER));
	init.atk_pat.reset(new AtkPat_Sniper);
	init.drop_value = 1;
	return init;
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
	logic(*this, init.pars, init_idle(init), std::move(init.atk_pat)),
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
	
	eqp.add_wpn(std::move(init.wpn));
}
EEnemyDrone::~EEnemyDrone()
{
	if (!core.is_freeing() && core.spawn_drop)
		EPickable::create_death_drop(core, get_pos(), drop_value);
}



EHunter::EHunter(GameCore& core, vec2fp at)
    :
	Entity(core),
	phy(*this, bodydef(at, true)),
	hlc(*this, 350),
	eqp(*this),
    logic(*this
	, []{
		// mutated by attack pattern
		/*static*/ std::shared_ptr<AI_DroneParams> pars;
		if (!pars) {
			pars = std::make_shared<AI_DroneParams>();
			pars->set_speed(5, 6, 9);
			pars->dist_minimal = 6;
			pars->dist_optimal = 14;
			pars->dist_visible = 24;
			pars->dist_suspect = 32;
			pars->rot_speed = deg_to_rad(120);
			pars->fov = {M_PI_2, M_PI};
			pars->helpcall = AI_DroneParams::HELP_NEVER;
			pars->placement_prio = 60;
			pars->placement_freerad = 4;
		}
		return pars;
	}()
	, AI_Drone::IdleChasePlayer{}
	, []{
		auto pat = std::make_unique<AtkPat_Boss>();
		{	auto& st = pat->stages.emplace_back();
			st.len  = TimeSpan::seconds(4);
			st.wait = TimeSpan::seconds(4);
			st.i_wpn = 1;
			st.rot_limit = deg_to_rad(45);
			st.opt_dist = 9;
		}
		{	auto& st = pat->stages.emplace_back();
			st.len  = TimeSpan::seconds(3);
			st.wait = TimeSpan::seconds(5);
			st.targeted = true;
			st.opt_dist = 16;
		}
		return pat;
	}()),
	mov(logic)
{
	ui_descr = "Hunter Drone";
	add_new<EC_RenderModel>(MODEL_HUNTER, FColor(1, 0, 0, 1), EC_RenderModel::DEATH_AND_EXPLOSION);
	phy.add(FixtureCreate::circle( fixtdef(0.2, 0.2), GameConst::hsz_drone_hunter, 80 ));
	
	hlc.add_filter(std::make_unique<DmgArmor>(300, 300));
	hlc.add_filter(std::make_unique<DmgShield>(250, 30, TimeSpan::seconds(6)));
	
	eqp.add_wpn(std::make_unique<WpnBarrage>());
	eqp.add_wpn(std::make_unique<WpnUber>());
	eqp.no_overheat = true;
	
	logic.always_online = true;
	
	ref<EC_RenderPos>().parts(FE_SPAWN, {{}, GameConst::hsz_drone_hunter});
	ref<EC_RenderModel>().parts(ME_AURA, {{}, 1, FColor(0, 0, 1, 2)});
}
EHunter::~EHunter()
{
	if (!core.is_freeing() && core.spawn_drop)
	{
		for (int i=0; i<8; ++i) {
			float rot = core.get_random().range_n2() * M_PI;
			float len = core.get_random().range(0, GameConst::cell_size /2);
			EPickable::create_death_drop(core, get_pos() + vec2fp(len, 0).rotate(rot), 2);
		}
		
		ref<EC_RenderPos>().parts(FE_WPN_EXPLOSION, {{}, 3.f});
		ref<EC_RenderPos>().parts(FE_CIRCLE_AURA,   {{}, 6.f, FColor(0.2, 0.8, 1, 1.5)});
		ref<EC_RenderModel>().parts(ME_AURA, {{}, 1, FColor(0, 1, 1, 2)});
	}
}



EHacker::EHacker(GameCore& core, vec2fp at)
    :
	Entity(core),
	phy(*this, bodydef(at, true)),
	hlc(*this, 50),
	eqp(*this),
    logic(*this
	, []{
		static std::shared_ptr<AI_DroneParams> pars;
		if (!pars) {
			pars = std::make_shared<AI_DroneParams>();
			pars->set_speed(5, 5, 5);
			pars->dist_minimal = 0;
			pars->dist_optimal = 0;
			pars->dist_visible = 0;
			pars->dist_suspect = 0;
			pars->helpcall = AI_DroneParams::HELP_NEVER;
		}
		return pars;
	}()
	, []{
		return AI_Drone::IdleResource{{ AI_SimResource::T_LEVELTERM, false, 0, AI_SimResource::max_capacity }};
	}()
	, {}),
	mov(logic)
{
	ui_descr = "Hacker";
	add_new<EC_RenderModel>(MODEL_HACKER, FColor(1, 0, 1, 1), EC_RenderModel::DEATH_AND_EXPLOSION);
	phy.add(FixtureCreate::circle( fixtdef(0.2, 0.2), GameConst::hsz_drone, 40 ));
	
	hlc.add_filter(std::make_unique<DmgShield>(200, 50, TimeSpan::seconds(4)));
	
	eqp.add_wpn(std::make_unique<WpnSMG>());
	
	logic.always_online = true;
	logic.ignore_battle = true;
	reg_this();
}
void EHacker::step()
{
	if (auto gst = std::get_if<AI_Drone::Idle>(&logic.get_state()))
	{
		auto& st = std::get<AI_Drone::IdleResource>(gst->ist);
		if (st.is_working_now)
		    dynamic_cast<GameMode_Normal&>(core.get_gmc()).hacker_work();
	}
	else logic.set_idle_state();
}
