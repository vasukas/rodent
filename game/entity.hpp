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

enum class GameStepOrder
{
	Logic,
	Move,
	TOTAL_COUNT // do not use
};



/// Entity component
struct EComp
{
	Entity* ent = nullptr;
	
	
	virtual const char *get_typename() const noexcept {return "Undefined";}
	virtual void step() {} // unused by most
	virtual ~EComp();
	
	void reg_step(GameStepOrder order); ///< Registers component so step() function will be called
	
private:
	size_t reg_step_id = size_t_inval;
};



/// Game object
class Entity final
{
public:
	const EntityIndex index;
	GameCore& core;
	
	std::string dbg_name = {};
	
	
	
	void destroy(); ///< Deletes entity immediatly or at the end of the step. Index garanteed to be not used in next step

	Transform get_pos() const; ///< Returns center position
	Transform get_vel() const; ///< Returns velocity per second
	vec2fp get_norm_dir() const; ///< Returns normalized face direction
	float get_radius() const; ///< Returns approximate radius of object
	
	void add(std::type_index type, EComp* c); ///< Adds new component (throws if already exists)
	void rem(std::type_index type) noexcept; ///< Removes component if exists
	EComp* get(std::type_index type) const noexcept; ///< Returns component if exists or null
	EComp& getref(std::type_index type) const; ///< Returns component if exists  or throws
	
	template<class T> void add(T* c) noexcept {add(std::type_index(typeid(T)), c);} ///< Adds new component (throws if already exists)
	template<class T> void rem() noexcept {rem(std::type_index(typeid(T)));} ///< Removes component if exists
	template<class T> T* get() const noexcept {return static_cast<T*>(get(std::type_index(typeid(T))));} ///< Returns component if exists or null
	template<class T> T& getref() const {return static_cast<T&>(getref(std::type_index(typeid(T))));} ///< Returns component if exists  or throws
	
private:
	friend class GameCore_Impl;
	
	std::unordered_map<std::type_index, EComp*> cs;
	std::vector<std::unique_ptr<EComp>> cs_ord;
	
	Entity(GameCore&, EntityIndex);
	~Entity();
};

#endif // ENTITY_HPP
