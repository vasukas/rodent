#include "client/presenter.hpp"
#include "utils/noise.hpp"
#include "game_core.hpp"
#include "physics.hpp"
#include "weapon.hpp"



void Weapon::ModAmmo::shoot()
{
	cur = std::max(cur - per_shot, 0.f);
}
bool Weapon::ModAmmo::add(float amount)
{
	if (cur == max) return false;
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
	value -= v_decr * GameCore::time_mul;
	if (value < 0) value = 0;
	if (value < thr_off) flag = false;
}



EC_Equipment::EC_Equipment(Entity* ent):
    EComp(ent)
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
}
bool EC_Equipment::shoot(vec2fp target)
{
	if (has_shot) return true;
	
	auto wpn = wpn_ptr();
	if (!wpn) return false;
	
	if (auto m = wpn->get_rof() ; m && !m->ok()) return false;
	if (auto m = wpn->get_heat(); m && !m->ok()) return false;
	if (auto m = wpn->get_ammo(); m && !m->ok()) return false;
	if (!wpn->shoot(ent, target)) return false;
	
	if (auto m = wpn->get_rof()) m->shoot();
	if (auto m = wpn->get_heat()) m->shoot();
	if (auto m = wpn->get_ammo(); m && !infinite_ammo) m->shoot();
	
	has_shot = true;
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
		if (auto m = wpn->get_rof())  ok &= m->ok();
		
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
			return false;
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
			float r = ent->get_phy().get_radius();
			rc->attach( ECompRender::ATT_WEAPON, Transform{vec2fp(r, r/2 * hand)}, ri.model, FColor(1, 0.4, 0, 1) );
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
	};
	Params pars;
	
	
	StdProjectile(Entity* ent, const Params& pars):
	    EComp(ent), pars(pars)
	{
		reg(ECompType::StepLogic);
	}
	void step() override
	{
		auto& self = dynamic_cast<EC_VirtualBody&>(ent->get_phy());
		if (pars.trail)
			ent->get_ren()->parts(FE_SPEED_DUST, {Transform({-self.radius, 0})});
		
		const b2Vec2 vel = conv(self.get_vel().pos);
		const vec2fp ray0 = self.pos.pos, rayd = self.get_vel().pos * GameCore::time_mul;
		
		auto hit = GameCore::get().get_phy().raycast_nearest(conv(ray0), conv(ray0 + rayd));
		if (!hit) return;
		
		auto apply = [&](Entity* tar, float k, b2Vec2 at, b2Vec2 v)
		{
			if (tar->get_team() == ent->get_team()) return;
			if (auto hc = tar->get_hlc()) {
				DamageQuant q = pars.dq;
				q.amount *= k;
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
			apply(hit->ent, 1, hit->poi, vel);
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
			};
			std::vector<Obj> os;
			os.reserve(num);
			
			for (int i=0; i<num; ++i)
			{
				vec2fp d(pars.rad, 0);
				d.rotate(2*M_PI*i/num);
				
				auto res = GameCore::get().get_phy().raycast_nearest(hit->poi, hit->poi + conv(d));
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
					auto& p = os.emplace_back();
					p.ent = res->ent;
					p.dist = res->distance;
					p.poi = res->poi;
				}
			}
			
			for (auto& r : os)
			{
				float k = pars.rad_full ? 1 : std::min(1.f, std::max((pars.rad - r.dist) / pars.rad, pars.rad_min));
				apply(r.ent, k, r.poi, r.poi - hit->poi);
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
	
	
	ProjectileEntity(vec2fp pos, vec2fp vel, size_t team, const StdProjectile::Params& pars, ModelType model, FColor clr)
	    :
	    phy(this, Transform{pos, vel.angle()}, Transform{vel}),
	    ren(this, model, clr),
	    proj(this, pars),
	    team(team)
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
	
	WpnMinigun():
	    ammo(1, 450, 100)
	{
		pars.dq.amount = 20.f;
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
		
		v *= 18.f;
		v.rotate( GameCore::get().get_random().range(-1, 1) * deg_to_rad(10) );
		
		new ProjectileEntity(p, v, ent->get_team(), pars, MODEL_MINIGUN_PROJ, FColor(1, 1, 0.2, 1.5));
		return true;
	}
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
	
	WpnRocket():
	    rof(TimeSpan::seconds(1)),
	    ammo(1, 40, 12)
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
		
		v *= 15.f;
		
		new ProjectileEntity(p, v, ent->get_team(), pars, MODEL_ROCKET_PROJ, FColor(0.2, 1, 0.6, 1.5));
		return true;
	}
	ModRof* get_rof() override {return &rof;}
	ModAmmo* get_ammo() override {return &ammo;}
	RenInfo get_reninfo() const override {return {"Rocket", MODEL_ROCKET};}
};



Weapon* Weapon::create_std(WeaponIndex i)
{
	switch (i)
	{
	case WeaponIndex::Minigun:
		return new WpnMinigun;
		
	case WeaponIndex::Rocket:
		return new WpnRocket;
		
	case WeaponIndex::Bat:
	case WeaponIndex::Handgun:
	case WeaponIndex::Bolter:
	case WeaponIndex::Grenade:
	case WeaponIndex::Electro:
		throw std::logic_error("Weapon::create_std() not implemented");
	}
	throw std::logic_error("Weapon::create_std() invalid index");
}
