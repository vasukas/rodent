#include "game_core.hpp"
#include "physics.hpp"
#include "presenter.hpp"
#include "weapon.hpp"



static const float safety_offset = 0.05;

static bool is_ph_bullet(float spd, float rad)
{
	spd *= GameCore::get().step_len.seconds();
	spd -= rad*2 - safety_offset;
	return spd > 0;
}



Projectile::Projectile(Entity* ent)
{
	EVS_CONNECT1(ent->getref<EC_Physics>().ev_contact, on_event);
}
void Projectile::on_event(const ContactEvent& ev)
{
	if (!ev.other->is_ok() || !ent->is_ok()) return;
	auto& self = ent->getref<EC_Physics>().body;
	
	auto apply = [&](Entity* tar, float k, b2Vec2 at)
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
				auto vel = self->GetLinearVelocity();
				vel.Normalize();
				vel *= k * pars.imp;
				
				GameCore::get().get_phy().post_step(
					[pc, vel, at]{ pc->body->ApplyLinearImpulse(vel, at, true); }
				);
			}
		}
	};
	
	switch (pars.type)
	{
	case T_BULLET:
		apply(ev.other, 1, self->GetWorldCenter());
		break;
		
	case T_AOE:
	{
		std::vector<PhysicsWorld::RaycastResult> cr;
		ent->core.get_phy().circle_cast_nearest(cr, self->GetWorldCenter(), pars.rad);
		for (auto& r : cr) {
			float k = pars.rad_full ? 1 : std::min(1.f, std::max((pars.rad - r.distance) / pars.rad, pars.rad_min));
			apply(r.ent, k, r.poi);
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
			del_cou -= GameCore::get().step_len;
		
		heat_cou -= heat_decr * GameCore::get().step_len.seconds();
		if (heat_cou < heat_off) heat_flag = false;
		if (heat_cou < 0) heat_cou = 0;
	}
	shot_was = false;
}
void Weapon::shoot(Transform from, Transform at, Entity* src)
{
	if (!can_shoot()) return;
	float shlen = std::max(shoot_delay.seconds(), GameCore::get().step_len.seconds());
	
	if (needs_ammo) ammo -= ammo_speed * shlen;
	del_cou = shoot_delay;
	heat_cou += heat_incr * shlen;
	heat_flag = heat_cou >= heat_on;
	shot_was = true;
	
	Entity* e = GameCore::get().create_ent();
	e->add(new EC_Render(e, proj_sprite));

	b2BodyDef def;
	def.type = b2_dynamicBody;
	def.bullet = is_ph_bullet(proj_spd, proj_radius);
	def.position = conv(from.pos);
	def.angle = (at.pos - from.pos).angle();
	def.linearVelocity = proj_spd * conv((at.pos - from.pos).get_norm());
	
	auto phy = e->add(new EC_Physics(def));
	b2FixtureDef fd;
	fd.filter.categoryBits = 0x8000; // disable collision between bullets
	fd.filter.maskBits = 0x7fff;
	fd.isSensor = true;
	phy->add_circle(fd, proj_radius, 0.5);
	
	auto prj = e->add(new Projectile(e));
	prj->pars = pars;
	if (is_homing) prj->target_pos = at;
	if (src) prj->src_eid = src->index;
}
void Weapon::shoot(Entity* parent, std::optional<Transform> at)
{
	if (!parent || !can_shoot()) return;
	
	Transform p0 = parent->get_pos();
	
	vec2fp dir = at? (at->pos - p0.pos).get_norm() : parent->get_norm_dir();
	p0.pos += dir * (parent->get_radius() + proj_radius + safety_offset);
	
	if (!at) {
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
	return !heat_flag && del_cou.is_negative() && (!needs_ammo || ammo > 0);
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
