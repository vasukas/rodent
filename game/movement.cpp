#include "game_core.hpp"
#include "movement.hpp"
#include "physics.hpp"
#include "presenter.hpp"



void EC_Movement::set_target(vec2fp pos)
{
	t_type = T_POS;
	t_tr.pos = pos;
	t_tr.rot = 0.f;
}
void EC_Movement::set_target_velocity(Transform vel)
{
	t_type = T_VEL;
	t_tr = vel;
}
void EC_Movement::set_target(EntityIndex eid, float dist)
{
	t_type = T_EID;
	t_eid = eid;
	t_dist = std::max(dist, base_dist);
}
bool EC_Movement::has_reached()
{
	switch (t_type)
	{
	case T_NONE: return true;
	case T_POS: return ent->get_pos().pos.dist( t_tr.pos ) < base_dist;
	case T_VEL:
		{	auto av = ent->get_vel();
			return av.pos.equals( t_tr.pos, vel_eps ) && 
			       aequ(av.rot, t_tr.rot, vel_eps); }
	case T_EID:
		{	auto te = ent->core.get_ent(t_eid);
			if (!te) t_type = T_NONE;
			else if (ent->get_pos().pos.dist( te->get_pos().pos ) > t_dist)
				return false;
			return true;
		}
	}
	return true;
}
void EC_Movement::step()
{
	const float tmul = ent->core.step_len.seconds();
	
	if (!has_reached())
	{
		vec2fp vel_req; // required velocity
		
		if (t_type == T_VEL) vel_req = t_tr.pos;
		else {
			vec2fp tar;
			
			if (t_type == T_POS) tar = t_tr.pos;
			else {
				auto te = ent->core.get_ent(t_eid);
				if (!te) tar = te->get_pos().pos;
				else {
					t_type = T_NONE;
					tar = ent->get_pos().pos;
				}
			}
			
			vel_req = tar - ent->get_pos().pos;
		}
		
		auto limvch = [&](auto& vch, float l)
		{
			if (l < vel_eps) return;
			else if (l > max_ch) vch *= max_ch / l;
			else if (l < min_ch) vch *= 1.f / tmul;
		};
		if (ent->setpos)
		{
			auto& v = ent->setpos->vel;
			
			auto d = vel_req - v.pos;
			limvch(d, d.len());
			v.pos += d;
			
			v.rot = t_tr.rot;
			t_tr.rot = 0.f;
		}
		else
		{
			b2Body* body = ent->get_phy()->body;
			const b2Vec2 vel = body->GetLinearVelocity();
			const b2Vec2 tar = conv(t_tr.pos);
			
			b2Vec2 vec = tar - vel;
			float xd = fabs(tar.x) - fabs(vel.x);
			float yd = fabs(tar.y) - fabs(vel.y);
			
			const float k1 = max_ch * 0.5, k2 = max_ch;
			if (xd > 0) vec.x *= k1; else vec.x *= inertial_mode? 2 : (xd > 1.0 ? k2 : k1);
			if (yd > 0) vec.y *= k1; else vec.y *= inertial_mode? 2 : (yd > 1.0 ? k2 : k1);
			
			float dl = vec.Length();
			if (dl != vel_eps)
			{
				limvch(vec, dl);
				body->ApplyLinearImpulse(body->GetMass() * tmul * vec, body->GetWorldCenter(), true);
				
				float ad = t_tr.rot - body->GetAngularVelocity();
				if (ad > vel_eps) {
					body->ApplyAngularImpulse(ad * tmul, true);
					t_tr.rot = body->GetAngularVelocity();
				}
			}
		}
	}
	
	if (use_damp)
	{
		if (ent->setpos)
		{
			auto& v = ent->setpos->vel;
			v.pos += tmul * -damp_lin * v.pos;
			v.rot += tmul * -damp_ang * v.rot;
		}
		else
		{
			b2Body* body = ent->get_phy()->body;
			const b2Vec2 vel = body->GetLinearVelocity();
			b2Vec2 damp = -damp_lin * vel;
			
			if (float l = damp.Length(); l > damp_max)
				damp *= damp_max / l;
			
			b2Vec2 imp = tmul * damp;
			body->ApplyLinearImpulse(body->GetMass() * imp, body->GetWorldCenter(), false);
			
			float ai = -damp_ang * body->GetAngularVelocity();
			ai *= tmul;
			body->ApplyAngularImpulse(body->GetMass() * ai, false);
		}
	}
	
	if (dust_vel != 0.f && ent->get_ren())
	{
		vec2fp vel_lin = ent->get_vel().pos;
		float vel_amp = vel_lin.len();
		if (vel_amp > dust_vel)
		{
			float p = vel_amp - dust_vel;
			Transform tr;
			tr.rot = vel_lin.angle();
			tr.pos = {-ent->get_radius(), 0};
			tr.pos.fastrotate(tr.rot);
			ent->get_ren()->parts(OE_DUST, p, tr);
		}
	}
}
