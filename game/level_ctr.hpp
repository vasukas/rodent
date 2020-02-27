#ifndef LEVEL_CTR_HPP
#define LEVEL_CTR_HPP

#include <memory>
#include "vaslib/vas_math.hpp"

class  GameCore;
class  PathSearch;
struct LevelTerrain;



struct PathRequest
{
	struct Evade
	{
		vec2fp pos;
		float radius;
		float added_cost;
	};
	
	struct Result
	{
		std::vector<vec2fp> ps; ///< Chain of line segments, including first and last. Always contains at least one element
		bool not_found; ///< Set to true if no path was found
	};
	
	static constexpr float default_max_length = 120;
	
	PathRequest() = default;
	PathRequest(GameCore& core, vec2fp from, vec2fp to, std::optional<float> max_length = {}, std::optional<Evade> evade = {});
	PathRequest(const PathRequest&) = delete;
	
	/// Returns true if result is available or waiting
	bool is_ok() const {return !!res;}
	
	/// Returns (moved) result if ready, resetting request
	std::optional<Result> result();
	
private:
	std::optional<Result> res;
};



class LevelControl final
{
public:
	struct Room
	{
		std::string name;
		bool is_final_term = false;
		
		Rect area; ///< cells
		std::vector<size_t> neis; ///< rooms
		
		int ai_radio_cost = 1;
		int tmp; ///< for algorithms
	};
	
	struct Cell
	{
		vec2i pos; ///< self
		bool is_wall;
		
		int tmp; ///< for algorithms
		
		std::optional<size_t> room_i;
		size_t room_nearest; ///< Always valid, even for walls
	};
	
	enum SpawnType
	{
		SP_PLAYER
	};
	
	struct Spawn
	{
		SpawnType type;
		vec2fp pos;
	};
	
	const float cell_size;
	
	
	
	static LevelControl* create(const LevelTerrain& lt);
	void fin_init(const LevelTerrain& lt); ///< Completes init after spawn is complete
	~LevelControl();
	
	vec2i get_size() const {return size;}
	bool is_valid(vec2i pos) const {return Rect{{}, size, true}.contains_le(pos);}
	
	Cell& mut_cell(vec2i pos); // Note: if is_wall is modified, aps must be updated
	const Cell* cell(vec2i pos) const noexcept;
	const Cell& cref(vec2i pos) const;
	const Room* ref_room(vec2fp pos) const noexcept;
	Room& ref_room(size_t index);
	
	vec2fp get_closest(SpawnType type, vec2fp from) const;
	const std::vector<Spawn>& get_spawns() const {return spps;}
	PathSearch& get_aps() {return *aps;}
	
	vec2i to_cell_coord(vec2fp p) const {return (p / cell_size).int_floor();}
	vec2fp to_center_coord(vec2i p) const {return vec2fp(p) * cell_size + vec2fp::one(cell_size * 0.5);}
	bool is_same_coord(vec2fp a, vec2fp b) const {return to_cell_coord(a) == to_cell_coord(b);}
	
	/// Converts world coords to cell, rounding to nearest non-wall cell if possible
	vec2i to_nonwall_coord(vec2fp p) const;
	
	void add_spawn(Spawn sp);
	
	void update_aps(); ///< Updates wall states for path search
	void set_wall(vec2i pos, bool is_wall); ///< Sets wall state and updates aps. Coord-safe
	
	void rooms_reset_tmp(int value) {
		for (auto& r : rooms) r.tmp = value;
	}
	
protected:
	vec2i size;
	std::vector<Room> rooms;
	std::vector<Cell> cells;
	std::vector<Spawn> spps;
	std::unique_ptr<PathSearch> aps;
	
	LevelControl(const LevelTerrain& lt);
};

#endif // LEVEL_CTR_HPP
