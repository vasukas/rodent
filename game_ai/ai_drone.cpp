#include "client/presenter.hpp"
#include "game/game_core.hpp"
#include "game_ai/ai_drone.hpp"
#include "game_ai/ai_group.hpp"
#include "utils/noise.hpp"



AI_Drone::AI_Drone(Entity* ent, std::shared_ptr<AI_DroneParams> pars, std::shared_ptr<AI_Group> grp, std::shared_ptr<State> def_idle)
	: EComp(ent), pars(std::move(pars)), grp(std::move(grp)), def_idle(std::move(def_idle))
{
	this->grp->reg_upd(this, true);
	prov = nullptr;
}
AI_Drone::~AI_Drone()
{
	grp->reg_upd(this, false);
	
	if (auto st = std::get_if<Suspect>(&state))
		grp->proxy_inspect(st->p);
}
void AI_Drone::set_task(Task new_task)
{
	task = std::move(new_task);
	
	if (std::holds_alternative<TaskIdle>(task))
	{
		state = *def_idle;
		if (mov)
		{
			if (auto st = std::get_if<IdlePoint>(&state)) mov->set_target(st->p, AI_Speed::Slow);
			else mov->set_target({});
		}
		text_alert("Idle");
	}
	else if (auto tk = std::get_if<TaskSuspect>(&task))
	{
		Suspect st;
		st.p = tk->pos;
		st.wait = AI_Const::suspect_wait;
		state = std::move(st);
		
		mov->set_target(tk->pos);
		text_alert("Inspect");
	}
	else if (std::holds_alternative<TaskSearch>(task))
	{
		state = Search{};
		mov->set_target({});
		text_alert("Searching");
	}
	else if (std::holds_alternative<TaskEngage>(task))
	{
		if (!std::holds_alternative<Engage>(state))
		{
			state = Engage{};
			text_alert("Engage");
		}
		
		if (mov) mov->set_target({});
		has_lost = false;
	}
}
void AI_Drone::update_enabled(bool now_enabled)
{
	if (now_enabled)
	{
		reg(ECompType::StepLogic);
		prov->reg(ECompType::StepPreUtil);
		if (mov) mov->reg(ECompType::StepPostUtil);
	}
	else
	{
		unreg(ECompType::StepLogic);
		prov->unreg(ECompType::StepPreUtil);
		if (mov) {
			mov->unreg(ECompType::StepPostUtil);
			ent->get_phobj().body->SetLinearVelocity({0, 0});
			ent->get_phobj().body->SetAngularVelocity(0);
		}
	}
}
std::string AI_Drone::get_dbg_state() const
{
	std::string s = "GROUP\n";
	s += grp->get_dbg_state();
	s += "\nDRONE\n";
	
	if		(std::holds_alternative<TaskIdle>   (task)) s += "Task: idle\n";
	else if (std::holds_alternative<TaskSuspect>(task)) s += "Task: suspect\n";
	else if (std::holds_alternative<TaskSearch> (task)) s += "Task: search...\n";
	else if (auto st =  std::get_if<TaskEngage>(&task))
	{
		s += "Task: engage (";
		auto ent = GameCore::get().get_ent(st->eid);
		s += ent? ent->dbg_id() : "INVALID";
		s += ")\n";
	}
	else s += "Task: UNKNOWN\n";
	
	if		(std::holds_alternative<IdleNoop> (state)) s += "State: idle (noop)";
	else if (std::holds_alternative<IdlePoint>(state)) s += "State: idle (point)";
	else if (std::holds_alternative<Suspect>  (state)) s += "State: suspect";
	else if (std::holds_alternative<Search>   (state)) s += "State: search...";
	else if (auto st =  std::get_if<Engage>  (&state))
	{
		s += "State: engage (";
		if (st->circ_left) s += "circ";
		s += ")\n";
	}
	else s += "State: UNKNOWN";
	
	return s;
}
void AI_Drone::step()
{
	std::optional<float> tar_dist; ///< Set if seen
	if (auto tar = prov->get_target())
	{
		if (!tar->is_suspect)
		{
			grp->report_target( GameCore::get().get_ent(tar->eid) );
			has_lost = false;
			
			if (auto tk = std::get_if<TaskEngage>(&task);
			    tk && tk->eid == tar->eid)
			{
				tar_dist = tar->dist;
			}
			else text_alert("Target");
		}
		else if (!std::holds_alternative<TaskEngage>(task))
		{
			vec2fp pos = GameCore::get().get_ent(tar->eid)->get_pos();
			if (!pars->is_camper && mov)
			{
				if (!std::holds_alternative<Suspect>(state))
					text_alert("Suspicious");
				
				Suspect st;
				st.p = pos;
				st.wait = TimeSpan::seconds(3);
				state = std::move(st);
				
				mov->set_target(pos, AI_Speed::Slow);
			}
			else grp->proxy_inspect(pos);
		}
	}
	
	//
	
	if (mov)
	{
		const float dist = AI_Const::mov_steer_dist + ent->get_phy().get_radius();
		const float evade_force = AI_Const::mov_steer_force;
		
		if (auto tar = mov->get_next_point())
		{
			vec2fp self = ent->get_pos();
			vec2fp dir = *tar - self;
			
			if (dir.len_squ() > dist * dist)
			{
				dir.norm();
				
				if (auto rc = GameCore::get().get_phy().raycast_nearest( conv(self), conv(self + dir * dist) ))
				{
					bool do_steer = true;
					
					// if collided with drone, check if it's moving, compare direction and speed
					// if direction is same & it's not slower - don't steer
					// if direction is opposite & it's slower - also don't steer
					if (auto drone = rc->ent->get_ai_drone();
					    drone && drone->mov)
					{
						if (auto next = drone->mov->get_next_point())
						{
							vec2fp mov_dt = *next - drone->ent->get_pos();
							mov_dt.norm();
							
							float cmp = dot(mov_dt, dir);
							if ((cmp > 0 && drone->mov->get_current_speed() > mov->get_current_speed() - 0.5) ||
							    (cmp < 0 && drone->mov->get_current_speed() < mov->get_current_speed() + 0.5))
								do_steer = false;
						}
					}
					
					if (do_steer)
					{
						dir.rot90cw(); // len = 1
						dir *= rc->ent->get_phy().get_radius() + ent->get_phy().get_radius();
						
						vec2fp t_pos = conv(rc->poi) + dir;
						vec2fp delta = t_pos - self;
						
						delta *= std::max(AI_Const::mov_steer_min_t, 1 - delta.len() / dist);
						delta *= evade_force;
						mov->steering = -delta;
					}
				}
			}
		}
	}
	
	//
	
	if (auto st = std::get_if<Engage>(&state))
	{
		if (!grp->area.contains( std::get<TaskEngage>(task).last_pos )) // out of range, can't move
		{
			if (mov) mov->set_target({}, AI_Speed::Accel);
			if (!tar_dist) grp->no_target( std::get<TaskEngage>(task).eid );
		}
		else if (tar_dist) // target is visible
		{
			auto& tk = std::get<TaskEngage>(task);
			const auto t_ent = GameCore::get().get_ent( tk.eid );
			const auto t_pos = t_ent->get_pos();
			
			tk.last_pos = t_pos;
			st->last_visible = GameCore::get().get_step_time();
			
			if (st->chase_cou.is_positive())
				st->chase_cou -= GameCore::step_len * AI_Const::chase_camp_decr;
			
			if (mov)
			{
				// maintain optimal distance
				if (*tar_dist < pars->dist_minimal)
				{
					const vec2fp delta = t_pos - ent->get_pos();
					const float da = delta.angle();
					
					vec2fp pos = ent->get_pos();
					vec2fp tar = t_pos + (delta / *tar_dist) * -pars->dist_minimal;
					
					auto rc = GameCore::get().get_phy().raycast_nearest( conv(pos), conv(tar) );
					if (rc) tar = conv(rc->poi);
					
					tar -= vec2fp( ent->get_phy().get_radius(), 0 ).get_rotated( da );
					mov->set_target( tar, AI_Speed::Slow );
				}
				else if (*tar_dist > pars->dist_optimal)
				{
					const vec2fp delta = t_pos - ent->get_pos();
					const float da = delta.angle();
					
					vec2fp pos = ent->get_pos();
					vec2fp tar = pos + (delta / *tar_dist) * pars->dist_optimal;
					
					tar += vec2fp( ent->get_phy().get_radius(), 0 ).get_rotated( da );
					mov->set_target( tar, AI_Speed::Accel );
				}
				else
				{
					if (st->circ_left)
					{
						if (st->crowd_cou.is_positive() || st->nolos_cou.is_positive() || st->circ_delay.is_positive())
						{
							st->circ_delay -= GameCore::step_len;
							if (!mov->has_target())
							{
								auto p = grp->get_aos_next(this, *st->circ_left);
								if (p) mov->set_target(p, st->nolos_cou.is_positive() ? AI_Speed::Normal : AI_Speed::Slow);
							}
						}
						else
						{
							mov->set_target({});
							st->circ_prev_time = GameCore::get().get_step_time();
							st->circ_prev_left = *st->circ_left;
							st->circ_left.reset();
						}
					}
					
					vec2fp crowd_dir = {}; // FROM crowd
					bool crowd_any = false;
					
					std::vector<PhysicsWorld::CastResult> es;
					GameCore::get().get_phy().circle_cast_all(es, conv(ent->get_pos()), AI_Const::crowd_distance);
					for (auto& e : es)
					{
						if (e.ent != ent &&
							e.ent->get_team() == ent->get_team())
						{
							if (auto d = e.ent->get_ai_drone();
								d /*&& d->mov && !d->mov->has_target()*/)
							{
								crowd_dir += ent->get_pos() - e.ent->get_pos();
								crowd_any = true;
							}
						}
					}
					
					if (!crowd_any) st->crowd_cou = {};
					else st->crowd_cou += GameCore::step_len;
					
					if ((st->crowd_cou > AI_Const::pos_req_crowd_time ||
						 st->nolos_cou > AI_Const::pos_req_los_time))
					{
						float dv = 0;
						if (crowd_any && crowd_dir.len_squ() > 0.1)
						{
							vec2fp side = t_pos - ent->get_pos();
							side.norm();
							side.rot90cw();
							crowd_dir.norm();
							dv = dot(side, crowd_dir);
						}
						
						if (std::fabs(dv) > 0.5) {
							st->circ_left = (dv > 0);
						}
						else if (GameCore::get().get_step_time() - st->circ_prev_time < AI_Const::pos_req_mem_time) {
							st->circ_left = st->circ_prev_left;
						}
						else st->circ_left = GameCore::get().get_random().flag();
						
						if (!st->circ_delay.is_positive())
							st->circ_delay = AI_Const::pos_req_reset_delay;
					}
				}
			}
		}
		else if (mov) // target is not visible
		{
			st->nolos_cou = st->crowd_cou = {};
			
			if (!pars->is_camper) // target is not visible
			{
				if (st->chase_cou < AI_Const::chase_camp_time)
				{
					if (!st->chase_cou.is_positive()) text_alert("Camp");
					st->chase_cou += GameCore::step_len;
				}
				else if (!mov->has_target())
				{
					const float d_ahead = AI_Const::chase_ahead_dist;
					auto& tk = std::get<TaskEngage>(task);

					vec2fp tar = tk.last_pos;
					
					if (GameCore::get().get_step_time() - st->last_visible < AI_Const::chase_camp_time)
					{
						vec2fp dir = tar - ent->get_pos();
						if (dir.len_squ() > 1)
						{
							dir.norm_to(d_ahead);
							if (auto rc = GameCore::get().get_phy().raycast_nearest( conv(tar), conv(tar + dir) ))
								dir = conv(rc->poi) - tar;
						}
						tar += dir;
					}
					
					if (mov->set_target( tar, AI_Speed::Accel ) || has_lost)
					{
						grp->no_target( tk.eid );
						text_alert("Lost");
					}
					else text_alert("Chase");
				}
				else if (!has_lost && LevelControl::get().is_same_coord( ent->get_pos(), std::get<TaskEngage>(task).last_pos ))
				{
					has_lost = true;
				}
			}
			else text_alert("Camp");
		}
	}
	else if (auto st = std::get_if<Search>(&state))
	{
		vec2fp prev = ent->get_pos();
		for (auto& p : std::get<TaskSearch>(task).waypoints)
			prev = p;
		
		if (st->end) {}
		else if (!mov->has_target())
		{
			st->tmo -= GameCore::step_len;
			if (st->tmo.is_negative())
			{
				auto& wps = std::get<TaskSearch>(task).waypoints;
				if (st->next_wp == wps.size())
				{
					st->end = true;
					text_alert("Wait");
				}
				else {
					mov->set_target(wps[st->next_wp], AI_Speed::Slow);
					st->tmo = AI_Const::search_point_wait;
					++st->next_wp;
					text_alert("Inspect");
				}
			}
		}
	}
	else if (auto st = std::get_if<Suspect>(&state))
	{
		if (!mov->has_target())
		{
			st->wait -= GameCore::step_len;
			if (st->wait.is_negative())
			{
				if (std::holds_alternative<TaskIdle>(task))
					set_task(TaskIdle{});
			}
		}
	}
	
	//
	
	std::optional<vec2fp> target_pos;
	
	if (tar_dist)
	{
		auto tar = GameCore::get().get_ent( std::get<TaskEngage>(task).eid );
		bool los_clear = atk.shoot(tar, *tar_dist, ent);
		target_pos = tar->get_pos();
		
		if (auto st = std::get_if<Engage>(&state))
		{
			if (los_clear) st->nolos_cou = {};
			else st->nolos_cou += GameCore::step_len;
		}
	}
	else if (auto proj = prov->get_projectile())
	{
		auto tar = GameCore::get().get_ent( proj->eid );
		atk.shoot(tar, proj->dist, ent);
		target_pos = tar->get_pos();
	}
	
	//
	
	ren_rot.update(this, target_pos, mov? mov->get_next_point() : std::optional<vec2fp>());
}
void AI_Drone::text_alert(std::string s)
{
	auto now = GameCore::get().get_step_time();
	if (now - text_alert_last < TimeSpan::seconds(2)) return;
	text_alert_last = now;
	
	vec2fp p = ent->get_pos();
	p.x += 0.7 * rnd_stat().range_n2();
	p.y += 0.2 * rnd_stat().range_n2();
	GamePresenter::get()->add_float_text({ p, std::move(s) });
}
