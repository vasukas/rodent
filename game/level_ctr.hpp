#ifndef LEVEL_CTR_HPP
#define LEVEL_CTR_HPP

#include "entity.hpp"

struct LevelTerrain;



struct PathRequest
{
	struct Result
	{
		std::vector<vec2fp> ps; ///< Chain of line segments, including first and last. Always contains at least one element.
		float len; ///< Total length, may be zero
		bool not_found; ///< Set to true if no path was found
	};
	
	PathRequest() = default;
	PathRequest(vec2fp from, vec2fp to);
	~PathRequest();
	
	PathRequest(PathRequest&&);
	PathRequest& operator=(PathRequest&&);
	
	PathRequest(const PathRequest&) = delete;
	
	bool is_ok() const; ///< Returns true if result is available or waiting
	bool is_ready() const; ///< Returns true if result computed and not yet returned
	
	/// Returns (moved) result if ready
	std::optional<Result> result();
	
private:
	std::pair<vec2fp, vec2fp> p0;
	mutable std::optional<size_t> i;
	mutable std::optional<Result> res;
};



class AsyncPathSearch;

class LevelControl final
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
		SP_TEST_TURRET,
		SP_TEST_BOX,
		SP_TEST_DRONE,
		SP_SUPPLY_RND
	};
	
	struct Spawn
	{
		SpawnType type;
		vec2fp pos;
	};
	
	const float cell_size;
	
	
	
	static LevelControl* init(const LevelTerrain& lt); ///< Creates singleton
	static LevelControl& get(); ///< Returns singleton
	~LevelControl();
	
	vec2i get_size() const {return size;}
	Cell* cell(vec2i pos) noexcept;
	Cell& cref(vec2i pos);
	
	void fin_init();
	const std::vector<Spawn>& get_spawns() const {return spps;}
	
	AsyncPathSearch& get_aps() {return *aps;}
	
	vec2i to_cell_coord(vec2fp p) const {return (p / cell_size).int_floor();}
	bool is_same_coord(vec2fp a, vec2fp b) const {return to_cell_coord(a) == to_cell_coord(b);}
	
protected:
	vec2i size;
	std::vector<Cell> cells;
	std::vector<Spawn> spps;
	std::unique_ptr<AsyncPathSearch> aps;
	
	LevelControl(const LevelTerrain& lt);
};

#endif // LEVEL_CTR_HPP
