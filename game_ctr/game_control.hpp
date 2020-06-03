#ifndef GAME_CONTROL_HPP
#define GAME_CONTROL_HPP

#include <mutex>
#include <variant>
#include "game/entity.hpp"
#include "game/game_mode.hpp"
#include "utils/noise.hpp"

struct LevelTerrain;
class  PlayerInput;
class  ReplayReader;
class  ReplayWriter;



class GameControl
{
public:
	struct ignore_exception : public std::runtime_error {
		ignore_exception(): std::runtime_error("<ignored>") {}
	};
	
	struct InitParams
	{
		RandomGen rndg;
		std::function<LevelTerrain*(RandomGen&)> terrgen;
		std::function<void(GameCore&, LevelTerrain&)> spawner;
		std::unique_ptr<GameModeCtr> mode_ctr;
		
		TimeSpan fastforward_time = TimeSpan::seconds(10); // total
		TimeSpan fastforward_fullworld = TimeSpan::seconds(5); // how long full world is simulated
		bool init_presenter = true;
		
		// Note: init data must be already written/read
		std::unique_ptr<ReplayReader> replay_rd;
		std::unique_ptr<ReplayWriter> replay_wr;
		std::function<bool()> loadgame_error_h;
		
		//
		bool disable_drop = false;
		bool disable_hunters = false;
		bool is_loadgame = false;
	};
	
	/// Creates new thread and initializes there. Initially is paused
	static GameControl* create(std::unique_ptr<InitParams> pars);
	virtual ~GameControl() = default;
	
	
	
	/// Core cannot be locked in this state
	struct CS_Init {
		std::string stage;
	};
	
	/// 
	struct CS_Run {
		bool paused;
	};
	
	/// Core lock not required during this stage, core thread is already terminated
	struct CS_End {
		std::string err_msg; ///< Exception description, if not empty
		GameModeCtr::FinalState fs;
	};
	
	using CoreState = std::variant<CS_Init, CS_Run, CS_End>;
	virtual CoreState get_state() = 0;
	
	
	
	using PostStep = std::function<void(TimeSpan)>; ///< Receives actual execution time
	virtual void set_post_step(PostStep f) = 0; ///< Called after logic step, if set and not paused
	
	/// Pointer is valid from finishing init to the end of object lifetime
	virtual const LevelTerrain* get_terrain() = 0;
	
	
	
	/// Required for all functions below
	[[nodiscard]] virtual std::unique_lock<std::mutex> core_lock() = 0;
	
	virtual GameCore& get_core() = 0;
	
	virtual void set_pause(bool on) = 0;
	virtual void set_speed(std::optional<float> k) = 0; ///< Multiplies sleep time
	virtual void step_paused(int steps) = 0; ///< Allows that many steps before pausing
	
	virtual ReplayReader* get_replay_reader() = 0;
	virtual ReplayWriter* get_replay_writer() = 0;
};

#endif // GAME_CONTROL_HPP
