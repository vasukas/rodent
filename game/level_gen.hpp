#ifndef LEVEL_GEN_HPP
#define LEVEL_GEN_HPP

#include "level_ctr.hpp"

struct ImageInfo;



struct LevelTerrain
{
	enum RoomType
	{
		RM_SMALL,
		RM_DEFAULT
	};
	struct Room
	{
		Rect area; ///< Boundaries
		RoomType type;
	};
	struct Cell
	{
		bool is_wall = true;
	};
	
	vec2i grid_size;
	float cell_size;
	
	std::vector<Cell> cs;
	std::vector<Room> rooms;
	
	std::vector<std::vector<vec2fp>> ls_wall; ///< Line segment loops representing walls
	std::vector<std::pair<vec2fp, vec2fp>> ls_grid; ///< Line segments representing background grid
	
	std::vector<std::pair<LevelControl::SpawnType, vec2i>> spps; ///< Spawn points
	
	
	
	struct GenParams
	{
		vec2i grid_size = {300, 200};
		float cell_size = 1.f;
	};
	
	/// Generates level, never fails
	static LevelTerrain* generate(const GenParams& pars);
	
	///
	static LevelTerrain* load_testlvl(float cell_size, const char *filename = "res/test_level.png");
	
	///
	ImageInfo draw_grid(bool is_debug) const;
	
	/// Saves level as image
	void test_save(const char *prefix = "level_test") const;
	
private:
	std::vector<std::vector<vec2fp>> vectorize() const;
	std::vector<std::pair<vec2fp, vec2fp>> gen_grid() const;
};

#endif // LEVEL_GEN_HPP
