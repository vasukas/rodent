#ifndef MOVEMENT_HPP
#define MOVEMENT_HPP

#include "vaslib/vas_time.hpp"
#include "entity.hpp"



struct EC_Movement : EComp
{
	TimeSpan app_inert = TimeSpan::seconds(0.5); ///< How much time takes to reach control velocity
	TimeSpan dec_inert = TimeSpan::seconds(2.0); ///< How much time takes to slow down from control velocity
	
	// All coefficients are per second
	vec2fp damp_sep = {0.99, 2}; ///< Separate damp_lin for forward and lateral directions
	float damp_lin = 0.99; ///< Linear dampening coefficient or 0 to use damp_sep
	float damp_ang = 1.2f; ///< Angular dampening coefficient
	float damp_minthr = 0.1f; ///< Velocity below this immediatly damped to zero
	
	float dust_vel = 8.f; ///< Minimal dust particle effect velocity (0 to disable)
	
	
	
	/// Sets applied (control) velocity
	void set_app_vel(vec2fp v);
	
	EC_Movement();
	
private:
	enum TarSt {T_NONE, T_VEL, T_ZERO};
	struct TarDir {
		TarSt st = T_NONE;
		float vel = 0;
		TimeSpan left;
	};
	TarDir tarx, tary;
	
	void step();
};

#endif // MOVEMENT_HPP
