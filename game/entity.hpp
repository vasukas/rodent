#ifndef ENTITY_HPP
#define ENTITY_HPP

#include <memory>
#include <optional>
#include <typeindex>
#include <unordered_map>
#include "utils/ev_signal.hpp"
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_types.hpp"

class Entity;
class GameCore;

/// Unique entity index, always non-zero for existing entity
typedef uint32_t EntityIndex;



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
	Entity* ent = nullptr;
	
	
	virtual const char *get_typename() const {return "Unknown";}
	virtual void step() {} // unused by most
	virtual ~EComp();
	
	void   reg(ECompType type) noexcept; ///< Adds component to list (safe)
	void unreg(ECompType type) noexcept; ///< Removes component from list (safe)
	
private:
	struct ComponentRegistration {ECompType type; size_t index;};
	std::vector<ComponentRegistration> _regs;
};



/// Game object
class Entity final
{
public:
	const EntityIndex index;
	std::string dbg_name = {};
	
	
	
	bool is_ok() const; ///< Returns true if entity is active
	void destroy(); ///< Deletes entity immediatly or at the end of the step. Index garanteed to be not used in next step

	Transform get_pos() const; ///< Returns center position
	Transform get_vel() const; ///< Returns velocity per second
	vec2fp get_norm_dir() const; ///< Returns normalized face direction
	float get_radius() const; ///< Returns approximate radius of object
	
	// Note: components are deleted in reverse order of addition
	
	void add(std::type_index type, EComp* c); ///< Adds new component (throws if already exists)
	void rem(std::type_index type) noexcept; ///< Destroys component if exists
	EComp* get(std::type_index type) const noexcept; ///< Returns component if exists or null
	EComp& getref(std::type_index type) const; ///< Returns component if exists or throws
	
	template<class T> T* add(T* c) noexcept {add(typeid(T), c); return c;} ///< Adds new component (throws if already exists)
	template<class T> void rem() noexcept {rem(typeid(T));} ///< Destroys component if it exists
	template<class T> T* get() const noexcept {return static_cast<T*>(get(typeid(T)));} ///< Returns component if exists or null
	template<class T> T& getref() const {return static_cast<T&>(getref(typeid(T)));} ///< Returns component if exists or throws
	
private:
	friend class GameCore_Impl;
	
	std::unordered_map<std::type_index, EComp*> cs;
	std::vector<std::unique_ptr<EComp>> cs_ord;
	bool was_destroyed = false;
	
	Entity(EntityIndex index) : index(index) {}
	~Entity();
};

#endif // ENTITY_HPP
