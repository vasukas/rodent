#include <memory>
#include "image_utils.hpp"
#include "noise.hpp"



void downscale_2x(ImageInfo& img)
{
	vec2i sz = img.get_size() /2;
	int bpp = img.get_bpp();
	
	for (int y=0; y < sz.y; ++y)
	for (int x=0; x < sz.x; ++x)
	{
		uint8_t out[ImageInfo::max_bpp_value] = {};
		
		for (int i=0; i<2; ++i)
		for (int j=0; j<2; ++j)
		{
			uint8_t* src = img.get_pixel_ptr_fast({ x*2 + i, y*2 + j });
			for (int k=0; k < bpp; ++k)
				out[k] += src[k] / 4;
		}
		
		uint8_t* dst = img.raw() + (y * sz.x + x) * bpp;
		for (int k=0; k < bpp; ++k)
			dst[k] = out[k];
	}
	
	img.reset(sz, {}, true);
}



void ImagePointBrush::apply(ImageInfo& img, vec2i at)
{
	if (img.is_in_bounds(at))
	{
		uint8_t *p = img.get_pixel_ptr_fast(at);
		int bpp = img.get_bpp();
		for (int i=0; i < bpp; ++i)
			p[i] = (clr >> 8 * (bpp - i - 1));
	}
}
void draw_line(ImageInfo& img, vec2i p0, vec2i p1, ImageBrush& brush)
{
	BresenhamLine g(p0, p1);
	while (auto p = g.step())
		brush.apply(img, *p);
}
void fill_rect(ImageInfo& img, Rect r, ImageBrush& brush)
{
	r.map([&](vec2i p){ brush.apply(img, p); });
}



ImageInfo ImageGlowGen::gen(vec2i size_limit, bool reset_shapes)
{
	// get min&max coords
	
	vec2fp m0 = vec2fp::one(std::numeric_limits<float>::max());
	vec2fp m1 = vec2fp::one(std::numeric_limits<float>::lowest());
	
	for (auto& s : shs)
	{
		for (auto& c : s.lines)
		{
			if (c.size() < 2) continue;
			
			size_t i = c.front().equals( c.back(), 1e-15 );
			for (; i < c.size(); ++i)
			{
				m0 = min(m0, c[i]);
				m1 = max(m1, c[i]);
			}
		}
	}
	m1 += vec2fp::one(0.1);
	
	// calc sizes and init
	
	vec2i off = vec2i::one(maxrad + 1);
	vec2i size = size_limit - off * 2;
	
	vec2fp k_ar = vec2fp(size) / (m1 - m0);
	float k = std::min(k_ar.x, k_ar.y);
	
	ImageInfo img;
	img.reset( ((m1 - m0) * k).int_ceil() + off*2 );
	
	// proc
	
	render(img.get_size() - off*2, k, -m0);
	glowify(img);
	
	if (reset_shapes) shs.clear();
	return img;
}
void ImageGlowGen::render(vec2i size, float k, vec2fp off)
{
	px.clear();
	px.resize( size.area() * 4 );
	
	for (auto& s : shs)
	{
		uint16_t src[4];
		for (int i=0; i<4; ++i)
			src[i] = s.clr[i] * 255;
		
		for (auto& c : s.lines)
		{
			auto prev = ((c.front() + off) * k).int_round();
			for (size_t i=1; i < c.size(); ++i)
			{
				auto p = ((c[i] + off) * k).int_round();
				
				BresenhamLine g(prev, p);
				while (auto p = g.step())
				{
					if (p->x < 0 || p->y < 0 || p->x >= size.x || p->y >= size.y)
						continue;
					
					uint16_t* dst = px.data() + (p->y * size.x + p->x) * 4;
					for (int i=0; i<4; ++i)
						dst[i] += src[i];
				}
				
				prev = p;
			}
		}
	}
}
void ImageGlowGen::glowify(ImageInfo& res)
{
	// setup
	
	struct Calc
	{
		const ImageGlowGen* tr;
		
		virtual ~Calc() = default;
		virtual void rad_calc() {}
		virtual float fact(float dist) = 0;
	};
	std::unique_ptr<Calc> c;
	
	switch (mode)
	{
		// even radiant glow
		case M_EVEN:
		{
			struct CalcImpl : Calc
			{
				float fact(float dist) {
					return (1. - dist / (tr->maxrad * tr->maxrad) / 2.5);
				}
			};
			c.reset(new CalcImpl);
		}
		break;
		
		// old glow from neondrv
		case M_NOISY:
		{
			struct CalcImpl : Calc
			{
				float dt_r;
				void rad_calc() {
					dt_r = tr->maxrad * (.9 + rnd_stat().range(-1, 1) * 0.05);
				}
				float fact(float dist) {
					float r = dt_r * (1. + rnd_stat().range(-1, 1) * 0.02);
					return (1. - dist / (r*r)) / 3.5;
				}
			};
			c.reset(new CalcImpl);
		}
		break;
	}
	
	if (!c) throw std::runtime_error("ImageGlowGen::gen() invalid enum");
	c->tr = this;
	
	// glowify
	
	const int adj_bright = 0x20; // how much adjacent colors become brighter on color overflow
	
	vec2i off = vec2i::one(maxrad + 1);
	vec2i or_sz = res.get_size() - off * 2;
	
	// for each pixel of original image
	for (int oy = 0; oy < or_sz.y; ++oy)
	for (int ox = 0; ox < or_sz.x; ++ox)
	{
		vec2i at = vec2i{ox, oy} + off;
		
		// skip if empty
		uint16_t *src = px.data() + (oy * or_sz.x + ox) * 4;
		if (!src[3]) continue;
		
		// calculate glow zone
		vec2i z0 = max(at - off, {});
		vec2i z1 = min(at + off, res.get_size() - vec2i::one(1));
		
		// per-point glow transform
		c->rad_calc();
		
		// for each (output) pixel in zone
		for (int cy = z0.y; cy <= z1.y; cy++)
		for (int cx = z0.x; cx <= z1.x; cx++)
		{
			float dy = (cy - at.y) * 0.85; // yet another magic value
			float dx = cx - at.x;
			
			// calculate glow factor based on distance
			double f = c->fact (std::sqrt(dx*dx + dy*dy));
			if (f <= 0.) continue;
			f *= glow_k;
			
			uint8_t *dst = res.get_pixel_ptr_fast({cx, cy});
			
			// for each color
			for (int i=0; i<3; i++)
			{
				uint16_t clr = src[i] * f;
				clr += dst[i];
				dst[i] = clr < 0xff? clr : 0xff;
				
				// overflow makes adjacent colors more bright
				if (clr > 0xff) {
                    for (int j=1; j<3; j++) {
						uint16_t nc = dst[(i+j)%3] + adj_bright;
						dst[(i+j)%3] = nc < 0xff? nc : 0xff;
                    }
				}
			}
			
			// alpha
			uint16_t clr = src[3] * f;
			clr += dst[3];
			dst[3] = clr < 0xff? clr : 0xff;
		}
	}
}



BresenhamLine::BresenhamLine(vec2i p0, vec2i p1)
{
	init(p0, p1);
}
void BresenhamLine::init(vec2i p0, vec2i p1)
{
	c0 = p0, c1 = p1;
	d.x = std::abs(c1.x - c0.x);
	d.y = std::abs(c1.y - c0.y);
	s.x = c0.x < c1.x ? 1 : -1;
	s.y = c0.y < c1.y ? 1 : -1;
	e = d.x - d.y;
	fin = false;
}
std::optional<vec2i> BresenhamLine::step()
{
	if (fin) return {};
	vec2i ret = c0;
	
	if (c0 == c1) fin = true;
	else
	{
		int e2 = e*2;
		if (e2 + d.y > d.x - e2) {
			e -= d.y;
			c0.x += s.x;
		}
		else {
			e += d.x;
			c0.y += s.y;
		}
	}
	
	return ret;
}
