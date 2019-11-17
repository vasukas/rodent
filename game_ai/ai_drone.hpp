#ifndef AI_DRONE_HPP
#define AI_DRONE_HPP

#include "ai_common.hpp"
#include "ai_components.hpp"

struct AI_GroupTarget;



class AI_Drone final : public EComp
{
public:
	// tasks
	
	struct TaskIdle {};
	struct TaskSuspect {
		vec2fp pos;
	};
	struct TaskSearch {
		std::vector<vec2fp> waypoints;
	};
	struct TaskEngage {
		EntityIndex eid;
		vec2fp last_pos;
		AI_GroupTarget* tar; ///< May be invalid; only for AI_Group
	};
	
	using Task = std::variant<TaskIdle, TaskSuspect, TaskSearch, TaskEngage>;
	
	// states

	struct IdleNoop {};	
	struct IdlePoint {vec2fp p;};
	struct Suspect {
		vec2fp p;
		TimeSpan wait; ///< Timeout
	};
	
	struct Search {
		size_t next_wp = 0; ///< Index of next waypoint
		TimeSpan tmo = {}; ///< Timeout on reach
		bool end = false; ///< If true, reached final point
	};
	struct Engage {
		TimeSpan chase_cou = {}; ///< How long already waited
		TimeSpan last_visible = {}; ///< Last time target was visible
		
		TimeSpan crowd_cou = {}; ///< How long it is crowded
		TimeSpan nolos_cou = {}; ///< How long has no fire LoS
		
		std::optional<bool> circ_left = {};
		TimeSpan circ_delay = {};
		TimeSpan circ_prev_time = {};
		bool circ_prev_left = false;
	};
	
	using State = std::variant<IdleNoop, IdlePoint, Suspect, Search, Engage>;
	
	
	
	AI_TargetProvider* prov; ///< Must be non-null
	AI_Movement* mov = nullptr; ///< May be null
	
	Task task = TaskIdle{};
	State state = IdleNoop{};
	
	float face_rot = 0;
	
	
	
	AI_Drone(Entity* ent, std::shared_ptr<AI_DroneParams> pars, std::shared_ptr<AI_Group> grp, std::shared_ptr<State> def_idle);
	~AI_Drone();
	
	void set_task(Task new_task);
	void update_enabled(bool now_enabled);
	
	AI_DroneParams& get_pars()  {return *pars;}
	AI_Group&       get_group() {return *grp;}
	
	std::string get_dbg_state() const;
	
private:
	std::shared_ptr<AI_DroneParams> pars;
	std::shared_ptr<AI_Group> grp;
	std::shared_ptr<State> def_idle;
	AI_Attack atk;
	AI_RenRotation ren_rot;
	TimeSpan text_alert_last; // GameCore time
	bool has_lost = false; // for Engage
	
	void step() override;
	void text_alert(std::string s);
};

#endif // AI_DRONE_HPP
