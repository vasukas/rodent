#include "client/presenter.hpp"
#include "utils/noise.hpp"
#include "ai_logic.hpp"
#include "game_core.hpp"
#include "player_mgr.hpp"
#include "weapon.hpp"



void AI_NetworkGroup::post_target(Entity* ent)
{
	tar = TargetInfo{};
	tar->eid = ent->index;
	tar->pos = ent->get_pos();
	tar->seen_at = GameCore::get().get_step_counter();
	tar->is_visible = true;
}
void AI_NetworkGroup::reset_target(vec2fp self_pos)
{
	if (tar && !tar->is_visible && LevelControl::get().is_same_coord(tar->pos, self_pos))
		tar.reset();
}
std::optional<AI_NetworkGroup::TargetInfo> AI_NetworkGroup::last_target()
{
	if (tar && !GameCore::get().get_ent(tar->eid)) tar.reset();
	if (tar && tar->seen_at != GameCore::get().get_step_counter()) tar->is_visible = false;
	return tar;
}



AI_TargetProvider::AI_TargetProvider(Entity* ent, std::shared_ptr<AI_NetworkGroup> group)
	: EComp(ent), group(std::move(group))
{
	reg(ECompType::StepPreUtil);
	
	if (auto hc = ent->get_hlc())
		EVS_CONNECT1(hc->on_damage, on_dmg);
}
void AI_TargetProvider::step()
{
	InfoInternal ii = update();
	if (ii.ent)
	{
		info = Visible{ii.ent, ii.dist};
		if (group) group->post_target(ii.ent);
	}
	else if (!group) info = NoTarget{};
	else
	{
		auto lt = group->last_target();
		if (!lt) info = NoTarget{};
		else {
			group->reset_target( ent->get_pos() );
			if (auto lt = group->last_target())
				info = LastKnown{lt->pos};
		}
	}
}
void AI_TargetProvider::on_dmg(const DamageQuant& q)
{
	if (!group) return;
	if (!std::holds_alternative<Visible>(info))
	{
		if (auto ent = GameCore::get().get_ent(q.src_eid))
			group->post_target(ent);
	}
}



void AI_Attack::shoot(AI_TargetProvider::Visible tar, Entity* self)
{
	auto wpn = &self->get_eqp()->get_wpn();
	if (!wpn) return;
	
	vec2fp p = tar.ent->get_pos();
	if (use_prediction)
	{
		vec2fp corr = correction(tar.dist, wpn->info->bullet_speed, tar.ent);
		GamePresenter::get()->dbg_line(p, p + corr, 0xff0000ff);
		p += corr;
	}
	
	self->get_eqp()->try_shoot(p, true, false);
}
void AI_Attack::shoot(vec2fp at, Entity* self)
{
	self->get_eqp()->try_shoot(at, true, false);
}
vec2fp AI_Attack::correction(float distance, float bullet_speed, Entity* target)
{
	const float acc_k = GameCore::step_len / TimeSpan::seconds(0.5);
	
	vec2fp tar_vel = target->get_phy().get_vel().pos;
	if (target->index == prev_tar) vel_acc += (tar_vel - vel_acc) * acc_k;
	else {
		prev_tar = target->index;
		vel_acc = tar_vel;
	}
	
	float ttr = distance / bullet_speed; // time to reach
	ttr += GameCore::get().get_random().range(0.1, 0.25); // hack
	return vel_acc * ttr;
}



constexpr float ai_evade_add = 1.5f; // additional evade distance
constexpr float ai_evade_size = 0.7f; // minimal perimeter delta
constexpr float ai_evade_speed = 5.f;

AI_Movement::AI_Movement(Entity* ent, float spd_slow, float spd_norm, float spd_accel)
	: EComp(ent), spd_k({spd_slow, spd_norm, spd_accel})
{
	reg(ECompType::StepPostUtil);
	evade_rad = ent->get_phy().get_radius() + ai_evade_add;
}
void AI_Movement::set_target(std::optional<vec2fp> new_tar, SpeedType speed)
{
	cur_spd = static_cast<size_t>(speed);
	
	if (!new_tar)
	{
		preq.reset();
		path.reset();
	}
	else
	{
		auto same = [&](vec2fp p) {return LevelControl::get().is_same_coord(*new_tar, p);};
		if (path && same(path->ps.back())) return;
		if (preq && same(preq->target)) return;
		
		// check if target is behind wall
		
		auto rc = GameCore::get().get_phy().raycast_nearest( conv(ent->get_pos()), conv(*new_tar),
		{[](auto, b2Fixture* f){ auto fi = getptr(f); return fi && (fi->typeflags & FixtureInfo::TYPEFLAG_WALL); }});
		
		if (rc)
		{
			path.reset();
			auto& p = preq.emplace();
			p.target = *new_tar;
			p.req = PathRequest( ent->get_pos(), *new_tar );
		}
		else
		{
			preq.reset();
			auto& p = path.emplace();
			p.ps.push_back(*new_tar);
			p.next = 0;
		}
	}
}
vec2fp AI_Movement::step_path()
{
	const vec2fp delta = path->ps[path->next] - ent->get_pos();
	
	vec2fp dir = delta;
	dir.norm_to( spd_k[cur_spd] );
	
	if (LevelControl::get().is_same_coord(path->ps[path->next], ent->get_pos()))
	{
		if (++path->next == path->ps.size())
		{
			path.reset();
			return {};
		}
		return step_path();
	}
	return dir;
}
void AI_Movement::step()
{
	b2Vec2 f = get_evade();
	
	if (preq)
	{
		if (auto res = preq->req.result())
		{
			if (!res->not_found)
			{
				auto& p = path.emplace();
				p.ps = std::move(res->ps);
				p.next = 1;
			}
			preq.reset();
		}
	}
	else if (path)
	{
//		for (size_t i=1; i<path->ps.size(); ++i)
//			GamePresenter::get()->dbg_line( path->ps[i-1], path->ps[i], -1 );
//		GamePresenter::get()->dbg_line( ent->get_pos(), path->ps[path->next], 0xff000080 );
		
		f += conv(step_path());
		f += get_evade_vel(f);
	}
	
	auto& body = ent->get_phobj().body;
	f -= body->GetLinearVelocity();
	f *= body->GetMass();
	f *= inert_k[cur_spd];
	body->ApplyForceToCenter(f, f.LengthSquared() > 0.01);
}
b2Vec2 AI_Movement::get_evade()
{
	auto& ph = ent->get_phobj();
	float min_size = ai_evade_size;
	
	float a = min_size / evade_rad; // delta = 2pi / num, num = 2pi * rad / min_size
	a *= evade_index;
	++evade_index; // won't overflow
	
	b2Vec2 pos = ph.body->GetWorldCenter();
	b2Vec2 tar = b2Mul(b2Rot(a), b2Vec2(evade_rad, 0));
	
	b2Vec2 corr = {0, 0};
	for (int i=0; i<4; ++i)
	{
		tar = tar.Skew();
		auto rc = GameCore::get().get_phy().raycast_nearest(pos, pos + tar);
		if (rc)
		{
			b2Vec2 delta = rc->poi - pos;
			float d = delta.Length();
			if (d < evade_rad && d > min_size)
				corr += (ai_evade_speed / -d) * delta;
		}
	}
	return corr;
}
b2Vec2 AI_Movement::get_evade_vel(b2Vec2 vel)
{
	constexpr float ahead_k = 0.5;
	
	auto& ph = ent->get_phobj();
	const b2Vec2 dir = ahead_k * vel;
	const float dist_max = ahead_k * dir.Length();
	
	float off[2] = {-1, 1};
	float dist[2];
	
	for (int i=0; i<2; ++i)
	{
		b2Vec2 pos = off[i] * dir.Skew();
		pos.Normalize();
		pos *= dist_max;
		pos += ph.body->GetWorldCenter();
		
		auto rc = GameCore::get().get_phy().raycast_nearest(pos, pos + dir);
		dist[i] = rc? (rc->poi - pos).LengthSquared() : dist_max;
	}
	
	if (std::fabs(dist[0] - dist[1]) < 1.f) return {0, 0};
	int i = dist[0] < dist[1] ? 0 : 1;
	
	b2Vec2 corr = off[i] * dir.Skew();
	corr.Normalize();
	corr *= 0.5f * std::fabs(std::sqrt(dist[0]) - std::sqrt(dist[1]));
	return corr;
}



AI_DroneLogic::AI_DroneLogic(Entity* ent, AI_TargetProvider* tar, AI_Movement* mov)
	: EComp(ent), tar(tar), mov(mov)
{
	reg(ECompType::StepLogic);
}
void AI_DroneLogic::step()
{
	auto& ti_var = tar->get_info();
	
	if (auto ti = std::get_if<AI_TargetProvider::Visible>(&ti_var))
	{
		if (GameCore::get().dbg_ai_attack)
			atk.shoot(*ti, ent);
		
		vec2fp pos = ti->ent->get_pos();
		const vec2fp delta = pos - ent->get_pos();
		const float da = delta.angle();
		
		if (auto r = ent->get_ren()) r->set_face(da);
		
		seen_tmo = {};
		seen_pos = pos;
		
		// maintain optimal distance
		if (mov)
		{
			if (ti->dist < min_dist)
			{
				vec2fp pos = ent->get_pos();
				vec2fp tar = pos + delta.get_norm() * -min_dist;
				
				auto rc = GameCore::get().get_phy().raycast_nearest( conv(pos), conv(tar) );
				if (rc) tar = conv(rc->poi);
				
				tar -= vec2fp( ent->get_phy().get_radius(), 0 ).get_rotated( da );
				mov->set_target(tar, AI_Movement::SPEED_SLOW);
			}
			else if (ti->dist > opt_dist)
			{
				vec2fp pos = ent->get_pos();
				vec2fp tar = pos + delta.get_norm() * opt_dist;
				
				tar += vec2fp( ent->get_phy().get_radius(), 0 ).get_rotated( da );
				mov->set_target( tar, AI_Movement::SPEED_ACCEL );
			}
			else mov->set_target({});
		}
	}
	else if (auto ti = std::get_if<AI_TargetProvider::LastKnown>(&ti_var); ti && mov)
	{
		mov->set_target(ti->pos);
		
		if (auto r = ent->get_ren()) {
			vec2fp delta = ti->pos - ent->get_pos();
			r->set_face(delta.angle());
		}
	}
	else if (mov)
	{
		mov->set_target({});
	}
	
	//
	
	if (seen_tmo.seconds() < 2.1f)
	{
		if (GameCore::get().dbg_ai_attack && seen_tmo.is_positive())
			atk.shoot(seen_pos, ent);
		
		seen_tmo += GameCore::step_len;
	}
}



AI_TargetPlayer::AI_TargetPlayer(Entity* ent, float vis_rad, std::shared_ptr<AI_NetworkGroup> grp)
	: AI_TargetProvider(ent, std::move(grp)), vis_rad(vis_rad)
{}
AI_TargetProvider::InfoInternal AI_TargetPlayer::update()
{
	auto plr = GameCore::get().get_pmg().get_ent();
	if (!plr) return {}; // no target
	
	vec2fp pos = ent->get_pos();
	vec2fp tar = plr->get_pos();
	
	float dist = pos.dist_squ(tar);
	if (dist > vis_rad * vis_rad) return {}; // out of range
	
	auto rc = GameCore::get().get_phy().los_check(pos, plr);
	if (!rc) return {}; // not visible
	
	return {plr, *rc};
}



AI_TargetSensor::AI_TargetSensor(EC_Physics& ph, float vis_rad)
	: AI_TargetProvider(ph.ent), vis_rad(vis_rad)
{
	b2FixtureDef fd;
	fd.isSensor = true;
	fd.filter.maskBits = ~(EC_Physics::CF_BULLET);
	fix = ph.add_circle(fd, vis_rad, 1, new FI_Sensor);
	
	EVS_CONNECT1(ph.ev_contact, on_cnt);
}
AI_TargetSensor::~AI_TargetSensor()
{
	ent->get_phobj().destroy(fix);
}
AI_TargetProvider::InfoInternal AI_TargetSensor::update()
{
	vec2fp pos = ent->get_pos();
	
	Entity* sel = nullptr; // nearest
	float sel_dist = vis_rad + 1;
	std::optional<float> prev_dist; // set if prev exists and sel != prev
	
	for (size_t i=0; i < tars.size(); ++i)
	{
		if (auto tar_e = GameCore::get().get_ent( tars[i] ))
		{
			auto d = GameCore::get().get_phy().los_check( pos, tar_e );
			if (!d) continue;
			
			if (*d < sel_dist)
			{
				sel = tar_e;
				sel_dist = *d;
			}
			else if (tar_e->index == prev_tar)
				prev_dist = *d;
		}
		else {
			tars.erase( tars.begin() + i );
			--i;
		}
	}
	
	if (!sel) // no nearest
	{
		prev_tar = {};
		return {};
	}
	
	if (prev_dist || sel->index == prev_tar) // locked exists
	{
		if (tmo.is_positive()) tmo -= GameCore::step_len;
		
		if (sel->index == prev_tar) // same as nearest
		{
			return {sel, sel_dist};
		}
		
		// timeout not expired, check distance re-lock
		if (tmo.is_positive() && *prev_dist - sel_dist < chx_dist)
		{
			return {sel, *prev_dist};
		}
	}
	
	// set new target
	
	prev_tar = sel->index;
	tmo = chx_tmo;
	return {sel, sel_dist};
}
void AI_TargetSensor::on_cnt(const CollisionEvent& ev)
{
	if (!ev.fix_this || typeid(*ev.fix_this) != typeid(FI_Sensor)) return;
	
	if (ev.type == CollisionEvent::T_BEGIN)
	{
		if ((ent->get_team() == TEAM_BOTS && ev.other->get_team() == TEAM_PLAYER) ||
		    (ent->get_team() == TEAM_PLAYER && ev.other->get_team() == TEAM_BOTS))
		{
			tars.push_back(ev.other->index);
		}
	}
	else if (ev.type == CollisionEvent::T_END)
	{
		auto it = std::find( tars.begin(), tars.end(), ev.other->index );
		if (it != tars.end()) tars.erase(it);
	}
}
