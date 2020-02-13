#ifndef IMAGE_UTILS_HPP
#define IMAGE_UTILS_HPP

#include "color_manip.hpp"
#include "res_image.hpp"

void downscale_2x(ImageInfo& img);



struct ImageBrush
{
	virtual void apply(ImageInfo& img, vec2i at) = 0;
	virtual ~ImageBrush() = default;
};

struct ImagePointBrush : ImageBrush
{
	uint32_t clr;
	
	ImagePointBrush(uint32_t clr = 0): clr(clr) {}
	void apply(ImageInfo& img, vec2i at) override;
};

void draw_line(ImageInfo& img, vec2i p0, vec2i p1, ImageBrush& brush);
void fill_rect(ImageInfo& img, Rect r, ImageBrush& brush);
void draw_circle(ImageInfo& img, vec2i c, int r, ImageBrush& brush);
void fill_circle(ImageInfo& img, vec2i c, int r, ImageBrush& brush);



struct ImageGlowGen
{
	// NOTE: glow doesn't work, see TODO in glowify()
	
	struct Shape
	{
		std::vector<std::vector<vec2fp>> lines; ///< Line segment
		FColor clr; ///< Alpha may be > 1
	};
	
	enum Mode
	{
		M_EVEN,
		M_NOISY
	};
	
	int maxrad = 6; ///< Max glow radius (pixels)
	Mode mode = M_EVEN;
	float glow_k = 1; ///< Glow brightness
	
	std::vector<Shape> shs;
	
	/// Generates RGBA image fitted to specified size
	ImageInfo gen(vec2i size_limit, bool reset_shapes = true);
	
private:
	std::vector<uint16_t> px;
	
	void render(vec2i size, float k, vec2fp off);
	void glowify(ImageInfo& img);
};



/// Non-diagonal straight line, [p0; p1]
struct BresenhamLine
{
	BresenhamLine() = default;
	BresenhamLine(vec2i p0, vec2i p1);
	void init(vec2i p0, vec2i p1);
	std::optional<vec2i> step(); ///< Returns next point, if any left
	
private:
	vec2i c0, c1, d, s;
	int e;
	bool fin = true;
};



/// Integer circle
struct BresenhamCircle
{
	BresenhamCircle() = default;
	BresenhamCircle(vec2i c, int r);
	void init(vec2i c, int r);
	std::optional<std::array<vec2i,4>> step(); ///< Returns next points in four quadrants, if any left
	/*	1 0   II  I
	    2 3   III IV  */
	std::optional<vec2i> step_offset(); ///< Returns non-offseted next point (second quadrant), if any left
	
private:
	vec2i offset;
	int x, y, r, err;
	bool fin = true;
};

#endif // IMAGE_UTILS_HPP
