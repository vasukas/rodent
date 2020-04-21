#ifndef ENTITY_HPP
#define ENTITY_HPP

#include <memory>
#include <unordered_map>
#include <typeindex>
#include "common_defs.hpp"

class Entity;
class GameCore;

class  AI_Drone;
struct EC_Equipment;
struct EC_Health;
struct EC_Physics;
struct EC_Position;



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
	Entity& ent;
	
	EComp(const EComp&) = delete;
	virtual ~EComp(); ///< Removes component from all lists
	
	// Note: only one list allowed for now
	void   reg(ECompType type); ///< Adds component to list (safe)
	void unreg(ECompType type); ///< Removes component from list (safe)
	
protected:
	friend class GameCore_Impl;
	EComp(Entity& ent): ent(ent) {}
	
	/// Called each step if registered in one of Step* lists
	virtual void step() {}
	
private:
	struct ComponentRegistration {ECompType type; size_t index;};
	std::optional<ComponentRegistration> _reg;
};

struct EDynComp : EComp {
protected:
	EDynComp(Entity& ent): EComp(ent) {}
};



struct EC_Position : EComp
{
	std::optional<float> rot_override; ///< Rotation override
	
	EC_Position(Entity& ent): EComp(ent) {}
	
	virtual vec2fp    get_pos()    const = 0;
	virtual vec2fp    get_vel()    const = 0; ///< Per second
	virtual float     get_radius() const = 0; ///< Approximate size
	virtual float get_real_angle() const = 0;
	
	Transform get_trans() const {return Transform{get_pos(), get_angle()};}
	float     get_angle() const {return rot_override ? *rot_override : get_real_angle();}
	vec2fp get_norm_dir() const {return vec2fp(1,0).rotate(get_angle());} ///< Normalized face direction
};



struct EntityIndex
{
	using Int = uint32_t;
	
	EntityIndex() = default;
	
	bool operator ==(const EntityIndex& ei) const {return i == ei.i;}
	bool operator !=(const EntityIndex& ei) const {return i != ei.i;}
	explicit operator bool() const {return i != std::numeric_limits<Int>::max();}
	
	[[nodiscard]] static EntityIndex from_int(Int i) {EntityIndex ei; ei.i = i; return ei;}
	Int to_int() const {return i;}
	
private:
	Int i = std::numeric_limits<Int>::max();
};



/// Game object
class Entity
{
public:
	GameCore& core;
	const EntityIndex index;
	const char* ui_descr = nullptr;
	
	
	
	virtual EC_Position&  ref_pc() = 0;
	virtual AI_Drone*     get_ai_drone() {return nullptr;}
	virtual EC_Equipment* get_eqp() {return nullptr;}
	virtual EC_Health*    get_hlc() {return nullptr;}
	
	AI_Drone&     ref_ai_drone();
	EC_Equipment& ref_eqp();
	EC_Health&    ref_hlc();
	EC_Physics&   ref_phobj();
	
	virtual size_t get_team() const {return TEAM_ENVIRON;}
	vec2fp get_pos() {return ref_pc().get_pos();}
	
	virtual bool is_creature() {return get_eqp();}

	
	
	/// Returns ID string
	std::string dbg_id() const;
	
	/// Deletes entity immediatly or at the end of the step. Index won't be used in next step
	void destroy();
	
	/// Returns true if wasn't deleted
	bool is_ok() const {return !was_destroyed;}
	
	///
	bool dbg_is_reg() const {return !!reglist_index;}
	
	
	
	// Dynamic components
	
	template <typename T, std::enable_if_t<std::is_base_of_v<EDynComp, T>, int> = 0>
	bool has() const noexcept {
		return dyn_comps.find(typeid(T)) != dyn_comps.end();
	}
	
	template <typename T, std::enable_if_t<std::is_base_of_v<EDynComp, T>, int> = 0>
	T& ref() {
		return const_cast<T&>(const_cast<const Entity&>(*this).ref<T>());
	}
	
	template <typename T, std::enable_if_t<std::is_base_of_v<EDynComp, T>, int> = 0>
	const T& ref() const {
		if (auto t = get<T>()) return *t;
		throw_type_error("ref", typeid(T));
	}
	
	template <typename T, std::enable_if_t<std::is_base_of_v<EDynComp, T>, int> = 0>
	T* get() noexcept {
		return const_cast<T*>(const_cast<const Entity&>(*this).get<T>());
	}
	
	template <typename T, std::enable_if_t<std::is_base_of_v<EDynComp, T>, int> = 0>
	const T* get() const noexcept {
		auto it = dyn_comps.find(typeid(T));
		return it != dyn_comps.end() ? static_cast<const T*>(it->second.c) : nullptr;
	}
	
	template <typename T, std::enable_if_t<std::is_base_of_v<EDynComp, T>, int> = 0>
	T& add(T* c) {
		if (has<T>()) throw_type_error("add", typeid(T));
		dyn_comps.emplace(typeid(T), DynComp{c, n_dyn_comps()});
	//	on_add_comp(*c);
		return *c;
	}
	
	template <typename T, typename... Args, std::enable_if_t<std::is_base_of_v<EDynComp, T>, int> = 0>
	T& add_new(Args&&... args) {
		return add(new T(*this, std::forward<Args>(args)...));
	}
	
	template <typename T, std::enable_if_t<std::is_base_of_v<EDynComp, T>, int> = 0>
	T& ensure() noexcept {
		if (auto p = get<T>()) return *p;
		return add_new<T>();
	}
	
	template <typename T, std::enable_if_t<std::is_base_of_v<EDynComp, T>, int> = 0>
	void remove() noexcept {
		auto it = dyn_comps.find(typeid(T));
		if (it != dyn_comps.end()) {
			int ord = it->second.order;
			
		//	on_rem_comp(*it->second.c);
			dyn_comps.erase(it);
			
			for (auto& c : dyn_comps)
				if (c.second.order > ord)
					--c.second.order;
		}
	}
	
	int n_dyn_comps() const noexcept {
		return static_cast<int>(dyn_comps.size());
	}
	
	void dyn_foreach(callable_ref<void(EComp&)> f) {
		for (auto& c : dyn_comps) f(*c.second.c);
	}
	
protected:
	Entity(GameCore& core);
	Entity(const Entity&) = delete;
	virtual ~Entity();
	
	/// Called only if registered (after ECompType::StepLogic) 
	virtual void step() {}
	
	void   reg_this() noexcept; ///< Adds entity to step list (safe)
	void unreg_this() noexcept; ///< Removes entity from step list (safe)
	
private:
	struct DynComp {
		EComp* c;
		int order;
		DynComp(DynComp&& v) noexcept : c(v.c), order(v.order) {v.c = {};}
		DynComp(EComp* c, int order) noexcept : c(c), order(order) {}
		void operator=(DynComp&& v) noexcept {
			if (c) delete c;
			c = v.c; v.c = {};
			order = v.order;
		}
		~DynComp() {if (c) delete c;}
	};
	std::unordered_map<std::type_index, DynComp> dyn_comps;
	
	friend class GameCore_Impl;
	std::optional<size_t> reglist_index;
	bool was_destroyed = false;
	
	//
	
	[[noreturn]] void throw_type_error(const char *func, const std::type_info& t) const;
	
	void iterate_direct (callable_ref<void(EComp*&)> f);
	void iterate_reverse(callable_ref<void(EComp*&)> f);
};

#endif // ENTITY_HPP
