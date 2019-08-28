#ifndef ENTITY_HPP
#define ENTITY_HPP

#include <memory>
#include <vector>
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_types.hpp"
#include "common_defs.hpp"

class Entity;
class GameCore;

/// Unique entity index, always non-zero for existing entity
typedef uint32_t EntityIndex;

/// Custom deleter (calls destroy)
struct EntityDeleter {void operator()(Entity*);};

/// Unique entity pointer
using EntityPtr = std::unique_ptr<Entity, EntityDeleter>;



/// Type of component list
enum class ECompType
{
	// step is called on these at each core step, in same order
	StepLogic,
	StepPostUtil,
	
	TOTAL_COUNT ///< Do not use
};



/// Entity component
struct EComp
{
	Entity* ent;
	
	
	virtual void step() {} // unused by most
	virtual ~EComp(); ///< Removes component from all lists
	
	void   reg(ECompType type) noexcept; ///< Adds component to list (safe)
	void unreg(ECompType type) noexcept; ///< Removes component from list (safe)
	
protected:
	EComp(Entity* ent): ent(ent) {}
	
private:
	struct ComponentRegistration {ECompType type; size_t index;};
	std::vector<ComponentRegistration> _regs;
};



/// Entity component with physics world properties
struct ECompPhysics : EComp
{
	ECompPhysics(Entity* ent): EComp(ent) {}
	virtual ~ECompPhysics() = default;
	virtual Transform get_trans() const = 0; 
	virtual vec2fp    get_pos() const {return get_trans().pos;}
	virtual Transform get_vel() const {return {};}
	virtual float get_radius() const = 0; ///< Returns approximate radius of object
	vec2fp get_norm_dir() const; ///< Returns normalized face direction
};



struct ECompRender;
struct EC_Equipment;
struct EC_Health;
struct EC_Physics;



/// Game object
class Entity
{
public:
	const EntityIndex index;
	std::string dbg_name = {};
	
	
	virtual EC_Physics&   get_phobj(); ///< Throws if wrong type
	virtual ECompPhysics& get_phy(); ///< Throws if doesn't exist
	virtual ECompRender*  get_ren() {return nullptr;}
	virtual EC_Health*    get_hlc() {return nullptr;}
	virtual EC_Equipment* get_eqp() {return nullptr;}
	
	virtual size_t get_team() const {return TEAM_ENVIRON;}
	
	
	/// Deletes entity immediatly or at the end of the step. Index garanteed to be not used in next step
	void destroy();
	
	/// Returns true if entity is not destroyed
	bool is_ok() const;
	
	/// Returns ID string
	std::string dbg_id() const;
	
protected:
	Entity();
	virtual ~Entity() = default;
	
private:
	bool was_destroyed = false;
	friend class GameCore_Impl;
};

#endif // ENTITY_HPP
