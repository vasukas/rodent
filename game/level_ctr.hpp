#ifndef LEVEL_CTR_HPP
#define LEVEL_CTR_HPP

#include "entity.hpp"

struct LevelTerrain;



class LevelControl
{
public:
	struct Cell
	{
		vec2i pos; ///< self
		bool is_wall;
		
		std::optional<size_t> room_i;
	};
	
	enum SpawnType
	{
		SP_PLAYER,
		SP_TEST_AI,
		SP_TEST_BOX
	};
	
	struct Spawn
	{
		SpawnType type;
		vec2fp pos;
	};
	
	
	
	static LevelControl* init(const LevelTerrain& lt); ///< Creates singleton
	static LevelControl& get(); ///< Returns singleton
	~LevelControl();
	
	vec2i get_size() const {return size;}
	Cell* cell(vec2i pos) noexcept;
	Cell& cref(vec2i pos);
	
	void fin_init();
	const std::vector<Spawn>& get_spawns() const {return spps;}
	
private:
	std::vector<Cell> cells;
	vec2i size;
	
	std::vector<Spawn> spps;
	
	LevelControl(const LevelTerrain& lt);
};

#endif // LEVEL_CTR_HPP
