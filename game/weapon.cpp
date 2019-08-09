#include "utils/noise.hpp"
#include "game_core.hpp"
#include "physics.hpp"
#include "presenter.hpp"
#include "weapon.hpp"



Projectile::Projectile()
{
	reg(ECompType::StepLogic);
}
void Projectile::step()
{
	auto& self = ent->getref<EC_VirtualBody>();
	if (pars.trail)
		ent->getref<EC_Render>().parts(OE_DUST);
	
	const b2Vec2 vel = conv(self.get_vel().pos);
	const vec2fp ray0 = self.pos.pos, rayd = self.get_vel().pos * GameCore::time_mul();
	
	auto hit = GameCore::get().get_phy().raycast_nearest(conv(ray0), conv(ray0 + rayd));
	if (!hit) return;
	
	auto apply = [&](Entity* tar, float k, b2Vec2 at, b2Vec2 v)
	{
		if (auto hc = tar->get<EC_Health>()) {
			DamageQuant q = pars.dq;
			q.amount *= k;
			hc->damage(q);
		}
		if (pars.imp != 0.f)
		{
			auto pc = tar->get<EC_Physics>();
			if (pc) {
				v.Normalize();
				v *= k * pars.imp;
				
				GameCore::get().get_phy().post_step(
					[pc, v, at]{ pc->body->ApplyLinearImpulse(v, at, true); }
				);
			}
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
		
		GamePresenter::get().effect( pars.rad_full? FE_WPN_IMPLOSION : FE_WPN_EXPLOSION, Transform{conv(hit->poi)}, pars.rad );
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



void Weapon::step()
{
	if (!shot_was)
	{
		if (!del_cou.is_negative())
			del_cou -= GameCore::step_len;
		
		heat_cou -= heat_decr * GameCore::time_mul();
		if (heat_cou < heat_off) heat_flag = false;
		if (heat_cou < 0) heat_cou = 0;
	}
	shot_was = false;
}
void Weapon::shoot(Transform from, Transform at, Entity* src)
{
	if (!can_shoot()) return;
	float shlen = std::max((float)shoot_delay.seconds(), GameCore::time_mul());
	
	if (needs_ammo) ammo -= ammo_speed * shlen;
	del_cou = shoot_delay;
	heat_cou += heat_incr * shlen;
	heat_flag = heat_cou >= heat_on;
	shot_was = true;
	
	Entity* e = GameCore::get().create_ent();
	e->add(new EC_Render(e, proj_sprite));
	
	from.rot = (at.pos - from.pos).angle();
	vec2fp vel = proj_spd * (at.pos - from.pos).get_norm();
	e->add(new EC_VirtualBody(from, true))->set_vel(Transform{vel});
	
	auto prj = e->add(new Projectile);
	prj->pars = pars;
	if (is_homing) prj->target_pos = at;
	if (src) prj->src_eid = src->index;
}
void Weapon::shoot(Entity* parent, std::optional<Transform> at)
{
	if (!parent || !can_shoot()) return;
	
	Transform p0 = parent->get_pos();
	
	vec2fp dir = at? (at->pos - p0.pos).get_norm() : parent->get_norm_dir();
	if (disp) dir.rotate( rnd_range(-*disp, *disp) );
	p0.pos += dir * (parent->get_radius() + proj_radius);
	
	if (!at || disp) {
		*at = p0;
		at->pos += dir;
	}
	
	shoot(p0, *at, parent);
}
void Weapon::reset()
{
	del_cou = TimeSpan::ms(-1);
	heat_cou = 0.f;
	heat_flag = false;
	shot_was = false;
}
bool Weapon::can_shoot() const
{
	return !heat_flag && !del_cou.is_positive() && (!needs_ammo || ammo > 0);
}



EC_Equipment::EC_Equipment()
{
	reg(ECompType::StepPostUtil);
}
EC_Equipment::~EC_Equipment()
{
	if (wpn_ren_id != size_t_inval) {
		auto rc = ent->get<EC_Render>();
		if (rc) rc->detach(wpn_ren_id);
	}
}
void EC_Equipment::step()
{
	if (auto w = wpn_ptr())
		w->step();
}
bool EC_Equipment::set_wpn(size_t index)
{
	if (index == wpn_cur) return true;
	
	auto old = wpn_ptr();
	if (old && !old->can_shoot()) return false;
	
	if (wpn_ren_id != size_t_inval) {
		auto rc = ent->get<EC_Render>();
		if (rc) rc->detach(wpn_ren_id);
	}
	
	wpn_cur = index;
	wpn_ren_id = size_t_inval;
	
	if (auto w = wpn_ptr())
	{
		w->reset();
		
		auto rc = ent->get<EC_Render>();
		if (rc) wpn_ren_id = rc->attach(w->ren_id, w->ren_tr);
	}
	return true;
}
Weapon* EC_Equipment::wpn_ptr()
{
	return wpn_cur != size_t_inval ? &wpns[wpn_cur] : nullptr;
}
Weapon& EC_Equipment::get_wpn()
{
	return wpns.at(wpn_cur);
}
