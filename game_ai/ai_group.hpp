#ifndef AI_GROUP_LOGIC_HPP
#define AI_GROUP_LOGIC_HPP

#include "ai_common.hpp"

struct AI_GroupTarget;



/// Internal
class AI_Controller
{
public:
	static AI_Controller& get(); ///< Returns singleton
	virtual ~AI_Controller();
	
	static AI_Controller* init(); ///< Initializes singleton
	virtual void step() = 0;
	
	virtual void   reg(AI_Group* g) = 0;
	virtual void unreg(AI_Group* g) = 0;
	
	virtual AI_GroupTarget* ref_target(EntityIndex eid, AI_Group* g) = 0;
	virtual void unref_target(AI_GroupTarget* tar, AI_Group* g) = 0;
};



/// Group must exist as long as all its members
class AI_Group final
{
public:
	const Rect g_area; ///< Grid coordinates
	const Rectfp area; ///< World coordinates
	
	AI_Group(Rect area);
	~AI_Group();
	
	void reg_upd(AI_Drone* drone, bool Add_or_rem);
	
	void report_target(Entity* ent); ///< Target is visible
	void no_target(EntityIndex eid); ///< No target on last known pos
	void proxy_inspect(vec2fp suspect); ///< Sets closest drone to inspect
	
	const std::vector<float>& get_aos_ring_dist(); /// Returns ring distances for AoS, and if it was updated
	const std::vector<AI_Drone*>& get_drones() const {return drones;}
	
	std::optional<vec2fp> get_aos_next(AI_Drone* d, bool Left_or_right);
	
private:
	std::vector<AI_Drone*> drones;
	TimeSpan last_inspect_time; ///< GameCore time
	std::vector<float> aos_ring_dist; ///< Ring distances for AoS
	
	friend class AI_Controller_Impl;
	void step();
	
	struct Idle {};
	struct Search
	{
		AI_GroupTarget* tar;
		bool all_final = false;
		TimeSpan left = AI_Const::group_search_time;
	};
	struct Battle
	{
		std::vector<AI_GroupTarget*> tars;
	};
	std::variant<Idle, Search, Battle> state;
	bool upd_tasks = false;
	
	void reset_state() {for_all_targets([&](auto t){ AI_Controller::get().unref_target(t, this); });}
	void for_all_targets(std::function<void(AI_GroupTarget*)> f);
};

#endif // AI_GROUP_LOGIC_HPP
