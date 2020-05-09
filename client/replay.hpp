#ifndef REPLAY_HPP
#define REPLAY_HPP

#include <variant>
#include "client/plr_input.hpp"
#include "game/entity.hpp"
#include "vaslib/vas_time.hpp"



/// Initialization info
struct ReplayInitData
{
	std::string rnd_init;
	TimeSpan fastforward;
	bool pmg_superman;
	bool pmg_dbg_ai_rect;
	bool mode_survival;
};

struct Replay_DebugTeleport
{
	vec2fp target;
};
struct Replay_UseTransitTeleport
{
	EntityIndex teleport;
};
using ReplayEvent = std::variant<Replay_DebugTeleport, Replay_UseTransitTeleport>;



class ReplayWriter
{
public:
	// All init functions throw on error and so never return null
	static ReplayWriter* write_net(ReplayInitData dat, const char *addr, const char *port, bool is_server); // blocking
	static ReplayWriter* write_file(ReplayInitData dat, const char *filename);
	virtual ~ReplayWriter() = default;
	
	virtual void add_event(ReplayEvent ev) = 0; ///< Would be written with next step
	virtual void update_client(PlayerInput& pc) = 0; ///< Call on each logic tic
};



class ReplayReader
{
public:
	// All init functions throw on error and so never return null
	
	static ReplayReader* read_net(ReplayInitData& dat, const char *addr, const char *port, bool is_server); // blocking
	static ReplayReader* read_file(ReplayInitData& dat, const char *filename);
	virtual ~ReplayReader() = default;
	
	struct RET_OK {
		std::optional<float> pb_speed = {}; ///< Playback speed, 1 if not set
		std::vector<ReplayEvent> evs; ///< Events to process before step
	};
	struct RET_WAIT {}; ///< Next tick not yet available (network)
	struct RET_END  {}; ///< Transmission/record ended
	
	using Ret = std::variant<RET_OK, RET_WAIT, RET_END>;
	virtual Ret update_server(PlayerInput& pc) = 0; ///< Call on each logic tick
};

#endif // REPLAY_HPP
