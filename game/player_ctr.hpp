#ifndef PLAYER_CTR_HPP
#define PLAYER_CTR_HPP

#include "entity.hpp"

union SDL_Event;

struct PlayerControl : EComp
{
	static Entity* create(GameCore& core, vec2fp pos); ///< Creates player entity
	virtual void on_event(const SDL_Event& ev) = 0; ///< Asynchronously processes event
	virtual void draw_hud() = 0;
	virtual void draw_ui() = 0;
};

#endif // PLAYER_CTR_HPP
