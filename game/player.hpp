#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "entity.hpp"
class PlayerController;

Entity* create_player(vec2fp pos, std::shared_ptr<PlayerController> ctr);

#endif // PLAYER_HPP
