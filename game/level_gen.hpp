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
		RM_ABANDON
	};
	struct Room
	{
		Rect area; ///< Boundaries
		float far_value; ///< [0, 1], bigger the more remote room is
		
		RoomType type;
		uint32_t dbg_color; ///< 24-bit RGB
	};
	struct Cell
	{
		bool is_wall = true;
		bool is_door = false; ///< Set only for outer cell(s). Can be either horizontal or vertical, up to 3 cells
		bool isolated = false; ///< For debug only
		Room* room = nullptr; ///< To which room belongs
	};
	
	vec2i grid_size;
	float cell_size;
	
	std::vector<Cell> cs;
	std::vector<Room> rooms; ///< [0] is always initial room
	
	std::vector<std::vector<vec2fp>> ls_wall; ///< Line segment loops representing walls
	std::vector<std::pair<vec2fp, vec2fp>> ls_grid; ///< Line segments representing background grid
	
	
	
	struct GenParams
	{
		RandomGen* rnd;
		vec2i grid_size;
		float cell_size;
	};
	
	/// Generates level, never fails
	static LevelTerrain* generate(const GenParams& pars);
	
	///
	ImageInfo draw_grid(bool is_debug) const;
	
	/// Saves level as image
	void test_save(const char *prefix = "level_test", bool img_line = true, bool img_grid = true) const;
	
private:
	std::vector<std::vector<vec2fp>> vectorize() const;
	std::vector<std::pair<vec2fp, vec2fp>> gen_grid() const;
};

#endif // LEVEL_GEN_HPP
