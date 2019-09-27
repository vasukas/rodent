#include "client/presenter.hpp"
#include "utils/noise.hpp"
#include "game_core.hpp"
#include "physics.hpp"
#include "weapon.hpp"



ModelType ammo_model(WeaponIndex wpn)
{
	switch (wpn)
	{
	case WeaponIndex::Bat:     return MODEL_ERROR;
	case WeaponIndex::Handgun: return MODEL_HANDGUN_AMMO;
	case WeaponIndex::Bolter:  return MODEL_BOLTER_AMMO;
	case WeaponIndex::Grenade: return MODEL_GRENADE_AMMO;
	case WeaponIndex::Minigun: return MODEL_MINIGUN_AMMO;
	case WeaponIndex::Rocket:  return MODEL_ROCKET_AMMO;
	case WeaponIndex::Electro: return MODEL_ELECTRO_AMMO;
	}
	return MODEL_ERROR;
}
bool Weapon::is_ready()
{
	if (auto m = get_rof() ; m && !m->ok()) return false;
	if (auto m = get_heat(); m && !m->ok()) return false;
	if (auto m = get_ammo(); m && !m->ok()) return false;
	return is_ready_internal();
}



void Weapon::ModAmmo::shoot()
{
	cur = std::max(cur - per_shot, 0.f);
}
bool Weapon::ModAmmo::add(float amount)
{
	if (cur >= max) return false;
	cur = std::min(cur + amount, max);
	return true;
}



void Weapon::ModOverheat::shoot()
{
	value += v_incr / shots_per_second;
	if (value >= 1) flag = true;
}
void Weapon::ModOverheat::cool()
{
	value -= (flag? v_cool : v_incr) * GameCore::time_mul;
	if (value < 0) value = 0;
	if (value < thr_off) flag = false;
}



EC_Equipment::EC_Equipment(Entity* ent)
    : EComp(ent)
{
	reg(ECompType::StepPostUtil);
}
EC_Equipment::~EC_Equipment()
{
	if (auto rc = ent->get_ren())
		rc->detach(ECompRender::ATT_WEAPON);
}
void EC_Equipment::step()
{
	if (last_req)
		set_wpn(*last_req);
	
	if (auto wpn = wpn_ptr())
	{
		if (auto m = wpn->get_heat()) if (!has_shot) m->cool();
		if (auto m = wpn->get_rof()) if (!m->ok()) m->wait -= GameCore::step_len;
		has_shot = false;
	}
	
	for (auto& wpn : wpns)
	{
		if (wpn.get() != wpn_ptr()) {
			if (auto m = wpn->get_rof())
				if (!m->ok()) m->wait -= GameCore::step_len;
		}
	}
}
bool EC_Equipment::shoot(vec2fp target)
{
	if (has_shot) return true;
	
	auto wpn = wpn_ptr();
	if (!wpn || !wpn->is_ready()) return false;
	
	has_shot = true;
	if (!wpn->shoot(ent, target)) return false;
	
	if (auto m = wpn->get_rof()) m->shoot();
	if (auto m = wpn->get_heat()) m->shoot();
	if (auto m = wpn->get_ammo(); m && !infinite_ammo) m->shoot();
	
	return true;
}
bool EC_Equipment::set_wpn(size_t index)
{
	if (index == wpn_cur) return true;
	
	// check if can be holstered
	if (auto wpn = wpn_ptr())
	{
		bool ok = true;
		if (auto m = wpn->get_heat()) ok &= m->ok();
		
		if (!ok)
		{
			last_req = index;
			return false;
		}
	}
	
	// check if can be equipped
	if (index != size_t_inval)
	{
		auto wpn = wpns[index].get();
		if (auto m = wpn->get_ammo(); m && !m->ok())
		{
			last_req.reset();
			return false;
		}
	}
	
	// reset
	if (auto rc = ent->get_ren())
		rc->detach(ECompRender::ATT_WEAPON);
	
	wpn_cur = index;
	
	last_req.reset();
	has_shot = false;
	
	// update
	if (auto wpn = wpn_ptr())
	{
		if (auto m = wpn->get_heat()) m->value = 0; // hack
		
		auto rc = ent->get_ren();
		if (rc) {
			Weapon::RenInfo ri = wpn->get_reninfo();
			int w_hand = ri.hand ? *ri.hand : hand;
			
			float r = ent->get_phy().get_radius();
			rc->attach( ECompRender::ATT_WEAPON, Transform{vec2fp(r, r/2 * w_hand)}, ri.model, FColor(1, 0.4, 0, 1) );
		}
	}
	return true;
}
Weapon* EC_Equipment::wpn_ptr()
{
	return wpn_cur != size_t_inval ? wpns[wpn_cur].get() : nullptr;
}
Weapon& EC_Equipment::get_wpn()
{
	return *wpns.at(wpn_cur);
}



struct StdProjectile : EComp
{
	enum Type
	{
		T_BULLET, ///< Only direct hit
		T_AOE, ///< Explosion
	};
	struct Params
	{
		DamageQuant dq = {DamageType::Kinetic, 0};
		Type type = T_BULLET;
		
		float rad = 1.f; ///< Area-of-effect radius
		float rad_min = 0.3f; ///< Minimal radius in damage calculation
		bool rad_full = false; ///< If true, radius ignored in damage calculation; otherwise linear fall-off is used
		
		float imp = 0.f; ///< Physical impulse applied to target
		bool trail = false; ///< If true, leaves particle trail
		
		float size = GameConst::hsz_proj; ///< Projectile radius
	};
	Params pars;
	EntityIndex src;
	
	
	
	StdProjectile(Entity* ent, const Params& pars, EntityIndex src)
	    : EComp(ent), pars(pars), src(src)
	{
		reg(ECompType::StepLogic);
	}
	void step() override
	{
		auto& self = dynamic_cast<EC_VirtualBody&>(ent->get_phy());
		if (pars.trail)
			ent->get_ren()->parts(FE_SPEED_DUST, {Transform({-self.radius, 0})});
		
		GameCore::get().valid_ent(src);
		
		const b2Vec2 vel = conv(self.get_vel().pos);
		const vec2fp ray0 = self.pos.pos,
		             rayd = self.get_vel().pos * GameCore::time_mul;
		
		vec2fp roff = rayd;
		roff.rot90cw();
		roff.norm_to(pars.size);
		
		std::optional<PhysicsWorld::RaycastResult> hit;
		PhysicsWorld::CastFilter check(
			[this](Entity* e, b2Fixture* f)
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
		
		if (!hit) hit = GameCore::get().get_phy().raycast_nearest(conv(ray0 + roff), conv(ray0 + rayd + roff), check);
		if (!hit) hit = GameCore::get().get_phy().raycast_nearest(conv(ray0 - roff), conv(ray0 + rayd - roff), check);
		if (!hit) {
			hit = GameCore::get().get_phy().raycast_nearest(conv(ray0 + rayd), conv(ray0), check);
			if (!hit) return;
			hit->poi = conv(ray0);
			hit->distance = 0;
		}
		
		auto apply = [&](Entity* tar, float k, b2Vec2 at, b2Vec2 v, std::optional<size_t> armor)
		{
			if (auto hc = tar->get_hlc(); hc && tar->get_team() != ent->get_team())
			{
				DamageQuant q = pars.dq;
				q.amount *= k;
				q.armor = armor;
				q.wpos = conv(at);
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
			if (hit->fix) armor = hit->fix->get_armor_index();
			apply(hit->ent, 1, hit->poi, vel, armor);
		}
		break;
			
		case T_AOE:
		{
			hit->poi -= conv(rayd * 0.1);
			
			GamePresenter::get()->effect( FE_WPN_EXPLOSION, {Transform{conv(hit->poi)}, pars.rad} );
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
				
				auto res = GameCore::get().get_phy().raycast_nearest(hit->poi, hit->poi + conv(d), check);
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
				apply(r.ent, k, r.poi, r.poi - hit->poi, r.armor);
			}
		}
		break;
		}
		
		ent->destroy();
	}
};



class ProjectileEntity : public Entity
{
public:
	EC_VirtualBody phy;
	EC_RenderSimple ren;
	StdProjectile proj;
	size_t team;
	
	
	ProjectileEntity(vec2fp pos, vec2fp vel, Entity* src, const StdProjectile::Params& pars, ModelType model, FColor clr)
	    :
	    phy(this, Transform{pos, vel.angle()}, Transform{vel}),
	    ren(this, model, clr),
	    proj(this, pars, src? src->index : EntityIndex{}),
	    team(src? src->get_team() : TEAM_FREEWPN)
	{}
	ECompPhysics& get_phy() override {return phy;}
	ECompRender* get_ren()  override {return &ren;}
	size_t get_team() const override {return team;}
};



class WpnMinigun : public Weapon
{
public:
	StdProjectile::Params pars;
	ModAmmo ammo;
	ModOverheat heat;
	
	WpnMinigun()
	    : ammo(WeaponIndex::Minigun, 1, 450, 100)
	{
		pars.dq.amount = 8.f;
		pars.imp = 5.f;
		
		heat.shots_per_second = 30;
		heat.thr_off = 0.1;
	}
	bool shoot(Entity* ent, vec2fp target) override
	{
		vec2fp p = ent->get_phy().get_pos();
		vec2fp v = target - p;
		
		v.norm();
		p += v * ent->get_phy().get_radius();
		
		v *= get_bullet_speed();
		v.rotate( GameCore::get().get_random().range(-1, 1) * deg_to_rad(10) );

		new ProjectileEntity(p, v, ent, pars, MODEL_MINIGUN_PROJ, FColor(1, 1, 0.2, 1.5));
		return true;
	}
	float get_bullet_speed() const override {return 18.f;}
	ModAmmo* get_ammo() override {return &ammo;}
	ModOverheat* get_heat() override {return &heat;}
	RenInfo get_reninfo() const override {return {"Minigun", MODEL_MINIGUN};}
};



class WpnRocket : public Weapon
{
public:
	StdProjectile::Params pars;
	ModRof rof;
	ModAmmo ammo;
	
	WpnRocket()
	    :
	    rof(TimeSpan::seconds(1)),
	    ammo(WeaponIndex::Rocket, 1, 40, 12)
	{
		pars.dq.amount = 120.f;
		pars.type = StdProjectile::T_AOE;
		pars.rad = 3.f;
		pars.rad_min = 0.f;
		pars.imp = 80.f;
		pars.trail = true;
	}
	bool shoot(Entity* ent, vec2fp target) override
	{
		vec2fp p = ent->get_phy().get_pos();
		vec2fp v = target - p;
		
		v.norm();
		p += v * ent->get_phy().get_radius();
		
		v *= get_bullet_speed();
		
		new ProjectileEntity(p, v, ent, pars, MODEL_ROCKET_PROJ, FColor(0.2, 1, 0.6, 1.5));
		return true;
	}
	float get_bullet_speed() const override {return 15.f;}
	ModRof* get_rof() override {return &rof;}
	ModAmmo* get_ammo() override {return &ammo;}
	RenInfo get_reninfo() const override {return {"Rocket", MODEL_ROCKET};}
};



#include "render/ren_aal.hpp"

class ElectroCharge : public Entity
{
public:
	struct SrcParams
	{
		size_t team;
		std::vector <Entity*> prev = {};
		
		bool ignore(Entity* ent)
		{
			auto it = std::find(prev.begin(), prev.end(), ent);
			bool ign = it != prev.end() || ent->get_team() == team;
			if (!ign) prev.push_back(ent);
			return ign;
		}
	};
	struct RenComp : ECompRender
	{
		struct Line {
			vec2fp a, b;
			float r;
		};
		std::vector<Line> ls;
		float t = 1, tps;
		
		RenComp(Entity* ent, std::vector<Line> ls, TimeSpan lt)
		    : ECompRender(ent), ls(std::move(ls)), tps(1.f / lt.seconds())
		{}
		void step() override
		{
			// aal width - 3
			for (auto& l : ls)
				RenAAL::get().draw_line(l.a, l.b, FColor(0.6, 0.85, 1).to_px(), 0.1f, l.r * 5.f, l.r * t * 2.5);
			t -= tps * GamePresenter::get()->get_passed().seconds();
		}
	};
	
	static constexpr float radius = 10.f;
	
	static std::vector<RenComp::Line> gen_ls(vec2fp a, vec2fp b, bool add)
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
	static bool generate(vec2fp pos, SrcParams src, vec2fp dir_lim, bool is_first)
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
			if (dot(dir_lim, (pos - r_poi).get_norm()) > -0.2) continue;
			
			auto& ro = rs_o.emplace_back();
			ro.hc = hc;
			ro.poi = r_poi;
			if (r.fix) ro.armor = r.fix->get_armor_index();
		}
		
		const size_t max_tars = 2;
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
			q.amount = 20;
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
	
	TimeSpan left = TimeSpan::seconds(0.3);
	EC_VirtualBody phy;
	RenComp ren;
	SrcParams src;
	vec2fp dir_lim;
	
	ElectroCharge(vec2fp pos, SrcParams src, std::vector<RenComp::Line> ls, vec2fp dir_lim)
		: phy(this, Transform{pos}), ren(this, std::move(ls), left), src(src), dir_lim(dir_lim)
	{
		reg();
	}
	ECompPhysics& get_phy() override {return phy;}
	ECompRender* get_ren() override {return &ren;}
	void step() override
	{
		left -= GameCore::step_len;
		if (left.is_negative())
		{
			generate(phy.get_pos(), src, dir_lim, false);
			destroy();
		}
	}
};



class WpnElectro : public Weapon
{
public:
	ModRof rof;
	ModAmmo ammo;
	
	WpnElectro()
	    :
	    rof(TimeSpan::seconds(0.3)),
	    ammo(WeaponIndex::Electro, 1, 60, 20)
	{}
	bool shoot(Entity* ent, vec2fp target) override
	{
		vec2fp p = ent->get_phy().get_pos();
		vec2fp v = target - p;
		
		v.norm();
		p += v * ent->get_phy().get_radius();
		
		return ElectroCharge::generate(p, {ent->get_team()}, v, true);
	}
	
	float get_bullet_speed() const override {return 0;}
	ModRof* get_rof() override {return &rof;}
	ModAmmo* get_ammo() override {return &ammo;}
	RenInfo get_reninfo() const override {return {"E-Chain", MODEL_ELECTRO, 0};}
};



Weapon* Weapon::create_std(WeaponIndex i)
{
	switch (i)
	{
	case WeaponIndex::Minigun:
		return new WpnMinigun;
		
	case WeaponIndex::Rocket:
		return new WpnRocket;
		
	case WeaponIndex::Electro:
		return new WpnElectro;
		
	case WeaponIndex::Bat:
	case WeaponIndex::Handgun:
	case WeaponIndex::Bolter:
	case WeaponIndex::Grenade:
		throw std::logic_error("Weapon::create_std() not implemented");
	}
	throw std::logic_error("Weapon::create_std() invalid index");
}
