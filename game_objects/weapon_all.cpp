#include "client/effects.hpp"
#include "client/presenter.hpp"
#include "render/ren_aal.hpp"
#include "utils/noise.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "weapon_all.hpp"



PhysicsWorld::CastFilter StdProjectile::make_cf(EntityIndex)
{
	return PhysicsWorld::CastFilter(
		[](auto, b2Fixture* f)
		{
			if (f->IsSensor())
			{
				if (auto fi = getptr(f); fi && fi->get_armor_index()) return true;
				return false;
			}
			return true;
		},
		[]{
			b2Filter f;
			f.categoryBits = EC_Physics::CF_BULLET;
			f.maskBits = ~f.categoryBits;
			return f;
		}(),
		false
	);
}
void StdProjectile::explode(size_t src_team, EntityIndex src_eid, b2Vec2 self_vel, PhysicsWorld::RaycastResult hit, const Params& pars)
{
	auto apply = [&](Entity* tar, float k, b2Vec2 at, b2Vec2 v, std::optional<size_t> armor)
	{
		if (auto hc = tar->get_hlc(); hc && (tar->get_team() != src_team || pars.friendly_fire))
		{
			DamageQuant q = pars.dq;
			q.amount *= k;
			q.armor = armor;
			q.wpos = conv(at);
			q.src_eid = src_eid;
			hc->apply(q);
		}
		if (pars.imp != 0.f && v.LengthSquared() > 0.01)
		{
			auto& pc = tar->get_phobj();
			
			v.Normalize();
			v *= k * pars.imp;
			
			GameCore::get().get_phy().post_step(
				[&pc, v, at]{ pc.body->ApplyLinearImpulse(v, at, true); }
			);
		}
	};
	
	switch (pars.type)
	{
	case T_BULLET:
	{	
		if (!hit.ent) break;
		std::optional<size_t> armor;
		if (hit.fix) armor = hit.fix->get_armor_index();
		apply(hit.ent, 1, hit.poi, self_vel, armor);
	}
	break;
		
	case T_AOE:
	{
		hit.poi -= 0.1 * GameCore::time_mul * self_vel;
		
		GamePresenter::get()->effect( FE_WPN_EXPLOSION, {Transform{conv(hit.poi)}, pars.rad} );
		int num = (2 * M_PI * pars.rad) / 0.7;
		
		struct Obj {
			Entity* ent;
			b2Vec2 poi;
			float dist;
			std::optional<size_t> armor;
		};
		std::vector<Obj> os;
		os.reserve(num);
		
		for (int i=0; i<num; ++i)
		{
			vec2fp d(pars.rad, 0);
			d.rotate(2*M_PI*i/num);
			
			auto res = GameCore::get().get_phy().raycast_nearest(hit.poi, hit.poi + conv(d), make_cf(src_eid));
			if (!res) continue;
			if (res->ent == hit.ent) continue;
			
			auto it = std::find_if(os.begin(), os.end(), [&res](auto&& v){return v.ent == res->ent;});
			if (it != os.end())
			{
				if (it->dist > res->distance) {
					it->dist = res->distance;
					it->poi = res->poi;
				}
			}
			else {
				bool ignore = false;
				Obj* p = nullptr;
				
				for (auto& op : os)
				{
					if (op.ent == res->ent)
					{
						if (op.dist <= res->distance) ignore = true;
						p = &op;
						break;
					}
				}
				if (!p)
				{
					if (ignore) continue;
					p = &os.emplace_back();
				}
				
				p->ent = res->ent;
				p->dist = res->distance;
				p->poi = res->poi;
				if (res->fix) p->armor = res->fix->get_armor_index();
			}
		}
		
		if (hit.ent)
		{
			std::optional<size_t> armor;
			if (hit.fix) armor = hit.fix->get_armor_index();
			apply(hit.ent, 1, hit.poi, self_vel, armor);
		}
		
		for (auto& r : os)
		{
			float k = pars.rad_full ? 1 : std::min(1.f, std::max((pars.rad - r.dist) / pars.rad, pars.rad_min));
			k = clampf(k, pars.aoe_min_k, pars.aoe_max_k);
			apply(r.ent, k, r.poi, r.poi - hit.poi, r.armor);
		}
	}
	break;
	}
}



StdProjectile::StdProjectile(Entity* ent, const Params& pars, EntityIndex src, std::optional<vec2fp> target)
    : EComp(ent), pars(pars), src(src), target(target)
{
	reg(ECompType::StepLogic);
}
void StdProjectile::step()
{
	auto& self = dynamic_cast<EC_VirtualBody&>(ent->get_phy());
	if (pars.trail)
		ent->get_ren()->parts(FE_SPEED_DUST, {Transform({-self.radius, 0}), 0.6});
	
	GameCore::get().valid_ent(src);
	
	const float d_err = 0.1;
	const vec2fp k_vel = self.get_vel().pos;
	const vec2fp rayd = k_vel * GameCore::time_mul + k_vel.get_norm() * d_err * 2,
	             ray0 = self.pos.pos - k_vel.get_norm() * d_err;
	
	if (target)
	{
		vec2fp p = ray0 - *target;
		vec2fp n = (ray0 + rayd) - *target;
		p *= n;
		if (p.x < 0 || p.y < 0)
		{
			PhysicsWorld::RaycastResult hit = {};
			hit.poi = conv(*target);
			explode(ent->get_team(), src, conv(k_vel), hit, pars);
			ent->destroy();
			return;
		}
	}
	
	auto hit = GameCore::get().get_phy().raycast_nearest(conv(ray0), conv(ray0 + rayd), make_cf(src), pars.size);
	if (hit) {
		explode(ent->get_team(), src, conv(k_vel), *hit, pars);
		ent->destroy();
	}
}
ProjectileEntity::ProjectileEntity(vec2fp pos, vec2fp vel, std::optional<vec2fp> target, Entity* src,
                                   const StdProjectile::Params& pars, ModelType model, FColor clr)
    :
    phy(this, Transform{pos, vel.angle()}, Transform{vel}),
    ren(this, model, clr),
    proj(this, pars, src? src->index : EntityIndex{}, target),
    team(src? src->get_team() : TEAM_ENVIRON)
{
	// Projectile created by (StepLogic -> EC_Equip -> Weapon) chain,
	// so first logic step happens after first position step.
	// This corrects position, so it'll be correct on first logic step
	phy.pos.pos -= vel * GameCore::time_mul;
}



WpnMinigun::WpnMinigun()
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "Minigun";
			info->model = MODEL_MINIGUN;
			info->ammo = AmmoType::Bullet;
			info->def_ammo = 1;
			info->def_delay = TimeSpan::fps(30);
			info->def_heat = 0.3;
			info->bullet_speed = 18;
			info->set_origin_from_model();
		}
		return &*info;
	}())
{
	pp.dq.amount = 15.f;
	pp.imp = 5.f;
	
	p2.dq.amount = 30.f;
	p2.imp = 15.f;
	
	overheat = Overheat{};
	overheat->v_decr = *info->def_heat;
	overheat->thr_off = 0.1;
}
std::optional<Weapon::ShootResult> WpnMinigun::shoot(ShootParams pars)
{
	auto dirs_tuple = get_direction(pars);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;

	if (pars.main)
	{
		float disp = lerp(8, 30, overheat->value); // was 10, fixed
		v *= info->bullet_speed;
		v.rotate( GameCore::get().get_random().range_n2() * deg_to_rad(disp) );
		new ProjectileEntity(p, v, {}, equip->ent, pp, MODEL_MINIGUN_PROJ, FColor(1, 1, 0.2, 1.5));
		return ShootResult{};
	}
	else if (pars.alt)
	{
		const int num = 6;
		for (int i=0; i<num; ++i)
		{
			vec2fp lv = v;
			lv *= GameCore::get().get_random().range(20, 28);
			lv.rotate( GameCore::get().get_random().range_n2() * deg_to_rad(25) );
			new ProjectileEntity(p, lv, {}, equip->ent, p2, MODEL_MINIGUN_PROJ, FColor(1, 1, 0.2, 1.5));
		}
		return ShootResult{num, TimeSpan::seconds(0.7)};
	}
	return {};
}



WpnMinigunTurret::WpnMinigunTurret()
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "Minigun";
			info->model = MODEL_MINIGUN;
			info->ammo = AmmoType::Bullet;
			info->def_ammo = 1;
			info->def_delay = TimeSpan::fps(30);
			info->def_heat = 0.3;
			info->bullet_speed = 18;
			info->set_origin_from_model();
		}
		return &*info;
	}())
{
	pp.dq.amount = 10.f;
	pp.imp = 5.f;
	
	overheat = Overheat{};
	overheat->v_decr = *info->def_heat;
	overheat->thr_off = 0.1;
}
std::optional<Weapon::ShootResult> WpnMinigunTurret::shoot(ShootParams pars)
{
	if (!pars.main) return {};
	
	auto dirs_tuple = get_direction(pars);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;

	v *= info->bullet_speed;
	v.rotate( GameCore::get().get_random().range_n2() * deg_to_rad(10) );
	new ProjectileEntity(p, v, {}, equip->ent, pp, MODEL_MINIGUN_PROJ, FColor(1, 1, 0.2, 1.5));
	return ShootResult{};
}



WpnRocket::WpnRocket()
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "Rocket";
			info->model = MODEL_ROCKET;
			info->ammo = AmmoType::Rocket;
			info->def_ammo = 1;
			info->def_delay = TimeSpan::seconds(1);
			info->bullet_speed = 15;
			info->set_origin_from_model();
		}
		return &*info;
	}())
{
	pp.dq.amount = 120.f;
	pp.type = StdProjectile::T_AOE;
	pp.rad = 3.f;
	pp.rad_min = 0.f;
	pp.imp = 80.f;
	pp.aoe_max_k = 80.f / pp.dq.amount;
	pp.aoe_min_k = 20.f / pp.dq.amount;
	pp.trail = true;
}
std::optional<Weapon::ShootResult> WpnRocket::shoot(ShootParams pars)
{
	if (!pars.main && !pars.alt) return {};

	auto dirs_tuple = get_direction(pars);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;
	
	v *= info->bullet_speed;
	
	std::optional<vec2fp> tar;
	if (pars.alt) tar = pars.target;
	
	new ProjectileEntity(p, v, tar, equip->ent, pp, MODEL_ROCKET_PROJ, FColor(0.2, 1, 0.6, 1.5));
	return ShootResult{};
}



bool ElectroCharge::SrcParams::ignore(Entity* ent)
{
	auto it = std::find(prev.begin(), prev.end(), ent);
	bool ign = it != prev.end() || ent->get_team() == team;
	if (!ign) prev.push_back(ent);
	return ign;
}
bool ElectroCharge::generate(vec2fp pos, SrcParams src, std::optional<vec2fp> dir_lim)
{
	PhysicsWorld::CastFilter check(
		[](auto, b2Fixture* f)
		{
			if (f->IsSensor())
			{
				if (auto fi = getptr(f); fi && fi->get_armor_index()) return true;
				return false;
			}
			return true;
		},
		[]{
			b2Filter f;
			f.categoryBits = EC_Physics::CF_BULLET;
			f.maskBits = ~f.categoryBits;
			return f;
		}(),
		false
	);
	std::vector<PhysicsWorld::RaycastResult> rs;
	GameCore::get().get_phy().circle_cast_nearest(rs, conv(pos), radius, check);
	
	std::vector<vec2fp> n_cs;
	
	struct Res
	{
		EC_Health* hc;
		vec2fp poi;
		std::optional<size_t> armor;
	};
	std::vector<Res> rs_o;
	rs_o.reserve(16);
	
	for (auto& r : rs)
	{
		if (src.ignore(r.ent)) continue;
		
		auto hc = r.ent->get_hlc();
		if (!hc) continue;
		
		auto rc = GameCore::get().get_phy().raycast_nearest(conv(pos), conv(r.ent->get_pos()),
			{[&](auto ent, auto fix){
		         FixtureInfo* f = getptr(fix);
		         return ent == r.ent || (f && (f->typeflags & FixtureInfo::TYPEFLAG_WALL));}});
		vec2fp r_poi = rc? conv(rc->poi) : r.ent->get_pos();
		if (dir_lim && dot(*dir_lim, (pos - r_poi).norm()) > -angle_lim) continue;
		
		auto& ro = rs_o.emplace_back();
		ro.hc = hc;
		ro.poi = r_poi;
		if (r.fix) ro.armor = r.fix->get_armor_index();
	}
	
	if (rs_o.size() > max_tars)
	{
		for (size_t i=0; i<rs_o.size(); ++i)
			std::swap(rs_o[i], rnd_stat().random_el(rs_o));
	}
	
	for (size_t i=0; i < std::min(max_tars, rs_o.size()); ++i)
	{
		auto& r = rs_o[i];
		
		DamageQuant q;
		q.type = DamageType::Kinetic;
		q.amount = damage;
		q.wpos = r.poi;
		q.armor = r.armor;
		q.src_eid = src.src_eid;
		r.hc->apply(q);
		
		auto& n = n_cs.emplace_back();
		n = r.poi;
		
		GamePresenter::get()->effect(FE_HIT_SHIELD, {Transform{r.poi}, 6});
	}
	
	for (auto& n : n_cs) {
		effect_lightning(pos, n, src.gener == 0 ? EffectLightning::First : EffectLightning::Regular, left_init);
		new ElectroCharge(n, src, (n - pos).norm());
	}
	
	return !n_cs.empty();
}
ElectroCharge::ElectroCharge(vec2fp pos, SrcParams src, std::optional<vec2fp> dir_lim)
	: phy(this, Transform{pos}), src(src), dir_lim(dir_lim)
{
	reg_this();
}
void ElectroCharge::step()
{
	GameCore::get().valid_ent(src.src_eid);
	
	left -= GameCore::step_len;
	if (left.is_negative())
	{
		if (src.gener != last_generation) {
			++src.gener;
			generate(phy.get_pos(), src, dir_lim);
		}
		destroy();
	}
}



WpnElectro::WpnElectro()
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "Bolter";
			info->model = MODEL_ELECTRO;
			info->hand = 0;
			info->ammo = AmmoType::Energy;
			info->def_ammo = 1;
			info->def_delay = TimeSpan::seconds(0.3);
			info->set_origin_from_model();
		}
		return &*info;
	}())
{}
std::optional<Weapon::ShootResult> WpnElectro::shoot(ShootParams pars)
{
	auto dirs_tuple = get_direction(pars);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;

	auto ent = equip->ent;
	if (pars.main || pars.main_was)
	{
		const TimeSpan charge_time = TimeSpan::seconds(2.5);
		const TimeSpan wait_time = TimeSpan::seconds(2);
		const int max_ammo = 10;
		const float max_damage = 250;
		const TimeSpan max_cd = TimeSpan::seconds(1);
		const float ray_width = 1; // full-width
		
		if (pars.main)
		{
			charge_lvl += GameCore::time_mul * (1.f / charge_time.seconds());
			if (charge_lvl >= 1.f)
			{
				charge_lvl = 1.f;
				charge_tmo += GameCore::step_len;
				
				GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform{p, v.angle()}, charge_lvl * 2.5f, FColor(1, 1, 1)});
			}
			else GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform{p, v.angle()}, charge_lvl * 2, FColor(0, 1, 1)});
		}
		
		if (pars.main && charge_tmo < wait_time && equip->has_ammo(*this, max_ammo * charge_lvl))
			return {};
		
		auto hit = GameCore::get().get_phy().raycast_nearest(conv(p), conv(p + 1000 * v), StdProjectile::make_cf(ent->index), ray_width);
		if (!hit) return {}; // unlikely
		
		effect_lightning( p, conv(hit->poi), EffectLightning::Straight, TimeSpan::seconds(0.3) );
		GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform(conv(hit->poi), v.angle() + M_PI),
		                                             charge_lvl * 20, FColor(0.6, 0.85, 1, 1.2)});
		
		StdProjectile::Params pp;
		pp.dq.amount = max_damage * charge_lvl;
		pp.imp = 400 * charge_lvl;
		StdProjectile::explode(ent->get_team(), ent->index, conv(v), *hit, pp);
		
		ShootResult res = {int_round(max_ammo * charge_lvl), max_cd * charge_lvl};
		charge_lvl = 0.f;
		charge_tmo = {};
		return res;
	}
	
	if (pars.alt)
	{
		bool ok = ElectroCharge::generate(p, {ent->get_team(), ent->index}, v);
		if (ok) return ShootResult{};
	}
	
	return {};
}
std::optional<Weapon::UI_Info> WpnElectro::get_ui_info()
{
	UI_Info inf;
	if (charge_lvl > 0.f) inf.charge_t = charge_lvl;
	return inf;
}



bool FoamProjectile::can_create(vec2fp pos, EntityIndex src_i)
{
	if (!src_i) return true;
	bool ok = true;
	
	GameCore::get().get_phy().query_circle_all(conv(pos), 2,
	[&](auto& ent, auto&){
		if (src_i == ent.index) {
			ok = false;
			return false;
		}
		return true;
	});
	return ok;
}
FoamProjectile::FoamProjectile(vec2fp pos, vec2fp vel, size_t team, EntityIndex src_i, bool is_first)
	:
	phy(this, [&]
	{
		b2BodyDef d;
		d.type = b2_dynamicBody;
		d.position = conv(pos);
		d.linearVelocity = conv(vel);
		return d;
	}()),
    ren(this, MODEL_GRENADE_PROJ_ALT, FColor(0.7, 1, 0.7)),
    hlc(this, 20),
	team(team),
    left(is_first? TimeSpan::seconds(4) : frozen_left),
    min_spd(vel.len_squ() / 4), // half of original
    is_first(is_first),
    src_i(src_i),
    vel_initial(vel)
{
	b2FixtureDef fd;
	fd.friction = 0;
	fd.restitution = 1;
	phy.add_circle(fd, GameConst::hsz_proj_big, 1);
	
	if (is_first) EVS_CONNECT1(phy.ev_contact, on_event);
	reg_this();
}
void FoamProjectile::step()
{
	if (src_i && !GameCore::get().get_ent(src_i)) src_i = {};
	
	left -= GameCore::step_len;
	if (left.is_negative())
	{
		if (!frozen) freeze();
		else destroy();
		return;
	}
	
	if (!frozen)
	{
		float n = phy.body->GetLinearVelocity().LengthSquared();
		if (n < min_spd) freeze();
		else if (n > 20*20) destroy();
	}
}
void FoamProjectile::on_event(const CollisionEvent& ev)
{
	if (ev.type == CollisionEvent::T_RESOLVE && ev.other->get_team() != team)
		freeze();
}
void FoamProjectile::freeze()
{
	if (!can_create(phy.get_pos(), src_i)) {
		destroy();
		return;
	}
	
	left = TimeSpan::seconds(10);
	frozen = true;
	
	auto& phw = GameCore::get().get_phy();
	
	if (is_first)
	{
		vel_initial.norm_to(5);
		for (int i=0; i<3; ++i)
		{
			vec2fp dir = {phy.get_radius() - 0.1f, 0};
			dir.fastrotate( GameCore::get().get_random().range_n2() * M_PI );
			
			phw.post_step([p = phy.get_pos(), v = dir, team = team, src_i = src_i, vel = vel_initial]{
				new FoamProjectile(p + v, vel, team, src_i, false);
			});
		}
	}
	
	hlc.get_hp().renew(80);
	
	phw.post_step([this]{ phy.body->SetType(b2_staticBody); });
	EVS_SUBSCR_UNSUB_ALL;
	
	team = TEAM_ENVIRON;
	ren.clr = FColor(0.95, 1, 1);
}



WpnFoam::WpnFoam()
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "Foam gun";
			info->model = MODEL_GRENADE;
			info->ammo = AmmoType::FoamCell;
			info->def_ammo = 1;
			info->def_delay = TimeSpan::seconds(0.2);
			info->set_origin_from_model();
		}
		return &*info;
	}())
{}
std::optional<Weapon::ShootResult> WpnFoam::shoot(ShootParams pars)
{
	if (!pars.main && !pars.alt) return {};
	
	auto dirs_tuple = get_direction(pars);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;
	        
	auto ent = equip->ent;
	if (pars.main)
	{
		new FoamProjectile(p, v * 10, ent->get_team(), ent->index, true);
		return ShootResult{};
	}
	if (pars.alt)
	{
		const size_t tars = 2;
		const int num = 6;
		const float a0 = deg_to_rad(50);
		const float ad = a0 * 2 / num;
		
		for (int i=0; i<num; ++i)
		{
			vec2fp d = v;
			d.fastrotate(-a0 + i * ad);
			
			std::vector<PhysicsWorld::RaycastResult> es;
			GameCore::get().get_phy().raycast_all(es, conv(p), conv(p + d * 8), StdProjectile::make_cf(ent->index));
			
			if (es.size() > 1)
			{
				std::sort(es.begin(), es.end(), [](auto& a, auto& b){return a.distance < b.distance;});
				if (es.size() > tars) es.resize(tars);
				
				for (auto it = es.begin(); it != es.end(); ++it)
				{
					if (it->fix && (it->fix->typeflags & FixtureInfo::TYPEFLAG_WALL))
					{
						es.erase( it, es.end() );
						break;
					}
				}
			}
			
			const float full_dmg = 60 * info->def_delay->seconds();
			const float foam_dmg = 150 * info->def_delay->seconds();
			
			float k = 1;
			for (auto& e : es)
			{
				if (auto hc = e.ent->get_hlc())
				{
					DamageQuant q;
					q.type = DamageType::Kinetic;
					q.wpos = conv(e.poi);
					q.amount = full_dmg;
					if (e.fix) q.armor = e.fix->get_armor_index();
					if (dynamic_cast<FoamProjectile*>(e.ent)) q.amount = foam_dmg;
					
					GamePresenter::get()->effect(FE_EXPLOSION, {Transform(*q.wpos), 0.2f * k});

					q.amount *= k;
					hc->apply(q);
				}
				k /= 2;
			}
			
			GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform(p, v.angle()), 3, FColor(1, 0.4, 0.2, 1)});
		}
		
		ammo_skip_count = (ammo_skip_count + 1) % 2;
		return ShootResult{ammo_skip_count ? 0 : 1};
	}
	return {};
}



GrenadeProjectile::GrenadeProjectile(vec2fp pos, vec2fp dir, EntityIndex src_eid)
    :
	phy(this, [&]
	{
		b2BodyDef d;
		d.type = b2_dynamicBody;
		d.position = conv(pos);
		d.linearVelocity = conv(dir * 15);
		return d;
	}()),
    ren(this, MODEL_GRENADE_PROJ, FColor(1, 0, 0)),
    hlc(this, 20),
    src_eid(src_eid),
    ignore_tmo(TimeSpan::seconds(0.3)),
    left(TimeSpan::seconds(6))
{
	b2FixtureDef fd;
	fd.friction = 0;
	fd.restitution = 1;
	phy.add_circle(fd, GameConst::hsz_proj, 1);
	
	fd.isSensor = true;
	phy.add_circle(fd, 2, 0);
	
	EVS_CONNECT1(phy.ev_contact, on_event);
	reg_this();
}
void GrenadeProjectile::step()
{
	GameCore::get().valid_ent(src_eid);
	
	float k = std::fmod(clr_t, 1);
	k = (k < 0.5 ? k : 1 - k) * 2;
	ren.clr.g = k;
	ren.clr.b = k * 0.2;
	
	clr_t += 2 * GameCore::time_mul;
	
	ignore_tmo -= GameCore::step_len;
	left -= GameCore::step_len;
	if (left.is_negative())
		explode();
}
void GrenadeProjectile::on_event(const CollisionEvent& ev)
{
	if (ev.type == CollisionEvent::T_BEGIN)
	{
		if (ev.other->get_team() != TEAM_ENVIRON && ignore_tmo.is_negative())
		{
			vec2fp dir = conv(phy.body->GetLinearVelocity());
			float t = lineseg_perpen_t(get_pos(), get_pos() + dir, ev.other->get_pos());
			if		(t < 0) left = {};
			else if (t < 1) left = TimeSpan::seconds(t);
		}
	}
	else if (ev.type == CollisionEvent::T_RESOLVE)
	{
		if (ev.other->get_team() != TEAM_ENVIRON || dynamic_cast<GrenadeProjectile*>(ev.other)) explode();
		else {
			float angle = GameCore::get().get_random().range_n2() * deg_to_rad(15);
			
			b2Vec2 vel = phy.body->GetLinearVelocity();
			vel = b2Mul(b2Rot(angle), vel);
			phy.body->SetLinearVelocity(vel);
		}
	}
}
void GrenadeProjectile::explode()
{
	PhysicsWorld::RaycastResult hit = {};
	hit.poi = phy.body->GetWorldCenter();
	
	StdProjectile::Params pp;
	pp.dq.amount = 120;
	pp.type = StdProjectile::T_AOE;
	pp.rad = 4;
	pp.rad_min = 1;
	pp.aoe_max_k = 0.8;
	pp.aoe_min_k = 0.2;
	pp.imp = 100;
	pp.friendly_fire = true;
	
	StdProjectile::explode(TEAM_ENVIRON, src_eid, phy.body->GetLinearVelocity(), hit, pp);
	destroy();
}



WpnRifle::WpnRifle()
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "Rifle";
			info->model = MODEL_BOLTER;
			info->ammo = AmmoType::Bullet;
			info->def_ammo = 1;
			info->def_delay = TimeSpan::seconds(0.15);
			info->bullet_speed = 25;
			info->set_origin_from_model();
		}
		return &*info;
	}())
{
	pp.dq.amount = 18.f;
	pp.imp = 7.f;
}
std::optional<Weapon::ShootResult> WpnRifle::shoot(ShootParams pars)
{
	auto dirs_tuple = get_direction(pars);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;
	        
	auto ent = equip->ent;
	if (pars.main)
	{
		v *= info->bullet_speed;
		v.rotate( GameCore::get().get_random().range(-1, 1) * deg_to_rad(2) );
	
		new ProjectileEntity(p, v, {}, ent, pp, MODEL_MINIGUN_PROJ, FColor(0.6, 0.8, 1, 1.5));
		return ShootResult{};
	}
	
	if (pars.alt)
	{
		new GrenadeProjectile(p, v, ent->index);
		return ShootResult{8, TimeSpan::seconds(0.7)};
	}
	
	return {};
}



ElectroBall::ElectroBall(vec2fp pos, vec2fp dir)
	:
	phy(this, [&]{
		b2BodyDef d;
		d.type = b2_kinematicBody;
		d.position = conv(pos);
		d.linearVelocity = conv(dir * speed_min);
		return d;
	}()),
	ren(this, MODEL_GRENADE_PROJ_ALT, FColor(0.4, 0.2, 1))
{
	b2FixtureDef fd;
	fd.isSensor = true;
	phy.add_circle(fd, expl_radius, 0);
	EVS_CONNECT1(phy.ev_contact, on_cnt);
	
	reg_this();
}
void ElectroBall::on_cnt(const CollisionEvent& ev)
{
	if (ev.other->get_eqp())
	{
		if		(ev.type == CollisionEvent::T_BEGIN) ++explode_cntc;
		else if (ev.type == CollisionEvent::T_END)   --explode_cntc;
	}
}
void ElectroBall::step()
{
	GamePresenter::get()->effect(FE_CIRCLE_AURA, {Transform{get_pos()}, 0.005, FColor(0.4, 0.2, 1, 0.5)});
	
	const float find_radius = 16;
	
	if (tmo_ignore.is_positive()) {
		tmo_ignore -= GameCore::step_len;
		return;
	}
	
	if (tmo_explode.is_positive()) tmo_explode -= GameCore::step_len;
	else if (explode_cntc != 0 || explode_close)
	{
		phy.body->SetLinearVelocity({0,0});
		auto& rnd = GameCore::get().get_random();
		
		StdProjectile::Params pp;
		pp.dq.amount = 150;
		pp.rad = 4;
		pp.rad_full = true;
		pp.imp = 1200;
		
		PhysicsWorld::RaycastResult rc = {};
		{
			vec2fp v(rnd.range(1, 4), 0);
			v.rotate( rnd.range_n2() * M_PI );
			rc.poi = conv(get_pos() + v);
		}
		StdProjectile::explode(TEAM_ENVIRON, {}, {}, rc, pp);
		
		for (int i=0; i<4; ++i)
		{
			vec2fp v(rnd.range(3, 12), 0);
			v.rotate( rnd.range_n2() * M_PI );
			
			vec2fp p = get_pos() + v;
			ElectroCharge::generate( p, {TEAM_ENVIRON, {}}, {} );
			
			GamePresenter::get()->effect(FE_EXPLOSION_FRAG, {Transform{p}, 0.2});
			GamePresenter::get()->effect(FE_CIRCLE_AURA, {Transform{p}, 4, FColor(0.6, 0.2, 1, 3)});
		}
		
		tmo_explode = TimeSpan::seconds(0.5);
		if (--explode_left == 0) destroy();
		return;
	}
	
	//
	
	if (auto tar = GameCore::get().valid_ent(target_id))
	{
		vec2fp dt = tar->get_pos() - get_pos();
		float dist = dt.len();
		if (dist > find_radius)
		{
			target_id = {};
			return;
		}
		
		float spd = lerp(speed_max, speed_min, dist / find_radius);
		dt.norm_to(spd);
		
		vec2fp vel = phy.get_vel().pos;
		vel += (dt - vel) * (GameCore::step_len / TimeSpan::seconds(0.8));
		phy.body->SetLinearVelocity(conv(vel));
		
		explode_close = (dist < expl_radius);
	}
	else
	{
		explode_close = false;
		
		auto& rnd = GameCore::get().get_random();
		vec2fp rv( rnd.range(3, 18), 0 );
		rv.rotate( rnd.range_n2() * M_PI );
		
		if (tmo_target.is_positive()) tmo_target -= GameCore::step_len;
		else {
			std::vector<EntityIndex> es_tars;
			std::vector<Entity*> es_grav;
			
			GameCore::get().get_phy().query_circle_all( conv(get_pos()), find_radius,
			[&](auto& ent, auto&) {
				if (ent.get_eqp()) es_tars.push_back(ent.index);
				else es_grav.push_back(&ent);
			},
			[&](auto& ent, auto& fix) {
				return (ent.get_eqp() && !fix.IsSensor()) || (typeid(ent) == typeid(ElectroBall) && (&ent) != this);
			});
			
			if (!es_tars.empty()) target_id = rnd.random_el(es_tars);
			else
			{
				tmo_target = TimeSpan::seconds(0.3);
				
				for (auto& e : es_grav) // just for fun - gravity
				{
					vec2fp dt = e->get_pos() - get_pos();
					float n = dt.len();
					if (n < 1e-5) continue;
					
					const float lim = expl_radius + 2;
					if (n > lim) n = 0.7 * lerp(speed_max, speed_min, (n - lim) / (find_radius - lim));
					else {
						dt = -dt;
						n = speed_max;
					}
					
					dt.norm_to(n);
					rv += dt;
					tmo_target = {};
				}
			}
		}
		
		vec2fp vel = phy.get_vel().pos;
		vel += (rv - vel) * (GameCore::step_len / TimeSpan::seconds(2));
		phy.body->SetLinearVelocity(conv(vel));
	}
	
	if (!LevelControl::get().cell( LevelControl::get().to_cell_coord( get_pos() ) ))
		destroy();
}



WpnUber_Ray::WpnUber_Ray(WpnUber& wpn)
	: wpn(wpn), phy(this), ren(nullptr)
{
	ren.ent = this;
}
void WpnUber_Ray::Render::step()
{
	auto& self = dynamic_cast<WpnUber_Ray&>(*ent);
	if (!self.show && !left.is_positive()) return;
	
	vec2fp b, a = get_pos().pos;
	
	constexpr TimeSpan left_max = TimeSpan::seconds(0.5);
	if (self.show) {
		left = left_max;
		b = self.b_last;
	}
	else {
		left -= GamePresenter::get()->get_passed();
		
		vec2fp v((self.b_last - a).fastlen() * (left / left_max), 0);
		v.fastrotate( self.wpn.equip->ent->get_face_rot() );
		b = a + v;
	}
	
	RenAAL::get().draw_line(a, b, FColor(1, 0.3, 0.2).to_px(), 0.2, 4, left / left_max);
}
Transform WpnUber_Ray::ProxyPos::get_trans() const
{
	return dynamic_cast<WpnUber_Ray&>(*ent).wpn.equip->ent->get_phy().get_trans();
}


WpnUber::WpnUber()
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "Plasma cannon";
			info->model = MODEL_UBERGUN;
			info->ammo = AmmoType::Energy;
			info->def_ammo = 1;
			info->set_origin_from_model();
		}
		return &*info;
	}())
{
	overheat = Overheat{};
	overheat->thr_off = 0.1;
	overheat->v_decr = 0.2;
	overheat->v_cool = 0.3;
	
	ray_ren = new WpnUber_Ray(*this);
}
WpnUber::~WpnUber()
{
	if (!GameCore::get().is_freeing())
		ray_ren->destroy();
}
std::optional<Weapon::ShootResult> WpnUber::shoot(ShootParams pars)
{
	if (pars.main_was) ray_ren->show = false;
	
	auto dirs_tuple = get_direction(pars);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;

	if (pars.main)
	{
		const float max_dist = 20;
		const float ray_width = 1; // full-width
		
		auto ent = equip->ent;
		b2Vec2 tar = conv(p + max_dist * v);
		auto hit = GameCore::get().get_phy().raycast_nearest(conv(p), tar, StdProjectile::make_cf(ent->index), ray_width);
		if (!hit) {
			hit = PhysicsWorld::RaycastResult{};
			hit->poi = tar;
		}
		
		ray_ren->b_last = conv(hit->poi);
		ray_ren->show = true;
		
		float dist = conv(hit->poi).dist(p) / max_dist;
		
		StdProjectile::Params pp;
		pp.dq.amount = lerp(450, 30, dist) * GameCore::time_mul;
		pp.imp = lerp(30, -5, dist);
		StdProjectile::explode(ent->get_team(), ent->index, conv(v), *hit, pp);
		
		GamePresenter::get()->effect(FE_EXPLOSION, {Transform{conv(hit->poi)}, lerp(0.2f, 0.07f, dist)});
		GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform(p, v.angle()), 0.3, FColor(1, 0.7, 0.5, 0.7)});
		
		ammo_skip_count = (ammo_skip_count + 1) % 3;
		return ShootResult{ammo_skip_count ? 0 : 1, {}, 0.3};
	}
	else if (pars.alt)
	{
		const int ammo = 30;
		if (!equip->has_ammo(*this, ammo)) return {};
		
		new ElectroBall(p, v);
		return ShootResult{ammo, {}, 1 / GameCore::time_mul};
	}
	return {};
}
