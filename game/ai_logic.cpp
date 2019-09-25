#include "client/presenter.hpp"
#include "ai_logic.hpp"
#include "game_core.hpp"
#include "player_mgr.hpp"
#include "weapon.hpp"



vec2fp AI_TargetPredictor::correction(float distance, float bullet_speed, Entity *target)
{	
	float ttr = distance / bullet_speed; // time to reach
	vec2fp tar_vel; // target velocity
	
	if (GameCore::get().get_pmg().is_player(target)) tar_vel = GameCore::get().get_pmg().get_avg_vel();
	else tar_vel = target->get_phy().get_vel().pos;
	
	return tar_vel * ttr;
}



void AI_TargetProvider::check_lpos(std::optional<vec2fp> &lpos, Entity* self)
{
	if (lpos) {
		const float rad = 3;
		if (self->get_pos().dist_squ(*lpos) < rad * rad)
			lpos.reset();
	}
}



AI_Movement::AI_Movement(Entity* ent, float spd_slow, float spd_norm, float spd_accel)
	: EComp(ent), spd_k({spd_slow, spd_norm, spd_accel})
{
	reg(ECompType::StepPostUtil);
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
		if (preq && preq->first .equals(*new_tar, near_dist)) return;
		if (path && path->back().equals(*new_tar, near_dist)) return;
		
		auto rc = GameCore::get().get_phy().raycast_nearest( conv(ent->get_pos()), conv(*new_tar),
		{[](auto, b2Fixture* f){ auto fi = getptr(f); return fi && (fi->typeflags & FixtureInfo::TYPEFLAG_WALL); }});
		
		if (rc)
		{
			preq.emplace(*new_tar, PathRequest( ent->get_pos(), *new_tar ));
			path.reset();
		}
		else
		{
			preq.reset();
			path.emplace().push_back(*new_tar);
			path_fixed_dir.reset();
			path_index = 0;
		}
	}
}
vec2fp AI_Movement::step_path()
{
	const vec2fp delta = (*path)[path_index] - ent->get_pos();
	
	vec2fp dir = delta;
	dir.norm_to( spd_k[cur_spd] );
	
	vec2fp line_dir = dir;
	line_dir.rot90cw();
	
	if (delta.len_squ() < reach_dist * reach_dist)
	{
		if (!path_fixed_dir) path_fixed_dir = dir;
		return *path_fixed_dir;
	}
	else if (path_fixed_dir)
	{
		path_fixed_dir.reset();
		
		if (++path_index == path->size())
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
	b2Vec2 f = {0, 0};
	
	if (preq)
	{
		auto res = preq->second.result();
		if (res)
		{
			if (!res->not_found)
			{
				path = std::move(res->ps);
				path_fixed_dir.reset();
				path_index = 1;
			}
			preq.reset();
		}
	}
	else if (path) {
		for (size_t i=1; i<path->size(); ++i)
			GamePresenter::get()->dbg_line( (*path)[i-1], (*path)[i], -1 );
		GamePresenter::get()->dbg_line( ent->get_pos(), (*path)[path_index], 0xff000080 );
		
		f = conv(step_path());
	}
	
	auto& body = ent->get_phobj().body;
	f -= body->GetLinearVelocity();
	f *= body->GetMass();
	f *= inert_k[cur_spd];
	body->ApplyForceToCenter(f, f.LengthSquared() > 0.01);
}



AI_DroneLogic::AI_DroneLogic(Entity* ent, std::unique_ptr<AI_TargetProvider> tar, AI_Movement* mov)
	: EComp(ent), tar(std::move(tar)), mov(mov)
{
	reg(ECompType::StepLogic);
}
void AI_DroneLogic::step()
{
	tar->step();
	auto ti = tar->get_target();
	
	if (ti.ent)
	{
		vec2fp pos = ti.ent->get_pos();
		const vec2fp delta = pos - ent->get_pos();
		const float da = delta.angle();
		
		if (tar_pred.enabled) {
			auto bs = ent->get_eqp()->get_wpn().get_bullet_speed();
			pos += tar_pred.correction(ti.dist, bs, ti.ent);
		}
		
		ent->get_eqp()->shoot(pos);
		if (auto r = ent->get_ren()) r->set_face(da);
		
		// maintain optimal distance
		if (mov)
		{
			if (ti.dist < min_dist)
			{
				vec2fp pos = ent->get_pos();
				vec2fp tar = pos + delta.get_norm() * -min_dist;
				
				auto rc = GameCore::get().get_phy().raycast_nearest( conv(pos), conv(tar) );
				if (rc) tar = conv(rc->poi);
				
				tar -= vec2fp( ent->get_phy().get_radius(), 0 ).get_rotated( da );
				mov->set_target(tar, AI_Movement::SPEED_SLOW);
			}
			else if (ti.dist > opt_dist)
			{
				vec2fp pos = ent->get_pos();
				vec2fp tar = pos + delta.get_norm() * opt_dist;
				
				tar += vec2fp( ent->get_phy().get_radius(), 0 ).get_rotated( da );
				mov->set_target( tar, AI_Movement::SPEED_ACCEL );
			}
			else mov->set_target({});
		}
	}
	else if (mov)
	{
		mov->set_target(ti.pos);
		
		if (auto r = ent->get_ren(); r && ti.pos) {
			vec2fp delta = *ti.pos - ent->get_pos();
			r->set_face(delta.angle());
		}
	}
}



AI_TargetPlayer::AI_TargetPlayer(Entity* ent, float vis_rad)
	: ent(ent), vis_rad(vis_rad)
{}
void AI_TargetPlayer::step()
{
	ti.ent = nullptr;
	ti.dist = 0.f;
	check_lpos(ti.pos, ent);
	
	auto plr = GameCore::get().get_pmg().get_ent();
	if (!plr) return; // no target
	
	vec2fp pos = ent->get_pos();
	vec2fp tar = plr->get_pos();
	
	float dist = pos.dist_squ(tar);
	if (dist > vis_rad * vis_rad) return; // out of range
	
	auto rc = GameCore::get().get_phy().los_check(pos, plr);
	if (!rc) return; // not visible
	
	ti.ent = plr;
	ti.pos = tar;
	ti.dist = *rc;
}



AI_TargetSensor::AI_TargetSensor(EC_Physics& ph, float vis_rad)
	: ent(ph.ent), vis_rad(vis_rad)
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
void AI_TargetSensor::step()
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
		ti.ent = nullptr;
		ti.dist = 0.f;
		check_lpos(ti.pos, ent);
		
		prev_tar = {};
		return;
	}
	
	if (prev_dist || sel->index == prev_tar) // locked exists
	{
		if (tmo.is_positive()) tmo -= GameCore::step_len;
		
		if (sel->index == prev_tar) // same as nearest
		{
			ti.pos = ti.ent->get_pos();
			ti.dist = sel_dist;
			return;
		}
		
		// timeout not expired, check distance re-lock
		if (tmo.is_positive() && *prev_dist - sel_dist < chx_dist)
		{
			ti.pos = ti.ent->get_pos();
			ti.dist = *prev_dist;
			return;
		}
	}
	
	// set new target
	
	prev_tar = sel->index;
	tmo = chx_tmo;
	
	ti.ent = sel;
	ti.pos = sel->get_phy().get_pos();
	ti.dist = sel_dist;
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



std::optional<vec2fp> AI_TargetNetwork::Group::get_pos()
{
	std::optional<vec2fp> r;
	for (auto& n : nodes)
	{
		auto ti = n->prov->get_target();
		if (ti.ent) return ti.ent->get_pos();
		if (ti.pos) r = ti.pos;
	}
	return r;
}
AI_TargetNetwork::AI_TargetNetwork(Entity* ent, std::shared_ptr<Group> grp, std::unique_ptr<AI_TargetProvider> prov)
	: ent(ent), grp(std::move(grp)), prov(std::move(prov))
{
	this->grp->nodes.push_back(this);
}
AI_TargetNetwork::~AI_TargetNetwork()
{
	auto it = std::find( grp->nodes.begin(), grp->nodes.end(), this );
	grp->nodes.erase(it);
}
void AI_TargetNetwork::step()
{
	prov->step();
	ti = prov->get_target();
	
	if (ti.ent || ti.pos)
	{
		net_pos.reset();
		return;
	}
	
	check_lpos(net_pos, ent);
	ti.pos = net_pos;
	ti.dist = 0;
	
	tmo -= GameCore::step_len;
	if (tmo.is_negative())
	{
		tmo = ask_tmo;
		if (auto p = grp->get_pos())
			net_pos = p;
	}
}
