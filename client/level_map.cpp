#include "game/level_gen.hpp"
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "render/texture.hpp"
#include "utils/res_image.hpp"
#include "utils/time_utils.hpp"
#include "level_map.hpp"



class LevelMap_Impl : public LevelMap
{
public:
	std::unique_ptr<Texture> tex;
	vec2fp coord_k;
	
	SmoothSwitch e_sw;
	
	Rectfp final_term;
	bool final_term_marked = false;
	
	
	
	LevelMap_Impl(const LevelTerrain& lt)
	{
		e_sw.reset(TimeSpan::seconds(0.15));
		coord_k = vec2fp::one(1) / (vec2fp(lt.grid_size) * lt.cell_size);
		
		ImageInfo img = lt.draw_grid(false);
		img.convert(ImageInfo::FMT_RGBA);
		img.map_pixel([](auto px)
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
		
		for (auto& r : lt.rooms) {
			if (r.type == LevelTerrain::RM_TERMINAL) {
				final_term = r.area;
				final_term.a *= vec2fp::one(2 * lt.cell_size);
				final_term.b *= vec2fp::one(2 * lt.cell_size);
				break;
			}
		}
		
		RenderControl::get().exec_task([this, img = std::move(img)]{
			tex.reset( Texture::create_from(img, Texture::FIL_LINEAR_MIPMAP) );
		});
	}
	void draw(TimeSpan passed, std::optional<vec2fp> plr_p, bool enabled)
	{
		e_sw.step(passed, enabled);
		float t_alpha = e_sw.value();
		const int a = 255 * t_alpha;
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
		
		if (final_term_marked)
		{
			Rectfp r = final_term;
			r.a += dst.lower();
			r.b += dst.lower();
			RenImm::get().draw_rect(r, 0x00ff4000 | int(64 * t_alpha));
		}
		
		if (plr_p)
		{
			vec2fp p = dst.size() * (*plr_p) + dst.lower();
			RenImm::get().draw_circle(p, 9.f, 0x40c0ff00 | a, 32);
			RenImm::get().draw_circle(p, 6.f, 0xc0f0ff00 | a, 32);
		}
	}
	void mark_final_term()
	{
		final_term_marked = true;
	}
	
	vec2fp map_coord(vec2fp world) const {return world * coord_k;}
};



static LevelMap* rni;
LevelMap* LevelMap::init(const LevelTerrain& lt) {return rni = new LevelMap_Impl (lt);}
LevelMap& LevelMap::get() {return *rni;}
LevelMap::~LevelMap() {rni = nullptr;}
