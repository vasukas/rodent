#ifndef LEVEL_MAP_HPP
#define LEVEL_MAP_HPP

#include "vaslib/vas_math.hpp"

class  GameCore;
struct LevelCtrRoom;
struct LevelTerrain;
struct TeleportInfo;
struct TimeSpan;



class LevelMap
{
public:
	static LevelMap* init(GameCore& core, const LevelTerrain& lt); ///< Inits singleton. Core not used here
	static LevelMap& get(); ///< Returns existing singleton
	virtual ~LevelMap();
	
	virtual void draw(vec2i add_offset, std::optional<vec2fp> plr_pos, TimeSpan passed, bool enabled, bool show_visited) = 0;
	
	/// If cursor is over room with teleport, returns that teleport. Current may be null
	virtual const TeleportInfo* draw_transit(vec2i cur_pos, const TeleportInfo* current) = 0;
	
	virtual void mark_final_term(const LevelCtrRoom& rm) = 0;
	virtual void mark_visited(const LevelCtrRoom& rm) = 0;
};

#endif // LEVEL_MAP_HPP
