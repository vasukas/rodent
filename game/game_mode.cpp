#include <queue>
#include "client/level_map.hpp"
#include "client/player_ui.hpp"
#include "client/presenter.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "game/physics.hpp"
#include "game/player_mgr.hpp"
#include "game_objects/objs_creature.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_log.hpp"
#include "game_mode.hpp"



class GameMode_Normal_Impl : public GameMode_Normal
{
public:
	static constexpr size_t tokens_needed = 3;
	static constexpr TimeSpan boot_length = TimeSpan::seconds(90);
	static constexpr float level_decr = 0.03; // per second
	static constexpr float level_incr = 0.025; // per second
	static constexpr size_t max_bots = 22; // including bosses and hackers
	
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
	
	GameMode_Normal_Impl()
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
	void init(GameCore& p_core) override
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
	void step() override
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
				auto room = core->get_lc().get_room(ent->get_pos());
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
						SoundEngine::once(SND_OBJ_TELEPORT, ent->get_pos());
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
	std::optional<FinalState> get_final_state() override
	{
		if		(state == State::Won) {
			return FinalState{FinalState::Won, "Game completed.\n\nA WINRAR IS YOU."};
		}
		else if (state == State::Lost) {
			return FinalState{FinalState::Lost, "Terminal destroyed.\n\nYOU LOST."};
		}
		return {};
	}
	
	State    get_state()      override {return state;}
	TimeSpan get_boot_left()  override {return boot_at - core->get_step_time();}
	float    get_boot_level() override {return boot_level;}
	
	void inc_objective() override
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
	void on_teleport_activation() override
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
	void terminal_use() override
	{
		if (state == State::HasTokens) {
			state = State::Booting;
			boot_at = core->get_step_time() + boot_length;
			if (fastboot) boot_at = core->get_step_time() + TimeSpan::seconds(2);
			ui_message("Protect control terminal");
		}
		else if (state == State::Final) {
			state = State::Won;
		}
	}
	void hacker_work() override
	{
		boot_level -= level_decr * core->time_mul;
		hacker_working = true;
	}
	void add_teleport(vec2fp at) override
	{
		teleps.emplace_back().at = at;
	}
	void factory_down(bool is_last) override
	{
		ui_message(is_last? "All factories are down" : "Factory is down");
		if (is_last) core->spawn_hunters = false;
	}
};
GameMode_Normal* GameMode_Normal::create() {
	return new GameMode_Normal_Impl;
}



void GameMode_Tutorial::init(GameCore& p_core)
{
	core = &p_core;
}
void GameMode_Tutorial::step()
{
	if (!state) {
		auto ent = core->get_pmg().get_ent();
		auto p = core->get_pmg().get_pui();
		if (ent && p) {
			state = 1;
			p->message("Training simulation started");
			p->objective = "activate control terminal";
		}
	}
}
std::optional<GameModeCtr::FinalState> GameMode_Tutorial::get_final_state()
{
	if (state == 2) return FinalState{FinalState::End, "Training session completed"};
	return {};
}
void GameMode_Tutorial::terminal_use()
{
	state = 2;
}



class GameMode_Survival_Impl : public GameMode_Survival
{
public:
	GameCore* core;
	bool was_alive = false;
	bool is_dead = false;
	TimeSpan total;
	
	TimeSpan wave_tmo = TimeSpan::seconds(10);
	int wave = 0;
	int wave_alive = 0;
	
	std::vector<vec2fp> teleps;
	
	
	
	void init(GameCore& p_core) override
	{
		core = &p_core;
	}
	void step() override
	{
		if (!core->get_pmg().get_ent()) {
			if (was_alive) is_dead = true;
		}
		else if (!was_alive) {
			was_alive = true;
			
			auto p = core->get_pmg().get_pui();
			if (p) {
				p->message("Survive for as long\nas possible");
				p->objective = "survive!";
			}
		}
		
		total += core->step_len;
		wave_tmo -= core->step_len;
		if (!wave_tmo.is_positive())
		{
			wave_tmo = TimeSpan::seconds(std::min(15 + 10 * wave, 90));
			int n_spawn = std::min(6 + wave * 3, 25);
			++wave;
			
			core->get_random().shuffle(teleps);
			vec2fp plr_pos = core->get_pmg().ref_ent().get_pos();
			auto tel_end = std::partition(teleps.begin(), teleps.end(),
			                              [&](auto& t) {return t.dist_squ(plr_pos) > std::pow(10, 2);});
			n_spawn = std::min<int>(n_spawn, std::distance(teleps.begin(), tel_end));
			
			auto eff = [&](Entity* ent){
				GamePresenter::get()->effect(FE_SPAWN, {{ent->ref_pc().get_trans()}, GameConst::hsz_drone_big});
				auto& d = ent->ref_ai_drone();
				d.always_online = true;
				d.replace_state(AI_Drone::Idle{AI_Drone::IdleChasePlayer{}});
				d.set_battle_state();
			};
			
			auto cs = normalize_chances<std::function<void(vec2fp)>, 4>({{
				{[&](vec2fp p) {eff(new EEnemyDrone(*core, p, EEnemyDrone::def_workr(*core)));},	10},
				{[&](vec2fp p) {eff(new EEnemyDrone(*core, p, EEnemyDrone::def_drone(*core)));},	80},
				{[&](vec2fp p) {
					if (wave > 2) eff(new EEnemyDrone(*core, p, EEnemyDrone::def_campr(*core)));
					else eff(new EEnemyDrone(*core, p, EEnemyDrone::def_workr(*core)));},			12},
				{[&](vec2fp p) {
					if (wave > 4) eff(new EHunter(*core, p));
					else eff(new EEnemyDrone(*core, p, EEnemyDrone::def_workr(*core)));},			8}
			}});
			for (int i=0; i<n_spawn; ++i)
				core->get_random().random_chance(cs)(teleps[i]);
		}
		
		wave_alive = 0;
		core->foreach([&](auto& e)
		{
			if (AI_Drone* d = e.get_ai_drone())
			{
				++wave_alive;
				
				if (auto i = std::get_if<AI_Drone::Idle>(&d->get_state())) {
					std::get<AI_Drone::IdleChasePlayer>(i->ist).after = {};
				}
				else if (auto st = std::get_if<AI_Drone::Search>(&d->get_state());
				         st && st->at != 0)
				{
					d->set_idle_state();
					auto& i = std::get<AI_Drone::Idle>(d->get_state());
					std::get<AI_Drone::IdleChasePlayer>(i.ist).after = {};
				}
			}
		});
	}
	std::optional<FinalState> get_final_state() override
	{
		if (is_dead) {
			int ms = total.ms();
			return FinalState{FinalState::End, FMT_FORMAT("You survived through {} waves.\nTotal time: {}:{:02}:{:02}.{:03}.",
				                                          wave, ms/3600'000, (ms/60'000)%60, (ms/1000)%60, ms%1000)};
		}
		return {};
	}
	State get_state() override
	{
		return {wave, wave_tmo, wave_alive, total};
	}
	void add_teleport(vec2fp at) override
	{
		teleps.push_back(at);
	}
};
GameMode_Survival* GameMode_Survival::create() {
	return new GameMode_Survival_Impl;
}
