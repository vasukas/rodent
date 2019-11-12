#include "client/presenter.hpp"
#include "utils/noise.hpp"
#include "game/game_core.hpp"
#include "game/player_mgr.hpp"
#include "game/weapon.hpp"
#include "ai_drone.hpp"



AI_Movement::AI_Movement(AI_Drone* drone)
	: EComp(drone->ent), spd_k(drone->get_pars().speed.data())
{
//	reg(ECompType::StepPostUtil); // controlled by AI_Drone
	drone->mov = this;
}
bool AI_Movement::set_target(std::optional<vec2fp> new_tar, AI_Speed speed)
{
	cur_spd = static_cast<size_t>(speed);
	
	if (!new_tar)
	{
		preq.reset();
		path.reset();
		return true;
	}
	else
	{
		auto same = [&](vec2fp p) {return LevelControl::get().is_same_coord(*new_tar, p);};
		if (same(ent->get_pos()))
		{
			preq.reset();
			path.reset();
			return true;
		}
		if (path && same(path->ps.back())) return false;
		if (preq && same(preq->target)) return false;
		
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
	return false;
}
std::optional<vec2fp> AI_Movement::get_next_point() const
{
	if (!path) return {};
	return path->ps[path->next];
}
vec2fp AI_Movement::step_path()
{
	if (LevelControl::get().is_same_coord(path->ps[path->next], ent->get_pos()))
	{
		if (++path->next == path->ps.size())
		{
			path.reset();
			return {};
		}
		return step_path();
	}
	
	vec2fp dt = path->ps[path->next] - ent->get_pos();
	float dt_n = dt.len_squ();
	if (dt_n < spd_k[cur_spd] * spd_k[cur_spd])
	{
		if (path->next + 1 != path->ps.size() && dt_n > 0.1)
			dt *= spd_k[cur_spd] / std::sqrt(dt_n);
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
			}
			preq.reset();
		}
	}
	else if (path)
		fvel = step_path();
	
	fvel += steering;
	steering = {};
	
	fvel.limit_to( spd_k[cur_spd] );
	b2Vec2 f = conv(fvel);
	
	auto& body = ent->get_phobj().body;
	f -= body->GetLinearVelocity();
	f *= body->GetMass();
	f *= inert_k[cur_spd];
	body->ApplyForceToCenter(f, f.LengthSquared() > 0.01);
}



bool AI_Attack::shoot(Entity* target, float distance, Entity* self)
{
	auto wpn = &self->get_eqp()->get_wpn();
	if (!wpn) return true;
	
	vec2fp p = target->get_pos();
	vec2fp corr = p + correction(distance, wpn->info->bullet_speed, target);
	
	if (!check_los(corr, target, self)) p = corr;
	else if (auto ent = check_los(p, target, self))
	{
		auto d = ent->get_ai_drone();
		if (d && d->mov && d->mov->get_next_point())
			return true;
		return false;
	}
	
	if (GameCore::get().dbg_ai_attack)
		self->get_eqp()->try_shoot(p, true, false);
	
	return true;
}
void AI_Attack::shoot(vec2fp at, Entity* self)
{
	if (!check_los(at, nullptr, self))
		self->get_eqp()->try_shoot(at, true, false);
}
vec2fp AI_Attack::correction(float distance, float bullet_speed, Entity* target)
{
	const float acc_k = GameCore::step_len / AI_Const::attack_acc_adjust_time;
	
	vec2fp tar_vel = target->get_phy().get_vel().pos;
	if (target->index == prev_tar) vel_acc += (tar_vel - vel_acc) * acc_k;
	else {
		prev_tar = target->index;
		vel_acc = tar_vel;
	}
	
	float ttr = distance / bullet_speed; // time to reach
	ttr += GameCore::get().get_random().range(AI_Const::attack_ttr_dev0, AI_Const::attack_ttr_dev1); // hack
	return vel_acc * ttr;
}
Entity* AI_Attack::check_los(vec2fp pos, Entity* target, Entity* self)
{
	const float hwidth = AI_Const::attack_los_hwidth; // half-width
	
	vec2fp skew = pos - self->get_pos();
	if (skew.len_squ() > hwidth * hwidth) skew.norm_to(hwidth);
	skew.rot90cw();
	
	auto rc = GameCore::get().get_phy().raycast_nearest(conv(self->get_pos() - skew), conv(pos - skew));
	if (rc && rc->ent != target) return rc->ent;
	
	     rc = GameCore::get().get_phy().raycast_nearest(conv(self->get_pos() + skew), conv(pos + skew));
	if (rc && rc->ent != target) return rc->ent;
	
	return nullptr;
}



AI_TargetProvider::AI_TargetProvider(AI_Drone* drone)
	: EComp(drone->ent), pars(drone->get_pars())
{
//	reg(ECompType::StepPreUtil); // controlled by AI_Drone
	drone->prov = this;
	
	if (auto hc = ent->get_hlc())
		EVS_CONNECT1(hc->on_damage, on_dmg);
}
std::optional<AI_TargetProvider::Target> AI_TargetProvider::get_target() const
{
	if (tar_sel && !GameCore::get().get_ent(tar_sel->eid)) return {};
	return tar_sel;
}
std::optional<AI_TargetProvider::ProjTarget> AI_TargetProvider::get_projectile() const
{
	if (tar_proj && !GameCore::get().get_ent(tar_proj->eid)) return {};
	return tar_proj;
}
void AI_TargetProvider::step()
{
	step_internal();
	
	if (tar_sel && !GameCore::get().get_ent(tar_sel->eid))
		tar_sel.reset();
	
	//
	
	vec2fp pos = ent->get_pos();
	
	Target* new_sel = nullptr; // nearest (non-projectile)
	float sel_dist = pars.dist_suspect + 0.1;
	std::optional<float> prev_dist; // set if prev exists and sel != prev
	
	for (size_t i=0; i < tars.size(); ++i)
	{
		if (auto tar_e = GameCore::get().get_ent( tars[i].eid ))
		{
			if (new_sel && !new_sel->is_suspect > tars[i].is_suspect)
				continue;
			
			auto d = GameCore::get().get_phy().los_check( pos, tar_e );
			if (!d) continue;
			
			if (*d < sel_dist)
			{
				new_sel = &tars[i];
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
	
	//
	
	if (!new_sel) // no nearest
	{
		tar_sel.reset();
	}
	else if (prev_dist || new_sel->eid == prev_tar) // locked exists
	{
		if (!sw_timeout.is_positive())
		{
			if (new_sel->eid != prev_tar) {
				tar_sel = *new_sel;
			}
			else if (prev_dist) {
				sel_dist = *prev_dist;
			}
		}
		else
		{
			sw_timeout -= GameCore::step_len;
			
			if (new_sel->eid != prev_tar && *prev_dist - sel_dist > AI_Const::target_switch_distance) {
				tar_sel = *new_sel;
			}
			else if (prev_dist) {
				sel_dist = *prev_dist;
			}
		}
	}
	else tar_sel = *new_sel;
	
	//
	
	if (tar_sel) 
	{
		if (prev_tar != tar_sel->eid) sw_timeout = AI_Const::target_switch_timeout;
		prev_tar = tar_sel->eid;
		
		tar_sel->dist = sel_dist;
		tar_sel->is_suspect = sel_dist > pars.dist_visible;
	}
	else if (damage_by)
	{
		tar_sel = Target{ damage_by, true, GameCore::get().get_ent(damage_by)->get_pos().dist( ent->get_pos() ) };
	}
	else prev_tar = {};
	
	damage_by = {};
	
	//
	
	EntityIndex proj_sel = {}; // nearest projectile
	float proj_dist = pars.dist_visible;
	
	for (size_t i=0; i < proj_tars.size(); ++i)
	{
		if (auto tar_e = GameCore::get().get_ent( proj_tars[i].eid ))
		{
			auto d = GameCore::get().get_phy().los_check( pos, tar_e );
			if (!d) continue;
			
			if (*d < proj_dist)
			{
				proj_sel = proj_tars[i].eid;
				proj_dist = *d;
			}
		}
		else {
			proj_tars.erase( proj_tars.begin() + i );
			--i;
		}
	}
	
	if (proj_sel) tar_proj = {proj_sel, proj_dist};
	else tar_proj.reset();
}
void AI_TargetProvider::on_dmg(const DamageQuant& q)
{
	if (q.src_eid)
	{
		if (is_primary(GameCore::get().get_ent( q.src_eid )))
			damage_by = q.src_eid;
	}
}



AI_TargetPlayer::AI_TargetPlayer(AI_Drone* drone)
	: AI_TargetProvider(drone)
{}
void AI_TargetPlayer::step_internal()
{
	if (auto plr = GameCore::get().get_pmg().get_ent())
	{
		if (tars.empty()) tars.emplace_back();
		tars[0] = Target{ plr->index, false };
	}
}



AI_TargetSensor::AI_TargetSensor(AI_Drone* drone)
    : AI_TargetProvider(drone)
{
	auto& ph = drone->ent->get_phobj();
	
	b2FixtureDef fd;
	fd.isSensor = true;
	fd.filter.maskBits = ~(EC_Physics::CF_BULLET);
	fix = ph.add_circle(fd, pars.dist_visible, 1, new FI_Sensor);
	
	EVS_CONNECT1(ph.ev_contact, on_cnt);
}
AI_TargetSensor::~AI_TargetSensor()
{
	ent->get_phobj().destroy(fix);
}
void AI_TargetSensor::step_internal() {}
void AI_TargetSensor::on_cnt(const CollisionEvent& ev)
{
	if (!ev.fix_this || typeid(*ev.fix_this) != typeid(FI_Sensor)) return;
	
	if (ev.other->get_team() == TEAM_ENVIRON ||
	    ev.other->get_team() == ent->get_team()) return;
	
	if (ev.type == CollisionEvent::T_BEGIN)
	{
		if (is_primary(ent)) tars.push_back({ ev.other->index, false });
		else proj_tars.push_back({ ev.other->index });
	}
	else if (ev.type == CollisionEvent::T_END)
	{
		auto it = std::find_if( tars.begin(), tars.end(), [&](auto& p){return p.eid == ev.other->index;} );
		if (it != tars.end()) tars.erase(it);
	}
}



AI_RenRotation::AI_RenRotation()
{
	rot = rnd_stat().range_n2() * M_PI;
}
void AI_RenRotation::update(Entity* ent, std::optional<vec2fp> target_pos, std::optional<vec2fp> move_pos)
{
	if (target_pos)
	{
		rot = (*target_pos - ent->get_pos()).fastangle();
		ent->get_ren()->set_face(rot);
		st = RR_NONE;
		left = 2;
	}
	else if (move_pos)
	{
		if (st != RR_NONE && left > 0) left -= GameCore::time_mul;
		else {
			rot = (*move_pos - ent->get_pos()).fastangle();
			ent->get_ren()->set_face(rot);
			st = RR_NONE;
			left = 0;
		}
	}
	else if (st == RR_NONE)
	{
		add = 0;
		st = RR_STOP;
		left += 1.5;
	}
	else
	{
		ent->get_ren()->set_face(rot += add);
		
		if (left > 0) left -= GameCore::time_mul;
		else {
			left = 1.3 + 0.3 * rnd_stat().range_n2();
			st = (st == RR_STOP) ? RR_ADD : RR_STOP;
			add = (st == RR_STOP) ? 0 : rnd_stat().range_n2() * M_PI / (left / GameCore::time_mul);
		}
	}
}
