#ifndef REN_PARTICLES_HPP
#define REN_PARTICLES_HPP

#include <vector>
#include "utils/color_manip.hpp"
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"



/// Single particle
struct ParticleParams
{
	vec2fp pos; // position
	vec2fp vel; // velocity
	vec2fp acc; // acceleration
	
	FColor clr;
	float lt, ft; // life and fade times, seconds
	float size; // particle radius
	
	/// Sets velocity and acceleration to zero
	void set_zero(bool vel = true, bool accel = true);
	
	/// Sets acceleration so particle would stop moving at the end of full lifetime. 
	/// Requires velocity and lt & ft to be already set
	void decel_to_zero();
	
	/// Adds to velocity and acceleration to simulate rise and fall. 
	/// t_fall is relative height [0-1] at the end. 
	/// Requires lt & ft to be already set
	void apply_gravity(float height, float t_fall = 0);
};



/// Generation parameters
struct ParticleBatchPars
{
	Transform tr = {};
	float power = 1.f;
	FColor clr = {1,1,1,1};
	float rad = 1.f;
};



struct ParticleGroupGenerator
{
	virtual ~ParticleGroupGenerator() = default;
	
	/// Generates group with specified transformation
	void draw(const ParticleBatchPars& pars);
	
protected:
	friend class RenParticles_Impl;
	
	/// Begins generating new group. Returns number of particles. 
	/// Params are already inited with zero rotations and acceleration, everything else is unset
	virtual size_t begin(const ParticleBatchPars& pars, ParticleParams& p) = 0;
	
	/// Fills params, value is same since last call and call to begin
	virtual void gen(ParticleParams& p) = 0;
	
	/// Group generation finished 
	virtual void end() {}
};



class RenParticles {
public:
	bool enabled = true; ///< Update is executed anyway
	
	static RenParticles& get(); ///< Returns singleton
	virtual void add(ParticleGroupGenerator& group, const ParticleBatchPars& pars) = 0;
	
protected:
	friend class Postproc_Impl;
	static RenParticles* init();
	virtual void render() = 0;
	virtual ~RenParticles();
};

#endif // REN_PARTICLES_HPP
