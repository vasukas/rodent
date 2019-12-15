#ifndef LEVEL_MAP_HPP
#define LEVEL_MAP_HPP

#include "vaslib/vas_math.hpp"

struct LevelTerrain;
struct TimeSpan;



class LevelMap
{
public:
	static LevelMap* init(const LevelTerrain& lt); ///< Inits singleton
	static LevelMap& get(); ///< Returns existing singleton
	virtual ~LevelMap();
	
	// Must be called from render thread
	virtual void ren_init() = 0;
	virtual void draw(TimeSpan passed, std::optional<vec2fp> plr_pos, bool enabled) = 0; 
	virtual void mark_final_term() = 0;
};

#endif // LEVEL_MAP_HPP
