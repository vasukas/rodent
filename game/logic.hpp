#ifndef LOGIC_HPP
#define LOGIC_HPP

#include "entity.hpp"



/// Internal behaviour
struct EC_Logic
{
	Entity* ent = nullptr;
	
	virtual ~EC_Logic() = default;
	virtual void step() = 0; ///< Called at each game step
};

#endif // LOGIC_HPP
