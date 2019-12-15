#ifndef SVG_SIMPLE_HPP
#define SVG_SIMPLE_HPP

#include <vector>
#include "vaslib/vas_math.hpp"

/// Line segment chain
struct SVG_Path
{
	std::string id;
	std::vector<vec2fp> ps;
};

/// Circle or ellipse
struct SVG_Point
{
	std::string id;
	vec2fp pos;
	float radius; ///< For ellipse uses maximum
};

struct SVG_File
{
	std::vector<SVG_Path> paths;
	std::vector<SVG_Point> points;
};

/// Throws on error
SVG_File svg_read(const char *filename);

#endif // SVG_SIMPLE_HPP
