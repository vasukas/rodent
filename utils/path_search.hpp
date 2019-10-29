#ifndef PATH_SEARCH_HPP
#define PATH_SEARCH_HPP

#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"

class AsyncPathSearch
{
public:
	struct Result
	{
		/// All points, excluding first. 
		/// Empty if path doesn't exist
		std::vector<vec2i> ps;
	};
	struct AddInfo
	{
		size_t max_length = 80;
	};
	
	static AsyncPathSearch* create_default();
	virtual ~AsyncPathSearch() = default;
	
	// Cost 0 indicates impassable; row-major. (COST NOT IMPLEMENTED)
	// No tasks must be queued when calling this.
	// Grid MUST be completely surrounded by impassable cells
	virtual void update(vec2i size, std::vector<uint8_t> cost_grid) = 0;
	
	// Task indices MUST be valid
	// all queued tasks MUST be cleaned (except when exiting)
	// coords must be valud
	
	virtual size_t add_task(vec2i from, vec2i to, AddInfo info) = 0;
	virtual void rem_task(size_t index) = 0;
	
	/// Removes if ready
	virtual std::optional<Result> get_task(size_t index) = 0;
};

#endif // PATH_SEARCH_HPP
