// --mptest ADDR PORT ISSERV

#include "game/game_mode.hpp"
#include "utils/tcp_net.hpp"
#include "client/plr_input.hpp"
#include "game/game_core.hpp"
#include "game_objects/objs_player.hpp"
#include "utils/image_utils.hpp"
#include "utils/serializer_defs.hpp"
#include "mptest.hpp"

class EProxy : public Entity {
public:
    #error client-side, only ec_rendermodel
};

#error entity data: init (presenter), delete, transform + velocity

struct EvCreate {
    uint16_t index;
    uint8_t model;
};
struct EvTransform {
    uint16_t index;
    float x, y, r;
    float vx, vy;
};
#error delete - indices array

#error serializers; also pinp conflicts with one from client/replay.cpp

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



struct NetInfo {
    std::unique_ptr<TCP_Socket> conn;
    std::unique_ptr<TCP_Server> serv;
};

class GameMode_MPTest : public GameModeCtr {
public:
    std::shared_ptr<NetInfo> net;
    bool is_server;

    GameMode_MPTest(std::shared_ptr<NetInfo> net, bool is_server)
        : net(net), is_server(is_server) {}
	void init(GameCore&) {}
	void step() {
	    if (is_server) {
#error net packets
#error spawn timer ONLY FOR REMOTE (set team, set plrmgr tracked, set input)
#error set pmg: nethack, nethack_input
	    }
	    else {
#error net packets
#error spawn timer ONLY FOR REMOTE (set team, set plrmgr tracked, set input)
#error set pmg: nethack
        }
	}
	std::optional<FinalState> get_final_state() {return {};}
};

struct MPTEST_Impl : MPTEST {
    bool is_server;
    std::shared_ptr<NetInfo> net;

    MPTEST_Impl(const char *addr, const char *port, bool is_server)
        : is_server(is_server)
    {
        
#error init net
#error load terrain image
    }
    void connect() {
#error wait
    }
    LevelTerrain* terrain() {
        connect();
#error gen terrain
    }
    void spawn(GameCore& core, LevelTerrain& lt) {
        if (!is_server) return;
#error spawn: regular doors (can't be drawn), destructibles
    }
    GameModeCtr* mode() {
        return new GameMode_MPTest(net, is_server);
    }
};
MPTEST* MPTEST::make(const char *addr, const char *port, bool is_server) {
    return new MPTEST_Impl(addr, port, is_server);
}

