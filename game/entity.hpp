#ifndef ENTITY_HPP
#define ENTITY_HPP

#include <memory>
#include <vector>
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_types.hpp"
#include "common_defs.hpp"

class Entity;
class GameCore;

class AI_Drone;
struct ECompRender;
struct EC_Equipment;
struct EC_Health;
struct EC_Physics;

/// Custom deleter (calls destroy)
struct EntityDeleter {void operator()(Entity*);};

/// Unique entity pointer
using EntityPtr = std::unique_ptr<Entity, EntityDeleter>;



/// Type of component list
enum class ECompType
{
	// step is called on these at each core step, in same order
	StepPreUtil,
	StepLogic,
	StepPostUtil,
	
	TOTAL_COUNT ///< Do not use
};

const char *enum_name(ECompType type);



/// Entity component
struct EComp
{
	Entity* ent;
	
	
	EComp(const EComp&) = delete;
	virtual void step() {} // unused by some
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



struct EntityIndex
{
	EntityIndex() = default;
	
	bool operator ==(const EntityIndex& ei) const {return i == ei.i;}
	bool operator !=(const EntityIndex& ei) const {return i != ei.i;}
	operator bool() const {return i != std::numeric_limits<uint32_t>::max();}
	
	static EntityIndex from_int(uint32_t i) {EntityIndex ei; ei.i = i; return ei;}
	uint32_t to_int() const {return i;}
	
private:
	uint32_t i = std::numeric_limits<uint32_t>::max();
};



/// Game object
class Entity
{
public:
	// WARN: must be created on heap
	
	const EntityIndex index;
	
	
	virtual EC_Physics&   get_phobj(); ///< Throws if wrong type
	virtual ECompPhysics& get_phy();   ///< Throws if doesn't exist
	virtual ECompRender*  get_ren() {return nullptr;}
	virtual EC_Health*    get_hlc() {return nullptr;}
	virtual EC_Equipment* get_eqp() {return nullptr;}
	
	virtual size_t get_team() const {return TEAM_ENVIRON;}
	virtual AI_Drone* get_ai_drone() {return nullptr;}
	
	virtual std::string ui_descr() const {return {};}
	virtual float get_face_rot() {return get_phy().get_trans().rot;}
	vec2fp get_pos() {return get_phy().get_pos();}

	
	/// Deletes entity immediatly or at the end of the step. Index garanteed to be not used in next step
	void destroy();
	
	/// Returns true if entity is not destroyed
	bool is_ok() const;
	
	/// Returns ID string
	std::string dbg_id() const;
	
	
	/// Called only if in step list (just after ECompType::StepLogic) 
	virtual void step() {}
	
	void   reg_this() noexcept; ///< Adds entity to step list (safe)
	void unreg_this() noexcept; ///< Removes entity from step list (safe)
	
protected:
	Entity();
	Entity(const Entity&) = delete;
	virtual ~Entity();
	
private:
	bool was_destroyed = false;
	std::optional<size_t> reglist_index;
	friend class GameCore_Impl;
};

#endif // ENTITY_HPP
