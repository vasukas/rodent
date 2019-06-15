#ifndef PARTICLES_HPP
#define PARTICLES_HPP

#include <vector>
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"
#include "texture.hpp"




/// Particle group generation parameters
struct ParticleGroup
{
	// in all min & max pairs max -1 means "same as min"
	
	int count = 20; ///< number of particles
	
	vec2i origin = {}; ///< start position
	int radius = 1; ///< maximum start radius
	float rot_min = 0, rot_max = M_PI*2; ///< start rotation range (radians)
	bool radius_fixed = false; ///< are all particles appear on maximum radius
	
	std::vector<TextureReg> sprs; ///< images
	int px_radius = 8; ///< radius of particle itself, in pixels
	
	std::vector<uint32_t> colors; ///< RGBA (alpha ignored unless 'alpha' is zero)
	uint8_t colors_range[6] = {}; ///< RGB, min (indices 0-2) and max (indices 3-5), ignored if 'colors' not empty
	float color_speed = 0.f; ///< color change speed (per second, unitary)
	int alpha = 192; ///< transparency (0-255)
	
	float speed_min = 0, speed_max = -1; ///< movement speed (pixels per second)
	float rot_speed_min = 0, rot_speed_max = -1; ///< rotation speed (radians per second)
	bool fade_stop = true; ///< is speed decreasing when fading
	
	TimeSpan TTL = TimeSpan::ms(1200), TTL_max = TimeSpan::ms(-1); ///< time to live (before fade)
	TimeSpan FT = TimeSpan::ms(800), FT_max = TimeSpan::ms(-1); ///< fade time
	
	int prerender_index = -1; ///< internal
	
	
	/// Adds group to renderer
	void submit();
};



class ParticleRenderer {
public:
	static ParticleRenderer& get(); ///< Returns singleton
	
protected:
	friend class RenderControl_Impl;
	static ParticleRenderer* init();
	virtual ~ParticleRenderer();
	virtual void render(TimeSpan passed) = 0;
	
	friend ParticleGroup;
	virtual void add(const ParticleGroup& group) = 0;
};

#endif // PARTICLES_HPP
