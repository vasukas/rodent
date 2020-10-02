// --mptest ADDR PORT ISSERV

#include <mutex>
#include <thread>
#include <unordered_map>
#include "game/game_mode.hpp"
#include "utils/tcp_net.hpp"
#include "client/plr_input.hpp"
#include "game/game_core.hpp"
#include "game_objects/objs_basic.hpp"
#include "game_objects/objs_player.hpp"
#include "game/level_ctr.hpp"
#include "utils/image_utils.hpp"
#include "utils/serializer_defs.hpp"
#include "mptest.hpp"
#include "vaslib/vas_log.hpp"
#include "client/replay_ser.hpp"


// client-side
class EProxy : public Entity {
public:
    EC_VirtualBody pc;

    EProxy(GameCore& core, Transform pos, int model)
        : Entity(core), pc(*this, pos)
    {
	    add_new<EC_RenderModel>(static_cast<ModelType>(model), FColor(0.8, 0.8, 0.8), EC_RenderModel::DEATH_AND_EXPLOSION);
    }
    EC_Position& ref_pc() {return pc;}
};


struct EvCreate {
    uint16_t index;
    uint8_t model;
};
struct EvTransform {
    uint16_t index;
    float x, y, r;
    float vx, vy;
};

struct ServPacket {
    std::vector<EvCreate> created;
    std::vector<EvTransform> trs;
    std::vector<uint16_t> dels;
    
    uint16_t plr;
    static constexpr auto invalid = std::numeric_limits<uint16_t>::max();
};

SERIALFUNC_PLACEMENT_1(EvCreate,
	SER_FD(index),
	SER_FD(model));
	
SERIALFUNC_PLACEMENT_1(EvTransform,
	SER_FD(index),
	SER_FDT(x, fp_16_8),
	SER_FDT(y, fp_16_8),
	SER_FDT(r, 8rad),
	SER_FDT(vx, fp_8_8),
	SER_FDT(vy, fp_8_8));
	
SERIALFUNC_PLACEMENT_1(ServPacket,
    SER_FDT(created, Array32),
    SER_FDT(trs, Array32),
    SER_FDT(dels, Array32),
    SER_FD(plr));


struct Common {
    std::unique_ptr<TCP_Socket> conn;
    vec2fp spawn_remote;
    
    struct Ammo {
        EntityIndex ent;
        vec2fp pos;
        TimeSpan timeout;
    };
    std::vector<Ammo> ammos;
    
    PlayerInput pinp_remote = PlayerInput::empty_proxy();
};

class GameMode_MPTest : public GameModeCtr {
public:
    std::shared_ptr<Common> common;
    GameCore* core;
    bool is_server;
    
    TimeSpan remote_resp;
    
    std::unordered_set<uint16_t> existing_prev;
    std::unordered_map<uint16_t, EntityIndex> client_ids;
    
    std::optional<PlayerInput::State> cl_to_sv;
    std::vector<Packet> sv_to_cl;
    
    std::mutex mutex;
    std::thread thr;

    GameMode_MPTest(std::shared_ptr<Common> net, bool is_server)
        : common(common), is_server(is_server) {}
	void init(GameCore& p_core) {
	    core = &p_core;
	    
	    thr = std::thread([this] {
	        while (true) {
	            if (is_server) {
	                PlayerInput::State p;
	                SERIALFUNC_READ(p, *common->conn);
	                std::unique_lock lock(mutex);
	                cl_to_sv.emplace(std::move(p));
	            }
	            else {
	                Packet p;
	                SERIALFUNC_READ(p, *common->conn);
	                std::unique_lock lock(mutex);
	                sv_to_cl.emplace_back(std::move(p));
	            }
	        }
        });
	}
	void step() {
	    if (is_server) {
	        {   std::unique_lock lock(mutex);
	            if (cl_to_sv) {
	                
#error update input
	                cl_to_sv.reset();
	            }
	        }
#error net packets - send world

            if (!core->valid_ent(*core->get_pmg.nethack)) {
                if (remote_resp.is_positive()) remote_resp -= core->step_len;
                else {
                    auto plr = new EPlayer(*core, common->spawn_remote);
                    plr->team = 1;
                    remote_resp = TimeSpan::seconds(3);
                    core->get_pmg.nethack = plr->index;
                }
            }
            
            for (auto& sp : common->ammos) {
                if (!core->valid_ent(sp.ent)) {
                    if (core->timeout.is_positive()) {
                        core->timeout -= core->step_len;
                    }
                    else {
                        new EPickable(*core, sp.pos, EPickable::rnd_ammo(*core));
                        core->timeout = TimeSpan::seconds(3);
                    }
                }
            }
	    }
	    else {
	        {   std::unique_lock lock(mutex);
	            while (!sv_to_cl.empty()) {
#error receive & set pmg: nethack
	            }
	        }
#error send packet
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
    vec2i sp_a, sp_b;
    std::vector<vec2i> destrs;
    std::vector<vec2i> walls;
    std::vector<vec2i> ammos;

    MPTEST_Impl(const char *addr, const char *port, bool is_server)
        : is_server(is_server), p_addr(addr), p_port(port)
    {
        common.reset(new Common);
        
        ImageInfo img;
        if (!img.load("data/mptest.png", ImageInfo::FMT_RGB))
            THROW_FMTSTR("MPTEST load failed");
            
        size = img.get_size();
        for (int y=0; y<size.y; ++y)
        for (int x=0; x<size.x; ++x) {
            vec2i p = {x,y};
            switch (img.get_pixel_fast(p)) {
            case 0xffffff: walls.push_back(p); break;
            case 0xff0000: sp_a = p; break;
            case 0x00ff00: sp_b = p; break;
            case 0x0000ff: destrs.push_back(p); break;
            case 0xffff00: ammos.push_back(p); break;
            }
        }
    }
    void connect() {
        if (is_server) {
            std::unique_ptr<TCP_Server> serv;
            serv.reset(TCP_Server::create(p_addr.data(), p_port.data());
            common->conn = serv->accept();
            if (!common->conn) THROW_FMTSTR("MPTEST accept failed");
        }
        else {
            common->conn.reset(TCP_Socket::connect(p_addr.data(), p_port.data());
        }
    }
    LevelTerrain* terrain() {
        connect();
        
	    auto lt = new LevelTerrain;
	    lt->grid_size = size;
	    lt->cs.resize(lt->grid_size.area());
	    
	    auto lt_cref = [&](vec2i pos) -> auto& {return lt->cs[pos.y * lt->grid_size.x + pos.x];};
	    
	    for (auto& p : lt->cs)
		    lt_cref(p).is_wall = false;
	    
	    for (auto& p : walls)
		    lt_cref(p).is_wall = true;
	    
	    lt->ls_grid = lt->gen_grid();
	    lt->ls_wall = lt->vectorize();
	    return lt;
    }
    void spawn(GameCore& core, LevelTerrain& lt) {
        core->get_pmg.nethack = EntityIndex{};
        if (!is_server) return;
        core->get_pmg.nethack_input = &common->pinp_remote;
        
        new EWall(core, lt.ls_wall);
        
        core.get_lc().add_spawn({LevelControl::SP_PLAYER, sp_a});
        common->spawn_remote = LevelControl::to_center_coord(sp_b);
        
        for (auto& p : destrs) {
            new EStorageBox(core, LevelControl::to_center_coord(p));
        }
        
        for (auto& p : ammos) {
            common->ammos.push_back(LevelControl::to_center_coord(p));
        }
    }
    GameModeCtr* mode() {
        return new GameMode_MPTest(common, is_server);
    }
};
MPTEST* MPTEST::make(const char *addr, const char *port, bool is_server) {
    return new MPTEST_Impl(addr, port, is_server);
}

