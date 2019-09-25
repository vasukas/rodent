#include "game/game_utils.hpp"
#include "game/level_gen.hpp"
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "render/texture.hpp"
#include "utils/res_image.hpp"
#include "level_map.hpp"



class LevelMap_Impl : public LevelMap
{
public:
	std::unique_ptr<Texture> tex;
	std::optional<ImageInfo> img;
	vec2fp coord_k;
	
	SmoothSwitch e_sw;
	
	
	
	LevelMap_Impl(const LevelTerrain& lt)
	{
		e_sw.set(TimeSpan::seconds(0.15));
		coord_k = vec2fp::one(1) / (vec2fp(lt.grid_size) * lt.cell_size);
		
		img = lt.draw_grid();
		img->convert(ImageInfo::FMT_RGBA);
		img->map_pixel([](auto px)
		{
			if (px[0])
			{
				px[0] = std::min(255., px[0] * 0.3);
				px[1] = std::min(255., px[1] * 0.9);
				px[2] = std::min(255., px[2] * 2.);
				px[3] = 255;
			}
			else {
				px[0] = px[1] = px[2] = 0;
				px[3] = 192;
			}
		});
	}
	void ren_init()
	{
		tex.reset( Texture::create_from(*img, Texture::FIL_LINEAR_MIPMAP) );
		img.reset();
	}
	void draw(TimeSpan passed, std::optional<vec2fp> plr_p, bool enabled)
	{
		e_sw.step(passed, enabled);
		const int a = 255 * e_sw.value();
		if (!a) return;
		
		vec2i scr = RenderControl::get_size() /2;
		vec2fp sz = tex->get_size();
		
		vec2fp sp = scr;
		if (plr_p) {
			plr_p = map_coord(*plr_p);
			sp -= (*plr_p - vec2fp::one(0.5)) * sz;
		}
		
		Rectfp dst = Rectfp::from_center(sp, sz);
		RenImm::get().draw_image(dst, tex.get(), 0xffffff00 | a);
		
		if (plr_p)
		{
			vec2fp p = dst.size() * (*plr_p) + dst.lower();
			RenImm::get().draw_circle(p, 9.f, 0x40c0ff00 | a, 32);
			RenImm::get().draw_circle(p, 6.f, 0xc0f0ff00 | a, 32);
		}
	}
	
	vec2fp map_coord(vec2fp world) const {return world * coord_k;}
};



static LevelMap* rni;
LevelMap* LevelMap::init(const LevelTerrain& lt) {return rni = new LevelMap_Impl (lt);}
LevelMap* LevelMap::get() {return rni;}
LevelMap::~LevelMap() {rni = nullptr;}
