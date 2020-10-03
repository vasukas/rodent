// --mptest ADDR PORT ISSERV

#include <mutex>
#include <thread>
#include <queue>
#include <unordered_map>
#include "game/game_mode.hpp"
#include "utils/tcp_net.hpp"
#include "client/effects.hpp"
#include "client/presenter.hpp"
#include "client/plr_input.hpp"
#include "game/game_core.hpp"
#include "game/player_mgr.hpp"
#include "game/level_gen.hpp"
#include "game_objects/objs_basic.hpp"
#include "game_objects/objs_player.hpp"
#include "game/level_ctr.hpp"
#include "utils/image_utils.hpp"
#include "utils/serializer_defs.hpp"
#include "mptest.hpp"
#include "vaslib/vas_log.hpp"
#include "client/replay_ser.hpp"
#include "utils/noise.hpp"


// client-side
class EProxy : public Entity {
public:
    EC_VirtualBody pc;

    EProxy(GameCore& core, Transform pos, int model, uint32_t color)
	    : Entity(core), pc(*this, pos)
    {
	    add_new<EC_RenderModel>(static_cast<ModelType>(model), color, EC_RenderModel::DEATH_NONE);
		pc.pos.pos.x = -1000;
    }
    EC_Position& ref_pc() {return pc;}
};


struct EvCreate {
    uint16_t index;
    uint8_t model;
	uint32_t color;
	float x, y, r;
};
struct EvTransform {
    uint16_t index;
    float x, y, r;
    float vx, vy;
};

struct NE_Bolt {
	vec2fp a, b;
	uint8_t type;
	TimeSpan len;
	FColor clr;
};

struct NE_Parts {
	ParticleBatchPars pars;
	uint8_t model;
	uint8_t effect;
};

struct ServPacket {
    std::vector<EvCreate> created;
    std::vector<EvTransform> trs;
    std::vector<uint16_t> dels;
	std::vector<NE_Bolt> bolts;
	std::vector<NE_Parts> parts;
    
    uint16_t plr;
    static constexpr auto invalid = std::numeric_limits<uint16_t>::max();
};

SERIALFUNC_PLACEMENT_1(EvCreate,
	SER_FD(index),
	SER_FD(model),
	SER_FD(color),
	SER_FD(x),
	SER_FD(y),
	SER_FD(r));
	
SERIALFUNC_PLACEMENT_1(EvTransform,
	SER_FD(index),
	SER_FD(x),
	SER_FD(y),
	SER_FD(r),
	SER_FD(vx),
	SER_FD(vy));

SERIALFUNC_PLACEMENT_1(NE_Bolt,
	SER_FD(a),
	SER_FD(b),
	SER_FD(type),
	SER_FD(len),
	SER_FD(clr));

SERIALFUNC_PLACEMENT_1(ParticleBatchPars,
	SER_FD(tr),
	SER_FD(power),
	SER_FD(clr),
	SER_FD(rad));

SERIALFUNC_PLACEMENT_1(NE_Parts,
	SER_FD(pars),
	SER_FD(model),
	SER_FD(effect));
	
SERIALFUNC_PLACEMENT_1(ServPacket,
    SER_FDT(created, Array32),
    SER_FDT(trs, Array32),
    SER_FDT(dels, Array32),
	SER_FDT(bolts, Array32),
	SER_FDT(parts, Array32),
    SER_FD(plr));


struct Common {
    std::unique_ptr<TCP_Socket> ccon; // client-side only
	Entity* wall;
    
    struct Resp {
        EntityIndex ent;
        vec2fp pos;
        TimeSpan timeout;
		TimeSpan full_timeout;
		std::function<EntityIndex(GameCore&, vec2fp)> f;
    };
    std::vector<Resp> resps;
    
	struct Player {
		std::unique_ptr<TCP_Socket> conn;
		PlayerInput pinp;
		TimeSpan resp;
		
		Player(): pinp(PlayerInput::empty_proxy()) {
			pinp.set_context(PlayerInput::CTX_GAME);
		}
	};
	std::vector<std::unique_ptr<Player>> plrs; // same indices as in PlrMgr
};

struct NetworkEffectWriter_Impl : NetworkEffectWriter {
	ServPacket* pkt;
	
	void on_lightning(vec2fp a, vec2fp b, int type, TimeSpan length, FColor clr) {
		reserve_more_block(pkt->bolts, 128);
		pkt->bolts.push_back({ a, b, static_cast<uint8_t>(type), length, clr });
	}
	void on_pgg(PGG_Pointer ppg, const ParticleBatchPars& pars) {
		reserve_more_block(pkt->parts, 1024);
		
		uint8_t model = 0;
		uint8_t effect;
		
		for (int i = FE_EXPLOSION; i < FE_TOTAL_COUNT_INTERNAL; ++i) {
			if (ppg.p == ResBase::get().get_eff(static_cast<FreeEffect>(i))) {
				model = 255;
				effect = i;
				break;
			}
		}
		if (!model) {
			for (int i = MODEL_PC_RAT; i < MODEL_TOTAL_COUNT_INTERNAL; ++i) {
				for (int j = ME_DEATH; j < ME_TOTAL_COUNT_INTERNAL; ++j) {
					if (ppg.p == ResBase::get().get_eff(static_cast<ModelType>(i), static_cast<ModelEffect>(j))) {
						model = i;
						effect = j;
						break;
					}
				}
				if (model)
					break;
			}
		}
		if (!model)
			return; // wat
		
		pkt->parts.push_back({ pars, model, effect });
	}
	static void apply(ServPacket& pkt) {
		for (auto& ev : pkt.bolts)
			effect_lightning(ev.a, ev.b, static_cast<EffectLightning>(ev.type), ev.len, ev.clr);
		
		for (auto& ev : pkt.parts) {
			if (ev.model == 255) {
				GamePresenter::get()->effect({static_cast<FreeEffect>(ev.effect)}, ev.pars);
			}
			else {
				GamePresenter::get()->effect({static_cast<ModelType>(ev.model), static_cast<ModelEffect>(ev.effect)}, ev.pars);
			}
		}
	}
};

class GameMode_MPTest : public GameModeCtr {
public:
    std::shared_ptr<Common> common;
    GameCore* core;
    bool is_server;
    
    std::unordered_map<uint16_t, bool> existing_prev;
	std::unordered_map<uint16_t, bool> existing_now;
    std::unordered_map<uint16_t, EntityIndex> client_ids;
    
    std::optional<std::vector<PlayerInput::State>> cl_to_sv;
    std::queue<ServPacket> sv_to_cl;
	std::optional<PlayerNetworkHUD> cl_hud;
	
    std::mutex mutex;
    std::thread thr;
	
	size_t largest_pkt = 0;
	NetworkEffectWriter_Impl fx_net;
	
	ServPacket p_shared;

    GameMode_MPTest(std::shared_ptr<Common> common, bool is_server)
        : common(common), is_server(is_server) {}
	~GameMode_MPTest() {
		VLOGI("Largest packet: {}", largest_pkt);
	}
	void init(GameCore& p_core) {
		if (is_server) {
			GamePresenter::get()->net_writer = &fx_net;
			fx_net.pkt = &p_shared;
		}
		
	    core = &p_core;
	    
	    thr = std::thread([this] {
	        while (true) {
	            if (is_server) {
					std::vector<PlayerInput::State> ins;
					ins.resize(common->plrs.size());
					for (size_t i = 0; i < ins.size(); ++i) {
						SERIALFUNC_READ(ins[i], *common->plrs[i]->conn);
					}
					
	                std::unique_lock lock(mutex);
	                cl_to_sv.emplace(std::move(ins));
	            }
	            else {
	                ServPacket p;
					PlayerNetworkHUD hud;
					
	                SERIALFUNC_READ(p, *common->ccon);
					bool has_hud = (p.plr != p.invalid);
					if (has_hud) hud.read(*common->ccon);
					
	                std::unique_lock lock(mutex);
					sv_to_cl.emplace(std::move(p));
					if (has_hud) cl_hud.emplace(std::move(hud));
	            }
	        }
        });
	}
	void step() {
	    if (is_server) {
			{
				auto& p = p_shared;
				p.created.reserve(128);
				p.trs.reserve(4096);
				p.dels.reserve(128);
				
				existing_now.clear();
				core->foreach([&](Entity& ent) {
					if (&ent == common->wall) return;
					if (!ent.has<EC_RenderModel>()) return;
					
					existing_now.emplace(ent.index.to_int(), true);
					
					auto& pc = ent.ref_pc();
					auto& tr = p.trs.emplace_back();
					tr.index = ent.index.to_int();
					tr.x = pc.get_pos().x;
					tr.y = pc.get_pos().y;
					tr.r = pc.get_angle();
					tr.vx = pc.get_vel().x;
					tr.vy = pc.get_vel().y;
				});
				
				for (auto& [_, v] : existing_prev) {
					v = false;
				}
				for (auto& [k, _] : existing_now) {
					auto it = existing_prev.find(k);
					if (it != existing_prev.end()) {
						it->second = true;
					}
					else {
						auto& ent = core->ent_ref(EntityIndex::from_int(k));
						auto& pc = ent.ref_pc();
						auto& rc = ent.ref<EC_RenderModel>();
						p.created.push_back({ k, static_cast<uint8_t>(rc.model), rc.clr.to_px(),
						                      pc.get_pos().x, pc.get_pos().y, pc.get_angle() });
					}
				}
				for (auto& [k, v] : existing_prev) {
					if (!v) {
						p.dels.push_back(k);
					}
				}
				
				existing_now.swap(existing_prev);
				
				std::unique_lock lock(mutex);
				for (size_t i = 0; i < common->plrs.size(); ++i) {
					auto plr_ent = core->valid_ent(core->get_pmg().nethack_server[i].first);
					p.plr = plr_ent ? plr_ent->index.to_int() : p.invalid;
					SERIALFUNC_WRITE(p, *common->plrs[i]->conn);
					
					if (plr_ent) {
						PlayerNetworkHUD hud;
						hud.write(*plr_ent, *common->plrs[i]->conn);
					}
					
					if (cl_to_sv) {
						common->plrs[i]->pinp.replay_set(PlayerInput::CTX_GAME, (*cl_to_sv)[i]);
					}
				}
				cl_to_sv.reset();
				
				size_t pkt_size = p.created.size() * 3
				                + p.trs.size() * (2 + 2*3 + 1 + 2*2)
			                    + p.dels.size() * 2;
				if (largest_pkt < pkt_size) {
					largest_pkt = pkt_size;
					VLOGD("New largest packet: {} bytes", largest_pkt);
				}
				
				p = ServPacket{};
	        }
			
			for (size_t i = 0; i < common->plrs.size(); ++i) {
				auto& eid = core->get_pmg().nethack_server[i].first;
				if (!core->valid_ent(eid)) {
					auto& p = common->plrs[i];
					if (p->resp.is_positive()) p->resp -= core->step_len;
					else {
						std::vector<vec2fp> poses;
						for (auto& p : core->get_lc().get_spawns()) {
							if (p.type == LevelControl::SP_PLAYER) {
								poses.push_back(p.pos);
							}
						}
						auto plr = new PlayerEntity(*core, core->get_random().random_el(poses), false);
	                    plr->team = 1;
	                    p->resp = TimeSpan::seconds(4);
	                    eid = plr->index;
					}
				}
			}
            
            for (auto& sp : common->resps) {
                if (!core->valid_ent(sp.ent)) {
                    if (sp.timeout.is_positive()) {
                        sp.timeout -= core->step_len;
                    }
                    else {
						sp.f(*core, sp.pos);
                        sp.timeout = sp.full_timeout;
                    }
                }
            }
	    }
	    else {
			auto state = PlayerInput::get().get_state(PlayerInput::CTX_GAME);
			PlayerInput::get().replay_fix(PlayerInput::CTX_GAME, state);
			
	        std::unique_lock lock(mutex);
			SERIALFUNC_WRITE(state, *common->ccon);
			
			while (!sv_to_cl.empty()) {
				auto& p = sv_to_cl.front();
				
				for (auto& ev : p.created) {
					auto ent = new EProxy(*core, Transform{vec2fp(ev.x, ev.y), ev.r}, ev.model, ev.color);
					client_ids.emplace(ev.index, ent->index);
				}
				for (auto& ev : p.trs) {
					auto ent = static_cast<EProxy*>(core->get_ent(client_ids.find(ev.index)->second));
					auto& pc = ent->pc;
					pc.pos = Transform{vec2fp(ev.x, ev.y), ev.r};
					pc.set_vel(Transform{vec2fp(ev.vx, ev.vy)});
				}
				for (auto& ev : p.dels) {
					auto it = client_ids.find(ev);
					if (it == client_ids.end()) THROW_FMTSTR("ID CRITICAL");
					auto ent = static_cast<EProxy*>(core->get_ent(it->second));
					client_ids.erase(it);
					ent->destroy();
				}
				fx_net.apply(p);
				
				if (p.plr == p.invalid) {
					core->get_pmg().nethack_client->first = EntityIndex{};
				}
				else {
					core->get_pmg().nethack_client->first = client_ids.find(p.plr)->second;
				}
				
				sv_to_cl.pop();
			}
			if (cl_hud) {
				core->get_pmg().nethack_client->second = std::move(*cl_hud);
				cl_hud.reset();
			}
        }
	}
	std::optional<FinalState> get_final_state() {return {};}
};

struct MPTEST_Impl : MPTEST {
    bool is_server;
    std::shared_ptr<Common> common;
    std::string p_addr;
    std::string p_port;
    
    vec2i size;
	std::vector<vec2i> sp_s;
    std::vector<vec2i> destrs;
    std::vector<vec2i> walls;
	
	struct RespInfo {
		vec2i pos;
		TimeSpan time;
		std::function<EntityIndex(GameCore&, vec2fp)> f;
	};
    std::vector<RespInfo> resps;

    MPTEST_Impl(const char *addr, const char *port, int num_clients)
        : is_server(num_clients != 0), p_addr(addr), p_port(port)
    {
        common.reset(new Common);
		for (int i=0; i<num_clients; ++i)
			common->plrs.emplace_back(std::make_unique<Common::Player>());
		
		Common::Player plr;
        
        ImageInfo img;
        if (!img.load("data/mptest.png", ImageInfo::FMT_RGB))
            THROW_FMTSTR("MPTEST load failed");
            
        size = img.get_size();
        for (int y=0; y<size.y; ++y)
        for (int x=0; x<size.x; ++x) {
            vec2i p = {x,y};
            switch (img.get_pixel_fast(p)) {
            case 0xffffff: walls.push_back(p); break;
            case 0xff0000: sp_s.push_back(p); break;
            case 0x0000ff: destrs.push_back(p); break;
				
			case 0xffff00: resps.push_back({ p, TimeSpan::seconds(8), [](auto& core, auto pos) {
				                                 return (new EPickable(core, pos, EPickable::rnd_ammo(core)))->index;} });
				        break;
			case 0x00ff00: resps.push_back({ p, TimeSpan::seconds(20), [](auto& core, auto pos) {
				                                 return (new EPickable(core, pos, EPickable::ArmorShard{30}))->index;} });
				        break;
			case 0xff00ff: resps.push_back({ p, TimeSpan::seconds(45), [](auto& core, auto pos) {
				                                 return (new EPickable(core, pos, EPickable::BigPack{}))->index;} });
				        break;
            }
        }
    }
    void connect() {
        if (is_server) {
            std::unique_ptr<TCP_Server> serv;
            serv.reset(TCP_Server::create(p_addr.data(), p_port.data()));
			for (auto& p : common->plrs) {
				p->conn = serv->accept();
				if (!p->conn) THROW_FMTSTR("MPTEST accept failed");
			}
        }
        else {
            common->ccon.reset(TCP_Socket::connect(p_addr.data(), p_port.data()));
        }
    }
    LevelTerrain* terrain() {
        connect();
        
	    auto lt = new LevelTerrain;
	    lt->grid_size = size;
	    lt->cs.resize(lt->grid_size.area());
	    
	    auto lt_cref = [&](vec2i pos) -> auto& {return lt->cs[pos.y * lt->grid_size.x + pos.x];};
	    
	    for (auto& c : lt->cs)
		    c.is_wall = false;
	    
	    for (auto& p : walls)
		    lt_cref(p).is_wall = true;
	    
	    lt->ls_grid = lt->gen_grid();
	    lt->ls_wall = lt->vectorize();
	    return lt;
    }
    void spawn(GameCore& core, LevelTerrain& lt) {
		common->wall = new EWall(core, lt.ls_wall);
		
		if (!is_server) {
			core.get_pmg().nethack_client.emplace();
			return;
		}
		core.get_pmg().nethack_server.resize(common->plrs.size());
		for (size_t i = 0; i < common->plrs.size(); ++i) {
			core.get_pmg().nethack_server[i].second = &common->plrs[i]->pinp;
		}
        
		for (auto& p : sp_s) {
			core.get_lc().add_spawn({LevelControl::SP_PLAYER, LevelControl::to_center_coord(p)});
        }
        for (auto& p : destrs) {
            new EStorageBox(core, LevelControl::to_center_coord(p));
        }
        for (auto& p : resps) {
			auto& sp = common->resps.emplace_back();
			sp.pos = LevelControl::to_center_coord(p.pos);
			sp.f = std::move(p.f);
			sp.full_timeout = p.time;
        }
    }
    GameModeCtr* mode() {
        return new GameMode_MPTest(common, is_server);
    }
};
MPTEST* MPTEST::make(const char *addr, const char *port, int num_clients) {
    return new MPTEST_Impl(addr, port, num_clients);
}

