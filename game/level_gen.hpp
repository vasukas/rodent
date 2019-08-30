#ifndef LEVEL_GEN_HPP
#define LEVEL_GEN_HPP

#include "entity.hpp"



struct LevelTerrain
{
	struct Room
	{
		Rect area; ///< Boundaries
	};
	struct CorridorEntry
	{
		vec2i inner;
		vec2i outer;
		size_t room_index;
	};
	struct Corridor
	{
		std::vector<vec2i> cells;
		std::vector<CorridorEntry> ents;
	};
	struct Cell
	{
		bool is_wall = true;
	};
	
	vec2i grid_size;
	float cell_size;
	
	std::vector<Cell> cs;
	std::vector<Room> rooms;
	std::vector<Corridor> cors;
	
	std::vector<std::vector<vec2fp>> ls_wall; ///< Line segment loops representing walls
	std::vector<std::pair<vec2fp, vec2fp>> ls_grid; ///< Line segments representing background grid
	
	
	
	struct GenParams
	{
		vec2i grid_size = {300, 200};
		float cell_size = 1.f;
	};
	
	/// Generates level, never fails
	static LevelTerrain* generate(const GenParams& pars);
	
	/// Saves level as image
	void test_save(const char *prefix = "level_test") const;
	
private:
	std::vector<std::vector<vec2fp>> vectorize() const;
	std::vector<std::pair<vec2fp, vec2fp>> gen_grid() const;
};

#endif // LEVEL_GEN_HPP
