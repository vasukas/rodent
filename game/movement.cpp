#include "game_core.hpp"
#include "movement.hpp"
#include "physics.hpp"
#include "presenter.hpp"

const float tar_near = 0.01;



EC_Movement::EC_Movement()
{
	reg_step(GameStepOrder::Move);
}
void EC_Movement::set_app_vel(vec2fp v)
{
	auto set = [this](TarDir& d, float v)
	{
		if (std::fabs(v) < tar_near) {
			if (d.st == T_ZERO) return;
			d.st = T_ZERO;
			d.left = dec_inert;
		}
		else {
			d.st = T_VEL;
			d.vel = v;
		}
	};
	set(tarx, v.x);
	set(tary, v.y);
}
void EC_Movement::step()
{
	b2Body* body = ent->getref<EC_Physics>().body;
	const b2Vec2 vel = body->GetLinearVelocity();
	const float tmul = ent->core.step_len.seconds();
	
	b2Vec2 corr{0, 0};
	bool wake = (tarx.st != T_NONE) || (tary.st != T_NONE);
	
	if (wake)
	{
		auto proc = [this](TarDir& d, float cur) -> float
		{
			if (d.st == T_NONE) return 0.f;
			if (d.st == T_VEL)
			{
				if (d.vel > 0) {
					if (d.vel > cur) return std::min(d.vel - cur, d.vel);
				}
				else if (d.vel < cur) return std::max(d.vel - cur, d.vel);
			}
			else if (d.st == T_ZERO)
			{
				d.left -= GameCore::get().step_len;
				if (d.left.is_negative())
					d.st = T_NONE;
				
				float k = app_inert.seconds() / dec_inert.seconds();
				
				if (d.vel > 0) {
					if (cur < 0) d.st = T_NONE;
					else return -std::min(cur, d.vel) * k;
				}
				else if (cur > 0) d.st = T_NONE;
				else return -std::max(cur, d.vel) * k;
			}
			return 0.f;
		};
		corr.x = proc(tarx, vel.x);
		corr.y = proc(tary, vel.y);
		corr *= app_inert.seconds();
	}
	
	if (!wake && vel.Length() < damp_minthr) corr = -vel;
	else if (damp_lin != 0.f)
	{
		if (vel.Length() < damp_lin) corr -= tmul * vel;
		else {
			b2Vec2 nv = vel;
			nv.Normalize();
			corr -= damp_lin * tmul * nv;
		}
//		corr += -damp_lin * tmul * vel;
	}
	else
	{
		b2Vec2 forwd = body->GetWorldVector({0, -1});
		corr += -damp_sep.x * tmul * b2Dot(forwd, vel) * forwd;
		
		b2Vec2 right = body->GetWorldVector({1, 0});
		corr += -damp_sep.y * tmul * b2Dot(right, vel) * right;
	}
	
	body->ApplyLinearImpulseToCenter(body->GetMass() * corr, wake);
	
	float ai = -damp_ang * tmul * body->GetAngularVelocity();
	body->ApplyAngularImpulse(body->GetMass() * ai, wake);
	
	
	
	if (dust_vel == 0.f) return;
	auto rc = ent->get<EC_Render>();
	if (!rc) return;
	
	float amp = vel.Length();
	if (amp > dust_vel)
	{
		float p = amp - dust_vel;
		Transform tr;
		tr.rot = std::atan2(vel.y, vel.x) - body->GetAngle();
		tr.pos = {-(ent->get_radius() + 0.1f), 0};
		tr.pos.fastrotate(tr.rot);
		rc->parts(OE_DUST, p, tr);
	}
}
