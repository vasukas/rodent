#ifndef GAME_CORE_HPP
#define GAME_CORE_HPP

#include "vaslib/vas_time.hpp"
#include "entity.hpp"

class PhysicsWorld;



/// Game entities handler
class GameCore
{
public:
	struct InitParams
	{
		uint32_t random_seed = 0;
	};
	
	const TimeSpan step_len = TimeSpan::seconds(1./30);
	
	static GameCore& get(); ///< Returns singleton
	static GameCore* create( const InitParams& pars ); ///< Creates empty handler and inits all systems
	virtual ~GameCore(); ///< Destroys all systems
	
	
	///
	virtual PhysicsWorld& get_phy() noexcept = 0;
	
	
	/// Returns index number of the next step (starting with 1)
	virtual uint32_t get_step_counter() const noexcept = 0;
	
	/// Returns true if step currently executed (for functions called inside it)
	virtual bool is_in_step() const noexcept = 0;
	
	/// Performs single step on all systems and increments step counter
	virtual void step() = 0;
	
	
	/// Creates new entity. Never fails
	virtual Entity* create_ent() noexcept = 0;
	
	/// Returns entity with such ID or nullptr
	virtual Entity* get_ent( EntityIndex uid ) const noexcept = 0;
	
	
	/// Returns pseudorandom value, advancing generator
	virtual uint32_t get_random() noexcept = 0;
	
	
protected:
	friend class Entity;
	virtual void mark_deleted( Entity* e ) noexcept = 0;
};

#endif // GAME_CORE_HPP
