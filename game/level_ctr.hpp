#ifndef LEVEL_CTR_HPP
#define LEVEL_CTR_HPP

#include "entity.hpp"

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
	PathRequest(const PathRequest&) = delete;
	
	PathRequest(GameCore& core, vec2fp from, vec2fp to,
	            std::optional<float> max_length = {},
	            std::optional<Evade> evade = {});
	
	/// Returns true if result is available or waiting
	bool is_ok() const {return !!res;}
	
	/// Returns (moved) result if ready, resetting request
	std::optional<Result> result();
	
private:
	std::optional<Result> res;
};



struct LevelCtrRoom
{
	enum RoomType
	{
		T_DEFAULT,
		T_TRANSIT,
		T_FINAL_TERM
	};
	
	std::string name;
	RoomType type = T_DEFAULT;
	
	Rect area; ///< cells
	std::vector<size_t> neis; ///< rooms
	
	int ai_radio_cost = 1;
	int tmp; ///< for algorithms
	
	int ai_patrolling = 0;
	
	Rectfp fp_area() const {
		return area.to_fp(GameConst::cell_size);
	}
};

class LevelControl final
{
public:
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
	
	
	
	static LevelControl* create(const LevelTerrain& lt);
	void fin_init(const LevelTerrain& lt); ///< Completes init after spawn is complete
	~LevelControl();
	
	vec2i get_size() const {return size;}
	bool is_valid(vec2i pos) const {return Rect{{}, size, true}.contains_le(pos);}
	
	Cell& mut_cell(vec2i pos); ///< Updates pathfinding at the end of the step
	const Cell* cell(vec2i pos) const noexcept;
	const Cell& cref(vec2i pos) const;
	
	const LevelCtrRoom* ref_room(vec2fp pos) const noexcept;
	LevelCtrRoom& ref_room(size_t index);
	const std::vector<LevelCtrRoom>& get_rooms() const {return rooms;}
	
	vec2fp get_closest(SpawnType type, vec2fp from) const;
	const std::vector<Spawn>& get_spawns() const {return spps;}
	PathSearch& get_aps() {return *aps;}
	
	static vec2i to_cell_coord(vec2fp p) {return (p / GameConst::cell_size).int_floor();}
	static vec2fp to_center_coord(vec2i p) {return vec2fp(p) * GameConst::cell_size + vec2fp::one(GameConst::cell_size * 0.5);}
	static bool is_same_coord(vec2fp a, vec2fp b) {return to_cell_coord(a) == to_cell_coord(b);}
	
	/// Converts world coords to cell, rounding to nearest non-wall cell if possible
	vec2i to_nonwall_coord(vec2fp p) const;
	
	void add_spawn(Spawn sp);
	
	void update_aps(bool forced = true); ///< Updates wall states for path search
	void set_wall(vec2i pos, bool is_wall); ///< Sets wall state and updates aps. Coord-safe
	
	void rooms_reset_tmp(int value) {
		for (auto& r : rooms) r.tmp = value;
	}
	
protected:
	vec2i size;
	std::vector<LevelCtrRoom> rooms;
	std::vector<Cell> cells;
	std::vector<Spawn> spps;
	std::unique_ptr<PathSearch> aps;
	bool aps_req_update = false;
	
	LevelControl(const LevelTerrain& lt);
};

#endif // LEVEL_CTR_HPP
