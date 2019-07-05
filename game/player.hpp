#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "entity.hpp"

class PlayerControl;



struct PlayerLogic : EComp
{
	static Entity* create(GameCore& core, vec2fp pos, std::shared_ptr<PlayerControl> ctr); ///< Creates player entity
	virtual void draw_hud() = 0;
	virtual void draw_ui() = 0;
};

#endif // PLAYER_HPP
