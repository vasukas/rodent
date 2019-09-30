#ifndef PARTICLES_HPP
#define PARTICLES_HPP

#include <vector>
#include "utils/color_manip.hpp"
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"
#include "texture.hpp"



/// Single particle
struct ParticleParams
{
	float px, py, pr; // position
	float vx, vy, vr; // velocity
	float ax, ay, ar; // acceleration
	FColor clr;
	float lt, ft; // life and fade times, seconds
	float size; // particle radius
	
	/// Sets velocity and acceleration to zero
	void set_zero(bool vel = true, bool accel = true);
	
	/// Sets acceleration so particle would stop moving at the end of full lifetime. 
	/// Needs velocity and lt & ft to be already set
	void decel_to_zero();
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
	friend class ParticleRenderer_Impl;
	
	/// Begins generating new group. Returns number of particles. 
	/// Params are already inited with zero rotations and acceleration, everything else is unset
	virtual size_t begin(const ParticleBatchPars& pars, ParticleParams& p) = 0;
	
	/// Fills params, value is same since last call and call to begin
	virtual void gen(ParticleParams& p) = 0;
	
	/// Group generation finished 
	virtual void end() {}
};



/// Particle group generation parameters
struct ParticleGroupStd : ParticleGroupGenerator
{
	// in all min & max pairs max -1 means "same as min"
	
	size_t count = 20; ///< Number of particles
	
	float radius = 1; ///< maximum start radius
	float rot_min = 0, rot_max = M_PI*2; ///< start rotation range (radians)
	bool radius_fixed = false; ///< are all particles appear on maximum radius
	
	float px_radius = 8; ///< radius of particle itself, in units
	
	std::vector<uint32_t> colors; ///< RGBA (alpha ignored unless 'alpha' is zero)
	uint8_t colors_range[6] = {}; ///< RGB, min (indices 0-2) and max (indices 3-5), ignored if 'colors' not empty
	float color_speed = 0.f; ///< color change speed (per second, unitary)
	int alpha = 192; ///< transparency (0-255)
	
	float speed_min = 0, speed_max = -1; ///< movement speed (pixels per second)
	float rot_speed_min = 0, rot_speed_max = -1; ///< rotation speed (radians per second)
	bool fade_stop = true; ///< is speed decreasing when fading
	
	TimeSpan TTL = TimeSpan::ms(1200), TTL_max = TimeSpan::ms(-1); ///< time to live (before fade)
	TimeSpan FT = TimeSpan::ms(800), FT_max = TimeSpan::ms(-1); ///< fade time
	
private:
	Transform t_tr;
	float t_spdmax, t_rotmax, t_lmax, t_fmax;
	
	size_t begin(const ParticleBatchPars& pars, ParticleParams& p);
	void gen(ParticleParams& p);
};



class ParticleRenderer {
public:
	static ParticleRenderer& get(); ///< Returns singleton
	
	virtual void add(ParticleGroupGenerator& group, const ParticleBatchPars& pars) = 0;
	
protected:
	friend class RenderControl_Impl;
	static ParticleRenderer* init();
	virtual ~ParticleRenderer();
	
	friend class Postproc_Impl;
	virtual void render() = 0;
};

#endif // PARTICLES_HPP
