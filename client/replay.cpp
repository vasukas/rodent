#include <condition_variable>
#include <queue>
#include <mutex>
#include <thread>
#include "core/hard_paths.hpp"
#include "game/game_core.hpp"
#include "utils/serializer_defs.hpp"
#include "utils/tcp_net.hpp"
#include "vaslib/vas_log.hpp"
#include "replay_ser.hpp"

constexpr char stream_header[] = "ratdemo";
const uint32_t stream_version = 7; // binary format

struct Header {
	SerialType_Void signature_hack;
	uint32_t version;
	std::string platform;
	std::string comment;
};
struct Frame {
	PlayerInput::ContextMode ctx;
	PlayerInput::State st;
	std::vector<ReplayEvent> evs;
};



SERIALFUNC_PLACEMENT_1(ReplayInitData,
	SER_FDT(rnd_init, Array32),
	SER_FD(fastforward),
	SER_FD(pmg_superman),
	SER_FD(pmg_dbg_ai_rect),
	SER_FD(mode_survival));

SERIALFUNC_PLACEMENT_1(Replay_DebugTeleport,
	SER_FD(target));

SERIALFUNC_PLACEMENT_1(Replay_UseTransitTeleport,
	SER_FD(teleport));

SERIALFUNC_PLACEMENT_1(Header,
	SER_FDT(signature_hack, Signature<stream_header>),
	SER_FD(version),
	SER_FD(platform),
	SER_FD(comment));

SERIALFUNC_PLACEMENT_1(Frame,
	SER_FDT(ctx, Enum< PlayerInput::CTX_TOTAL_COUNT_INTERNAL >),
	SER_FD(st),
	SER_FDT(evs, Array32));



static void write_header(File& f, const ReplayInitData& init)
{
	Header h;
	h.version = stream_version;
	h.platform = get_full_platform_version();
	
	h.comment += "\n\n===== COMMENT =====\n\n";
	h.comment += FMT_FORMAT("Date: {}\n", date_time_str());
	h.comment += FMT_FORMAT("Stream version: {}\n", stream_version);
	h.comment += FMT_FORMAT("Platform: {}\n", get_full_platform_version());
	h.comment += FMT_FORMAT("Terrain seed: {}\n", init.rnd_init);
	h.comment += "\n\n===== COMMENT =====\n\n";
	
	SERIALFUNC_WRITE(h, f);
	SERIALFUNC_WRITE(init, f);
}
static void read_header(File& f, ReplayInitData& init)
{
	Header h;
	SERIALFUNC_READ(h, f);
	
	if (h.version != stream_version)
	    throw std::runtime_error("ReplayReader:: unsupported file version");
	
	if (h.platform != get_full_platform_version()) {
		VLOGW("ReplayReader:: incompatible platform - {}", h.platform);
		init.incompat_version = h.platform;
	}
	
	SERIALFUNC_READ(init, f);
}
static std::unique_ptr<File> net_init(const char *addr, const char *port, bool is_server)
{
	VLOGD("Replay:: net_init - {}, [{}]:{}", is_server ? "server" : "client", addr, port);
	if (is_server) {
		std::unique_ptr<TCP_Server> serv(TCP_Server::create(addr, port));
		if (auto f = serv->accept()) return f;
		throw std::runtime_error("Network failed");
	}
	else {
		return std::unique_ptr<TCP_Socket>(TCP_Socket::connect(addr, port));
	}
}



struct ReplayThread
{
	static ReplayThread mk_write(File* f)
	{
		return ReplayThread([f](ReplayThread* t_ptr)
		{
			set_this_thread_name("replay netwrite");
			auto& t = *t_ptr;
			while (true)
			{
				std::unique_lock lock(t.mut);
				t.sig.wait(lock, [&]{
					return !t.q.empty() || t.close;
				});
				if (t.close) break;
				
				auto q = std::move(t.q.front());
				t.q.pop();
				lock.unlock();
				
				try {
					SERIALFUNC_WRITE(q, *f);
				}
				catch (std::exception& e) {
					VLOGE("ReplayThread:: exception - {}", e.what());
					t.error = true;
					break;
				}
			}
		});
	}
	static ReplayThread mk_net_read(File* f)
	{
		return ReplayThread([f](ReplayThread* t_ptr)
		{
			set_this_thread_name("replay netread");
			auto& t = *t_ptr;
			while (!t.close)
			{
				Frame frm;
				try {
					SERIALFUNC_READ(frm, *f);
				}
				catch (std::exception& e) {
					VLOGE("ReplayThread:: exception - {}", e.what());
					t.error = true;
					break;
				}
				std::unique_lock lock(t.mut);
				t.q.emplace(std::move(frm));
			}
		});
	}
	ReplayThread(ReplayThread&& t) noexcept
	{
		thr = std::move(t.thr);
		q = std::move(t.q);
		close = t.close;
		error = t.error;
		t.error = true;
	}
	~ReplayThread()
	{
		close = true;
		sig.notify_one();
		if (thr.joinable()) thr.join();
	}
	
	void write(Frame frm)
	{
		if (error) throw std::runtime_error("ReplayThread:: failed");
		std::unique_lock lock(mut);
		q.emplace(std::move(frm));
		sig.notify_one();
	}
	std::queue<Frame> read()
	{
		if (error) throw std::runtime_error("ReplayThread:: failed");
		std::unique_lock lock(mut);
		return std::move(q);
	}
	
private:
	std::thread thr;
	std::mutex mut;
	std::queue<Frame> q;
	std::condition_variable sig;
	bool close = false;
	bool error = false;
	
	ReplayThread(std::function<void(ReplayThread*)> f): thr(std::move(f), this) {}
};



class Replay_NetReader : public ReplayReader
{
public:
	// number of frames
	static constexpr size_t fn_delay    = TimeSpan::seconds(2)   / GameCore::step_len;
	static constexpr size_t fn_skip_thr = TimeSpan::seconds(3)   / GameCore::step_len;
	static constexpr size_t fn_ffwd_thr = TimeSpan::seconds(2.2) / GameCore::step_len;
	
	std::unique_ptr<File> f;
	ReplayThread thr;
	
	std::queue<Frame> frms;
	bool delay_check = true;
	
	Replay_NetReader(std::unique_ptr<File> f)
	    : f(std::move(f)), thr(ReplayThread::mk_net_read(this->f.get()))
	{}
	Ret update_server(PlayerInput& pc) override
	{
		auto nq = thr.read();
		while (!nq.empty()) {
			frms.emplace(std::move(nq.front()));
			nq.pop();
		}
		
		if (delay_check)
		{
			if (frms.size() < fn_delay)
				return RET_WAIT{};
			
			delay_check = false;
		}
		if (frms.empty())
			return RET_WAIT{};
		
		auto& frm = frms.front();
		auto evs = std::move(frm.evs);
		pc.replay_set(frm.ctx, std::move(frm.st));
		frms.pop();
		
		if		(frms.size() > fn_skip_thr) return RET_OK{ 0.f, std::move(evs) };
		else if (frms.size() > fn_ffwd_thr)
			return RET_OK{ 1.f - inv_lerp<float>(fn_ffwd_thr, fn_skip_thr, frms.size()), std::move(evs) };
		
		return RET_OK{ {}, std::move(evs) };
	}
};
ReplayReader* ReplayReader::read_net(ReplayInitData& dat, const char *addr, const char *port, bool is_server)
{
	auto f = net_init(addr, port, is_server);
	read_header(*f, dat);
	return new Replay_NetReader(std::move(f));
}



class Replay_NetWriter : public ReplayWriter
{
public:
	std::unique_ptr<File> f;
	ReplayThread thr;
	Frame frm;
	
	Replay_NetWriter(std::unique_ptr<File> f)
	    : f(std::move(f)), thr(ReplayThread::mk_write(this->f.get()))
	{}
	void add_event(ReplayEvent ev) override {
		frm.evs.emplace_back(std::move(ev));
	}
	void update_client(PlayerInput& pc) override {
		frm.ctx = pc.get_context();
		if (frm.ctx == PlayerInput::CTX_GAME) {
			frm.st = pc.get_state(frm.ctx);
			pc.replay_fix(frm.ctx, frm.st);
		}
		else frm.st = {};
		thr.write(std::move(frm));
		frm.evs.clear();
	}
};
ReplayWriter* ReplayWriter::write_net(ReplayInitData dat, const char *addr, const char *port, bool is_server)
{
	auto f = net_init(addr, port, is_server);
	write_header(*f, dat);
	return new Replay_NetWriter(std::move(f));
}



class Replay_File : public ReplayWriter, public ReplayReader
{
public:
	std::unique_ptr<File> f;
	Frame frm;
	
	Replay_File(std::unique_ptr<File> f): f(std::move(f)) {}
	void add_event(ReplayEvent ev) override {
		frm.evs.emplace_back(std::move(ev));
	}
	void update_client(PlayerInput& pc) override {
		frm.ctx = pc.get_context();
		if (frm.ctx == PlayerInput::CTX_GAME) {
			frm.st = pc.get_state(frm.ctx);
			pc.replay_fix(frm.ctx, frm.st);
		}
		else frm.st = {};
		SERIALFUNC_WRITE(frm, *f);
		frm.evs.clear();
	}
	Ret update_server(PlayerInput& pc) override
	{
		if (f->tell() == f->get_size())
			return RET_END{};
		
		Frame frm;
		SERIALFUNC_READ(frm, *f);
		
		pc.replay_set(frm.ctx, std::move(frm.st));
		return RET_OK{{}, std::move(frm.evs)};
	}
};
ReplayWriter* ReplayWriter::write_file(ReplayInitData dat, const char *filename)
{
	auto f = File::open_ptr(filename, File::OpenCreate);
	write_header(*f, dat);
	return new Replay_File(std::move(f));
}
ReplayReader* ReplayReader::read_file(ReplayInitData& dat, const char *filename)
{
	auto f = File::open_ptr(filename);
	read_header(*f, dat);
	return new Replay_File(std::move(f));
}
