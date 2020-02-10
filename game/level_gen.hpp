#ifndef LEVEL_GEN_HPP
#define LEVEL_GEN_HPP

#include "vaslib/vas_math.hpp"

struct ImageInfo;
struct RandomGen;



struct LevelTerrain
{
	enum RoomType
	{
		RM_DEFAULT, ///< Just empty
		
		RM_CONNECT,
		RM_LIVING,
		RM_WORKER,
		RM_REPAIR,
		RM_FACTORY,
		RM_LAB,
		RM_STORAGE,
		RM_KEY,
		RM_TERMINAL,
		RM_TRANSIT,
		RM_ABANDON,
		
		RM_TYPE_TOTAL_COUNT
	};
	enum StructureIndex
	{
		STR_NONE,
		STR_RIB_IN,
		STR_RIB_OUT,
		STR_COLUMN
	};
	struct Room
	{
		Rect area; ///< Boundaries
		RoomType type;
		uint32_t dbg_color; ///< 24-bit RGB
	};
	struct Cell
	{
		bool is_wall = true;
		bool is_door = false; ///< Set only for outer cell(s). Can be either horizontal or vertical, up to 3 cells
		bool isolated = false; ///< For debug only
		bool decor_used = false; ///< Not used here
		StructureIndex structure = STR_NONE; ///< Index for room-specific objects
		Room* room = nullptr; ///< To which room belongs (if any)
		
		int tmp; ///< May be used for various algorithms
	};
	struct Corridor
	{
		std::vector<size_t> rs; ///< Room indices
		std::vector<vec2i> cs; ///< Cell positions
	};
	
	vec2i grid_size;
	float cell_size;
	
	std::vector<Cell> cs;
	std::vector<Room> rooms; ///< [0] is always initial room
	std::vector<Corridor> corrs;
	
	std::vector<std::vector<vec2fp>> ls_wall; ///< Line segment loops representing walls
	std::vector<std::pair<vec2fp, vec2fp>> ls_grid; ///< Line segments representing background grid
	
	enum DbgSpawn
	{
		DBG_SPAWN_PLAYER,
		DBG_SPAWN_DRONE
	};
	std::vector<std::pair<vec2i, DbgSpawn>> dbg_spawns; ///< If set, normal spawning doesn't happen
	
	
	
	struct GenParams
	{
		RandomGen* rnd; ///< Must be non-null
		vec2i grid_size;
		float cell_size;
	};
	
	/// Generates level, never returns null
	static LevelTerrain* generate(const GenParams& pars);
	
	/// Loads debug level from image. Throws on error. Never returns null
	static LevelTerrain* load_test(const char *filename, float cell_size);
	
	///
	ImageInfo draw_grid(bool is_debug) const;
	
	/// Saves level as image
	void test_save(const char *prefix = "level_test", bool img_line = true, bool img_grid = true) const;
	
private:
	void find_corridors();
	std::vector<std::vector<vec2fp>> vectorize() const;
	std::vector<std::pair<vec2fp, vec2fp>> gen_grid() const;
};

#endif // LEVEL_GEN_HPP
