#ifndef GAME_CORE_HPP
#define GAME_CORE_HPP

#include "entity.hpp"

class  AI_Controller;
class  GameInfoList;
class  GameModeCtr;
class  LevelControl;
class  PhysicsWorld;
class  PlayerManager;
struct RandomGen;



/// Game entities handler
class GameCore
{
public:
	struct InitParams
	{
		std::string random_init;
		std::unique_ptr<LevelControl> lc;
		std::unique_ptr<GameModeCtr> gmc;
	};
	
	bool dbg_break_now = false;
	bool dbg_ai_attack; ///< Is AI attack enabled
	bool dbg_ai_see_plr; ///< Is AI seeing player
	
	bool spawn_drop; ///< From destroyed enemies
	bool spawn_hunters;
	
	static GameCore* create(InitParams pars); ///< Creates empty handler and inits all systems
	virtual ~GameCore() = default; ///< Destroys all systems
	
	
	
	/// Logic step length
	static constexpr TimeSpan step_len = TimeSpan::fps(30);
	
	/// For 'per second' -> 'per step' conversions
	static constexpr float time_mul = step_len.seconds();
	
	virtual AI_Controller& get_aic() noexcept = 0;
	virtual GameInfoList&  get_info()noexcept = 0;
	virtual GameModeCtr&   get_gmc() noexcept = 0;
	virtual LevelControl&  get_lc()  noexcept = 0;
	virtual PhysicsWorld&  get_phy() noexcept = 0;
	virtual PlayerManager& get_pmg() noexcept = 0;
	virtual RandomGen&     get_random() noexcept = 0;
	
	
	
	/// Returns index number of the next step (starting with 1)
	virtual uint32_t get_step_counter() const noexcept = 0;
	
	/// Returns time since start
	virtual TimeSpan get_step_time() const noexcept = 0;
	
	/// Returns true if step currently executed (for functions called inside it)
	virtual bool is_in_step() const noexcept = 0;
	
	/// Returns true if currently being destroyed
	virtual bool is_freeing() const noexcept = 0;
	
	/// Performs single step on all systems and increments step counter
	virtual void step(TimeSpan now) = 0;
	
	
	
	/// Returns entity with such ID or nullptr. 
	/// WARNING: does NOT return destroyed entity, even if it's still exists
	virtual Entity* get_ent(EntityIndex ei) const noexcept = 0;
	
	/// Returns entity if it exists, or returns nullptr and sets uid to 0
	virtual Entity* valid_ent(EntityIndex& ei) const noexcept = 0;
	
	/// Returns entity or throws if it doesn't exist
	virtual Entity& ent_ref(EntityIndex ei) const = 0;
	
	/// Calls function for each existing entity
	virtual void foreach(callable_ref<void(Entity&)> f) = 0;
	
	
	
protected:
	friend Entity;
	virtual EntityIndex on_ent_create(Entity* e) noexcept = 0;
	virtual void on_ent_destroy(Entity* e) noexcept = 0;
	
	virtual size_t reg_ent(Entity* e) noexcept = 0;
	virtual void unreg_ent(size_t i)  noexcept = 0;
	
	friend EComp;
	virtual size_t reg_c(ECompType type, EComp* c) noexcept = 0;
	virtual void unreg_c(ECompType type, size_t i) noexcept = 0;
};

#endif // GAME_CORE_HPP
