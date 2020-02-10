#ifndef PATH_SEARCH_HPP
#define PATH_SEARCH_HPP

#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"

class PathSearch
{
public:
	struct Args
	{
		vec2i src, dst;
		size_t max_length = 80;
		
		std::optional<vec2i> evade = {};
		int evade_radius = {};
		int evade_cost = {};
	};
	struct Result
	{
		/// All points, excluding first. 
		/// Empty if path doesn't exist
		std::vector<vec2i> ps;
	};
	
	static PathSearch* create();
	virtual ~PathSearch() = default;
	
	/// Cost 0 indicates impassable; row-major. (COST NOT IMPLEMENTED). 
	/// No tasks must be queued when calling this. 
	/// Grid MUST be completely surrounded by impassable cells
	virtual void update(vec2i size, std::vector<uint8_t> cost_grid) = 0;
	
	// Note: all coords must be valid
	
	/// Executes task synchronously w/o any checks
	virtual Result find_path(Args args) = 0;
	
	/// Calculates path length (return size_t_inval if none)
	virtual size_t find_length(Args args) = 0;
};

#endif // PATH_SEARCH_HPP
