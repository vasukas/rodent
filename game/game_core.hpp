#ifndef GAME_CORE_HPP
#define GAME_CORE_HPP

#include "vaslib/vas_time.hpp"
#include "entity.hpp"

struct RandomGen;
class  PhysicsWorld;

#define GAME_THROW LOG_THROW_X

#define GAME_DEBUG VLOGV



/// Game entities handler
class GameCore
{
public:
	struct InitParams
	{
		uint32_t random_seed = 0;
	};
	
	static GameCore& get(); ///< Returns singleton
	static GameCore* create( const InitParams& pars ); ///< Creates empty handler and inits all systems
	virtual ~GameCore(); ///< Destroys all systems
	
	
	
	/// Logic step length
	inline static const TimeSpan step_len = TimeSpan::fps(30);
	
	/// For 'per second' -> 'per step' conversions
	inline static const float time_mul = step_len.seconds();
	
	///
	virtual PhysicsWorld& get_phy() noexcept = 0;
	
	/// Returns generator
	virtual RandomGen& get_random() noexcept = 0;
	
	
	
	/// Returns index number of the next step (starting with 1)
	virtual uint32_t get_step_counter() const noexcept = 0;
	
	/// Returns true if step currently executed (for functions called inside it)
	virtual bool is_in_step() const noexcept = 0;
	
	/// Performs single step on all systems and increments step counter
	virtual void step() = 0;
	
	
	
	/// Returns entity with such ID or nullptr
	virtual Entity* get_ent(EntityIndex uid) const noexcept = 0;
	
	/// Returns entity if it exists, or returns nullptr and sets uid to 0
	virtual Entity* valid_ent(EntityIndex& uid) const noexcept = 0;
	
	
protected:
	friend Entity;
	virtual EntityIndex create_ent(Entity* e) noexcept = 0;
	virtual void mark_deleted(Entity* e) noexcept = 0;
	
	friend EComp;
	virtual size_t reg_c(ECompType type, EComp* c) noexcept = 0;
	virtual void unreg_c(ECompType type, size_t i) noexcept = 0;
};

#endif // GAME_CORE_HPP
