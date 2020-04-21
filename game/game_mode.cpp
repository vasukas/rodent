#include <queue>
#include "client/level_map.hpp"
#include "client/player_ui.hpp"
#include "client/presenter.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "game/physics.hpp"
#include "game/player_mgr.hpp"
#include "game_objects/objs_creature.hpp"
#include "vaslib/vas_log.hpp"
#include "game_mode.hpp"

constexpr size_t tokens_needed = 3;

constexpr TimeSpan boot_length = TimeSpan::seconds(90);
constexpr float level_decr = 0.03; // per second
constexpr float level_incr = 0.025; // per second

constexpr size_t max_bots = 22; // including bosses and hackers



class GameModeCtr_Impl : public GameModeCtr
{
public:
	GameCore* core;
	Rectfp term_area;
	
	State state = State::NoTokens;
	TimeSpan boot_at;
	
	bool first_message = false;
	bool term_found = false;
	size_t token_count = 0;
	float boot_level = 1;
	bool hacker_working = false;
	
	struct Teleport {
		vec2fp at;
		int used = 0;
	};
	std::vector<Teleport> teleps;
	
	//
	
	struct Wave
	{
		TimeSpan tmo = TimeSpan::seconds(15);
		int n_worker  = 4;
		int n_drones  = 8;
		int n_campers = 1;
		int n_bosses  = 1;
		int n_hackers = 2;
	};
	std::queue<Wave> waves;
	
	
	
	void ui_message(std::string s) {
		if (auto p = core->get_pmg().get_pui())
			p->message(std::move(s));
	}
	void ui_float(std::string s) {
		auto p = GamePresenter::get();
		auto e = core->get_pmg().get_ent();
		if (p && e)
			p->add_float_text({ e->get_pos(), std::move(s) });
	}
	void ui_objective(std::string s) {
		if (auto p = core->get_pmg().get_pui())
			p->objective = std::move(s);
	}
	int count_bots() {
		int n = 0;
		core->get_phy().query_aabb(term_area, [&](Entity& ent, auto&) {if (ent.get_ai_drone()) ++n;});
		return n;
	}
	
	GameModeCtr_Impl()
	{
		{
			auto& w = waves.emplace();
			w.tmo = TimeSpan::seconds(3);
			w.n_worker  = 2;
			w.n_drones  = 4;
			w.n_campers = 1;
			w.n_bosses  = 0;
			w.n_hackers = 1;
		}{
			auto& w = waves.emplace();
			w.tmo = TimeSpan::seconds(18);
			w.n_worker  = 1;
			w.n_drones  = 5;
			w.n_campers = 0;
			w.n_bosses  = 1;
			w.n_hackers = 1;
		}{
			auto& w = waves.emplace();
			w.tmo = TimeSpan::seconds(30);
			w.n_worker  = 2;
			w.n_drones  = 4;
			w.n_campers = 3;
			w.n_bosses  = 1;
			w.n_hackers = 2;
		}{
			auto& w = waves.emplace();
			w.tmo = TimeSpan::seconds(25);
			w.n_worker  = 0;
			w.n_drones  = 8;
			w.n_campers = 1;
			w.n_bosses  = 1;
			w.n_hackers = 2;
		}
	}
	void init(GameCore& p_core)
	{
		core = &p_core;
		
		term_area = {};
		for (auto& r : core->get_lc().get_rooms()) {
			if (r.type == LevelCtrRoom::T_FINAL_TERM) {
				term_area = r.fp_area();
				break;
			}
		}
	}
	void step()
	{
		if (state == State::NoTokens || state == State::HasTokens)
		{
			auto ent = core->get_pmg().get_ent();
			if (ent && !first_message && core->get_pmg().get_pui())
			{
				first_message = true;
				ui_message(FMT_FORMAT("Activate control terminal\nWith {} security tokens", tokens_needed));
				ui_objective(FMT_FORMAT("collect security tokens ({} left)", tokens_needed - token_count));
			}
			if (ent && !term_found)
			{
				auto room = core->get_lc().ref_room(ent->get_pos());
				if (room && room->type == LevelCtrRoom::T_FINAL_TERM)
				{
					term_found = true;
					LevelMap::get().mark_final_term(*room);
					
					ui_message("You have found\ncontrol room");
					if (state == State::HasTokens)
						ui_objective("activate control terminal");
				}
			}
		}
		else if (state == State::Booting)
		{
			ui_objective(FMT_FORMAT("protect terminal while it's booting - {:.1f} seconds", get_boot_left().seconds()));
			
			if (!hacker_working)
				boot_level = std::min(1.f, boot_level + level_incr * core->time_mul);
			
			if (boot_level < 0) {
				state = State::Lost;
				
				Transform tr{term_area.center()};
				GamePresenter::get()->effect(FE_WPN_EXPLOSION, {tr, 6});
				GamePresenter::get()->effect(FE_SPAWN, {tr, 3});
			}
			
			if (boot_at <= core->get_step_time()) {
				state = State::Cleanup;
				ui_objective("remove hostiles from terminal room");
			}
			
			if (!waves.empty())
			{
				auto& w = waves.front();
				if (w.tmo.is_positive()) w.tmo -= core->step_len;
				else {
					std::sort(teleps.begin(), teleps.end(), [](auto& a, auto& b) {return a.used < b.used;});
					size_t i_tp = -1;
					auto tp = [&]{
						i_tp = (i_tp + 1) % teleps.size();
						++teleps[i_tp].used;
						return teleps[i_tp].at;
					};
					
					auto eff = [&](Entity* ent){
						GamePresenter::get()->effect(FE_SPAWN, {{ent->ref_pc().get_trans()}, GameConst::hsz_drone_big});
						ent->ref_ai_drone().set_battle_state();
					};
					
					int n_spawn = max_bots - count_bots();
					
					for (int i=0; i<w.n_drones; ++i) {
						if (!n_spawn) break; else --n_spawn;
						eff(new EEnemyDrone(*core, tp(), EEnemyDrone::def_drone(*core)));
					}
					for (int i=0; i<w.n_campers; ++i) {
						if (!n_spawn) break; else --n_spawn;
						eff(new EEnemyDrone(*core, tp(), EEnemyDrone::def_campr(*core)));
					}
					for (int i=0; i<w.n_worker; ++i) {
						if (!n_spawn) break; else --n_spawn;
						auto init = EEnemyDrone::def_workr(*core);
						init.is_worker = false;
						eff(new EEnemyDrone(*core, tp(), std::move(init)));
					}
					if (core->spawn_hunters) {
						for (int i=0; i<w.n_bosses; ++i) {
							new EHunter(*core, tp());
						}
					}
					for (int i=0; i<w.n_hackers; ++i) {
						eff(new EHacker(*core, tp()));
					}
					
					waves.pop();
				}
			}
		}
		else if (state == State::Cleanup)
		{
			if (!count_bots()) {
				state = State::Final;
				ui_message("Terminal unlocked");
				ui_objective("use control terminal");
			}
		}
	}
	
	State    get_state()      {return state;}
	TimeSpan get_boot_left()  {return boot_at - core->get_step_time();}
	float    get_boot_level() {return boot_level;}
	
	void inc_objective()
	{
		++token_count;
		if (token_count == tokens_needed)
		{
			if (term_found) {
				ui_message("Activate control terminal");
				ui_objective("activate control terminal");
			}
			else {
				ui_message("Find control terminal");
				ui_objective("find control terminal");
			}
			state = State::HasTokens;
		}
		else /*if (token_count < tokens_needed)*/ // this bug is funny
		{
			ui_float(FMT_FORMAT("Security token!\nNeed {} more", tokens_needed - token_count));
			if (state == State::NoTokens)
				ui_objective(FMT_FORMAT("collect security tokens ({} left)", tokens_needed - token_count));
		}
	}
	void on_teleport_activation()
	{
		if (!term_found && state == State::HasTokens)
		{
			term_found = true;
			ui_message("Map updated.\nActivate control terminal");
			ui_objective("activate control terminal");
			
			LevelMap::get().mark_final_term([&]{
				for (auto& r : core->get_lc().get_rooms()) {
					if (r.type == LevelCtrRoom::T_FINAL_TERM)
						return r;
				}
				throw std::runtime_error("GameModeCtr::on_teleport_activation() no T_FINAL_TERM room");
			}());
		}
	}
	void terminal_use()
	{
		if (state == State::HasTokens) {
			state = State::Booting;
			boot_at = core->get_step_time() + boot_length;
			ui_message("Protect control terminal");
		}
		else if (state == State::Final) {
			state = State::Won;
		}
	}
	void hacker_work()
	{
		boot_level -= level_decr * core->time_mul;
		hacker_working = true;
	}
	void add_teleport(vec2fp at)
	{
		teleps.emplace_back().at = at;
	}
	void factory_down(bool is_last)
	{
		ui_message(is_last? "All factories are down" : "Factory down");
	}
};
GameModeCtr* GameModeCtr::create() {
	return new GameModeCtr_Impl;
}



class GameModeCtr_Tutorial : public GameModeCtr
{
public:
	GameCore* core;
	State state = State::Final;
	bool first_message = false;
	
	void init(GameCore& core) {this->core = &core;}
	void step() {
		if (!first_message) {
			auto ent = core->get_pmg().get_ent();
			auto p = core->get_pmg().get_pui();
			if (ent && p) {
				first_message = true;
				p->message("Training simulation started");
				p->objective = "activate control terminal";
			}
		}
	}
	
	State get_state() {return state;}
	TimeSpan get_boot_left() {return {};}
	float get_boot_level() {return 0;}
	
	void inc_objective() {}
	void on_teleport_activation() {}
	void terminal_use() {
		state = State::TutComplete;
	}
	void hacker_work() {}
	void add_teleport(vec2fp) {}
	void factory_down(bool) {}
};
GameModeCtr* GameModeCtr::create_tutorial() {
	return new GameModeCtr_Tutorial;
}
