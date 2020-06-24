#include "client/effects.hpp"
#include "client/presenter.hpp"
#include "core/hard_paths.hpp"
#include "game/game_core.hpp"
#include "game/game_info_list.hpp"
#include "game/game_mode.hpp"
#include "game/level_ctr.hpp"
#include "game/level_gen.hpp"
#include "utils/image_utils.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_log.hpp"
#include "objs_basic.hpp"
#include "objs_creature.hpp"
#include "spawners.hpp"
#include "weapon_all.hpp"



class EDeathRay final : public Entity
{
	EC_VirtualBody phy;
	bool furnace;
	TimeSpan reload_tmo;
	float dir_shift = 0;
	
	void step() override;
	
public:
	EDeathRay(GameCore& core, vec2fp pos, float angle, bool furnace);
	EC_Position& ref_pc() override {return phy;}
};
EDeathRay::EDeathRay(GameCore& core, vec2fp pos, float angle, bool furnace)
    :
	Entity(core),
	phy(*this, Transform{pos - vec2fp(GameConst::cell_size /2, 0).rotate(angle), angle}),
	furnace(furnace)
{
	add_new<EC_RenderModel>(MODEL_BOLTER, FColor(1, 0.3, 0, 1.2));
	if (furnace) add_new<EC_Uberray>().clr = FColor(1, 0.3, 0, 5);
	reg_this();
}
void EDeathRay::step()
{
	vec2fp dir = {1, 0};
	dir.rotate(phy.get_angle() + dir_shift);
	dir_shift = deg_to_rad(15) * clampf_n(dir_shift + 0.5 * core.get_random().range_n2() * core.time_mul);
	
	const float distance = GameConst::cell_size * 4;
	auto dmg = [&](int amount)
	{
		auto p = StdProjectile::multiray(core, get_pos() + 0.5 * dir, dir,
		[&](auto& hit, auto){
			StdProjectile::Params pars;
			pars.dq.amount = amount;
			pars.imp = furnace ? 25 : 160;
			StdProjectile::explode(core, TEAM_ENVIRON, index, conv(dir), hit, pars);
		}
		, 1.5, distance, 0, index);
		
		if (!p) p = phy.get_pos() + dir * distance;
		return *p;
	};
	
	if (furnace) {
		auto p = dmg(200 * core.time_mul);
		ensure<EC_Uberray>().trigger(p);
	}
	else if (reload_tmo.is_positive()) reload_tmo -= core.step_len;
	else {
		auto p = dmg(100);
		effect_lightning(get_pos(), p, EffectLightning::Straight, TimeSpan::seconds(1.5));
		reload_tmo += TimeSpan::seconds(2);
	}
}



class ETutorialScript : public Entity
{
	EVS_SUBSCR;
	EC_Physics phy;
	
	void step() override;
	void on_cnt(const CollisionEvent& ev);
	
public:
	ETeleport* tele1 = {};
	ETeleport* tele2 = {};
	vec2fp spawn = {};
	
	ETutorialScript(GameCore& core, vec2fp at);
	EC_Position& ref_pc() override {return phy;}
};
ETutorialScript::ETutorialScript(GameCore& core, vec2fp at)
	: Entity(core), phy(*this, bodydef(at, false))
{
	phy.add(FixtureCreate::box(fixtsensor(), vec2fp::one(GameConst::cell_size * 1.5), 0));
	EVS_CONNECT1(phy.ev_contact, on_cnt);
	reg_this();
}
void ETutorialScript::step()
{
	int num = 0;
	for (auto& t : core.get_info().get_teleport_list()) {
		if (t.discovered) ++num;
	}
	if (num >= 2) {
		tele1->activate(false);
		core.get_lc().add_spawn({LevelControl::SP_PLAYER, spawn});
		unreg_this();
	}
}
void ETutorialScript::on_cnt(const CollisionEvent& ev)
{
	if (ev.type == CollisionEvent::T_BEGIN && ev.other->is_creature()) {
		tele2->activate(false);
		destroy();
	}
}



class ETutorialFinal final : public EInteractive
{
	EC_Physics phy;
	
public:
	ETutorialFinal(GameCore& core, vec2fp at);
	EC_Position& ref_pc() override {return phy;}
	
	std::pair<bool, std::string> use_string() override;
	void use(Entity* by) override;
};
ETutorialFinal::ETutorialFinal(GameCore& core, vec2fp at)
    :
	EInteractive(core),
    phy(*this, bodydef(at, false))
{
	phy.add(FixtureCreate::circle( fixtdef(0.5, 0), GameConst::hsz_termfin, 0,
	                               FixtureInfo{FixtureInfo::TYPEFLAG_INTERACTIVE | FixtureInfo::TYPEFLAG_OPAQUE}));
	add_new<EC_RenderModel>(MODEL_TERMINAL_FIN, FColor(1, 0.8, 0.4));
	ui_descr = "Control terminal";
}
std::pair<bool, std::string> ETutorialFinal::use_string()
{
	return {true, "Exit simulation"};
}
void ETutorialFinal::use(Entity*)
{
	dynamic_cast<GameMode_Tutorial&>(core.get_gmc()).terminal_use();
}



struct TutData
{
	// colors are RGB
	struct Room
	{
		std::array<uint32_t, 4> marker;
		Rect area;
		std::vector<std::pair<vec2fp, uint32_t>> ps; // global coords
	};
	
	std::array<Room, 4> rooms;
	std::vector<vec2i> free;
	std::vector<std::pair<vec2fp, uint32_t>> texts;
	vec2i size;
	
	TutData()
	{
		rooms[0].marker = {0xff0000, 0x00ff00, 0x00ff00};
		rooms[1].marker = {0xff0000, 0x00ff00, 0xff0000};
		rooms[2].marker = {0xff0000, 0x00ff00, 0x0000ff};
		rooms[3].marker = {0xff0000, 0x0000ff, 0x0000ff};
		
		ImageInfo img;
		if (!img.load(HARDPATH_TUTORIAL_LVL, ImageInfo::FMT_RGB))
			throw std::runtime_error("Tutorial loader: failed");
		
		size = img.get_size();
		size.y /= 2;
		
		for (auto& room : rooms)
		{
			auto find_marker = [&](int y0) -> vec2i {
				int i=0;
				for (int y=y0; y < size.y; ++y)
				for (int x=0;  x < size.x; ++x)
				{
					if (img.get_pixel_fast({x,y}) == room.marker[i]) {
						++i;
						if (i == 3) return {x,y};
					}
					else i = 0;
				}
				throw std::runtime_error("Tutorial loader: no room marker");
			};
			vec2i p0 = find_marker(0);
			vec2i p1 = find_marker(p0.y + 1);
			room.area.set({p0.x - 2, p0.y + 1}, {p1.x + 1, p1.y - 1}, false);

			room.area.map([&](vec2i p){
				uint32_t val = img.get_pixel_fast(p);
				if (val && val != 0xffffff) {
					free.push_back(p);
					room.ps.emplace_back(LevelControl::to_center_coord(p), val);
				}
			});
		}
		
		for (int y=0; y < size.y; ++y)
		for (int x=0; x < size.x; ++x)
		{
			vec2i p = {x, y};
			if (img.get_pixel_fast(p) == 0xffffff)
				free.push_back(p);
			
			uint32_t val = img.get_pixel_fast({x, y + size.y});
			if (val != 0 && val != 0xffffff)
				texts.emplace_back(LevelControl::to_center_coord({p.x, p.y}), val & 0xff);
		}
	}
};
static std::unique_ptr<TutData> tut_dat;

LevelTerrain* tutorial_terrain()
{
	if (!tut_dat) tut_dat.reset(new TutData);
	auto& dat = *tut_dat;
	
	auto lt = new LevelTerrain;
	lt->grid_size = dat.size;
	lt->cs.resize(lt->grid_size.area());
	
	auto lt_cref = [&](vec2i pos) -> auto& {return lt->cs[pos.y * lt->grid_size.x + pos.x];};
	
	lt->rooms.reserve(dat.rooms.size());
	for (auto& rr : dat.rooms)
	{
		auto& rm = lt->rooms.emplace_back();
		rm.area = rr.area;
		rm.type = LevelTerrain::RM_CONNECT;
		
		rm.area.map([&](vec2i p){
			lt_cref(p).room = &rm;
		});
	}
	
	for (auto& p : dat.free)
		lt_cref(p).is_wall = false;
	
	lt->ls_grid = lt->gen_grid();
	lt->ls_wall = lt->vectorize();
	return lt;
}
void tutorial_spawn(GameCore& core, LevelTerrain& lt)
{
	auto wall = new EWall(core, lt.ls_wall);
	lightmap_spawn(core, HARDPATH_TUTORIAL_LVL_LMAP, *wall);
	
	/// Square grid non-diagonal directions (-x, +x, -y, +y)
	const std::array<vec2i, 4> sg_dirs{{{-1,0}, {1,0}, {0,-1}, {0,1}}};
	auto& rnd = core.get_random();
	auto lt_cref = [&](vec2i pos) -> auto& {return lt.cs[pos.y * lt.grid_size.x + pos.x];};
	
	auto& dat = *tut_dat;
	ETutorialScript* script = nullptr;
	
	auto init_r0 = [&](size_t i_room){
		for (auto& pt : dat.rooms[i_room].ps)
		{
			vec2fp pos = pt.first;
			vec2i at = LevelControl::to_cell_coord(pos);
			
			auto door = [&](bool player){
				vec2i ext;
				if (lt_cref({at.x + 1, at.y}).is_wall) ext = {1, 0};
				else ext = {0, 1};
				new EDoor(core, at, ext, {1,1}, player);
			};
			auto get_rot = [&]() -> std::optional<float>
			{
				std::vector<float> rots;
				for (int i=0; i<4; ++i) {
					auto& c = lt_cref(at + sg_dirs[i]);
					if (c.is_wall && !c.decor_used) {
						switch (i) {
						case 0: rots.push_back(M_PI); break;
						case 1: rots.push_back(0); break;
						case 2: rots.push_back(-M_PI/2); break;
						case 3: rots.push_back(M_PI/2); break;
						}
					}
				}
				if (rots.empty()) return {};
				return rnd.random_el(rots);
			};
			
			switch (pt.second)
			{
			case 0x00ffff: // teleport
				if (i_room == 0) {
					script = new ETutorialScript(core, pos);
					script->tele1 = new ETeleport(core, pos);
					
					for (auto& pt : dat.rooms[0].ps) {
						if (pt.second == 0x00ff00)
							script->spawn = pt.first;
					}
				}
				else new ETeleport(core, pos);
				break;
				
			case 0x00ff00: // player
				if (i_room != 0) core.get_lc().add_spawn({LevelControl::SP_PLAYER, pos});
				break;
				
			case 0xff0000: // death ray
				new EDeathRay(core, pos, M_PI_2, true);
				break;
				
			case 0xff8000: // death pulse
				new EDeathRay(core, pos, M_PI_2, false);
				break;
				
			case 0xffff00: // dummy
				new ERespawnFunc(core, pos, [core = &core, pos = pos]{return new ETutorialDummy(*core, pos);});
				break;
				
			case 0xff00ff: // ammo
				new EPickable(core, pos, EPickable::rnd_ammo(core));
				break;
				
			case 0xff80ff: // armor
				new EPickable(core, pos, EPickable::ArmorShard{60});
				break;
				
			case 0xc0c0c0: // dispenser
				new EDispenser(core, pos, get_rot().value_or(0), true);
				break;
				
			case 0x8080ff: // minidock
				new EMinidock(core, pos, get_rot().value_or(0));
				break;
				
			case 0x808080: // door
				door(true);
				break;
				
			case 0x0000ff: // turret
				new ERespawnFunc(core, pos, [core = &core, pos = pos]{return new ETurret(*core, pos, TEAM_BOTS);});
				break;
				
			default:
				THROW_FMTSTR("Tutorial loader: invalid color at {}:{} (0-based)", at.x, at.y);
			}
		}
	};
	init_r0(0);
	init_r0(2);
	init_r0(3);
	
	int num_docks  = 2;
	int num_shards = 2;
	int num_disps  = 3;
	
	for (auto& pt : dat.rooms[1].ps)
	{
		vec2fp pos = pt.first;
		vec2i at = LevelControl::to_cell_coord(pos);
		
		auto door = [&](bool player){
			vec2i ext;
			if (lt_cref({at.x + 1, at.y}).is_wall) ext = {1, 0};
			else ext = {0, 1};
			new EDoor(core, at, ext, {1,1}, player);
		};
		auto get_rot = [&]() -> std::optional<float>
		{
			std::vector<float> rots;
			for (int i=0; i<4; ++i) {
				auto& c = lt_cref(at + sg_dirs[i]);
				if (c.is_wall && !c.decor_used) {
					switch (i) {
					case 0: rots.push_back(M_PI); break;
					case 1: rots.push_back(0); break;
					case 2: rots.push_back(-M_PI/2); break;
					case 3: rots.push_back(M_PI/2); break;
					}
				}
			}
			if (rots.empty()) return {};
			return rnd.random_el(rots);
		};
		
		switch (pt.second)
		{
		case 0x00ffff: // teleport
			if (script) script->tele2 = new ETeleport(core, pos);
			break;
			
		case 0xff00ff: // objective
			new ETutorialFinal(core, pos);
			break;
			
		case 0x00ff00: // player door
			door(true);
			break;
			
		case 0x0000ff: // common door
			door(false);
			break;
			
		case 0xffff00: // random resource
			if (auto rot = get_rot(); rot && num_docks && num_disps) {
				if (num_docks) {
					--num_docks;
					new EMinidock(core, pos, *rot);
				}
				else {
					--num_disps;
					new EDispenser(core, pos, *rot, true);
				}
			}
			else {
				if (num_shards) {
					--num_shards;
					new EPickable(core, pos, EPickable::ArmorShard{40});
				}
				else new EPickable(core, pos, EPickable::rnd_ammo(core));
			}
			break;
			
		case 0xff0000: // worker
			new EEnemyDrone(core, pos, EEnemyDrone::def_workr(core));
			break;
			
		case 0xff8000: // drone
			{
				auto init = EEnemyDrone::def_drone(core);
				for (auto& pt : dat.rooms[1].ps) {
					if (pt.second == 0xffc080)
						init.patrol.push_back(pt.first);
				}
				new EEnemyDrone(core, pos, std::move(init));
			}
			break;
			
		case 0xffc080: // patrol point
			break;
			
		case 0x808080: // storage
			new EStorageBox(core, pos);
			for (auto& d : sg_dirs)
			{
				if (lt_cref(at + d).is_wall) continue;
				vec2fp wp = pos + vec2fp(d) * GameConst::cell_size;
				new AI_SimResource(core, {AI_SimResource::T_ROCK, false, 0, AI_SimResource::max_capacity},
				                   wp, pos);
			}
			break;
			
		case 0xc0c0c0: // rock
			{
				vec2fp vp = pos + vec2fp(GameConst::cell_size, 0).fastrotate(get_rot().value_or(0));
				new AI_SimResource(core, {AI_SimResource::T_ROCK, true,
										  AI_SimResource::max_capacity, AI_SimResource::max_capacity},
										  pos, vp);
			}
			break;
			
		default:
			THROW_FMTSTR("Tutorial loader: invalid color at {}:{} (0-based coords)", at.x, at.y);
		}
	}
	
	std::unordered_map<int, const char*> ms =
	{
	    {1, "Use W,A,S,D keys to move.\n"
			"Aim with mouse.\n"
			"Hold CTRL to look farther."},
	    
	    {2, "Use teleporter ring\n"
			"standing close to it."},
	    
	    {3, "Hold SPACE to move faster."},
	    
	    {4, "Those glowing rays inflict DAMAGE.\n"
			"You can see your health stats in top left corner.\n"
			"Armor decreases received damage, breaking down in the process.\n"
			"Shields fully absorb damage and slowly regenerating."},
	    
	    {5, "Damage and shooting stop unlimited acceleration.\n"
			"It will restore after some time, but for a while\n"
	        "you'll able to use it only in short bursts.\n"
			"You can see acceleration bar near hit points' one."},
	    
	    {6, "Press 'G' to switch projected shield on/off.\n"
			"It's tougher than normal shields, but protects\n"
			"only front area and slows you down."},
	    
	    {7, "Healing stations restore your health and shields\n"
			"If you're not receiving damage for a few seconds."},
	    
	    {8, "Dummy. Displays amount of damage on being hit."},
	    
	    {9, "Hold 'TAB' to show info about nearby objects.\n"},
	    
	    // 10
	    
	    {11, "Ammo packs"},
	    
	    {12, "Ammunition also can be restored by using dispensers.\n"
			 "Each contains limited amount of ammo packs."},
	    
	    {13, "All weapons have two fire modes.\n"
			 "Press Left  mouse button to fire in primary   mode.\n"
			 "Press Right mouse button to fire in secondary mode.\n"
			 "Switch weapons with mouse wheel or 1-6 keys.\n"
			 "Some weapons can be charged before shot - hold\n"
			 "fire button to charge, release to shoot."},
	    
	    {14, "You can also damage enemies by ramming them.\n"
			 "It works only on high speeds (with acceleration).\n"
			 "Successful ramming restores acceleration bar to full\n"
			 "and partially resore shield."},
	    
	    {15, "That's automated turret. You can just run past it.\n"
			 "If you do it while it's looking in opposite direction,\n"
			 "it won't see you."},
	    
	    {16, "Level map can be viewed by pressing 'M'.\n"
	         "Hold TAB while looking to highlight visited rooms.\n"
			 "Drag with mouse to scroll."},
	    
	    {17, "There's another teleport - but you can't use it\n"
			 "until you've activated it. To do that,\n"
	         "simply walk up to it until it lits up."},
	    
	    {18, "If you activated both teleports, new area is now available."},
	    
	    {19, "Weapon test area. Get familiar with weapons, modes and\n"
	         "their side effects before moving on.\n"
			 "When you're done, next stage is available with teleporter.\n\n"
			 "Reminder: left/right button to shoot in primary/secondary mode,\n"
			 "mouse wheel or 1-6 keys to change current weapon,\n"
			 "charge shots where possible by holding fire button.\n\n"
			 "Press 'R' to switch laser designator."},
	    
	    {20, "Simulation of real combat conditions.\n"
	         "Get to the small room on the other side and activate control terminal.\n"
			 "Your current objective is always written in bottom-left corner."},
	    
	    {21, "Most of the time hostile units are busy with their\n"
	         "own tasks and do not actively search for you.\n"
			 "You can use that to sneak past them undetected."},
	    
	    {22, "You're close!"},
	    
	    {23, "Shortcut to the next training stage"}
	};
	for (auto& t : dat.texts) {
		auto it = ms.find(t.second);
		if (it != ms.end()) core.get_info().get_tut_messages().emplace_back(t.first, it->second);
		else {
			vec2i at = LevelControl::to_cell_coord(t.first);
			THROW_FMTSTR("Tutorial loader: invalid message index at {}:{} (0-based coords)", at.x, at.y);
		}
	}
}
