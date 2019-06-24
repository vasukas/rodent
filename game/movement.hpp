#ifndef MOVEMENT_HPP
#define MOVEMENT_HPP

#include "entity.hpp"



struct EC_Movement
{
	Entity* ent = nullptr;
	
	float min_ch = 0.1f; ///< Minimal velocity change
	float max_ch = 8.f; ///< Maximal velocity change
	bool inertial_mode = false; ///< For PC only
	
	bool use_damp = true;
	float damp_lin = 0.99f; ///< Linear coefficient
	float damp_max = 2.f; ///< Max linear damping velocity
	float damp_ang = 2.f; ///< Angular (rotation) coefficient
	
	float dust_vel = 8.f; ///< Minimal dust particle effect velocity (0 to disable)
	const float base_dist = 0.2f; ///< Base target distance
	const float vel_eps = 0.0001f; ///< Velocities are considered equal if difference is less
	
	
	
	void set_target(vec2fp pos);
	void set_target_velocity(Transform vel);
	void set_target(EntityIndex eid, float dist);
	
	bool has_reached();
	
private:
	enum TarType {T_NONE, T_POS, T_VEL, T_EID};
	TarType t_type = T_NONE;
	Transform t_tr;
	EntityIndex t_eid = 0;
	float t_dist;
	
	friend class GameCore_Impl;
	void step();
};

#endif // MOVEMENT_HPP
