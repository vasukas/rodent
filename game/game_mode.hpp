#ifndef GAME_MODE_HPP
#define GAME_MODE_HPP

#include "entity.hpp"



class GameModeCtr
{
public:
	struct FinalState {
		enum Type {
			Won,
			Lost,
			End // fades screen
		};
		Type type;
		std::string msg;
	};
	
	virtual ~GameModeCtr() = default;
	
	virtual void init(GameCore& core) = 0;
	virtual void step() = 0;
	virtual std::optional<FinalState> get_final_state() = 0;
};



class GameMode_Normal : public GameModeCtr
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
		Lost
	};
	
	bool fastboot = false;
	
	static GameMode_Normal* create();
	
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



class GameMode_Tutorial : public GameModeCtr
{
public:
	GameMode_Tutorial() = default;
	
	void init(GameCore& core) override;
	void step() override;
	std::optional<FinalState> get_final_state() override;
	
	void terminal_use();
	
private:
	GameCore* core;
	int state = 0;
};



class GameMode_Survival : public GameModeCtr
{
public:
	struct State {
		int current_wave; // 0 if not started
		TimeSpan tmo; // till next
		int alive; // all drones
		TimeSpan total; // total time
	};
	
	static GameMode_Survival* create();
	virtual State get_state() = 0;
	virtual void add_teleport(vec2fp at) = 0;
};

#endif // GAME_MODE_HPP
