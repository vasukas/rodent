#ifndef LEVEL_CTR_HPP
#define LEVEL_CTR_HPP

#include "entity.hpp"



class LevelControl
{
public:
	struct Room;
	
	struct CorridorEntry
	{
		vec2i pos; ///< Inside corridor
		size_t room_i = size_t_inval; ///< May be null
//		EntityPtr door;
	};
	struct Corridor
	{
		std::vector<vec2i> cells;
		std::vector<CorridorEntry> ents;
	};
	struct Room
	{
		size_t index; ///< Unique
		Rect area; ///< Grid position
		vec2fp w_ctr; ///< World center
//		std::vector<size_t> cors;
		
		size_t gener;
	};
	struct Cell
	{
		vec2i pos; ///< Self position
		bool is_border = false;
		size_t cor_i = size_t_inval;
		size_t room_i = size_t_inval;
	};
	
	static const vec2fp cell_size;

	
	
	vec2i size() const {return grid_size;}
	Cell* getc(vec2i pos); ///< Returns cell or nullptr if out of bounds
	Cell& cref(vec2i pos); ///< Returns cell or throws if out of bounds
	
	static LevelControl* init(); ///< Creates singleton
	static LevelControl& get(); ///< Returns singleton
	virtual ~LevelControl();
	
	virtual Room*     get_room(size_t index) = 0; ///< Returns nullptr if index is invalid
	virtual Corridor* get_corr(size_t index) = 0; ///< Returns nullptr if index is invalid
	
private:
	std::vector<Cell> cs;
	vec2i grid_size;
	
protected:
	std::vector<Cell>& get_all_cs() {return cs;}
	LevelControl();
};

#endif // LEVEL_CTR_HPP
