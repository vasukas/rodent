#ifndef PATH_SEARCH_HPP
#define PATH_SEARCH_HPP

#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"

class PathSearch
{
public:
	using WhoType = uint32_t;
	
	struct Args
	{
		vec2i src, dst;
		size_t max_length = 80;
		
		std::optional<vec2i> evade = {};
		int evade_radius = {};
		int evade_cost = {};
		WhoType who = 0; ///< 0 is 'none'
	};
	struct Result
	{
		/// All points, excluding first. 
		/// Empty if path doesn't exist
		std::vector<vec2i> ps;
	};
	
	TimeSpan debug_time; // reset manually
	size_t debug_request_count = 0; // reset manually
	size_t debug_lock_count = 0; // set on update
	
	static PathSearch* create();
	virtual ~PathSearch() = default;
	
	/// Cost 0 indicates impassable; row-major. (COST NOT IMPLEMENTED). 
	/// No tasks must be queued when calling this. 
	/// Grid MUST be completely surrounded by impassable cells. 
	/// Note: only first 255 locks are used
	virtual void update(vec2i size, std::vector<uint8_t> cost_grid, std::vector<std::pair<WhoType, vec2i>> locks = {}) = 0;
	
	// Note: all coords must be valid
	
	/// Executes task synchronously w/o any checks
	virtual Result find_path(Args args) = 0;
	
	/// Calculates path length (return size_t_inval if none)
	virtual size_t find_length(Args args) = 0;
};

#endif // PATH_SEARCH_HPP
