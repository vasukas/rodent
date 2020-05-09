#include "client/plr_input.hpp"
#include "client/presenter.hpp"
#include "game/game_core.hpp"
#include "game_ai/ai_control.hpp"
#include "vaslib/vas_log.hpp"
#include "objs_player.hpp"
#include "weapon_all.hpp"



PlayerMovement::PlayerMovement(PlayerEntity& ent)
	: EComp(ent)
{
	reg(ECompType::StepPostUtil);
	
	accel_ss.min_sus = TimeSpan::seconds(0.2);
	peace_parts = ent.ensure<EC_ParticleEmitter>().new_channel();
}
void PlayerMovement::upd_vel(Entity& ent, vec2fp dir, bool is_accel, vec2fp look_pos)
{
	const float zero_thr = 0.001;
	const float slide_k = 0.3;
	
	auto& self = static_cast<PlayerEntity&>(ent);
	
	// get sidecol
	
	const float off = GameConst::hsz_rat - 0.05;
	const float len = GameConst::hsz_rat - 0.1;
	const float wid = max_mov_speed * GameCore::time_mul + 0.1;
	bool side_col[4] = {};
	
	for (int i=0; i<4; ++i)
	{
		auto sz = i&1? vec2fp(len, wid) : vec2fp(wid, len);
		auto pos = vec2fp(off, 0).rotate( M_PI_2 * i );
		pos += ent.get_pos();
		
		ent.core.get_phy().query_aabb(Rectfp::from_center(pos, sz),
		[&](auto&, b2Fixture& fix) {
			auto d = get_info(fix);
			if (d && (d->typeflags & FixtureInfo::TYPEFLAG_WALL)) {
				side_col[i] = true;
				return false;
			}
			return true;
		});
	}
	
	// prepare
	
	if (is_infinite_accel()) {
		acc_flag = true;
		acc_val = 1;
	}
	
	accel_ss.step(GameCore::step_len, is_accel && acc_flag);
	is_accel = !accel_ss.is_zero();
	
	std::optional<float> spd;
	if (self.log.shlc.get_state().first == ShieldControl::ST_ACTIVE)
	{
		is_accel = false;
		spd = spd_shld;
	}
	
	if (is_accel) {
		if (self.phy.get_vel().len_squ() > spd_norm*spd_norm)
			acc_val -= acc_decr * GameCore::time_mul;
		if (acc_val < 0) acc_flag = false;
	}
	else {
		acc_val = std::min(acc_val + acc_incr * GameCore::time_mul, 1.f);
		if (acc_val > acc_on_thr) acc_flag = true;
	}
	
	if (!spd) spd = is_accel ? spd_accel : spd_norm;
	if (is_accel && dir.len_squ() < zero_thr) dir = prev_dir;
	
	// pgain
	
	if (peace_tmo_was_zero && peace_tmo >= TimeSpan::seconds(2))
	{
		peace_tmo_was_zero = false;
		peace_parts->stop(true);
		peace_parts->once({MODEL_PC_RAT, ME_AURA}, {{}, 2, FColor(0.2, 0.8, 1, 1)}, TimeSpan{});
	}
	if (peace_tmo.is_positive())
	{
		peace_tmo -= GameCore::step_len;
		if (!peace_tmo.is_positive())
		{
			peace_parts->stop();
		}
		else if (peace_tmo < TimeSpan::seconds(2) && !peace_parts->is_playing())
		{
			peace_tmo_was_zero = true;
			peace_parts->play({MODEL_PC_RAT, ME_AURA}, {{}, 1, FColor(1, 0.4, 0.2, 0.2)}, TimeSpan::seconds(0.3), peace_tmo);
		}
	}
	
	// check & slide
	
	vec2fp vd = look_pos - ent.get_pos();
	vec2fp slide = {};
	
	if (dir.x >  zero_thr && side_col[0]) slide.y += vd.y;
	if (dir.y >  zero_thr && side_col[1]) slide.x += vd.x;
	if (dir.x < -zero_thr && side_col[2]) slide.y += vd.y;
	if (dir.y < -zero_thr && side_col[3]) slide.x += vd.x;
	
	if (dir.x >  zero_thr && side_col[0]) dir.x = 0;
	if (dir.x < -zero_thr && side_col[2]) dir.x = 0;
	if (dir.y >  zero_thr && side_col[1]) dir.y = 0;
	if (dir.y < -zero_thr && side_col[3]) dir.y = 0;
	
	// move
	
	const float in_tps = GameCore::time_mul / 1.5; // seconds
	if (is_accel) inert_t = std::min(1.f, inert_t + in_tps);
	else inert_t = std::max(0.f, inert_t - in_tps);
	
	inert_t *= 1.f - inert_reduce;
	inert_reduce = std::max(0.f, inert_reduce - inert_reduce_decr);
	
	bool dir_ok = dir.len_squ() > zero_thr;
	if (dir_ok || slide.len_squ() > zero_thr)
	{
		if (dir_ok) dir.norm_to(*spd);
		else dir = slide.norm_to(*spd * slide_k);
		tar_dir = prev_dir = dir;
	}
	else tar_dir = {};
}
void PlayerMovement::step()
{
	const float inert_k = lerp(10, 3, inert_t);
	
	auto& self = static_cast<PlayerEntity&>(ent);
	auto& body = self.phy.body;
	b2Vec2 vel = body.GetLinearVelocity();
	
	b2Vec2 f = conv(tar_dir) - vel;
	f *= body.GetMass();
	f *= inert_k;
	body.ApplyForceToCenter(f, true);
	
	float amp = vel.Length();
	if (amp > spd_norm + 1)
	{
		float p = clampf((amp - spd_norm) / (spd_accel - spd_norm), 0.3, 1);
		Transform tr;
		tr.rot = std::atan2(vel.y, vel.x) - self.phy.get_angle();
		tr.pos = {-self.phy.get_radius(), 0};
		tr.pos.fastrotate(tr.rot);
		ent.ref<EC_RenderPos>().parts(FE_SPEED_DUST, {tr, p});
	}
}



ShieldControl::ShieldControl(Entity& ent)
	: ent(ent), tr{{ ent.ref_pc().get_radius() + GameConst::hsz_pshl.x, 0 }}
{
	b2FixtureDef fd;
	fd.filter.maskBits = EC_Physics::CF_BULLET;
	fd.isSensor = true;
	
	sh = new DmgShield(500, 500/12.f, TimeSpan::seconds(3), FixtureCreate::box(fd, GameConst::hsz_pshl, tr, 0.1f));
	ent.ref_hlc().add_phys( std::unique_ptr<DmgShield>(sh) );
}
bool ShieldControl::step(bool sw_state)
{
	switch (state)
	{
	case ST_DEAD:
		tmo -= ent.core.step_len;
		if (!tmo.is_positive()) state = ST_DISABLED;
		return false;
		
	case ST_DISABLED:
		if (sw_state) {
			state = ST_SWITCHING;
			tmo = activate_time;
		}
		break;
		
	case ST_SWITCHING:
		if (!sw_state) state = ST_DISABLED;
		else {
			ent.ref<EC_RenderPos>().parts({MODEL_PC_SHLD, ME_AURA}, {tr, 0.05, FColor(0.9, 0.5, 0.8, 0.3)});
			
			tmo -= ent.core.step_len;
			if (!tmo.is_positive())
			{
				state = ST_ACTIVE;
				sh->set_enabled(ent, true);
				
				ent.ref<EC_RenderEquip>().attach(EC_RenderEquip::ATT_SHIELD, tr, MODEL_PC_SHLD, FColor(0.9, 0.9, 1, 1));
				ent.ref<EC_RenderPos>().parts({MODEL_PC_SHLD, ME_AURA}, {tr, 0.35, FColor(0.9, 0.9, 1, 2)});
			}
		}
		break;
		
	case ST_ACTIVE:
		if (!sw_state || !sh->get_hp().is_alive())
		{
			bool was_alive = !sw_state;
			
			if (was_alive) state = ST_DISABLED;
			else {
				state = ST_DEAD;
				tmo = dead_time;
			}
			
			sh->set_enabled(ent, false);
			ent.ref<EC_RenderEquip>().detach(EC_RenderEquip::ATT_SHIELD);
			
			float pow = was_alive ? 0.4 : 2;
			ent.ref<EC_RenderPos>().parts(
				{ MODEL_PC_SHLD, ME_AURA },
				{ tr, pow, FColor(0.9, 0.9, 1, was_alive ? 2 : 5) });
			
			return false;
		}
		break;
	}
	return sw_state;
}



EC_PlayerLogic::EC_PlayerLogic(PlayerEntity& ent)
    : EComp(ent), pmov(ent), shlc(ent)
{
	prev_tar = ent.get_pos() + vec2fp(1, 0);
	
	vec2fp ram_ext{0.7, 1.2};
	Transform ram_pos{{GameConst::hsz_rat + ram_ext.x, 0}};
	ram_sensor = &ent.phy.add (FixtureCreate::box (fixtsensor(), ram_ext, ram_pos, 0));
	
	EVS_CONNECT1(ent.phy.ev_contact, on_cnt);
	EVS_CONNECT1(ent.hlc.on_damage , on_dmg);
}
void EC_PlayerLogic::on_cnt(const CollisionEvent& ev)
{
	if (ev.fix_phy == ram_sensor && ev.type == CollisionEvent::T_BEGIN)
	{
		if (auto hc = ev.other->get_hlc())
		{
			auto& body = ent.ref_phobj().body;
			b2Vec2 vel = body.GetLinearVelocity();
			float spd = vel.LengthSquared();
			
			if (spd > col_dmg_spd_min * col_dmg_spd_min)
			{
				hc->apply({DamageType::Direct, col_dmg_val});
				shld_restore_left += col_dmg_restore;
				
				pmov.acc_val = 1;
				pmov.inert_reduce = 1;
				
				const float min = 1.2 * pmov.spd_accel;
				if (spd < min * min) {
					if (spd > 5) vel *= min / b2Sqrt(spd);
					else vel = conv(vec2fp(pmov.prev_dir).norm_to(min));
					body.SetLinearVelocity(vel);
				}
				
				ent.ref<EC_RenderModel>().parts(ME_AURA, {Transform{{2, 0}}, 0.5, FColor(0, 1, 0.2)});
			}
		}
	}
}
void EC_PlayerLogic::on_dmg(const DamageQuant&)
{
	pmov.battle_trigger();
}
void EC_PlayerLogic::m_step()
{
	auto& pinp = PlayerInput::get();
	auto& cst = pinp.get_state(PlayerInput::CTX_GAME);
	auto& eqp = ent.ref_eqp();
	
	// shield
	
	pinp.set_switch(PlayerInput::CTX_GAME, PlayerInput::A_SHIELD_SW, shlc.step(cst.is[PlayerInput::A_SHIELD_SW]));
	
	float sh_rest = std::min( shld_restore_left, shld_restore_rate * GameCore::time_mul );
	pers_shld->get_hp().apply( sh_rest );
	shld_restore_left -= sh_rest;
	
	// actions
	
	bool accel = cst.is[PlayerInput::A_ACCEL];
	
	for (auto& a : cst.acts)
	{
		if		(a == PlayerInput::A_WPN_PREV)
		{
			auto& wpns = eqp.raw_wpns();
			auto i = eqp.wpn_index(), j = i - 1;
			for (; j != i; --j)
			{
				if (j >= wpns.size()) j = wpns.size() - 1;
				if (eqp.set_wpn(j, true)) break;
			}
			if (i == j) eqp.set_wpn(i, true);
		}
		else if (a == PlayerInput::A_WPN_NEXT)
		{
			auto& wpns = eqp.raw_wpns();
			auto i = eqp.wpn_index(), j = i + 1;
			for (; j != i; ++j)
			{
				if (j >= wpns.size()) j = 0;
				if (eqp.set_wpn(j, true)) break;
			}
			if (i == j) eqp.set_wpn(i, true);
		}
		else if (a == PlayerInput::A_WPN_1) eqp.set_wpn(0);
		else if (a == PlayerInput::A_WPN_2) eqp.set_wpn(1);
		else if (a == PlayerInput::A_WPN_3) eqp.set_wpn(2);
		else if (a == PlayerInput::A_WPN_4) eqp.set_wpn(3);
		else if (a == PlayerInput::A_WPN_5) eqp.set_wpn(4);
		else if (a == PlayerInput::A_WPN_6) eqp.set_wpn(5);
	}
	
	// move & shoot
	
	auto spos = ent.get_pos();
	auto tar = cst.tar_pos;
	
	if (spos.dist(tar) < min_tar_dist)
		tar = prev_tar;
	prev_tar = tar;
	
	pmov.upd_vel(ent, cst.mov, accel, tar);
	
	eqp.shoot(tar, cst.is[PlayerInput::A_SHOOT], cst.is[PlayerInput::A_SHOOT_ALT] );
	
	if (cst.is[PlayerInput::A_SHOOT] || cst.is[PlayerInput::A_SHOOT_ALT])
		pmov.battle_trigger();
	
	// set rotation
	
	float self_angle = (tar - spos).angle();
	ent.ref_pc().rot_override = self_angle;
	
	auto& body = ent.ref_phobj().body;
	float nextAngle = body.GetAngle() + body.GetAngularVelocity() * GameCore::time_mul;
	float tar_torq = angle_delta(self_angle, nextAngle);
	body.ApplyAngularImpulse( body.GetInertia() * tar_torq / GameCore::time_mul, true );
	
	// calc laser
	
	auto& laser = ent.ref<EC_LaserDesigRay>();
	tar -= spos;
	laser.enabled = cst.is[PlayerInput::A_LASER_DESIG] && tar.len_squ() > 0.1;
	if (laser.enabled)
		laser.find_target( tar.norm() );
	
	// check if any enemies are nearby
	
	if (!ent.core.get_aic().is_targeted(ent) &&
	    ent.core.get_phy().query_aabb(
			Rectfp::from_center(ent.get_pos(), enemy_radius),
	        [](auto& ent, auto&) {return !ent.get_ai_drone();}
		))
	{
		pmov.peace_tmo = std::min(pmov.peace_tmo, pmov.c_peace_tmo_short);
	}
}



PlayerEntity::PlayerEntity(GameCore& core, vec2fp pos, bool is_superman)
	:
	Entity(core),
	phy(*this, bodydef(pos, true)),
	hlc(*this, 250),
	eqp(*this),
	log(*this)
{
	ui_descr = "The Rat";
	phy.add(FixtureCreate::circle( fixtdef(0.3, 0.4), GameConst::hsz_rat, is_superman ? 1500 : 15 ));
	
	// rendering
	
	ensure<EC_RenderPos>().immediate_rotation = true;
	ensure<EC_RenderPos>().disable_culling = true;
	add_new<EC_RenderModel>(MODEL_PC_RAT, FColor(0.4, 0.9, 1, 1), EC_RenderModel::DEATH_AND_EXPLOSION);
	add_new<EC_RenderEquip>();
	add_new<EC_LaserDesigRay>();
	
	GamePresenter::get()->effect(FE_SPAWN, {Transform{pos}, GameConst::hsz_rat});
	GamePresenter::get()->effect({MODEL_PC_RAT, ME_AURA}, {Transform{pos}, 3, FColor(0.3, 0.7, 1, 0.7)});
	
	// weapons
	
	eqp.infinite_ammo = false;
//	eqp.hand = 1;
	
	eqp.get_ammo(AmmoType::Bullet).add(200);
	eqp.get_ammo(AmmoType::Rocket).add(12);
	eqp.get_ammo(AmmoType::Energy).add(40);
	eqp.get_ammo(AmmoType::FoamCell).add(30);
	
	eqp.add_wpn(std::make_unique<WpnMinigun>());
	eqp.add_wpn(std::make_unique<WpnRocket>(true));
	eqp.add_wpn(std::make_unique<WpnElectro>(is_superman ? WpnElectro::T_ONESHOT : WpnElectro::T_PLAYER));
	eqp.add_wpn(std::make_unique<WpnFoam>());
	eqp.add_wpn(std::make_unique<WpnRifle>());
	eqp.add_wpn(std::make_unique<WpnUber>());
	eqp.set_wpn(1);
	
	// health
	
	auto& hp = hlc.get_hp();
//	hp.regen_at = 0.7;
	hp.regen_hp = 3;
	hp.regen_cd = TimeSpan::seconds(0.7);
	hp.regen_wait = TimeSpan::seconds(12);
	hlc.upd_reg();
	
	log.armor = new DmgArmor(300);
	log.armor->get_hp().apply(40);
	hlc.add_filter(std::unique_ptr<DamageFilter>( log.armor ));
	
	log.pers_shld = new DmgShield (250, 25, TimeSpan::seconds(8));
	hlc.add_filter(std::unique_ptr<DamageFilter>( log.pers_shld ));
}
PlayerEntity::~PlayerEntity()
{
	ref<EC_RenderModel>().parts(ME_AURA, {{}, 2, FColor(1, 0.4, 0.1, 2)});
	PlayerInput::get().set_switch(PlayerInput::CTX_GAME, PlayerInput::A_SHIELD_SW, false);
}
