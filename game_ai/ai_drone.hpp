#ifndef AI_DRONE_HPP
#define AI_DRONE_HPP

#include "client/ec_render.hpp"
#include "ai_components.hpp"
#include "ai_control.hpp"
#include "ai_sim.hpp"



class AI_Drone final : public EComp
{
public:
	// idle states
	
	struct IdlePoint {};
	
	struct IdleResource
	{
		AI_SimResource::Value val;
		AI_SimResource::WorkerReg reg = {};
		TimeSpan reg_try_tmo = {}; // before trying to find one again
		bool is_loading = true; // is acquiring resources
		TimeSpan particle_tmo = {}; // initial delay before showing particles
	};
	
	struct IdlePatrol
	{
		std::vector<vec2fp> pts;
		size_t at = 0;
		TimeSpan tmo = {};
		
		void next();
	};
	
	using IdleState = std::variant<IdlePoint, IdleResource, IdlePatrol>;
	
	// main (battle) states
	
	struct Idle
	{
		IdleState ist;
	};
	
	struct Battle
	{
		AI_GroupPtr grp;
		
		TimeSpan not_visible; ///< For how long (by this drone)
		TimeSpan firstshot_time; ///< GameCore time after which can shoot
		float chase_wait = 0; ///< Wait level, <= 0 - chase immediatly, 1 - full delay
		std::optional<vec2fp> placement; ///< Optimal position, assigned by group
		
		Battle(AI_Drone& drone);
		void reset_firstshot();
	};
	
	struct Suspect
	{
		enum Prio {
			PRIO_NORMAL,
			PRIO_HELPCALL,
			PRIO_HELPCALL_HIGH
		};
		
		vec2fp pos;
		float level = AI_Const::suspect_initial;
		Prio prio = PRIO_NORMAL;
		bool was_visible = true;
	};
	
	struct Search
	{
		std::vector<vec2fp> pts;
		TimeSpan tmo;
		size_t at = 0;
		float susp_level = AI_Const::suspect_max_level;
	};
	
	struct Puppet
	{
		std::optional<std::pair<vec2fp, AI_Speed>> mov_tar = {};
		bool ret_atpos = false; // true if reached target position
	};
	
	using State = std::variant<Idle, Battle, Suspect, Search, Puppet>;
	
	
	
	AI_Movement* mov = nullptr; ///< May be null
	uint8_t is_online = 0;
	
	
	
	AI_Drone(Entity& ent, std::shared_ptr<AI_DroneParams> pars, IdleState idle, std::unique_ptr<AI_AttackPattern> atkpat);
	~AI_Drone();
	
	void set_online(bool is);
	
	bool is_camper() const {return pars->is_camper || !mov;}
	const AI_DroneParams& get_pars() const {return *pars;}
	
	State& get_state() {return state_stack.back();}
	std::string get_dbg_state() const;
	
	void add_state(State new_state);
	void replace_state(State new_state);
	void remove_state();
	
	void set_single_state(State new_state); ///< Removes all states except idle before adding
	void set_battle_state() {set_single_state(Battle{*this});}
	void set_idle_state(); ///< Removes all states except idle
	
	AI_RotationControl& get_rot_ctl() {return rot_ctl;}
	
private:
	std::shared_ptr<AI_DroneParams> pars;
	vec2fp home_point; // original spawn
	std::vector<State> state_stack; // never empty
	
	AI_TargetProvider prov;
	AI_Attack atk;
	AI_RotationControl rot_ctl;
	
	TimeSpan helpcall_last;
	TimeSpan retaliation_tmo;
	
	TimeSpan text_alert_last; // GameCore time
	std::unique_ptr<EC_ParticleEmitter::Channel> particles;
	
	
	void state_on_enter(State& state);
	void state_on_leave(State& state);
	void helpcall(std::optional<vec2fp> target, bool high_prio);
	
	void step() override;
	void text_alert(std::string s, bool important = false, size_t num = 1);
};

#endif // AI_DRONE_HPP
