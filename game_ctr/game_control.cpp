#include <atomic>
#include <thread>
#include "client/presenter.hpp"
#include "client/replay.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "game/player_mgr.hpp"
#include "vaslib/vas_log.hpp"
#include "game_control.hpp"



#include "game/physics.hpp"

static void play(GameCore& core, ReplayEvent ev)
{
	std::visit([&](Replay_DebugTeleport& e) {
		core.get_pmg().ref_ent().ref_phobj().body.SetTransform( conv(e.target), 0 );
	}, ev);
}



class GameControl_Impl : public GameControl
{
public:
	// thread sync
	std::thread thr;
	std::atomic<bool> thr_term = false;
	std::mutex ren_lock;
	
	// game control
	std::atomic<bool> pause_on = true;
	
	std::mutex post_step_lock;
	PostStep post_step;
	
	std::mutex state_lock;
	CoreState cur_state = CS_Init{"Loading..."};
	
	// game
	std::unique_ptr<GamePresenter> pres;
	std::unique_ptr<GameCore> core;
	std::shared_ptr<PlayerController> pc_ctr;
	
	// playback
	std::unique_ptr<ReplayReader> replay_rd;
	std::unique_ptr<ReplayWriter> replay_wr;
	std::optional<float> speed_k;
	
	
	
	GameControl_Impl(std::unique_ptr<InitParams> pars)
	{
		auto lock = core_lock();
		
		replay_rd = std::move(pars->replay_rd);
		replay_wr = std::move(pars->replay_wr);
		pc_ctr = std::move(pars->pc_ctr);
		
		init(std::move(pars));
		VLOGI("Game initialized");
		
		thr = std::thread([this]{ thr_func(); });
	}
	~GameControl_Impl()
	{
		thr_term = true;
		if (thr.joinable())
			thr.join();
	}
	void init(std::unique_ptr<InitParams> p_pars)
	{
		TimeSpan t0 = TimeSpan::since_start();
		auto& pars = *p_pars;
		
		GameCore::InitParams gci;
		gci.lc.reset( LevelControl::create(*pars.lt) );
		core.reset( GameCore::create(std::move(gci)) );
		
		core->spawn_drop = true;
		core->get_random() = std::move(pars.rndg);
		core->get_pmg().set_ctr(pc_ctr);
		
		if (pars.init_presenter)
			pres.reset(GamePresenter::init({ core.get(), pars.lt.get() }));
		
		pars.spawner(*core, *pars.lt);
		core->get_lc().fin_init(*pars.lt);
		
		VLOGI("GameControl::init() finished in {:.3f} seconds", (TimeSpan::since_start() - t0).seconds());
		
		if (pars.fastforward_time.is_positive())
		{
			set_state(CS_Init{"Forwarding..."});
			TimeSpan t1 = TimeSpan::since_start();
	
			core->get_pmg().fastforward = true;
//			lvl->get_aps().set_forced_sync(true);
			
			while (core->get_step_time() < pars.fastforward_time)
			{
				core->step(t0);
				if (core->get_step_time() > pars.fastforward_fullworld)
					core->get_pmg().fastforward = false;
			}
			
			core->get_pmg().fastforward = false;
//			lvl->get_aps().set_forced_sync(false);
			
			VLOGI("GameControl::init() fastforwarded {:.3f} seconds in {:.3f}",
				 pars.fastforward_time.seconds(), (TimeSpan::since_start() - t1).seconds());
		}
	}
	void thr_func()
	try
	{
		while (!thr_term)
		{
			auto t0 = TimeSpan::since_start();
			std::optional<float> sleep_time_k;
			
			if (pause_on) {
				auto ctr_lock = pc_ctr->lock();
				pc_ctr->update();
			}
			else
			{
				std::unique_lock lock(ren_lock);
				auto ctr_lock = pc_ctr->lock();
				
				if (!replay_rd) pc_ctr->update();
				else {
					auto ret = replay_rd->update_server(*pc_ctr);
					if (auto r = std::get_if<ReplayReader::RET_OK>(&ret))
					{
						sleep_time_k = r->pb_speed;
						for (auto& e : r->evs)
						{
							play(*core, e);
							if (replay_wr)
								replay_wr->add_event(e);
						}
					}
					else if (std::holds_alternative<ReplayReader::RET_WAIT>(ret)) {
						sleep(core->step_len);
						continue;
					}
					else if (std::holds_alternative<ReplayReader::RET_END>(ret))
					{
						VLOGW("DEMO PLAYBACK FINISHED");
						set_state(CS_End{"Playback finished"});
						return;
					}
				}
				if (replay_wr)
					replay_wr->update_client(*pc_ctr);

				if (!sleep_time_k && speed_k) sleep_time_k = *speed_k;
				if (pres) pres->playback_hack = !!sleep_time_k;
				
				core->step(t0);
			}
			
			if (core->get_pmg().is_game_finished())
			{
				set_state(CS_End{});
				break;
			}
			set_state(CS_Run{pause_on});
			
			auto dt = TimeSpan::since_start() - t0;
			if (!pause_on) {
				std::unique_lock lock(post_step_lock);
				post_step(dt);
			}
			
			TimeSpan t_sleep = core->step_len;
			if (sleep_time_k) t_sleep *= *sleep_time_k;
			sleep(t_sleep - dt); // precise_sleep causes more stutter on Linux
		}
		
	} catch (std::exception& e) {
		thr_term = true;
		set_state(CS_End{e.what(), true});
	}
	void set_state(CoreState state) {
		std::unique_lock lock(state_lock);
		cur_state = std::move(state);
	}
	
	
	
	CoreState get_state() {
		std::unique_lock lock(state_lock);
		return cur_state;
	}
	void set_post_step(PostStep f) {
		std::unique_lock lock(post_step_lock);
		post_step = std::move(f);
	}
	std::unique_lock<std::mutex> core_lock() {
		return std::unique_lock(ren_lock);
	}
	GameCore& get_core() {
		return *core;
	}
	void set_pause(bool on) {
		pause_on = on;
	}
	void set_speed(std::optional<float> k) {
		speed_k = k;
	}
	ReplayReader* get_replay_reader() {
		return replay_rd.get();
	}
	ReplayWriter* get_replay_writer() {
		return replay_wr.get();
	}
};
GameControl* GameControl::create(std::unique_ptr<InitParams> pars) {
	return new GameControl_Impl(std::move(pars));
}
