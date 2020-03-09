#include "client/presenter.hpp"
#include "game/game_core.hpp"
#include "game/physics.hpp"
#include "utils/noise.hpp"
#include "ai_drone.hpp"



void AI_Drone::IdlePatrol::next()
{
	at = (at + 1) % pts.size();
	tmo = {};
}



AI_Drone::Battle::Battle(AI_Drone& drone)
	: grp(drone.ent.core.get_aic().get_group(drone))
{
	reset_firstshot();
}
void AI_Drone::Battle::reset_firstshot()
{
	firstshot_time = grp->core.get_step_time() + AI_Const::attack_first_shot_delay;
}



AI_Drone::AI_Drone(Entity& ent, std::shared_ptr<AI_DroneParams> pars, IdleState idle, std::unique_ptr<AI_AttackPattern> atkpat)
	:
	EComp(ent),
	pars(std::move(pars)),
    prov(*this),
    atk(std::move(atkpat)),
    particles( ent.ensure<EC_ParticleEmitter>().new_channel() )
{
	state_stack.emplace_back(Idle{ std::move(idle) });
	home_point = ent.get_pos();
	ent.core.get_aic().ref_drone(this);
	ent.ref_pc().rot_override = ent.core.get_random().range_n2() * M_PI;
}
AI_Drone::~AI_Drone()
{
	if (!ent.core.is_freeing())
	{
		bool hcp =
			std::holds_alternative<Suspect>(get_state()) ||
		    std::holds_alternative<Search> (get_state());
		
		helpcall({}, hcp);
		ent.core.get_aic().unref_drone(this);
	}
}
void AI_Drone::set_online(bool is)
{
	if ((is_online != 0) == is) return;
	is_online = is;
	
	if (is_online)
	{
		reg(ECompType::StepLogic);
		prov.reg(ECompType::StepPreUtil);
		if (mov) mov->reg(ECompType::StepPostUtil);
	}
	else
	{
		set_idle_state();
		
		unreg(ECompType::StepLogic);
		prov.unreg(ECompType::StepPreUtil);
		if (mov) {
			mov->unreg(ECompType::StepPostUtil);
			ent.ref_phobj().body.SetLinearVelocity({0, 0});
			ent.ref_phobj().body.SetAngularVelocity(0);
		}
	}
}
std::string AI_Drone::get_dbg_state() const
{
	std::string s;
	for (auto& state : state_stack)
	{
		std::visit(overloaded{
			[&s](const Idle   &){ s += "STATE: IDLE\n"; },
			[&s](const Battle &){ s += "STATE: BATTLE\n"; },
			[&s](const Suspect&){ s += "STATE: SUSPECT\n"; },
			[&s](const Search &){ s += "STATE: SEARCHING\n"; },
			[&s](const Puppet &){ s += "STATE: PUPPET\n"; }
		}, state);
	}
	return s;
}
void AI_Drone::add_state(State new_state)
{
	state_stack.emplace_back( std::move(new_state) );
	state_on_enter( state_stack.back() );
}
void AI_Drone::replace_state(State new_state)
{
	state_on_leave( state_stack.back() );
	state_stack.back() = std::move(new_state);
	state_on_enter( state_stack.back() );
}
void AI_Drone::remove_state()
{
	state_on_leave( state_stack.back() );
	state_stack.pop_back();
	state_on_enter( state_stack.back() );
}
void AI_Drone::set_single_state(State new_state)
{
	while (state_stack.size() > 1) {
		state_on_leave(state_stack.back());
		state_stack.pop_back();
	}
	add_state(std::move(new_state));
}
void AI_Drone::set_idle_state()
{
	while (state_stack.size() > 1) {
		state_on_leave(state_stack.back());
		state_stack.pop_back();
	}
	state_on_enter( state_stack.back() );
}
void AI_Drone::state_on_enter(State& state)
{
	if (mov) mov->set_target({});
	particles->stop(true);
	
	std::visit(overloaded{
		[&](Idle& gst)
		{
			prov.fov_t = 0;
			text_alert("...", true);

			if		(auto st = std::get_if<IdleResource>(&gst.ist)) st->reg = {};
			else if (auto st = std::get_if<IdlePatrol  >(&gst.ist)) st->next();
		},
		[&](Battle&)
		{
			prov.fov_t = {};
			text_alert("!!!", true, 3);

			atk.reset_target();
		},
		[&](Suspect& st)
		{
			prov.fov_t = clampf_n(st.level);
			text_alert("?", true, 3);
		},
		[&](Search&)
		{
			prov.fov_t = 1;
			text_alert("!..", true);
		},
		[](Puppet&){}
	}
	, state);
}
void AI_Drone::state_on_leave(State& state)
{
	if (std::holds_alternative<Battle>(state))
	{
		if (atk.atkpat)
			atk.atkpat->reset(ent);
	}
}
void AI_Drone::helpcall(std::optional<vec2fp> target, bool high_prio)
{
	auto now = ent.core.get_step_time();
	if (now - helpcall_last > AI_Const::helpcall_timeout || high_prio)
	{
		helpcall_last = now;
		ent.core.get_aic().help_call(*this, target, high_prio);
	}
}
void AI_Drone::step()
{
	std::optional<float> tar_dist; ///< Set if seen
	bool damaged_by_tar = false;
	
	prov.is_battle = std::holds_alternative<Battle>(get_state());
	if (auto tar = prov.get_target())
	{
		tar_dist = tar->dist;
		damaged_by_tar = tar->is_damaging;
		
		if (std::holds_alternative<Idle>(get_state()) || std::holds_alternative<Search>(get_state()))
		{
			if (tar->is_damaging && !mov) // also - current state can't be 'Search'
			{
				if (tar->is_suspect) {
					vec2fp p = ent.core.ent_ref(tar->eid).get_pos();
					helpcall(p, true);
				}
				else set_battle_state();
			}
			else
			{
				float lvl = AI_Const::suspect_initial;
				if (auto st = std::get_if<Search>(&get_state())) lvl = std::max(lvl, st->susp_level);
				if (tar->is_damaging) lvl = std::max(lvl, AI_Const::suspect_on_damage);
				
				vec2fp p = ent.core.ent_ref(tar->eid).get_pos();
				add_state(Suspect{ p, lvl });
				if (!mov) helpcall(p, false);
			}
		}
		else if (auto st = std::get_if<Suspect>(&get_state()))
		{
			if (tar->is_damaging)
			{
				if (st->level < AI_Const::suspect_on_damage)
					st->level = AI_Const::suspect_on_damage;
				else
					set_battle_state();
			}
			else st->pos = ent.core.ent_ref(tar->eid).get_pos();
		}
	}
	
	if (damaged_by_tar) retaliation_tmo = AI_Const::retaliation_length;
	else if (retaliation_tmo.is_positive())
		retaliation_tmo -= GameCore::step_len;
	
	//
	
	std::optional<vec2fp> rot_target; // set to what should be facing now
	
	auto reached = [this](TimeSpan t) {
		return t <= ent.core.get_step_time();
	};
	
	// main states
	
	if (auto st = std::get_if<Battle>(&get_state()))
	{
		auto goto_placement = [&]
		{
			PathRequest::Evade evade;
			evade.pos = st->grp->get_last_pos();
			evade.radius     = AI_Const::placement_follow_evade_radius;
			evade.added_cost = AI_Const::placement_follow_evade_cost;
			
			if (mov->set_target( st->placement, AI_Speed::Normal, evade ))
			    st->placement = {};
		};
		
		if (tar_dist) // target is visible
		{
			auto& t_ent = ent.core.ent_ref( st->grp->tar_eid );
			const auto t_pos = t_ent.get_pos();
			rot_target = t_pos;
			
			st->grp->report_seen();
			st->not_visible = {};
			st->chase_wait = std::min(1., st->chase_wait + GameCore::step_len / AI_Const::chase_wait_incr);
			
			const float atk_range = pars->dist_battle.value_or( pars->dist_visible );
			if ((*tar_dist <= atk_range && *tar_dist >= pars->dist_panic && reached(st->firstshot_time))
			    || damaged_by_tar || retaliation_tmo.is_positive())
			{
//				bool los_clear = atk.shoot(t_ent, *tar_dist, *ent);
				                 atk.shoot(t_ent, *tar_dist, ent);
			}
			if (atk.atkpat) atk.atkpat->idle(ent);
			
			if (mov)
			{
				// maintain optimal distance
				if (st->placement) goto_placement();
				else if (*tar_dist < pars->dist_minimal)
				{
					const vec2fp delta = t_pos - ent.get_pos();
					const float da = delta.angle();
					
					vec2fp pos = ent.get_pos();
					vec2fp tar = t_pos + (delta / *tar_dist) * -pars->dist_minimal;
					
					auto rc = ent.core.get_phy().raycast_nearest( conv(pos), conv(tar) );
					if (rc) tar = conv(rc->poi);
					tar -= vec2fp( ent.ref_pc().get_radius() + 0.1, 0 ).rotate( da );
					
					if (*tar_dist >= pars->dist_panic) mov->set_target( tar, AI_Speed::Slow );
					else {
						tar_dist.reset();
						mov->set_target( tar, AI_Speed::Accel );
					}
				}
				else if (*tar_dist > pars->dist_optimal)
				{
					const vec2fp delta = t_pos - ent.get_pos();
					const float da = delta.angle();
					
					vec2fp pos = ent.get_pos();
					vec2fp tar = pos + (delta / *tar_dist) * pars->dist_optimal;
					
					tar += vec2fp( ent.ref_pc().get_radius(), 0 ).rotate( da );
					mov->set_target( tar, AI_Speed::Accel );
				}
				else mov->set_target({});
			}
		}
		else if (!ent.core.get_ent(st->grp->tar_eid)) // target destroyed
		{
			st->grp->init_search();
		}
		else // target is not visible
		{
			if (st->grp->passed_since_seen() > AI_Const::attack_reset_timeout)
			{
				atk.reset_target();
				st->reset_firstshot();
			}
			if (atk.atkpat) atk.atkpat->idle(ent);
			
			st->not_visible += GameCore::step_len;
			
			if (st->placement)
			{
				goto_placement();
			}
			else if (st->chase_wait > 0)
			{
				st->chase_wait -= GameCore::step_len / AI_Const::chase_wait_decr;
				if (mov) mov->set_target({});
			}
			else if (!is_camper())
			{
				vec2fp tar = st->grp->get_last_pos();
				
				if (st->not_visible > st->grp->passed_since_seen())
				{
					mov->set_target( tar, AI_Speed::Accel );
					// someone in group is closer, so no init_search on failure
				}
				else if (!mov->has_target() || st->not_visible <= GameCore::step_len) // chase ahead
				{
					vec2fp dir = tar - ent.get_pos();
					if (dir.len_squ() > 1)
					{
						dir.norm_to( AI_Const::chase_ahead_dist );
						if (auto rc = ent.core.get_phy().raycast_nearest( conv(tar), conv(tar + dir) ))
							dir = conv(rc->poi) - tar;
					}
					tar += dir;
					
					if (mov->set_target( tar, AI_Speed::Accel ) || mov->has_failed())
						st->grp->init_search();
				}
			}
		}
	}
	else if (auto st = std::get_if<Suspect>(&get_state()))
	{
		prov.fov_t = clampf_n(st->level);
		rot_target = st->pos;
		
		if (tar_dist)
		{
			st->was_visible = true;
			st->level += GameCore::step_len / (tar_dist < pars->dist_visible ? AI_Const::suspect_incr_close : AI_Const::suspect_incr);
			
			if (st->level > 1) set_battle_state();
			else if (!is_camper())
			{
				if (*tar_dist < pars->dist_optimal) mov->set_target({}, AI_Speed::Slow);
				else if (st->level >= AI_Const::suspect_chase_thr) mov->set_target(st->pos);
			}
		}
		else if (st->prio != Suspect::PRIO_NORMAL)
		{
			st->was_visible = false;
			auto spd = st->prio == Suspect::PRIO_HELPCALL_HIGH ? AI_Speed::Accel : AI_Speed::Normal;
			if (mov->set_target(st->pos, spd))
				st->prio = Suspect::PRIO_NORMAL;
		}
		else
		{
			st->was_visible = false;
			st->level -= GameCore::step_len / AI_Const::suspect_decr;
			if (mov && mov->has_target()) st->level = std::max(st->level, AI_Const::suspect_chase_thr);
			else if (st->level < 0) remove_state();
		}
	}
	else if (auto st = std::get_if<Search>(&get_state()))
	{
		if (st->susp_level > 0)
			st->susp_level -= GameCore::step_len / AI_Const::suspect_decr;
		
		if (st->tmo.is_positive())
		{
			st->tmo -= GameCore::step_len;
			if (!st->tmo.is_positive())
			{
				bool last = st->at == st->pts.size() - 1;
				if (last) set_idle_state();
			}
		}
		else if (mov->set_target( st->pts[st->at], AI_Speed::Patrol ))
		{
			bool last = st->at == st->pts.size() - 1;
			st->tmo = last ? AI_Const::search_point_wait_last : AI_Const::search_point_wait;
			++st->at;
		}
	}
	else if (auto st = std::get_if<Puppet>(&get_state()))
	{
		if (!st->mov_tar) mov->set_target({});
		st->ret_atpos = (!st->mov_tar || mov->set_target(st->mov_tar->first, st->mov_tar->second));
		GamePresenter::get()->dbg_text( ent.get_pos(), "PUP" );
	}
	
	// idle states
	
	else
	{
		auto& gst = std::get<Idle>(get_state());
		
		if (!mov) {}
		else if (std::holds_alternative<IdlePoint>(gst.ist))
		{
			mov->set_target(home_point, AI_Speed::Patrol);
		}
		else if (auto st = std::get_if<IdleResource>(&gst.ist))
		{
			if (st->reg.is_reg())
			{
				auto res = st->reg.process( st->val, ent.get_pos() );
				std::visit(overloaded{
					[&](AI_SimResource::ResultFinished)
					{
						mov->set_target({});
						st->reg = {};

						particles->stop(true);
					},
					[&](AI_SimResource::ResultNotInRange wr)
					{
						mov->set_target(wr.move_target, AI_Speed::SlowPrecise);

						particles->stop(true);
						st->particle_tmo = TimeSpan::seconds(2);
					},
					[&](AI_SimResource::ResultWorking wr)
					{
						mov->set_target({});
						rot_target = wr.view_target;
				                   
						if (!particles->is_playing())
						{
							if (st->particle_tmo.is_positive())
								st->particle_tmo -= GameCore::step_len;
							else {
								vec2fp p = {ent.ref_pc().get_radius(), 0};
								if (st->is_loading)
									particles->play(FE_WPN_CHARGE, {Transform{p}, 1, FColor(0.5, 0.4, 0.8), 0.5},
									                TimeSpan::seconds(0.2), TimeSpan::nearinfinity);
								else
									particles->play(FE_WPN_CHARGE, {Transform{p}, 1, FColor(0.8, 0.7, 0.4), 0.4},
									                TimeSpan::seconds(1), TimeSpan::nearinfinity);
							}
						}
					}
				}, res);
			}
			else if (st->reg_try_tmo.is_positive()) {
				st->reg_try_tmo -= GameCore::step_len;
			}
			else {
				if (st->is_loading) {
					if (st->val.amount >= st->val.capacity) st->is_loading = false;
				}
				else {
					if (st->val.amount <= 0) st->is_loading = true;
				}
				
				flags_t flags;
				if (st->is_loading) flags = AI_SimResource::FIND_NEAREST_RANDOM;
				else flags = AI_SimResource::FIND_NEAREST_STRICT | AI_SimResource::FIND_F_SAME_ROOM;
				
				st->reg = AI_SimResource::find( ent.core, home_point, 40, st->val.type, st->is_loading ? -1 : 5, flags );
				if (!st->reg.is_reg()) st->reg_try_tmo = TimeSpan::seconds(2);
				else st->particle_tmo = TimeSpan::seconds(2);
			}
		}
		else if (auto st = std::get_if<IdlePatrol>(&gst.ist))
		{
			const vec2fp pos = ent.get_pos();
			const vec2fp npt = st->pts[st->at];
			const vec2fp tar = pos + AI_Const::patrol_raycast_length * (npt - pos).norm();
			
			// prevent crowding
			bool stop = false;
			if (auto rc = ent.core.get_phy().raycast_nearest( conv(pos), conv(tar), {}, AI_Const::patrol_raycast_width ))
			{
				if (auto d = rc->ent->get_ai_drone())
				{
					if (auto d_st = std::get_if<Idle>(&d->get_state()))
					{
						if (auto os = std::get_if<IdlePatrol>(&d_st->ist);
						    os && os->pts[os->at].equals( npt, GameConst::cell_size ))
						{
							stop = true;
						}
					}
					
				}
			}
			
			if (stop) mov->set_target({});
			else if (mov->set_target( npt, AI_Speed::Slow ))
			{
				if (st->tmo < AI_Const::patrol_point_wait) st->tmo += GameCore::step_len;
				else st->next();
			}
		}
	}
	
	//
	
	rot_ctl.update(*this, rot_target, mov ? mov->get_next_point() : std::optional<vec2fp>{});
}
void AI_Drone::text_alert(std::string s, bool important, size_t num)
{
	auto now = ent.core.get_step_time();
	if (now - text_alert_last < TimeSpan::seconds(2) && !important) return;
	text_alert_last = now;
	
	for (size_t i=0; i<num; ++i)
	{
		vec2fp p = ent.get_pos();
		p.x += 0.7 * rnd_stat().range_n2();
		p.y += 0.2 * rnd_stat().range_n2();
		GamePresenter::get()->add_float_text({ p, std::move(s) });
	}
}
