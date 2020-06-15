#include "client/effects.hpp"
#include "client/presenter.hpp"
#include "client/sounds.hpp"
#include "utils/noise.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "weapon_all.hpp"



PhysicsWorld::CastFilter StdProjectile::make_cf(EntityIndex)
{
	return PhysicsWorld::CastFilter(
		[](auto& ent, b2Fixture& f)
		{
			if (!ent.is_ok())
				return false;
			
			if (f.IsSensor())
			{
				auto fi = get_info(f);
				if (fi && fi->armor_index) return true;
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
void StdProjectile::explode(GameCore& core, size_t src_team, EntityIndex src_eid,
                            b2Vec2 self_vel, PhysicsWorld::RaycastResult hit, const Params& pars)
{
	if (pars.type == T_AOE)
		SoundEngine::once(SND_ENV_EXPLOSION, conv(hit.poi));
	
	auto apply = [&](Entity& tar, float k, b2Vec2 at, b2Vec2 v, std::optional<size_t> armor)
	{
		if (pars.ignore_hack && std::type_index(typeid(tar)) == *pars.ignore_hack) return;
		if (auto hc = tar.get_hlc(); hc && (tar.get_team() != src_team || pars.friendly_fire > 0))
		{
			DamageQuant q = pars.dq;
			q.amount *= k;
			q.armor = armor;
			q.wpos = conv(at);
			q.src_eid = src_eid;
			if (tar.get_team() == src_team) q.amount *= pars.friendly_fire;
			hc->apply(q);
		}
		if (pars.imp != 0.f && v.LengthSquared() > 0.01)
		{
			v.Normalize();
			v *= k * pars.imp;
			tar.ref_phobj().body.ApplyLinearImpulse(v, at, true);
		}
	};
	
	switch (pars.type)
	{
	case T_BULLET:
	{	
		if (!hit.ent) break;
		std::optional<size_t> armor;
		if (hit.fix) armor = hit.fix->armor_index;
		apply(*hit.ent, 1, hit.poi, self_vel, armor);
		
		if (hit.fix && (hit.fix->typeflags & FixtureInfo::TYPEFLAG_WALL))
			SoundEngine::once(SND_ENV_BULLET_HIT, conv(hit.poi));
	}
	break;
		
	case T_AOE:
	{
		hit.poi -= 0.1 * GameCore::time_mul * self_vel;
		
		GamePresenter::get()->effect( FE_WPN_EXPLOSION, {Transform{conv(hit.poi)}, pars.rad * pars.particles_power} );
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
			
			auto res = core.get_phy().raycast_nearest(hit.poi, hit.poi + conv(d), make_cf(src_eid));
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
				if (res->fix) p->armor = res->fix->armor_index;
			}
		}
		
		if (hit.ent)
		{
			std::optional<size_t> armor;
			if (hit.fix) armor = hit.fix->armor_index;
			apply(*hit.ent, 1, hit.poi, self_vel, armor);
		}
		
		for (auto& r : os)
		{
			float k = pars.rad_full ? 1 : std::min(1.f, std::max((pars.rad - r.dist) / pars.rad, pars.rad_min));
			k = clampf(k, pars.aoe_min_k, pars.aoe_max_k);
			apply(*r.ent, k, r.poi, r.poi - hit.poi, r.armor);
		}
	}
	break;
	}
}
std::optional<vec2fp> StdProjectile::multiray(GameCore& core, vec2fp p, vec2fp v,
                                              callable_ref<void(PhysicsWorld::RaycastResult&, int)> on_hit,
                                              float full_width, float max_distance, int shoot_through, EntityIndex src_eid)
{
	constexpr int max_shoot_through = 10;
	if (shoot_through > max_shoot_through)
		throw std::logic_error("StdProjectile::multiray() shoot_through > max");
	
	b2Vec2 r_from = conv(p);
	b2Vec2 r_to = conv(p + max_distance * v);
	
	b2Vec2 skew = (r_to - r_from).Skew();
	skew.Normalize();
	skew *= full_width / 2;
	
	PhysicsWorld::RaycastResult hit;
	EntityIndex prev_hits[max_shoot_through + 1] = {}; // no target is hit twice
	
	for (int step = 0; step <= shoot_through; ++step)
	{
		PhysicsWorld::RaycastResult r_hits[3];
		int n_hits = 0;
		
		b2Vec2 r_offs[3] = {{0,0}, skew, -skew};
		auto cf = StdProjectile::make_cf(src_eid);
		
		auto f_cf = std::move(cf.check);
		cf.check = [&](Entity& ent, auto& fix) {
			if (!f_cf(ent, fix)) return false;
			for (int i=0; i<step; ++i) if (prev_hits[i] == ent.index) return false;
			return true;
		};

		//
		
		for (int i=0; i<3; ++i) {
			if (auto hit = core.get_phy().raycast_nearest(r_from + r_offs[i], r_to + r_offs[i], cf))
				r_hits[n_hits++] = *hit;
		}
		if (!n_hits) {
			if (step == shoot_through) return {};
			continue;
		}
		
		hit = r_hits[0];
		for (int i=1; i < n_hits; ++i)
		{
			auto& h = r_hits[i];
			if (h.ent->is_creature() && (!hit.ent->is_creature() || h.distance < hit.distance))
				hit = h;
		}
		
		if (!hit.fix || (hit.fix->typeflags & FixtureInfo::TYPEFLAG_WALL) == 0)
			prev_hits[step] = hit.ent->index;
		
		//
		
		on_hit(hit, step);
	}
	
	return p + v * conv(hit.poi).dist(p);
}



StdProjectile::StdProjectile(Entity& ent, const Params& pars, EntityIndex src, std::optional<vec2fp> target)
    : EComp(ent), pars(pars), src(src), target(target)
{
	reg(ECompType::StepLogic);
}
void StdProjectile::step()
{
	auto& pc = ent.ref_pc();
	if (pars.trail)
		ent.ensure<EC_RenderPos>().parts(FE_SPEED_DUST, {Transform({-pc.get_radius(), 0}), 0.6});
	
	ent.core.valid_ent(src);
	
	const float d_err = 0.1;
	const vec2fp k_vel = pc.get_vel();
	const vec2fp rayd = k_vel * GameCore::time_mul + k_vel.get_norm() * d_err * 2,
	             ray0 = pc.get_pos() - k_vel.get_norm() * d_err;
	
	if (target)
	{
		vec2fp p = ray0 - *target;
		vec2fp n = (ray0 + rayd) - *target;
		p *= n;
		if (p.x < 0 || p.y < 0)
		{
			PhysicsWorld::RaycastResult hit = {};
			hit.poi = conv(*target);
			explode(ent.core, ent.get_team(), src, conv(k_vel), hit, pars);
			ent.destroy();
			return;
		}
	}
	
	auto hit = ent.core.get_phy().raycast_nearest(conv(ray0), conv(ray0 + rayd), make_cf(src), pars.size);
	if (hit) {
		explode(ent.core, ent.get_team(), src, conv(k_vel), *hit, pars);
		hit_location = conv(hit->poi);
		ent.destroy();
	}
}
ProjectileEntity::ProjectileEntity(GameCore& core, vec2fp pos, vec2fp vel, std::optional<vec2fp> target,
                                   Entity* src, const StdProjectile::Params& pars, ModelType model, FColor clr)
    :
	Entity(core),
    phy(*this, Transform{pos, vel.angle()}, Transform{vel}),
    proj(*this, pars, src? src->index : EntityIndex{}, target),
    team(src? src->get_team() : TEAM_ENVIRON)
{
	// This prevents projectiles from going through walls at point-blank range.
	// Projectile is created by chain of: StepLogic -> EC_Equip -> Weapon,
	// so it is moved one step before logic (and collision check) is executed. 
	phy.pos.pos -= vel * GameCore::time_mul;
	
	add_new<EC_RenderModel>(model, clr);
}
ProjectileEntity::~ProjectileEntity()
{
	// fix display position
	vec2fp sz = ResBase::get().get_size(ref<EC_RenderModel>().model).size();
	Transform at{proj.hit_location, phy.pos.rot};
	at.combine(Transform{ -sz/2 });
	phy.pos = at;
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
	auto& core = equip->ent.core;

	if (pars.main)
	{
		float disp = lerp(8, 30, overheat->value); // was 10, fixed
		v *= info->bullet_speed;
		v.rotate( core.get_random().range_n2() * deg_to_rad(disp) );
		new ProjectileEntity(core, p, v, {}, &equip->ent, pp, MODEL_MINIGUN_PROJ, FColor(1, 1, 0.2, 1.5));
		
		sound(SND_WPN_MINIGUN, TimeSpan{});
		return ShootResult{};
	}
	else if (pars.alt)
	{
		const int num = 6;
		for (int i=0; i<num; ++i)
		{
			vec2fp lv = v;
			lv *= core.get_random().range(20, 28);
			lv.rotate( core.get_random().range_n2() * deg_to_rad(25) );
			new ProjectileEntity(core, p, lv, {}, &equip->ent, p2, MODEL_MINIGUN_PROJ, FColor(1, 1, 0.2, 1.5));
		}
		
		sound(SND_WPN_SHOTGUN);
		shoot_smoke(p, v);
		return ShootResult{num, TimeSpan::seconds(0.7)};
	}
	return {};
}



WpnMinigunTurret::WpnMinigunTurret()
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "Minigun turret";
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
	auto& core = equip->ent.core;

	v *= info->bullet_speed;
	v.rotate( core.get_random().range_n2() * deg_to_rad(10) );
	new ProjectileEntity(core, p, v, {}, &equip->ent, pp, MODEL_MINIGUN_PROJ, FColor(1, 1, 0.2, 1.5));
	
	sound(SND_WPN_MINIGUN_TURRET, TimeSpan{});
	return ShootResult{};
}



WpnRocket::WpnRocket(bool is_player)
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
	pp.dq.amount = is_player? 120 : 90;
	pp.type = StdProjectile::T_AOE;
	pp.rad = 4.f;
	pp.rad_min = 0.f;
	pp.imp = 80.f;
	pp.aoe_max_k = 80.f / pp.dq.amount;
	pp.aoe_min_k = 20.f / pp.dq.amount;
	pp.particles_power = 3 / pp.rad;
	pp.trail = true;
	if (is_player) pp.size = 0.7;
}
std::optional<Weapon::ShootResult> WpnRocket::shoot(ShootParams pars)
{
	if (!pars.main && !pars.alt) return {};

	auto dirs_tuple = get_direction(pars);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;
	auto& core = equip->ent.core;
	        
	shoot_smoke(p, v);
	v *= info->bullet_speed;
	
	std::optional<vec2fp> tar;
	if (pars.alt) tar = pars.target;
	
	new ProjectileEntity(core, p, v, tar, &equip->ent, pp, MODEL_ROCKET_PROJ, FColor(0.2, 1, 0.6, 1.5));
	sound(SND_WPN_ROCKET);
	return ShootResult{};
}



BarrageMissile::BarrageMissile(GameCore& core, vec2fp pos, vec2fp dir, EntityIndex target, bool armed_start)
	:
	Entity(core),
	phy(*this, bodydef(pos, true)),
	hlc(*this, 15),
	target(target)
{
	phy.add(FixtureCreate::circle(fixtdef(0, 2), GameConst::hsz_proj_big, 0.5, {}));
	phy.body.SetLinearVelocity(conv(dir * speed));
	add_new<EC_RenderModel>(MODEL_ROCKET_PROJ, FColor(1, 0.4, 0.1, 1.2), EC_RenderModel::DEATH_NONE);
	
	reg_this();
	EVS_CONNECT1(phy.ev_contact, on_cnt);
	
	if (!armed_start) left = TimeSpan::seconds(8.f / speed);
	armed = false;
}
BarrageMissile::~BarrageMissile()
{
	if (core.is_freeing()) return;
	
	StdProjectile::Params pp;
	pp.dq.amount = 80;
	pp.type = StdProjectile::T_AOE;
	pp.rad = 5;
	pp.rad_min = 1;
	pp.aoe_max_k = 0.8;
	pp.aoe_min_k = 0;
	pp.particles_power = 2 / pp.rad;
	pp.friendly_fire = 0.25;
	pp.ignore_hack = typeid(BarrageMissile);
	
	PhysicsWorld::RaycastResult ray = {};
	ray.poi = conv(get_pos());
	StdProjectile::explode(core, get_team(), {}, {0,0}, ray, pp);
}
void BarrageMissile::on_cnt(const CollisionEvent& ev)
{
	if (ev.type == CollisionEvent::T_RESOLVE && armed && ev.imp > speed / phy.body.GetMass())
		left = {};
}
void BarrageMissile::step()
{
	const TimeSpan lifetime = TimeSpan::seconds(20); // after armed
	const float dist_expl = 1; // explosion close
	const float max_speed = speed * 2.1;
	
	const float min_speed = speed * 0.1;
	const TimeSpan min_spd_max = TimeSpan::seconds(1);
	
	const float dist_precise = 2; // faster correction
	const float precise_k = 5;
	
	const TimeSpan reverse_time = TimeSpan::seconds(1.5); // how long to do 180 rotation
	const float shift_max = speed * 0.8;
	const float shift_spd = shift_max * (TimeSpan::seconds(1) / core.step_len);
	
	ref<EC_RenderPos>().parts(FE_SPEED_DUST, {Transform({-ref_pc().get_radius(), 0}), 0.25});
	if (armed) snd.update(*this, SoundPlayParams{SND_AUTOLOCK_PING}._t(left / lifetime));
	
	left -= core.step_len;
	if (left.is_negative())
	{
		if (!armed) {
			armed = true;
			left = lifetime;
		}
		else {
			destroy();
			return;
		}
	}
	
	auto vel = phy.body.GetLinearVelocity();
	if (vel.LengthSquared() > max_speed * max_speed + 1) {
		destroy();
		return;
	}
	if (vel.LengthSquared() < min_speed * min_speed) {
		min_spd_tmo += core.step_len;
		if (min_spd_tmo > min_spd_max) {
			destroy();
			return;
		}
	}
	else min_spd_tmo = {};
	
	if (auto tar = core.valid_ent(target);
	    tar && armed)
	{
		vec2fp dt = tar->get_pos() - get_pos();
		ref_pc().rot_override = dt.fastangle();
		
		float dist = dt.len();
		dist -= tar->ref_pc().get_radius();
		if (dist < dist_expl) {
			destroy();
			return;
		}
		
		shift.x += core.get_random().range_n2() * shift_spd;
		shift.y += core.get_random().range_n2() * shift_spd;
		shift.limit_to(shift_max);
		
		dt.norm_to(speed);
		dt += shift;
		
		float k_dist = dist > dist_precise ? 1 : precise_k;
		vel += (core.step_len / reverse_time * k_dist) * (conv(dt) - vel);
		phy.body.SetLinearVelocity(vel);
	}
	if (!target) {
		left = TimeSpan::seconds(core.get_random().range(0, 0.5));
	}
}



WpnBarrage::WpnBarrage()
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "Barrage";
			info->model = MODEL_UBERGUN;
			info->ammo = AmmoType::Rocket;
			info->def_ammo = 1;
			info->def_delay = TimeSpan::seconds(0.1);
			info->bullet_offset = vec2fp(-3 * GameConst::hsz_drone_hunter, 0);
		}
		return &*info;
	}())
{}
WpnBarrage::~WpnBarrage()
{
	if (detect) equip->ent.ref_phobj().destroy(detect);
}
std::optional<Weapon::ShootResult> WpnBarrage::shoot(ShootParams pars)
{
	auto dirs_tuple = get_direction(pars, DIRTYPE_IGNORE_ANGLE);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;
	auto& core = equip->ent.core;
	        
	if (!detect && cnt_num != -1)
	{
		auto phy = dynamic_cast<EC_Physics*>(&equip->ent.ref_pc());
		if (!phy) cnt_num = -1;
		else {
			detect = &phy->add(FixtureCreate::box(fixtsensor(), {3, 7.5}, 0));
			EVS_CONNECT1(phy->ev_contact, on_cnt);
		}
	}

	if (pars.main)
	{
		sound(SND_WPN_BARRAGE, *info->def_delay);
		
		auto res = core.get_phy().point_cast(conv(pars.target), 0.1, {[](auto& ent, auto&){ return ent.is_creature(); }});
		if (res) {
			bool armed;
			if (cnt_num <= 0) {
				v = -v.fastrotate((left ? 1 : -1) * deg_to_rad(45));
				armed = false;
			}
			else {
				p = equip->ent.get_pos() - v * equip->ent.ref_pc().get_radius() * 1.05;
				v = (-v).fastrotate((left ? 1 : -1) * deg_to_rad(15));
				armed = true;
			}
			left = !left;
			new BarrageMissile(core, p, v, res->ent->index, armed);
			return ShootResult{};
		}
		else if (auto m = equip->msgrep)
			m->jerr(WeaponMsgReport::ERR_NO_TARGET);
	}
	return {};
}
void WpnBarrage::on_cnt(const CollisionEvent& ev)
{
	if (ev.fix_phy == detect && ev.other->ref_phobj().body.GetType() == b2_staticBody)
		cnt_num += ev.type == CollisionEvent::T_BEGIN ? 1 : -1;
}



bool ElectroCharge::SrcParams::ignore(Entity* ent)
{
	auto it = std::find(prev.begin(), prev.end(), ent);
	bool ign = it != prev.end() || ent->get_team() == team;
	if (!ign) prev.push_back(ent);
	return ign;
}
bool ElectroCharge::generate(GameCore& core, vec2fp pos, SrcParams src, std::optional<vec2fp> dir_lim)
{
	PhysicsWorld::CastFilter check = StdProjectile::make_cf({});
	std::vector<PhysicsWorld::RaycastResult> rs;
	core.get_phy().circle_cast_nearest(rs, conv(pos), radius, check);
	
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
		
		auto rc = core.get_phy().raycast_nearest(conv(pos), conv(r.ent->get_pos()),
			{[&](auto& ent, auto& fix){
		         FixtureInfo* f = get_info(fix);
		         return &ent == r.ent || (f && (f->typeflags & FixtureInfo::TYPEFLAG_WALL));}});
		
		vec2fp r_poi = rc? conv(rc->poi) : r.ent->get_pos();
		if (dir_lim && dot(*dir_lim, (pos - r_poi).norm()) > -angle_lim) continue;
		
		auto& ro = rs_o.emplace_back();
		ro.hc = hc;
		ro.poi = r_poi;
		if (r.fix) ro.armor = r.fix->armor_index;
	}
	
	if (rs_o.size() > max_tars)
	{
		for (int i=0; i<max_tars; ++i)
			rs_o[i] = core.get_random().random_el(rs_o);
		rs_o.resize(max_tars);
	}
	for (auto& r : rs_o)
	{
		DamageQuant q;
		q.type = DamageType::Kinetic;
		q.amount = damage;
		q.wpos = r.poi;
		q.armor = r.armor;
		q.src_eid = src.src_eid;
		r.hc->apply(q);
		
		n_cs.push_back(r.poi);
		GamePresenter::get()->effect(FE_HIT_SHIELD, {Transform{r.poi}, 6});
	}
	
	bool is_first = (src.generation == 0);
	for (auto& n : n_cs) {
		effect_lightning(pos, n, is_first ? EffectLightning::First : EffectLightning::Regular, left_init);
		new ElectroCharge(core, n, src, (n - pos).norm());
	}
	
	return !n_cs.empty();
}
ElectroCharge::ElectroCharge(GameCore& core, vec2fp pos, SrcParams src, std::optional<vec2fp> dir_lim)
	:
	Entity(core),
	phy(*this, Transform{pos}),
    src(src), dir_lim(dir_lim)
{
	SoundEngine::once(SND_ENV_LIGHTNING, pos);
	reg_this();
}
void ElectroCharge::step()
{
	core.valid_ent(src.src_eid);
	
	left -= GameCore::step_len;
	if (left.is_negative())
	{
		if (src.generation != last_generation) {
			++src.generation;
			generate(core, phy.get_pos(), src, dir_lim);
		}
		destroy();
	}
}



struct WpnElectro_Pars
{
	TimeSpan alt_delay = TimeSpan::seconds(0.3); ///< Cooldown for alt fire
	float ai_alt_distance = -1; ///< Squared. If distance to target is smaller, primary replace with alt
	
	// primary fire
	TimeSpan charge_time = TimeSpan::seconds(1.8);
	TimeSpan wait_time = TimeSpan::seconds(3); ///< Wait before auto-discharge when fully charged
	int max_ammo = 6; ///< At max charge
	float max_damage = 270;
	TimeSpan max_cd = TimeSpan::seconds(1); ///< Cooldown at max charge
};
static const WpnElectro_Pars& get_wpr(WpnElectro::Type type)
{
	static std::array<WpnElectro_Pars, WpnElectro::T_TOTAL_COUNT_INTERNAL> ts;
	static bool inited = false;
	if (!inited)
	{
		{	auto& t = ts[WpnElectro::T_ONESHOT];
			t.max_damage = 16000;
		}
		{	auto& t = ts[WpnElectro::T_WORKER];
			//
			t.alt_delay = TimeSpan::seconds(1);
			t.ai_alt_distance = 4;
			//
			t.charge_time = TimeSpan::seconds(1.5);
			t.wait_time = {};
			t.max_damage = 50;
			t.max_cd = TimeSpan::seconds(1);
		}
		{	auto& t = ts[WpnElectro::T_CAMPER];
			//
			t.charge_time = TimeSpan::seconds(3);
			t.wait_time = TimeSpan::seconds(0.7);
			t.max_damage = 280;
			t.max_cd = TimeSpan::seconds(3);
		}
	}
	return ts[type];
}

WpnElectro::WpnElectro(Type type)
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "Bolter";
			info->model = MODEL_ELECTRO;
			info->hand = 0;
			info->ammo = AmmoType::Energy;
			info->def_ammo = 1;
			info->def_delay = TimeSpan::seconds(0.4); // minimal primary cooldown
			info->angle_limit = deg_to_rad(10);
			info->set_origin_from_model();
		}
		return &*info;
	}()),
	wpr(get_wpr(type))
{}
std::optional<Weapon::ShootResult> WpnElectro::shoot(ShootParams pars)
{
	auto dirs_tuple = get_direction(pars, DIRTYPE_FORWARD_FAIL);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;
	auto& core = equip->ent.core;
	        
	if (wpr.ai_alt_distance > 0)
	{
		if (pars.main)
		{
			if (!pars.main_was && wpr.ai_alt_distance > dirs_tuple->origin.dist_squ( pars.target ))
				ai_alt = true;
			
			if (ai_alt) {
				pars.main = pars.main_was = false;
				pars.alt = true;
			}
		}
		else if (pars.main_was && ai_alt)
		{
			pars.main = pars.main_was = false;
			pars.alt = true;
			ai_alt = false;
		}
	}

	auto& ent = equip->ent;
	if ((pars.main || pars.main_was) && pars.is_ok)
	{
		const float min_charge_lvl = 0.51f / wpr.max_ammo;
		const float ray_width = 2; // full-width
		
		if (pars.main)
		{
			if (charge_lvl < 1.f && equip->has_ammo(*this, wpr.max_ammo * charge_lvl))
			{
				charge_lvl += GameCore::time_mul * (1.f / wpr.charge_time.seconds());
				if (charge_lvl >= 1.f) charge_lvl = 1.f;
				GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform{p, v.angle()}, charge_lvl * 2, FColor(0, 1, 1, 0.15)});
			}
			else {
				charge_tmo += GameCore::step_len;
				GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform{p, v.angle()}, charge_lvl * 2.5f, FColor(1, 1, 1, 0.1)});
			}
			
			if (charge_tmo < wpr.wait_time) {
				sound(SND_WPN_BOLTER_CHARGE, TimeSpan{}, charge_lvl);
				return {};
			}
		}
		charge_lvl = std::max(charge_lvl, min_charge_lvl);
		
		//
		
		std::vector<vec2fp> interm_hit;
		
		vec2fp r_hit = StdProjectile::multiray(ent.core, p, v, [&, &v = v](auto& hit, int step)
		{
			float k_dmg = std::pow(2.f, -step);
			
			StdProjectile::Params pp;
			pp.dq.amount = wpr.max_damage * charge_lvl * k_dmg;
			pp.imp = 400 * charge_lvl;
			StdProjectile::explode(core, ent.get_team(), ent.index, conv(v), hit, pp);
			
			interm_hit.push_back(conv(hit.poi));
		}
		, ray_width, 1000, shoot_through, ent.index).value_or(p + v);
		
		// just for fun
		if (auto r = ent.core.get_phy().raycast_nearest(conv(p), conv(p + v * 80),
				{[](Entity& ent, b2Fixture&) {return typeid(ent) == typeid(ElectroBall);}, {}, false});
		    r && r->distance * r->distance < r_hit.dist_squ(p))
		{
			auto& body = r->ent->ref_phobj().body;
			auto vel = body.GetLinearVelocity();
			vel += conv(v * 70 * charge_lvl);
			body.SetLinearVelocity(vel);
			GamePresenter::get()->effect(FE_HIT_SHIELD, {Transform(conv(r->poi), v.angle() + M_PI), 4, FColor(0.6, 0.85, 1, 2)});
		}
		
		if (!interm_hit.empty()) interm_hit.pop_back();
		for (auto& p : interm_hit)
			GamePresenter::get()->effect(FE_HIT_SHIELD, {Transform(p, v.angle() + M_PI),
			                                             2, FColor(0.6, 0.85, 1, 1.2)});
		
		effect_lightning( p, r_hit, EffectLightning::Straight, TimeSpan::seconds(0.3) );
		GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform(r_hit, v.angle() + M_PI),
		                                             charge_lvl * 5, FColor(0.6, 0.85, 1, 1.2)});
		sound(SND_WPN_BOLTER_DISCHARGE);
		
		ShootResult res = {int_round(wpr.max_ammo * charge_lvl), std::max(wpr.max_cd * charge_lvl, *info->def_delay)};
		charge_lvl = 0.f;
		charge_tmo = {};
		return res;
	}
	
	if (pars.alt)
	{
		if (ElectroCharge::generate(core, p, {ent.get_team(), ent.index}, v)) {
			sound(SND_WPN_BOLTER_ELECTRO, wpr.alt_delay);
			return ShootResult{{}, wpr.alt_delay};
		}
		else {
			effect_lightning(p, p + v*2, EffectLightning::First, TimeSpan::seconds(0.3), FColor(0.5, 0.7, 1, 0.8));
			sound(SND_WPN_BOLTER_FAIL);
			return ShootResult{0, wpr.alt_delay};
		}
	}
	
	return {};
}
std::optional<Weapon::UI_Info> WpnElectro::get_ui_info()
{
	UI_Info inf;
	if (charge_lvl > 0.f) inf.charge_t = equip->has_ammo(*this, 1) ? charge_lvl : 1.f;
	return inf;
}
bool WpnElectro::is_preparing()
{
	return charge_lvl > 0.f;
}



bool FoamProjectile::can_create(GameCore& core, vec2fp pos, EntityIndex src_i)
{
	if (!src_i) return true;
	bool ok = true;
	
	core.get_phy().query_circle_all(conv(pos), 2,
	[&](auto& ent, auto&){
		if (src_i == ent.index) {
			ok = false;
			return false;
		}
		return true;
	});
	return ok;
}
FoamProjectile::FoamProjectile(GameCore& core, vec2fp pos, vec2fp vel, size_t team, EntityIndex src_i, bool is_first)
	:
	Entity(core),
	phy(*this, [&]
	{
		b2BodyDef d;
		d.type = b2_dynamicBody;
		d.position = conv(pos);
		d.linearVelocity = conv(vel);
		return d;
	}()),
    hlc(*this, 20),
	team(team),
    left(is_first? TimeSpan::seconds(4) : frozen_left),
    min_spd(vel.len_squ() / 4), // half of original
    is_first(is_first),
    src_i(src_i),
    vel_initial(vel)
{
	add_new<EC_RenderModel>(MODEL_GRENADE_PROJ_ALT, FColor(0.7, 1, 0.7));
	phy.add(FixtureCreate::circle( fixtdef(0, 1), 0.6, 1 ));
	
	if (is_first) EVS_CONNECT1(phy.ev_contact, on_event);
	reg_this();
}
FoamProjectile::~FoamProjectile()
{
	if (!core.is_freeing() && !frozen)
		freeze(false);
}
void FoamProjectile::step()
{
	if (src_i && !core.get_ent(src_i)) src_i = {};
	
	left -= GameCore::step_len;
	if (left.is_negative())
	{
		if (!frozen) freeze();
		else destroy();
		return;
	}
	
	if (!frozen)
	{
		float n = phy.get_vel().len_squ();
		if (n < min_spd) freeze();
		else if (n > 20*20) destroy();
	}
}
void FoamProjectile::on_event(const CollisionEvent& ev)
{
	if (ev.type == CollisionEvent::T_RESOLVE
	    && (ev.other->get_team() != team || typeid(*ev.other) == typeid(FoamProjectile))
	)
		freeze();
}
void FoamProjectile::freeze(bool is_normal)
{
	if (!can_create(core, phy.get_pos(), src_i)) {
		if (is_normal) destroy();
		return;
	}
	
	left = TimeSpan::seconds(12);
	frozen = true;
	ui_descr = "Foam";
	
	if (is_first)
	{
		vel_initial.norm_to(8);
		for (int i=0; i < (is_normal ? 12 : 4); ++i)
		{
			vec2fp dir = {phy.get_radius() - 0.2f, 0};
			dir.fastrotate( core.get_random().range_n2() * M_PI );
			new FoamProjectile(core, phy.get_pos() + dir, vel_initial, team, src_i, false);
		}
	}
	
	if (is_normal) {
		hlc.get_hp().renew(80);
		team = TEAM_ENVIRON;
		ref<EC_RenderModel>().clr = FColor(0.95, 1, 1);
	}
	
	phy.body.SetType(b2_staticBody);
	EVS_SUBSCR_UNSUB_ALL;
	
	static int counter = 0;
	if ((++counter) % 3 == 0) {
		snd.update(*this, SoundPlayParams{SND_WPN_FOAM_AMBIENT}._period({}));
		add_new<EC_ParticleEmitter>().effect(FE_FROST_AURA, {{}, 1.5f, FColor(1, 1, 1, 0.07), 0.5},
											 TimeSpan::seconds(rnd_stat().range(0.3, 0.5)), TimeSpan::nearinfinity);
	}
}



FireletProjectile::FireletProjectile(GameCore& core, vec2fp pos, vec2fp vel, size_t team, EntityIndex src_eid)
	:
	Entity(core),
	phy(*this, [&]
	{
		b2BodyDef d;
		d.type = b2_dynamicBody;
		d.position = conv(pos);
		d.linearVelocity = conv(vel);
		d.fixedRotation = true;
		return d;
	}()),
	left(TimeSpan::seconds(2.5)),
    tmo(TimeSpan::seconds(0.4)),
	team(team),
	src_eid(src_eid)
{
	float rest = core.get_random().range(0, 0.5);
	
	auto fc = FixtureCreate::circle( fixtdef(0.3, rest), 0.2, 6 );
	fc.fd.filter.maskBits     = ~EC_Physics::CF_FIRELET;
	fc.fd.filter.categoryBits =  EC_Physics::CF_FIRELET;
	phy.add(fc);
	
	reg_this();
	snd.update(*this, SoundPlayParams{SND_WPN_FIRE_LOOP}._period({}));
}
void FireletProjectile::step()
{
	const float dist = 3;
	const TimeSpan tmo_full = TimeSpan::seconds(0.2);
	const float full_dmg = 60  * tmo_full.seconds();
	const float foam_dmg = 200 * tmo_full.seconds();
	const float charge_per_hit = 0.34;
	const float ally_damage = 0.5;
	
	GamePresenter::get()->effect(FE_FIRE_SPREAD, {Transform(phy.get_pos()), 2.5, {}, M_PI});
	
	tmo -= GameCore::step_len;
	if (tmo.is_negative())
	{
		tmo = tmo_full;
		
		std::vector<PhysicsWorld::CastResult> es;
		core.get_phy().circle_cast_all(es, conv(get_pos()), dist, StdProjectile::make_cf({}));
		
		for (auto& e : es)
		{
			vec2fp poi;
			if (auto h = core.get_phy().raycast_nearest(conv(get_pos()), conv(e.ent->get_pos()), {
				[&](auto& ent, auto&){ return &ent == e.ent; }}))
			{
				poi = conv(h->poi);
			}
			else poi = e.ent->get_pos();
			
			float k = 1;
			if (e.ent->get_team() == team)
				k *= ally_damage;
			
			if (auto hc = e.ent->get_hlc())
			{
				bool is_foam = typeid(*e.ent) == typeid(FoamProjectile);
				
				DamageQuant q;
				q.type = DamageType::Kinetic;
				q.wpos = poi;
				q.amount = is_foam ? foam_dmg : full_dmg;
				if (e.fix) q.armor = e.fix->armor_index;
				q.src_eid = src_eid;
				
				q.amount *= k;
				hc->apply(q);
				
				if (is_foam) k = 0.1;
				charge -= charge_per_hit * k;
				if (charge < 0) {
					destroy();
					return;
				}
			}
		}
	}
	
	left -= GameCore::step_len;
	if (left.is_negative())
		destroy();
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
	auto& core = equip->ent.core;
	        
	auto& ent = equip->ent;
	if (pars.alt)
	{
		new FoamProjectile(core, p, v * 10, ent.get_team(), ent.index, true);
		sound(SND_WPN_FOAM_SHOOT);
		return ShootResult{};
	}
	if (pars.main)
	{
		const int num = 2;
		const float a0 = deg_to_rad(15); // half-spread
		const float ad = a0 * 2 / num;
		
		float a = -a0 + core.get_random().range_n() * ad;
		for (int i=0; i<num; ++i)
			new FireletProjectile(ent.core, p, 11 * vec2fp(v).fastrotate(a + i * ad), ent.get_team(), ent.index);
		
		ammo_skip_count = (ammo_skip_count + 1) % 2;
		sound(SND_WPN_FIRE_SHOOT, TimeSpan::seconds(0.3));
		return ShootResult{ammo_skip_count ? 0 : 1, TimeSpan::seconds(0.3)};
	}
	return {};
}



GrenadeProjectile::GrenadeProjectile(GameCore& core, vec2fp pos, vec2fp dir, EntityIndex src_eid)
    :
	Entity(core),
	phy(*this, [&]
	{
		b2BodyDef d;
		d.type = b2_dynamicBody;
		d.position = conv(pos);
		d.linearVelocity = conv(dir * 15);
		return d;
	}()),
    hlc(*this, 20),
    src_eid(src_eid),
    ignore_tmo(TimeSpan::seconds(0.3)),
    left(TimeSpan::seconds(6))
{
	add_new<EC_RenderModel>(MODEL_GRENADE_PROJ, FColor(1, 0, 0));
	snd.update(*this, {SND_AUTOLOCK_PING});
	
	phy.add(FixtureCreate::circle( fixtdef(0,1), GameConst::hsz_proj, 1 ));
	phy.add(FixtureCreate::circle( fixtsensor(), 2, 0 ));
	
	EVS_CONNECT1(phy.ev_contact, on_event);
	reg_this();
}
void GrenadeProjectile::step()
{
	core.valid_ent(src_eid);
	auto& ren = ref<EC_RenderModel>();
	
	float k = fracpart(clr_t);
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
		if (ev.fix_other->IsSensor()) return;
		if (ev.other->get_team() != TEAM_ENVIRON && ignore_tmo.is_negative())
		{
			vec2fp dir = phy.get_vel();
			float t = lineseg_perpen_t(get_pos(), get_pos() + dir, ev.other->get_pos());
			if		(t < 0) left = {};
			else if (t < 1) left = TimeSpan::seconds(t);
		}
	}
	else if (ev.type == CollisionEvent::T_RESOLVE)
	{
		if (ev.other->get_team() != TEAM_ENVIRON || dynamic_cast<GrenadeProjectile*>(ev.other)) explode();
		else {
			float angle = core.get_random().range_n2() * deg_to_rad(15);
			
			b2Vec2 vel = phy.body.GetLinearVelocity();
			vel = b2Mul(b2Rot(angle), vel);
			phy.body.SetLinearVelocity(vel);
		}
	}
}
void GrenadeProjectile::explode()
{
	PhysicsWorld::RaycastResult hit = {};
	hit.poi = phy.body.GetWorldCenter();
	
	StdProjectile::Params pp;
	pp.dq.amount = 120;
	pp.type = StdProjectile::T_AOE;
	pp.rad = 4;
	pp.rad_min = 1;
	pp.aoe_max_k = 0.8;
	pp.aoe_min_k = 0.2;
	pp.imp = 100;
	pp.friendly_fire = 1;
	
	StdProjectile::explode(core, TEAM_ENVIRON, src_eid, phy.body.GetLinearVelocity(), hit, pp);
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
			info->bullet_speed = 35;
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
	auto& core = equip->ent.core;
	        
	auto& ent = equip->ent;
	if (pars.main)
	{
		v *= info->bullet_speed;
		v.rotate( core.get_random().range(-1, 1) * deg_to_rad(2) );
	
		new ProjectileEntity(core, p, v, {}, &ent, pp, MODEL_MINIGUN_PROJ, FColor(0.6, 0.8, 1, 1.5));
		sound(SND_WPN_RIFLE, *info->def_delay);
		return ShootResult{};
	}
	
	if (pars.alt)
	{
		const int ammo = 8;
		if (!equip->has_ammo(*this, ammo))
		{
			if (auto m = equip->msgrep) m->no_ammo(ammo);
			return {};
		}
		
		new GrenadeProjectile(core, p, v, ent.index);
		sound(SND_WPN_GRENADE_SHOT);
		return ShootResult{ammo, TimeSpan::seconds(0.7)};
	}
	
	return {};
}



WpnSMG::WpnSMG()
    : Weapon([]{
		static std::optional<Info> info;
		if (!info) {
			info = Info{};
			info->name = "SMG";
			info->model = MODEL_HANDGUN;
			info->ammo = AmmoType::Bullet;
			info->def_ammo = 1;
			info->def_delay = TimeSpan::fps(30);
			info->def_heat = 1;
			info->bullet_speed = 28;
			info->set_origin_from_model();
		}
		return &*info;
	}())
{
	pp.dq.amount = 5.f;
	pp.imp = 20.f;
	
	overheat = Overheat{};
	overheat->v_decr = overheat->v_cool = 1 / 1.7;
	overheat->thr_off = 0.01;
}
std::optional<Weapon::ShootResult> WpnSMG::shoot(ShootParams pars)
{
	auto dirs_tuple = get_direction(pars);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;
	auto& core = equip->ent.core;

	if (pars.main)
	{
		v *= info->bullet_speed;
		v.rotate( core.get_random().range(-1, 1) * deg_to_rad(8) );
	
		new ProjectileEntity(core, p, v, {}, &equip->ent, pp, MODEL_MINIGUN_PROJ, FColor(0.9, 0.8, 0.5, 1.5));
		sound(SND_WPN_SMG, *info->def_delay);
		return ShootResult{};
	}
	return {};
}



ElectroBall::ElectroBall(GameCore& core, vec2fp pos, vec2fp dir)
	:
	Entity(core),
	phy(*this, [&]{
		b2BodyDef d;
		d.type = b2_kinematicBody;
		d.position = conv(pos);
		d.linearVelocity = conv(dir * speed_min);
		return d;
	}())
{
	ui_descr = "Plasma ball";
	add_new<EC_RenderModel>(MODEL_GRENADE_PROJ_ALT, FColor(0.4, 0.2, 1));
	snd.update(*this, SoundPlayParams{SND_WPN_UBER_AMBIENT}._period({}));
	
	phy.add(FixtureCreate::circle( fixtsensor(), expl_radius, 0 ));
	EVS_CONNECT1(phy.ev_contact, on_cnt);
	
	reg_this();
}
void ElectroBall::on_cnt(const CollisionEvent& ev)
{
	if (ev.other->is_creature())
	{
		if		(ev.type == CollisionEvent::T_BEGIN) ++explode_cntc;
		else if (ev.type == CollisionEvent::T_END)   --explode_cntc;
	}
}
void ElectroBall::step()
{
	GamePresenter::get()->effect(FE_CIRCLE_AURA, {Transform{get_pos()}, 0.005, FColor(0.4, 0.2, 1, 0.5)});
	
	const float find_radius = 22;
	
	if (tmo_ignore.is_positive()) {
		tmo_ignore -= GameCore::step_len;
		return;
	}
	
	if (tmo_explode.is_positive()) tmo_explode -= GameCore::step_len;
	else if (explode_cntc != 0 || explode_close || explode_left == 1)
	{
		auto& rnd = core.get_random();
		
		StdProjectile::Params pp;
		pp.type = StdProjectile::T_AOE;
		pp.dq.amount = 180;
		pp.rad = 4.5;
		pp.rad_full = true;
		pp.imp = 1000;
		
		PhysicsWorld::RaycastResult rc = {};
		rc.poi = conv(get_pos());
		StdProjectile::explode(core, TEAM_ENVIRON, {}, {0,0}, rc, pp);
		
		for (int i=0; i<4; ++i)
		{
			vec2fp v(rnd.range(3, 12), 0);
			v.rotate( rnd.range_n2() * M_PI );
			
			vec2fp p = get_pos() + v;
			ElectroCharge::generate( core, p, {TEAM_ENVIRON, {}}, {} );
			
			GamePresenter::get()->effect(FE_EXPLOSION_FRAG, {Transform{p}, 0.2});
			GamePresenter::get()->effect(FE_CIRCLE_AURA, {Transform{p}, 4, FColor(0.6, 0.2, 1, 3)});
		}
		
		if (!core.valid_ent(target_id))
			phy.body.SetLinearVelocity({0, 0});
		
		tmo_explode = TimeSpan::seconds(0.5);
		if	  (--explode_left == 0) destroy();
		else if (explode_left == 1) remove<EC_RenderModel>();
		return;
	}
	
	// movement
	
	if (auto tar = core.valid_ent(target_id))
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
		
		vec2fp vel = phy.get_vel();
		if (!explode_cntc) dt.fastrotate( deg_to_rad(-15) );
		vel += (dt - vel) * (GameCore::step_len / TimeSpan::seconds(0.8));
		phy.body.SetLinearVelocity(conv(vel));
		
		explode_close = (dist < expl_radius);
	}
	else
	{
		explode_close = false;
		
		auto& rnd = core.get_random();
		vec2fp rv( rnd.range(3, 18), 0 );
		rv.rotate( rnd.range_n2() * M_PI );
		
		if (tmo_target.is_positive()) tmo_target -= GameCore::step_len;
		else {
			std::vector<EntityIndex> es_tars;
			std::vector<Entity*> es_grav;
			
			core.get_phy().query_circle_all( conv(get_pos()), find_radius,
			[&](auto& ent, auto&) {
				if (ent.is_creature()) es_tars.push_back(ent.index);
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
		
		// push from walls & check bounds
		auto cell = core.get_lc().cell( core.get_lc().to_cell_coord( get_pos() ) );
		if (!cell) {
			destroy();
			return;
		}
		else if (cell->is_wall) {
			vec2fp dir = core.get_lc().ref_room(cell->room_nearest).fp_area().center() - get_pos();
			if (std::fabs(dir.x) > std::fabs(dir.y)) dir.y = 0;
			else dir.x = 0;
			rv += dir.norm_to(5);
		}
		
		// apply velocity
		vec2fp vel = phy.get_vel();
		rv.fastrotate( deg_to_rad(15) );
		vel += (rv - vel) * (GameCore::step_len / TimeSpan::seconds(2));
		phy.body.SetLinearVelocity(conv(vel));
	}
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
}
WpnUber::~WpnUber()
{
	equip->ent.remove<EC_Uberray>();
}
std::optional<Weapon::ShootResult> WpnUber::shoot(ShootParams pars)
{
	auto dirs_tuple = get_direction(pars, DIRTYPE_FORWARD_FAIL);
	if (!dirs_tuple) return {};
	auto [p, v] = *dirs_tuple;
	auto& core = equip->ent.core;

	if (pars.main)
	{
		const float min_dist = 5;
		const float max_dist = 20;
		const float ray_width = 1.5; // full-width
		
		auto& ent = equip->ent;
		float dist;
		
		auto r_hit = StdProjectile::multiray(ent.core, p, v, [&, &p = p, &v = v](auto& hit, auto)
		{
			dist = std::max(0.f, conv(hit.poi).dist(p) - min_dist) / (max_dist - min_dist);
			
			StdProjectile::Params pp;
			pp.dq.amount = lerp(450, 80, dist) * GameCore::time_mul;
			pp.imp = lerp(30, -5, dist);
			if (ent.get_team() == TEAM_ENVIRON) pp.friendly_fire = 1;
			StdProjectile::explode(core, ent.get_team(), ent.index, conv(v), hit, pp);
		}
		, ray_width, max_dist, 0, ent.index);
		
		if (r_hit) GamePresenter::get()->effect(FE_EXPLOSION, {Transform{*r_hit}, lerp(0.1f, 0.04f, dist)});
		else r_hit = p + v * max_dist;
		
		ent.ensure<EC_Uberray>().trigger(*r_hit);
		GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform(p, v.angle()), 0.3, FColor(1, 0.7, 0.5, 0.7)});
		
		ammo_skip_count = (ammo_skip_count + 1) % 5;
		sound(SND_WPN_UBER_RAY, TimeSpan{});
		return ShootResult{ammo_skip_count ? 0 : 1, {}, 0.3};
	}
	else if (pars.alt)
	{
		const int ammo = 30;
		if (!equip->has_ammo(*this, ammo))
		{
			if (auto m = equip->msgrep) m->no_ammo(ammo);
			return {};
		}
		
		new ElectroBall(core, p, v);
		sound(SND_WPN_UBER_LAUNCH);
		return ShootResult{ammo, {}, 1 / GameCore::time_mul};
	}
	return {};
}
