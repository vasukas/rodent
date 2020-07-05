#ifndef AI_GROUP_HPP
#define AI_GROUP_HPP

#include "game/entity.hpp"
#include "ai_common.hpp"

class  AI_AOS;
struct AI_GroupPtr;
class  AI_SimResource;



class AI_Group final
{
public:
	GameCore& core;
	const EntityIndex tar_eid;
	
	AI_Group(GameCore& core, EntityIndex tar_eid);
	
	const std::vector<AI_Drone*>& get_drones() const {return drones;}
	void forall(callable_ref<void(AI_Drone&)> f, AI_Drone* except = nullptr);
	
	void report_seen(); ///< Called if just seen
	void init_search();
	
	bool is_visible() const; ///< Returns true if was just seen
	TimeSpan passed_since_seen() const; ///< Time since last seen
	vec2fp get_last_pos() const {return last_pos;} ///< Returns last known position
	
	/// Sets search state for all capable drones, if possible
	static bool init_search(GameCore& core, const std::vector<AI_Drone*>& drones, vec2fp target_pos,
	                        bool was_in_battle, std::optional<vec2fp> real_target_pos);
	
private:
	friend class AI_Controller_Impl;
	friend AI_GroupPtr;
	
	void update();
	
	TimeSpan last_seen;
	vec2fp last_pos;
	std::vector<AI_Drone*> drones;
	std::unique_ptr<AI_AOS> aos;
	
	int msg_range_extend = 0;
};



struct AI_GroupPtr
{
	AI_GroupPtr(AI_Group* grp, AI_Drone* dr);
	~AI_GroupPtr();
	
	AI_GroupPtr(AI_GroupPtr&& p) noexcept {*this = std::move(p);}
	AI_GroupPtr& operator= (AI_GroupPtr&& p) noexcept;
	
	AI_Group& operator* () const noexcept {return *grp;}
	AI_Group* operator->() const noexcept {return grp;}
	
private:
	AI_Group* grp = nullptr;
	AI_Drone* dr;
};



class AI_Controller
{
public:
	bool show_aos_debug = false;
	bool show_states_debug = false;
	size_t debug_batle_number = 0;
	
	static AI_Controller* create(GameCore& core);
	virtual ~AI_Controller() = default;
	virtual void step() = 0;
	
	virtual AI_GroupPtr get_group(AI_Drone& drone) = 0;
	virtual void help_call(AI_Drone& drone, std::optional<vec2fp> target, bool high_prio) = 0;
	
	/// Returns true if any group with such target exist
	virtual bool is_targeted(Entity& ent) = 0;
	
	virtual void force_reset_scanner(TimeSpan timeout) = 0;
	
	/// Returns global suspicion level, already limited
	virtual float get_global_suspicion() = 0;
	
	/// Sets maximum level
	virtual void trigger_global_suspicion() = 0;
	
protected:
	friend AI_Drone;
	friend AI_GroupPtr;
	friend AI_SimResource;
	
	virtual void free_group(AI_Group* p) = 0;
	
	virtual void ref_drone(AI_Drone* d) = 0;
	virtual void unref_drone(AI_Drone* d) = 0;
	
	virtual int ref_resource(Rectfp p, AI_SimResource* r) = 0;
	virtual void find_resource(Rectfp p, callable_ref<void(AI_SimResource&)> f) = 0;
	
	virtual void mark_scan_failed() = 0;
};

#endif // AI_GROUP_HPP
