#include "core/hard_paths.hpp"
#include "game/game_core.hpp"
#include "game/game_mode.hpp"
#include "game/level_ctr.hpp"
#include "game/level_gen.hpp"
#include "utils/noise.hpp"
#include "utils/res_image.hpp"
#include "vaslib/vas_log.hpp"
#include "objs_basic.hpp"
#include "spawners.hpp"



struct LvlData
{
	std::vector<std::pair<vec2fp, uint32_t>> ps;
	std::vector<vec2i> free;
	vec2i size;
	
	LvlData()
	{
		ImageInfo img;
		if (!img.load(HARDPATH_SURVIVAL_LVL, ImageInfo::FMT_RGB))
			throw std::runtime_error("Survival loader: failed");
		
		size = img.get_size();
		for (int y=0; y < size.y; ++y)
		for (int x=0; x < size.x; ++x)
		{
			vec2i p = {x, y};
			uint32_t val = img.get_pixel_fast(p);
			if (val) {
				free.push_back(p);
				if (val != 0xffffff) ps.emplace_back(LevelControl::to_center_coord(p), val);
			}
		}
	}
};
static std::unique_ptr<LvlData> lvl_dat;

LevelTerrain* survival_terrain()
{
	if (!lvl_dat) lvl_dat.reset(new LvlData);
	auto& dat = *lvl_dat;
	
	auto lt = new LevelTerrain;
	lt->grid_size = dat.size;
	lt->cs.resize(lt->grid_size.area());
	
	auto lt_cref = [&](vec2i pos) -> auto& {return lt->cs[pos.y * lt->grid_size.x + pos.x];};
	for (auto& p : dat.free)
		lt_cref(p).is_wall = false;
	
	auto& rm = lt->rooms.emplace_back();
	rm.type = LevelTerrain::RM_CONNECT;
	rm.area = Rect({1,1}, lt->grid_size - vec2i::one(1), false);
	rm.area.map([&](vec2i p){
		lt_cref(p).room = &rm;
	});
	
	lt->ls_grid = lt->gen_grid();
	lt->ls_wall = lt->vectorize();
	return lt;
}
void survival_spawn(GameCore& core, LevelTerrain& lt)
{
	auto wall = new EWall(core, lt.ls_wall);
	lightmap_spawn(core, HARDPATH_SURVIVAL_LVL_LMAP, *wall);
	
	/// Square grid non-diagonal directions (-x, +x, -y, +y)
	const std::array<vec2i, 4> sg_dirs{{{-1,0}, {1,0}, {0,-1}, {0,1}}};
	auto& rnd = core.get_random();
	auto lt_cref = [&](vec2i pos)-> auto& {return lt.cs[pos.y * lt.grid_size.x + pos.x];};
	
	for (auto& pair : lvl_dat->ps)
	{
		vec2fp p = pair.first;
		vec2i cp = core.get_lc().to_cell_coord(p);
		
		auto mount_rot = [&]()-> std::optional<float> {
			int ok = -1;
			for (int i=0; i<4; ++i) {
				if (lt_cref(cp + sg_dirs[i]).is_wall)
					if (ok == -1 || rnd.flag()) ok = i;
			}
			if (ok != -1) {
				const float rots[] = {-M_PI, 0, -M_PI_2, M_PI_2};
				return rots[ok];
			}
			return {};
		};
		
		switch (pair.second)
		{
		case 0x00ff00: // player spawn
			core.get_lc().add_spawn({LevelControl::SP_PLAYER, p});
			break;
			
		case 0xff0000: // teleport
			dynamic_cast<GameMode_Survival&>(core.get_gmc()).add_teleport(p);
			new EDecorGhost(core, Transform(p, rnd.range_index(4) * M_PI_2), MODEL_TELEPAD);
			break;
			
		case 0x00ffff: // meds
			if (auto r = mount_rot())
				new EMinidock(core, p, *r);
			break;
			
		case 0xffff00: // ammo resp
			(new ERespawnFunc(core, p, [core = &core, p = p] {
				auto ap = EPickable::rnd_ammo(*core);
				ap.amount *= core->get_random().range(0.4, 1);
				return new EPickable(*core, p, ap);
			}))
				->period = TimeSpan::seconds(60);
			new EDecorGhost(core, Transform(p, rnd.range_index(4) * M_PI_2), MODEL_DOCKPAD);
			break;
		
		default:
			THROW_FMTSTR("Survival loader: invalid color at {}:{} (0-based)", cp.x, cp.y);
		}
	}	
}



const uint32_t clr_light_1 = 0xff00ff;
const uint32_t clr_light_2 = 0xff70ff; // 112
const uint32_t clr_light_3 = 0xffaaff; // 170

void lightmap_spawn(GameCore& core, const char *filename, Entity& walls)
{
	/// Square grid non-diagonal directions (-x, +x, -y, +y)
	const std::array<vec2i, 4> sg_dirs{{{-1,0}, {1,0}, {0,-1}, {0,1}}};
	const float rots[] = {-M_PI, 0, -M_PI_2, M_PI_2};
	auto& lc = core.get_lc();
	auto& rnd = core.get_random();
	
	ImageInfo img;
	if (!img.load(filename, ImageInfo::FMT_RGB)) {
		VLOGE("lightmap_spawn() failed for \"{}\"", filename);
		return;
	}
	if (img.get_size() != lc.get_size()) {
		VLOGE("lightmap_spawn() failed for \"{}\" - image size doesn't match level size", filename);
		return;
	}
	
	struct Pt {
		uint32_t val;
		vec2i pos, offset;
	};
	std::vector<Pt> lps;
	lps.reserve(1024);
	
	for (int y=0; y < img.get_size().y; ++y)
	for (int x=0; x < img.get_size().x; ++x)
	{
		vec2i p = {x, y};
		uint32_t v = img.get_pixel_fast(p);
		if (v != 0 && v != 0xfff'fff)
			lps.push_back({v, p, {}});
	}
	
	for (size_t i=0; i<lps.size(); ++i)
	for (size_t j=i+1; j<lps.size(); ++j)
	{
		if (lps[i].pos.ndg_dist(lps[j].pos) == 1)
		{
			lps[i].offset += lps[j].pos - lps[i].pos;
			lps.erase(lps.begin() + j);
			--j;
		}
	}
	
	for (auto& pt : lps)
	{
		auto make_light = [&](FColor clr, bool bright = false) {
			int ok = -1;
			for (int i=0; i<4; ++i) {
				if (lc.cref(pt.pos + sg_dirs[i]).is_wall)
					if (ok == -1 || rnd.flag()) ok = i;
			}
			if (ok != -1) {
				vec2fp pos = lc.to_center_coord(pt.pos) + pt.offset * (GameConst::cell_size/2);
				pos += vec2fp(GameConst::cell_size/2, 0).fastrotate(rots[ok]);
				walls.ensure<EC_LightSource>().add(pos, rots[ok], clr, bright ? 15 : 9);
			}
		};
		
		switch (pt.val)
		{
		case 0:
		case 0xffffff:
			break;
			
		case clr_light_1:
			make_light(FColor(1, 0.2, 0.2, 0.7));
			break;
			
		case clr_light_2:
			make_light(FColor(1, 1, 0.65, 0.7));
			break;
			
		case clr_light_3:
			make_light(FColor(0.8, 1, 1, 0.7), true);
			break;
			
		default:
			VLOGW("lightmap_spawn() - invalid color at {}:{} (0-based)", pt.pos.x, pt.pos.y);
		}
	}
}
