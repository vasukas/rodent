#include <unordered_set>
#include "game/game_core.hpp"
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
	
	std::optional<Rectfp> final_term;
	TimeSpan final_marked_at;
	
	GameCore& core;
	std::unordered_set<const LevelCtrRoom*> visited;
	std::unique_ptr<Texture> tex_vis;
	bool upd_visited_flag = false;
	
	vec2fp prim_offset = {};
	
	
	
	LevelMap_Impl(GameCore& core, const LevelTerrain& lt)
		: e_sw(TimeSpan::seconds(0.15)), core(core)
	{
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
	Rectfp draw_map(float t_alpha, std::optional<vec2fp> plr_pos,
	                float scale, vec2fp offset,
	                bool is_primary, bool show_visited)
	{
		const int max_margin = 80;
		const int clr_a = 255 * t_alpha;
		float k_cell = 2 * GameConst::cell_size * scale;
		
		const vec2i scr = RenderControl::get_size() /2;
		const vec2fp sz = tex->get_size() * scale;
		
		vec2fp sp = scr;
		if (plr_pos) {
			plr_pos = (*plr_pos) / (vec2fp(core.get_lc().get_size()) * GameConst::cell_size);
			sp -= (*plr_pos - vec2fp::one(0.5)) * sz;
		}
		if (is_primary)
		{
			if (scr.x >= sz.x) prim_offset.x = 0;
			else if (sp.x + prim_offset.x > sz.x + max_margin) {
				prim_offset.x = (sz.x + max_margin) - sp.x;
			}
			else if (sp.x + prim_offset.x < -(sz.x - (scr.x*2 - max_margin))) {
				prim_offset.x = -(sz.x - (scr.x*2 - max_margin)) - sp.x;
			}
			
			if (scr.y >= sz.y) prim_offset.y = 0;
			else if (sp.y + prim_offset.y > sz.y + max_margin) {
				prim_offset.y = (sz.y + max_margin) - sp.y;
			}
			else if (sp.y + prim_offset.y < -(sz.y - (scr.y*2 - max_margin))) {
				prim_offset.y = -(sz.y - (scr.y*2 - max_margin)) - sp.y;
			}
			
			offset = prim_offset;
		}
		sp += offset;
		
		Rectfp dst = Rectfp::from_center(sp, sz);
		RenImm::get().draw_rect({{}, RenderControl::get_size(), false}, 192 * t_alpha);
		if (show_visited)
		{
			if (upd_visited_flag) upd_visited();
			if (tex_vis) RenImm::get().draw_image(dst, tex_vis.get(), 0x80ff4000 | int(128 * t_alpha), true);
		}
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
		
		if (is_primary)
		{
			for (const auto& t : core.get_info().get_teleport_list())
			{
				if (!t.discovered) continue;
				Rectfp r = t.room.area.to_fp(k_cell);
				r.a += dst.lower();
				r.b += dst.lower() + vec2fp::one(scale * 1.05);
				RenImm::get().draw_rect(r, 0x6080ff00 | int(192 * t_alpha));
			}
			
			std::unordered_set<const LevelCtrRoom*> asms;
			for (const auto& t : core.get_info().get_assembler_list())
			{
				auto rm = core.get_lc().get_room(t.prod_pos); // not null
				if (asms.emplace(rm).second && visited.find(rm) != visited.end())
				{
					Rectfp r = rm->area.to_fp(k_cell);
					r.a += dst.lower();
					r.b += dst.lower() + vec2fp::one(scale * 1.05);
					RenImm::get().draw_rect(r, 0xff800000 | int(80 * t_alpha));
				}
			}
		}
		
		if (plr_pos)
		{
			vec2fp p = dst.size() * (*plr_pos) + dst.lower();
			RenImm::get().draw_circle(p, 9.f, 0x40c0ff00 | clr_a, 32);
			RenImm::get().draw_circle(p, 6.f, 0xc0f0ff00 | clr_a, 32);
		}
		
		size_t n_ass = core.get_info().get_assembler_list().size();
		if (n_ass) draw_text_hud({0, -50}, "Active assembling lines detected", 0xffc0a000 | clr_a);
		else draw_text_hud({0, -50}, "No active assembling lines found", 0xa0ffa000 | clr_a);
		
		return dst;
	}
	void draw(vec2i add_offset, std::optional<vec2fp> plr_p, TimeSpan passed, bool enabled, bool show_visited) override
	{
		e_sw.step(passed, enabled);
		float t_alpha = e_sw.value();
		if (t_alpha > 1.f/256) {
			prim_offset += add_offset;
			draw_map(t_alpha, plr_p, 1, {}, true, show_visited);
		}
	}
	const TeleportInfo* draw_transit(vec2i cur_pos, const TeleportInfo* cur) override
	{
		auto [scale, offset] = fit_rect(tex->get_size() *2, RenderControl::get_size());
		float k_cell = 2 * GameConst::cell_size * scale;
		
		Rectfp dst = draw_map(1, {}, scale, offset, false, false);
		vec2i cur_cell = ((vec2fp(cur_pos) - dst.lower()) / k_cell).int_floor();
		
		const TeleportInfo* hover = nullptr;
		for (const auto& t : core.get_info().get_teleport_list())
		{
			if (!t.discovered) continue;
			auto& room = t.room;
			
			uint32_t clr;
			bool is_hov = room.area.contains_le(cur_cell);
			
			if (&t == cur) {
				clr = is_hov ? 0xb080'0000 : 0x6040'0000;
				if (is_hov) hover = &t;
			}
			else if (is_hov) {
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
	void mark_final_term(const LevelCtrRoom& rm) override
	{
		if (!final_term) {
			final_term = rm.area.to_fp(2 * GameConst::cell_size);
			final_marked_at = TimeSpan::since_start();
		}
	}
	void mark_visited(const LevelCtrRoom& rm) override
	{
		if (visited.emplace(&rm).second)
			upd_visited_flag = true;
	}
	std::vector<const LevelCtrRoom*> get_visited() override
	{
		std::vector<const LevelCtrRoom*> rs;
		rs.reserve(visited.size());
		for (auto& v : visited) rs.push_back(v);
		return rs;
	}
	void upd_visited()
	{
		upd_visited_flag = false;
		auto& lc = core.get_lc();
		
		std::vector<uint8_t> px;
		px.resize(lc.get_size().area());
		
		for (auto& v : visited)
			v->area.map([&](vec2i p) {px[p.y * lc.get_size().x + p.x] = 255;});
		
		for (int y=0; y<lc.get_size().y; ++y)
		for (int x=0; x<lc.get_size().x; ++x)
		{
			auto& c = lc.cref({x,y});
			if (!c.is_wall && !c.room_i)
			{
				if (contains(visited, lc.get_rooms().data() + c.room_nearest))
					px[y * lc.get_size().x + x] = 255;
			}
		}
		
		if (!tex_vis) tex_vis.reset(Texture::create_from(lc.get_size(), Texture::FMT_SINGLE, px.data(), Texture::FIL_NEAREST));
		else tex_vis->update_full(px.data());
	}
};



static LevelMap* rni;
LevelMap* LevelMap::init(GameCore& core, const LevelTerrain& lt) {return rni = new LevelMap_Impl (core, lt);}
LevelMap& LevelMap::get() {return *rni;}
LevelMap::~LevelMap() {rni = nullptr;}
