#ifndef PLAYER_UI_HPP
#define PLAYER_UI_HPP

#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"

class  PlayerManager;
class  EInteractive;
struct WeaponMsgReport;


class PlayerUI
{
public:
	bool full_info = false;
	bool debug_mode = false;
	
	static PlayerUI* create();
	virtual ~PlayerUI() = default;
	
	virtual void message(std::string s, TimeSpan show, TimeSpan fade = TimeSpan::seconds(1.5)) = 0;
	
protected:
	friend class PlayerManager_Impl;
	
	struct DrawState
	{
		TimeSpan resp_left;
		std::string objective;
		std::string lookat;
		EInteractive* einter = {};
	};
	
	/// May not be called each cycle
	virtual void render(PlayerManager& mgr, const DrawState& dst, TimeSpan passed, vec2i cursor_pos) = 0;

	virtual WeaponMsgReport& get_wpnrep() = 0;
};

#endif // PLAYER_UI_HPP
