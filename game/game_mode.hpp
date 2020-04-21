#ifndef GAME_MODE_HPP
#define GAME_MODE_HPP

#include "entity.hpp"

class GameModeCtr
{
public:
	enum class State
	{
		NoTokens,
		HasTokens,
		Booting,
		Cleanup,
		Final,
		
		Won,
		Lost,
		TutComplete
	};
	
	static GameModeCtr* create();
	static GameModeCtr* create_tutorial();
	virtual ~GameModeCtr() = default;
	
	virtual void init(GameCore& core) = 0;
	virtual void step() = 0;
	
	virtual State get_state() = 0;
	virtual TimeSpan get_boot_left() = 0;
	virtual float get_boot_level() = 0;
	
	virtual void inc_objective() = 0;
	virtual void on_teleport_activation() = 0;
	virtual void terminal_use() = 0;
	virtual void hacker_work() = 0;
	virtual void add_teleport(vec2fp at) = 0;
	virtual void factory_down(bool is_last) = 0;
};

#endif // GAME_MODE_HPP
