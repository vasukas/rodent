#ifndef LOGIC_HPP
#define LOGIC_HPP

#include "entity.hpp"



/// Internal behaviour
struct EC_Logic
{
	struct ContactEvent
	{
		enum Type {
			T_BEGIN,  ///< Beginning of contact
			T_END,    ///< End of contact
			T_RESOLVE ///< Collision resolution
		};
		Type type;
		
		Entity* other; ///< Another entity
		void* fix_this; ///< Fixture usedata for this entity
		void* fix_other; ///< Fixture userdata for another entity
		vec2fp point; ///< Averaged world point of impact
		float imp; ///< Resolution impulse (set only for T_RESOLVE)
	};
	
	Entity* ent = nullptr;
	
	virtual ~EC_Logic() = default;
	virtual void step() = 0; ///< Called at each game step
	
	/// Collision event callback
	virtual void on_event(const ContactEvent& ev) {(void) ev;}
};

#endif // LOGIC_HPP
