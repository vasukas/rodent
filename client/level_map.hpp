#ifndef LEVEL_MAP_HPP
#define LEVEL_MAP_HPP

#include "vaslib/vas_math.hpp"

struct LevelCtrRoom;
struct LevelTerrain;
struct TeleportInfo;
struct TimeSpan;



class LevelMap
{
public:
	static LevelMap* init(const LevelTerrain& lt); ///< Inits singleton
	static LevelMap& get(); ///< Returns existing singleton
	virtual ~LevelMap();
	
	virtual void draw(TimeSpan passed, std::optional<vec2fp> plr_pos, bool enabled) = 0;
	
	/// If cursor is over room with teleport, returns that teleport. Current may be null
	virtual const TeleportInfo* draw_transit(vec2i cur_pos, const TeleportInfo* current, const std::vector<TeleportInfo>& teleps) = 0;
	
	virtual void mark_final_term(const LevelCtrRoom& rm) = 0;
};

#endif // LEVEL_MAP_HPP
