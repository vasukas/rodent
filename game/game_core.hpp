#ifndef GAME_CORE_HPP
#define GAME_CORE_HPP

#include "vaslib/vas_time.hpp"
#include "entity.hpp"

class PhysicsWorld;

#define GAME_THROW LOG_THROW_X



/// Game entities handler
class GameCore
{
public:
	struct InitParams
	{
		uint32_t random_seed = 0;
	};
	
	static const TimeSpan step_len;
	
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
	
	/// Returns list of components of such type. May contain nullptrs
	virtual std::vector<EComp*>& get_comp_list(ECompType type) noexcept = 0;
	
	
	
	/// Returns pseudorandom value, advancing generator
	virtual uint32_t get_random() noexcept = 0;
	
protected:
	friend Entity;
	virtual void mark_deleted( Entity* e ) noexcept = 0;
	
	friend EComp;
	virtual size_t reg_c(ECompType type, EComp* c) noexcept = 0;
	virtual void unreg_c(ECompType type, size_t i) noexcept = 0;
};

#endif // GAME_CORE_HPP
