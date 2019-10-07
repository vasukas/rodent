#include "client/presenter.hpp"
#include "render/ren_aal.hpp"
#include "utils/noise.hpp"
#include "game_core.hpp"
#include "weapon_all.hpp"



PhysicsWorld::CastFilter StdProjectile::make_cf(EntityIndex src)
{
	return PhysicsWorld::CastFilter(
		[src](Entity* e, b2Fixture* f)
		{
			if (e->index == src) return false;
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
		if (auto hc = tar->get_hlc(); hc && tar->get_team() != src_team)
		{
			DamageQuant q = pars.dq;
			q.amount *= k;
			q.armor = armor;
			q.wpos = conv(at);
			q.src_eid = src_eid;
			hc->apply(q);
		}
		if (pars.imp != 0.f)
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
		ent->get_ren()->parts(FE_SPEED_DUST, {Transform({-self.radius, 0})});
	
	GameCore::get().valid_ent(src);
	
	const b2Vec2 vel = conv(self.get_vel().pos);
	const vec2fp ray0 = self.pos.pos,
	             rayd = self.get_vel().pos * GameCore::time_mul;
	
	if (target)
	{
		vec2fp p = ray0 - *target;
		vec2fp n = (ray0 + rayd) - *target;
		p *= n;
		if (p.x < 0 || p.y < 0)
		{
			PhysicsWorld::RaycastResult hit = {};
			hit.poi = conv(*target);
			explode(ent->get_team(), src, vel, hit, pars);
			ent->destroy();
			return;
		}
	}
	
	vec2fp roff = rayd;
	roff.rot90cw();
	roff.norm_to(pars.size);
	
	std::optional<PhysicsWorld::RaycastResult> hit;
	PhysicsWorld::CastFilter check = make_cf(src);
	
	if (!hit) hit = GameCore::get().get_phy().raycast_nearest(conv(ray0 + roff), conv(ray0 + rayd + roff), check);
	if (!hit) hit = GameCore::get().get_phy().raycast_nearest(conv(ray0 - roff), conv(ray0 + rayd - roff), check);
	if (!hit) {
		hit = GameCore::get().get_phy().raycast_nearest(conv(ray0 + rayd), conv(ray0), check);
		if (!hit) return;
		hit->poi = conv(ray0);
		hit->distance = 0;
	}
	
	explode(ent->get_team(), src, vel, *hit, pars);
	ent->destroy();
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
			info->def_heat = 0.3;
			info->bullet_speed = 18;
		}
		return &*info;
	}())
{
	pp.dq.amount = 8.f;
	pp.imp = 5.f;
	
	overheat = Overheat{};
	overheat->v_decr = *info->def_heat;
	overheat->thr_off = 0.1;
}
std::optional<Weapon::ShootResult> WpnMinigun::shoot(ShootParams pars)
{
	if (!pars.main && !pars.alt) return {};
	bool is_alt = pars.alt;
	
	auto ent = equip->ent;
	
	vec2fp p = ent->get_phy().get_pos();
	vec2fp v = pars.target - p;
	
	v.norm();
	p += v * ent->get_phy().get_radius();
	
	v *= info->bullet_speed;
	v.rotate( GameCore::get().get_random().range(-1, 1) * deg_to_rad(is_alt? 2 : 10) );

	new ProjectileEntity(p, v, {}, ent, pp, MODEL_MINIGUN_PROJ, FColor(1, 1, 0.2, 1.5));
	return ShootResult{{}, TimeSpan::seconds(is_alt? 0.2 : 0)};
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
		}
		return &*info;
	}())
{
	pp.dq.amount = 120.f;
	pp.type = StdProjectile::T_AOE;
	pp.rad = 3.f;
	pp.rad_min = 0.f;
	pp.imp = 80.f;
	pp.trail = true;
}
std::optional<Weapon::ShootResult> WpnRocket::shoot(ShootParams pars)
{
	if (!pars.main && !pars.alt) return {};
	auto ent = equip->ent;
	
	vec2fp p = ent->get_phy().get_pos();
	vec2fp v = pars.target - p;
	
	v.norm();
	p += v * ent->get_phy().get_radius();
	
	v *= info->bullet_speed;
	
	std::optional<vec2fp> tar;
	if (pars.alt) tar = pars.target;
	
	new ProjectileEntity(p, v, tar, ent, pp, MODEL_ROCKET_PROJ, FColor(0.2, 1, 0.6, 1.5));
	return ShootResult{};
}



bool ElectroCharge::SrcParams::ignore(Entity* ent)
{
	auto it = std::find(prev.begin(), prev.end(), ent);
	bool ign = it != prev.end() || ent->get_team() == team;
	if (!ign) prev.push_back(ent);
	return ign;
}
ElectroCharge::RenComp::RenComp(Entity* ent, std::vector<Line> ls, TimeSpan lt)
    : ECompRender(ent), ls(std::move(ls)), tps(1.f / lt.seconds())
{}
void ElectroCharge::RenComp::step()
{
	for (auto& l : ls)
		RenAAL::get().draw_line(l.a, l.b, FColor(0.6, 0.85, 1).to_px(), 0.1f, l.r * 5.f, l.r * t * 2.5);
	t -= tps * GamePresenter::get()->get_passed().seconds();
}



std::vector<ElectroCharge::RenComp::Line> ElectroCharge::gen_ls(vec2fp a, vec2fp b, bool add)
{
	const float min_len = 0.7;
	const float min_squ = min_len * min_len;
	
	std::vector<RenComp::Line> ls;
	ls.reserve(64);
	
	if (add) {
		auto rvec = [] {
			vec2fp p(rnd_stat().range(0.2, 1), 0);
			p.fastrotate( rnd_stat().range(-M_PI, M_PI) );
			return p;
		};
		ls.push_back({a, b + rvec(), 0.5});
		ls.push_back({a, b + rvec(), 0.1});
	}
	ls.push_back({a, b, 1});
	
	for (size_t i=0; i<ls.size(); ++i)
	{
		auto& ln = ls[i];
		float sl = ln.a.dist_squ(ln.b);
		if (sl > min_squ)
		{
			float d = rnd_stat().range(0.2, 0.8);
			float rot_k = std::max(min_len / std::sqrt(sl), 0.4f);
			
			vec2fp c = (ln.b - ln.a) * d;
			c.fastrotate( rnd_stat().range(-M_PI_2, M_PI_2) * rot_k );
			c += ln.a;
			
			vec2fp nb = ln.b;
			ln.b = c;
			ls.push_back({c, nb, ln.r});
		}
	}
	
	return ls;
}
bool ElectroCharge::generate(vec2fp pos, SrcParams src, std::optional<vec2fp> dir_lim, bool is_first)
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
	std::vector<PhysicsWorld::CastResult> rs;
	GameCore::get().get_phy().circle_cast_all(rs, conv(pos), radius, check);
	
	std::vector<std::pair<vec2fp, std::vector<RenComp::Line>>> n_cs;
	
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
		if (dir_lim && dot(*dir_lim, (pos - r_poi).get_norm()) > -angle_lim) continue;
		
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
		r.hc->apply(q);
		
		auto& n = n_cs.emplace_back();
		n.first = r.poi;
		n.second = gen_ls(pos, n.first, is_first);
		
		GamePresenter::get()->effect(FE_HIT_SHIELD, {Transform{r.poi}, 6});
	}
	
	for (auto& n : n_cs)
		new ElectroCharge(n.first, src, std::move(n.second), (n.first - pos).get_norm());
	
	return !n_cs.empty();
}
void ElectroCharge::step()
{
	left -= GameCore::step_len;
	if (left.is_negative())
	{
		generate(phy.get_pos(), src, dir_lim, false);
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
		}
		return &*info;
	}())
{}
std::optional<Weapon::ShootResult> WpnElectro::shoot(ShootParams pars)
{
	auto ent = equip->ent;
	
	vec2fp p = ent->get_phy().get_pos();
	vec2fp v = pars.target - p;
	
	v.norm();
	p += v * ent->get_phy().get_radius();
	
	if (pars.main || pars.main_was)
	{
		const TimeSpan charge_time = TimeSpan::seconds(3);
		const TimeSpan wait_time = TimeSpan::seconds(2);
		const float max_ammo = 10;
		const float max_damage = 200;
		const TimeSpan max_cd = TimeSpan::seconds(1);
		
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
		
		if (pars.main && charge_tmo < wait_time && equip->has_ammo(*this, max_ammo * charge_lvl)) return {};
		
		auto hit = GameCore::get().get_phy().raycast_nearest(conv(p), conv(p + 1000 * v), StdProjectile::make_cf(ent->index));
		if (!hit) return {};
		
		struct Ren
		{
			vec2fp a, b;
			float t, tps;
			
			Ren(vec2fp a, vec2fp b, TimeSpan lt) : a(a), b(b), t(1), tps(1.f / lt.seconds()) {}
			bool operator()(TimeSpan passed) {
				RenAAL::get().draw_line(a, b, FColor(0.6, 0.85, 1).to_px(), 0.1f, 5.f, t * 2.5);
				t -= tps * passed.seconds();
				return t > 0;
			}
		};
		GamePresenter::get()->add_effect(Ren( p, conv(hit->poi), TimeSpan::seconds(0.3) ));
		GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform(conv(hit->poi), v.angle() + M_PI),
		                                             charge_lvl * 20, FColor(0.6, 0.85, 1, 1.2)});
		
		StdProjectile::Params pp;
		pp.dq.amount = max_damage * charge_lvl;
		pp.imp = 400 * charge_lvl;
		StdProjectile::explode(ent->get_team(), ent->index, conv(v), *hit, pp);
		
		ShootResult res = {max_ammo * charge_lvl, max_cd * charge_lvl};
		charge_lvl = 0.f;
		charge_tmo = {};
		return res;
	}
	
	if (pars.alt)
	{
		bool ok = ElectroCharge::generate(p, {ent->get_team()}, v, true);
		if (ok) return ShootResult{};
	}
	
	return {};
}
std::optional<Weapon::UI_Info> WpnElectro::get_ui_info()
{
	if (charge_lvl > 0.f)
	{
		UI_Info inf;
		inf.charge_t = charge_lvl;
		return inf;
	}
	return {};
}



bool FoamProjectile::can_create(vec2fp pos, EntityIndex src_i)
{
	if (!src_i) return true;
	bool ok = true;
	
	std::vector<PhysicsWorld::CastResult> es;
	GameCore::get().get_phy().circle_cast_all(es, conv(pos), 2, {[&](auto ent, auto){
		if (src_i == ent->index) ok = false;
		return false;
	}});
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
    hlc(this, 30),
	team(team),
    left(TimeSpan::seconds(is_first? 4 : 0.1)),
    min_spd(vel.len_squ() / 4), // half of original
    is_first(is_first),
    src_i(src_i)
{
	b2FixtureDef fd;
	fd.friction = 0;
	fd.restitution = 1;
	phy.add_circle(fd, GameConst::hsz_proj_big, 1);
	
	EVS_CONNECT1(phy.ev_contact, on_event);
	reg();
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
		for (int i=0; i<3; ++i)
		{
			vec2fp dir = {phy.get_radius() + 0.1f, 0};
			dir.fastrotate( GameCore::get().get_random().range_n2() * M_PI );
			
			phw.post_step([p = phy.get_pos(), v = dir, team = team, src_i = src_i]{
				new FoamProjectile(p + v, v.get_norm() * 5, team, src_i, false);
			});
		}
	}
	
	hlc.get_hp().renew(80);
	
	phw.post_step([this]{ phy.body->SetType(b2_staticBody); });
	EVS_SUBSCR_FORCE_ALL;
	
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
			info->ammo = AmmoType::None;
			info->def_delay = TimeSpan::seconds(0.2);
		}
		return &*info;
	}())
{}
std::optional<Weapon::ShootResult> WpnFoam::shoot(ShootParams pars)
{
	if (pars.main)
	{
		auto ent = equip->ent;
		
		vec2fp p = ent->get_phy().get_pos();
		vec2fp v = pars.target - p;
		
		v.norm();
		p += v * ent->get_phy().get_radius();
		
		new FoamProjectile(p, v * 10, ent->get_team(), ent->index, true);
		return ShootResult{};
	}
	if (pars.alt)
	{
		auto ent = equip->ent;
		
		vec2fp p = ent->get_phy().get_pos();
		vec2fp v = pars.target - p;
		
		v.norm();
		p += v * ent->get_phy().get_radius();
		
		const size_t tars = 2;
		const int num = 6;
		const float a0 = deg_to_rad(40);
		const float ad = a0 * 2 / num;
		
		for (int i=0; i<num; ++i)
		{
			vec2fp d = v;
			d.fastrotate(-a0 + i * ad);
			
			std::vector<PhysicsWorld::RaycastResult> es;
			GameCore::get().get_phy().raycast_all(es, conv(p), conv(p + d * 8), StdProjectile::make_cf(ent->index));
			
			if (es.size() > tars) {
				std::sort(es.begin(), es.end(), [](auto& a, auto& b){return a.distance < b.distance;});
				es.resize(tars);
			}
			
			float k = 1;
			for (auto& e : es)
			{
				if (auto hc = e.ent->get_hlc())
				{
					DamageQuant q;
					q.type = DamageType::Kinetic;
					q.wpos = conv(e.poi);
					q.amount = dynamic_cast<FoamProjectile*>(e.ent) ? 35 : 5;
					if (e.fix) q.armor = e.fix->get_armor_index();
					
					GamePresenter::get()->effect(FE_EXPLOSION, {Transform(*q.wpos), 0.4f * k});
				
					q.amount *= k;
					hc->apply(q);
				}
				k /= 2;
			}
			
			GamePresenter::get()->effect(FE_WPN_CHARGE, {Transform(p, v.angle()), 5, FColor(1, 0.4, 0.2, 1)});
		}
		
		return ShootResult{};
	}
	return {};
}



GrenadeProjectile::GrenadeProjectile(vec2fp pos, vec2fp dir, size_t team)
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
	team(team),
    left(TimeSpan::seconds(6))
{
	b2FixtureDef fd;
	fd.friction = 0;
	fd.restitution = 1;
	phy.add_circle(fd, GameConst::hsz_proj, 1);
	
	EVS_CONNECT1(phy.ev_contact, on_event);
	reg();
}
void GrenadeProjectile::step()
{
	float k = std::fmod(clr_t, 1);
	k = (k < 0.5 ? k : 1 - k) * 2;
	ren.clr.g = k;
	ren.clr.b = k * 0.2;
	
	clr_t += 2 * GameCore::time_mul;
	
	left -= GameCore::step_len;
	if (left.is_negative())
		explode();
}
void GrenadeProjectile::on_event(const CollisionEvent& ev)
{
	if (ev.type == CollisionEvent::T_RESOLVE &&
	    ev.other->get_team() != team &&
	    ev.other->get_team() != TEAM_ENVIRON
	   )
		explode();
}
void GrenadeProjectile::explode()
{
	PhysicsWorld::RaycastResult hit = {};
	hit.poi = phy.body->GetWorldCenter();
	
	StdProjectile::Params pp;
	pp.dq.amount = 200;
	pp.type = StdProjectile::T_AOE;
	pp.rad = 4;
	pp.rad_min = 1;
	pp.aoe_max_k = 0.7;
	pp.imp = 100;
	
	StdProjectile::explode(team, {}, phy.body->GetLinearVelocity(), hit, pp);
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
			info->bullet_speed = 20;
		}
		return &*info;
	}())
{
	pp.dq.amount = 18.f;
	pp.imp = 7.f;
}
std::optional<Weapon::ShootResult> WpnRifle::shoot(ShootParams pars)
{
	auto ent = equip->ent;
	
	vec2fp p = ent->get_phy().get_pos();
	vec2fp v = pars.target - p;
	
	v.norm();
	p += v * ent->get_phy().get_radius();
	
	if (pars.main)
	{
		v *= info->bullet_speed;
		v.rotate( GameCore::get().get_random().range(-1, 1) * deg_to_rad(2) );
	
		new ProjectileEntity(p, v, {}, ent, pp, MODEL_MINIGUN_PROJ, FColor(0.6, 0.8, 1, 1.5));
		return ShootResult{};
	}
	
	if (pars.alt)
	{
		new GrenadeProjectile(p, v, ent->get_team());
		return ShootResult{3, TimeSpan::seconds(0.7)};
	}
	
	return {};
}
