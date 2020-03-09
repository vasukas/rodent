#include "game/game_info_list.hpp"
#include "game/level_ctr.hpp"
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
	SmoothSwitch e_sw;
	std::unique_ptr<Texture> tex;
	vec2fp coord_k;
	
	std::optional<Rectfp> final_term;
	TimeSpan final_marked_at;
	
	
	LevelMap_Impl(const LevelTerrain& lt)
		: e_sw(TimeSpan::seconds(0.15))
	{
		coord_k = vec2fp::one(1) / (vec2fp(lt.grid_size) * GameConst::cell_size);
		
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
				px[0] = px[1] = px[2] = px[3] = 0;
			}
		});
		
		RenderControl::get().exec_task([this, img = std::move(img)]{
			tex.reset( Texture::create_from(img, Texture::FIL_LINEAR_MIPMAP) );
		});
	}
	Rectfp draw_map(float t_alpha, std::optional<vec2fp> plr_pos, float scale, vec2fp offset)
	{
		const int clr_a = 255 * t_alpha;
		
		vec2i scr = RenderControl::get_size() /2;
		vec2fp sz = tex->get_size() * scale;
		
		vec2fp sp = scr;
		if (plr_pos) {
			plr_pos = (*plr_pos) * coord_k;
			sp -= (*plr_pos - vec2fp::one(0.5)) * sz;
		}
		sp += offset;
		
		Rectfp dst = Rectfp::from_center(sp, sz);
		RenImm::get().draw_rect({{}, RenderControl::get_size(), false}, 192 * t_alpha);
		RenImm::get().draw_image(dst, tex.get(), 0xffffff00 | clr_a);
		
		if (final_term)
		{
			const float period = 0.8;
			const float num = 5.4;
			float t;
			
			TimeSpan md = TimeSpan::since_start() - final_marked_at;
			if (md > TimeSpan::seconds( period * num ))
				t = t_alpha;
			else {
				t = std::fmod(md.seconds(), period) < period/2 ? 3 : 1;
			}
			
			Rectfp r = *final_term;
			r.a = r.a * scale + dst.lower();
			r.b = r.b * scale + dst.lower() + vec2fp::one(scale * 1.05);
			RenImm::get().draw_rect(r, 0x00ff4000 | int(64 * t));
		}
		
		if (plr_pos)
		{
			vec2fp p = dst.size() * (*plr_pos) + dst.lower();
			RenImm::get().draw_circle(p, 9.f, 0x40c0ff00 | clr_a, 32);
			RenImm::get().draw_circle(p, 6.f, 0xc0f0ff00 | clr_a, 32);
		}
		
		return dst;
	}
	void draw(TimeSpan passed, std::optional<vec2fp> plr_p, bool enabled)
	{
		e_sw.step(passed, enabled);
		float t_alpha = e_sw.value();
		if (t_alpha > 1.f/256)
			draw_map(t_alpha, plr_p, 1, {});
	}
	const TeleportInfo* draw_transit(vec2i cur_pos, const TeleportInfo* cur, const std::vector<TeleportInfo>& teleps)
	{
		auto [scale, offset] = fit_rect(tex->get_size() *2, RenderControl::get_size());
		float k_cell = 2 * GameConst::cell_size * scale;
		
		Rectfp dst = draw_map(1, {}, scale, offset);
		vec2i cur_cell = ((vec2fp(cur_pos) - dst.lower()) / k_cell).int_floor();
		
		const TeleportInfo* hover = nullptr;
		for (auto& t : teleps)
		{
			if (!t.discovered) continue;
			auto& room = t.room;
			
			uint32_t clr;
			if (&t == cur) {
				clr = room.area.contains_le(cur_cell) ? 0xb080'0000 : 0x6040'0000;
			}
			else if (room.area.contains_le(cur_cell)) {
				clr = 0xc0ff'ff80;
				hover = &t;
			}
			else clr = 0x6080ff00;
			
			Rectfp r = room.area.to_fp(k_cell);
			r.a += dst.lower();
			r.b += dst.lower() + vec2fp::one(scale * 1.05);
			RenImm::get().draw_rect(r, clr | 192);
			
			RenImm::get().draw_text(r.center(), room.name, 0xffc0'00ff, true);
		}
		return hover;
	}
	void mark_final_term(const LevelCtrRoom& rm)
	{
		if (!final_term) {
			final_term = rm.area.to_fp(2 * GameConst::cell_size);
			final_marked_at = TimeSpan::since_start();
		}
	}
};



static LevelMap* rni;
LevelMap* LevelMap::init(const LevelTerrain& lt) {return rni = new LevelMap_Impl (lt);}
LevelMap& LevelMap::get() {return *rni;}
LevelMap::~LevelMap() {rni = nullptr;}
