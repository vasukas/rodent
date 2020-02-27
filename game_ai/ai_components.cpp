#include "utils/noise.hpp"
#include "game/game_core.hpp"
#include "game/physics.hpp"
#include "game/player_mgr.hpp"
#include "game/weapon.hpp"
#include "ai_drone.hpp"



AI_Movement::AI_Movement(AI_Drone& drone)
	: EComp(drone.ent), drone(drone)
{
//	reg(ECompType::StepPostUtil); // controlled by AI_Drone
	drone.mov = this;
}
bool AI_Movement::set_target(std::optional<vec2fp> new_tar, AI_Speed speed, std::optional<PathRequest::Evade> evade)
{
	cur_spd = speed;
	
	if (!new_tar)
	{
		preq.reset();
		path.reset();
		preq_failed = false;
		patrol_reset = {};
		return true;
	}
	else
	{
		auto same = [&](vec2fp p) {return is_same(p, *new_tar);};
		if (same(ent.get_pos()))
		{
			preq.reset();
			path.reset();
			preq_failed = false;
			patrol_reset = {};
			return true;
		}
		if (patrol_reset && same(*patrol_reset)) return true;
		if (path && same(path->ps.back())) return false;
		if (preq && same(preq->target)) return false;
		
		// check if target is behind wall
		auto rc = ent.core.get_phy().raycast_nearest( conv(ent.get_pos()), conv(*new_tar),
			{[](auto&, b2Fixture& f){ auto fi = get_info(f); return fi && (fi->typeflags & FixtureInfo::TYPEFLAG_WALL); }},
			ent.ref_pc().get_radius() + 0.1 );
		
		if (rc)
		{
			path.reset();
			auto& p = preq.emplace();
			p.target = *new_tar;
			p.req = PathRequest( ent.core, ent.get_pos(), *new_tar, {}, evade );
		}
		else
		{
			preq.reset();
			auto& p = path.emplace();
			p.ps.push_back(*new_tar);
			p.next = 0;
		}
		preq_failed = false;
		patrol_reset = {};
	}
	return false;
}
std::optional<vec2fp> AI_Movement::get_next_point() const
{
	if (!path) return {};
	return path->ps[path->next];
}
float AI_Movement::get_set_speed() const
{
	return drone.get_pars().get_speed(cur_spd);
}
bool AI_Movement::has_failed() const
{
	return preq_failed;
}
bool AI_Movement::is_same(vec2fp a, vec2fp b) const
{
	if (cur_spd == AI_Speed::SlowPrecise)
		return a.dist_squ(b) < AI_Const::move_slowprecise_dist_squ;
	return ent.core.get_lc().is_same_coord(a, b);
}
std::optional<vec2fp> AI_Movement::get_target() const
{
	if (path) return path->ps.back();
	if (preq) return preq->target;
	return {};
}
std::optional<vec2fp> AI_Movement::calc_avoidance()
{
	const float ray_width = std::max(0.2, ent.ref_pc().get_radius() - 0.1);
	const float side_ray_width = 1.5;
	const float min_tar_dist = ent.ref_pc().get_radius() + 0.5;
	const float max_ray_dist = ent.core.get_lc().cell_size * 2;
	const float side_ray_angle = deg_to_rad(30);
	const float headon_hack_angle = deg_to_rad(15);
	const float side_ray_dist = ent.core.get_lc().cell_size * 2;
	
	auto tar = get_next_point();
	if (!tar) return {};
	
	vec2fp self = ent.get_pos();
	vec2fp dir = *tar - self;
	if (dir.len_squ() < min_tar_dist * min_tar_dist) return {};
	
	const float ray_dist = std::min( dir.fastlen(), max_ray_dist );
	dir.norm();
	
	auto rc = ent.core.get_phy().raycast_nearest( conv(self), conv(self + dir * ray_dist), {}, ray_width );
	if (!rc) return {};
	
	bool is_patrol = (cur_spd == AI_Speed::Patrol) && path
	                 && path->ps.back().dist_squ( ent.get_pos() ) < AI_Const::move_patrol_reset_distance_squ;
	
	// ignore drone moving in same direction
	if (auto drone = rc->ent->get_ai_drone();
		drone && drone->mov)
	{
		auto& drmov = *drone->mov;
		
		if ([&]{
			if (is_patrol && drmov.cur_spd == AI_Speed::Patrol)
			{
				if (drmov.path) {
					if (is_same( path->ps.back(), drmov.path->ps.back()) )
					    return true;
				}
				else if (is_same( path->ps.back(), drone->ent.get_pos() ))
					return true;
				else if (drmov.patrol_reset && is_same( path->ps.back(), *drmov.patrol_reset ))
					return true;
			}
			return false;
		}()) {
			patrol_reset = path->ps.back();
			path = {};
			return {};
		}
		
		if (auto next = drmov.get_next_point())
		{
			vec2fp d_dir = *next - drone->ent.get_pos();
			d_dir.norm();
			
			float cmp = dot(d_dir, dir);
			if (cmp > 0 && drmov.get_set_speed() > get_set_speed() - 0.5)
				return {};
		}
	}
	
	// path search should already go around static drones and other obstacles
	if (rc->ent->ref_phobj().body.GetType() == b2_staticBody)
		return {};
	
	// avoid left or right?
	
	float l_dist = std::numeric_limits<float>::max();
	float r_dist = std::numeric_limits<float>::max();
	
	PhysicsWorld::CastFilter cf{ [&](Entity& ent, auto&){ return &ent != rc->ent; } };
	
	if (auto rc = ent.core.get_phy().raycast_nearest( conv(self), // right far
			conv(self + vec2fp(dir).fastrotate( side_ray_angle) * side_ray_dist), cf, side_ray_width ))
		r_dist = rc->distance;
	
	if (auto rc = ent.core.get_phy().raycast_nearest( conv(self), // left far
			conv(self + vec2fp(dir).fastrotate(-side_ray_angle) * side_ray_dist), cf, side_ray_width ))
		l_dist = rc->distance;
	
	//
	
	float t = 1 - rc->distance / ray_dist;
	float spd = get_set_speed() * clampf_n(t);
	
	dir.rot90ccw();
	dir *= spd;
	if (r_dist < l_dist) dir = -dir;
	
	dir.fastrotate(headon_hack_angle);
	return dir;
}
vec2fp AI_Movement::step_path()
{
	if (is_same( path->ps[path->next], ent.get_pos() ))
	{
		if (++path->next == path->ps.size())
		{
			if (cur_spd == AI_Speed::Patrol) patrol_reset = path->ps.back();
			path.reset();
			return {};
		}
		return step_path();
	}
	
	const float speed = get_set_speed();
	
	vec2fp dt = path->ps[path->next] - ent.get_pos();
	float dt_n = dt.len_squ();
	if (dt_n < speed * speed)
	{
		if (path->next + 1 != path->ps.size() && dt_n > 0.1)
			dt *= speed / std::sqrt(dt_n);
	}
	
	return dt;
}
void AI_Movement::step()
{
	vec2fp fvel = {};
	
	if (preq)
	{
		if (auto res = preq->req.result())
		{
			if (!res->not_found)
			{
				auto& p = path.emplace();
				p.ps = std::move(res->ps);
				p.next = 1;
				preq.reset();
			}
			else preq_failed = true;
		}
	}
	else if (path)
		fvel = step_path();
	
	if (auto s = calc_avoidance())
		fvel += *s;
	
	fvel.limit_to( get_set_speed() );
	b2Vec2 f = conv(fvel);
	if (locked) f = {0,0};
	
	auto& body = ent.ref_phobj().body;
	f -= body.GetLinearVelocity();
	f *= body.GetMass();
	f *= inert_k(cur_spd);
	body.ApplyForceToCenter(f, f.LengthSquared() > 0.01);
}
float AI_Movement::inert_k(AI_Speed speed)
{
	switch (speed)
	{
	case AI_Speed::SlowPrecise:
		return 50;
		
	case AI_Speed::Slow:
	case AI_Speed::Patrol:
		return 6;
		
	case AI_Speed::Normal:
		return 4;
		
	case AI_Speed::Accel:
	case AI_Speed::TOTAL_COUNT: // silence warning
		return 8;
	}
	return 1; // silence warning
}



bool AI_Attack::shoot(Entity& target, float distance, Entity& self)
{
	auto& wpn = self.get_eqp()->get_wpn();
	
	vec2fp p = target.get_pos();
	vec2fp corr = p + correction(distance, wpn.info->bullet_speed, target);
	
	if (!check_los(corr, target, self)) p = corr;
	else if (auto ent = check_los(p, target, self))
	{
		auto d = ent->get_ai_drone();
		if (d && d->mov && d->mov->get_next_point())
			return true;
		return false;
	}
	
	if (target.core.dbg_ai_attack)
	{
		if (atkpat) atkpat->shoot(target, distance, self);
		else self.get_eqp()->shoot(p, true, false);
	}
	return true;
}
vec2fp AI_Attack::correction(float distance, float bullet_speed, Entity& target)
{
	const float acc_k = GameCore::step_len / AI_Const::attack_acc_adjust_time;
	
	if (target.index == prev_tar) {
		vec2fp tar_vel = target.ref_pc().get_vel();
		vel_acc += (tar_vel - vel_acc) * acc_k;
	}
	else {
		prev_tar = target.index;
		vel_acc = {};
	}
	
	float ttr = distance / bullet_speed; // time to reach
	ttr += target.core.get_random().range(AI_Const::attack_ttr_dev0, AI_Const::attack_ttr_dev1); // hack
	return vel_acc * ttr;
}
Entity* AI_Attack::check_los(vec2fp pos, Entity& target, Entity& self)
{
	auto rc = self.core.get_phy().raycast_nearest( conv(self.get_pos()), conv(pos), {}, AI_Const::attack_los_hwidth );
	if (rc && rc->ent != &target) return rc->ent;
	return nullptr;
}



AI_TargetProvider::AI_TargetProvider(AI_Drone& drone)
	: EComp(drone.ent), drone(drone)
{
//	reg(ECompType::StepPreUtil); // controlled by AI_Drone
	
	if (auto hc = ent.get_hlc())
		EVS_CONNECT1(hc->on_damage, on_dmg);
}
std::optional<AI_TargetProvider::Target> AI_TargetProvider::get_target() const
{
	if (tar_sel && !ent.core.get_ent(tar_sel->eid)) return {};
	return tar_sel;
}
void AI_TargetProvider::step()
{
	const auto& pars = drone.get_pars();
	const vec2fp pos = ent.get_pos();
	
	tar_sel.reset();
	
	if (auto plr = ent.core.get_pmg().get_ent();
	    plr && ent.core.dbg_ai_see_plr)
	{
		[&]{
			Entity* tar_e = plr;
			
			if (pars.fov && fov_t)
			{
				vec2fp delta = tar_e->get_pos() - pos;
				float dist = delta.len_squ();
				float spd = tar_e->ref_pc().get_vel().len_squ();
				
				if (dist > AI_Const::target_hearing_range_squ && (
				     spd < AI_Const::target_hearing_ext_spd_threshold_squ ||
				     dist > AI_Const::target_hearing_ext_range_squ))
				{
					float a = delta.fastangle();
					float fov = lerp(pars.fov->first, pars.fov->second, *fov_t);
					if (std::fabs(wrap_angle( a - ent.ref_pc().get_angle() )) > fov)
						return;
				}
			}
			
			auto d = ent.core.get_phy().los_check( pos, *tar_e );
			if (!d) return;
			
			float range = is_battle && pars.dist_battle ? *pars.dist_battle : pars.dist_visible;
			if (*d > std::max(range, pars.dist_suspect)) return;
			
			tar_sel = Target{ tar_e->index, *d > range, damage_by == tar_e->index, *d };
		}();
	}
	
	if (!tar_sel && damage_by)
	{
		if (Entity* tar_e = ent.core.get_ent(*damage_by))
		{
			float dist = tar_e->get_pos().dist(pos);
			tar_sel = Target{ tar_e->index, dist > pars.dist_visible, true, dist };
		}
	}
	
	was_damaged_flag = !!damage_by;
	damage_by.reset();
}
void AI_TargetProvider::on_dmg(const DamageQuant& q)
{
	if (q.src_eid)
	{
		if (auto e = ent.core.get_ent( q.src_eid ))
		{
			if (is_primary(*e))
				damage_by = q.src_eid;
			else
				damage_by = EntityIndex{};
		}
	}
}



void AI_RotationControl::update(AI_Drone& dr, std::optional<vec2fp> view_target, std::optional<vec2fp> mov_target)
{
	float rot_spd = speed_override ? *speed_override : dr.get_pars().rot_speed;
	if (rot_spd < 1e-5) {
		tmo = AI_Const::fixed_rotation_length;
		state = ST_WAIT;
		return;
	}
	
	auto set_tar = [&](float tar, TimeSpan time)
	{
		float d = angle_delta(tar, dr.ent.ref_pc().get_angle());
		tmo = std::max(time, TimeSpan::seconds(d / rot_spd));
		
		if (tmo > GameCore::step_len) {
			add = d / (tmo / GameCore::step_len);
		}
		else {
			add = 0;
			dr.ent.ref_pc().rot_override = tar;
			tmo = {};
		}
	};
	auto reset = [&](State st)
	{
		state = st;
		add = 0;
	};
	
	if (tmo.is_positive())
	{
		dr.ent.ref_pc().rot_override = dr.ent.ref_pc().get_angle() + add;
		tmo -= GameCore::step_len;
	}
	
	if (view_target)
	{
		if ((state == ST_TAR_MOV || state == ST_WAIT_MOV) && tmo.is_positive())
		{
			tmo = std::min(tmo, AI_Const::fixed_rotation_length);
			reset(ST_WAIT_MOV);
		}
		else {
			set_tar((*view_target - dr.ent.get_pos()).fastangle(), {});
			state = ST_TAR_VIEW;
		}
	}
	else if (mov_target)
	{
		if ((state == ST_TAR_VIEW || state == ST_WAIT_VIEW) && tmo.is_positive())
		{
			tmo = std::min(tmo, AI_Const::fixed_rotation_length);
			reset(ST_WAIT_VIEW);
		}
		else {
			set_tar((*mov_target - dr.ent.get_pos()).fastangle(), {});
			state = ST_TAR_MOV;
		}
	}
	else if (state == ST_TAR_MOV)
	{
		tmo += TimeSpan::seconds(1.5);
		reset(ST_WAIT_MOV);
	}
	else if (state == ST_TAR_VIEW)
	{
		tmo += TimeSpan::seconds(1.5);
		reset(ST_WAIT_VIEW);
	}
	else if (!tmo.is_positive())
	{
		tmo = TimeSpan::seconds(1.3 + 0.3 * dr.ent.core.get_random().range_n2());
		
		if (state == ST_RANDOM) reset(ST_WAIT);
		else {
			set_tar(dr.ent.core.get_random().range_n2() * M_PI, tmo);
			state = ST_RANDOM;
		}
	}
}
