#include <condition_variable>
#include <queue>
#include <mutex>
#include <thread>
#include "core/hard_paths.hpp"
#include "game/game_core.hpp"
#include "utils/serializer_defs.hpp"
#include "utils/tcp_net.hpp"
#include "vaslib/vas_log.hpp"
#include "replay.hpp"

constexpr std::string_view stream_header = "ratdemo";
const uint32_t stream_version = 4;



SERIALFUNC_PLACEMENT_1(PlayerController::State,
	SER_FD(is),
	SER_FDT(acts, Array32<SerialTag_Enum< PlayerController::ACTION_TOTAL_COUNT_INTERNAL >>),
	SER_FD(mov),
	SER_FD(tar_pos));

SERIALFUNC_PLACEMENT_1(ReplayInitData,
	SER_FDT(rnd_init, Array32),
	SER_FD(fastforward));

SERIALFUNC_PLACEMENT_1(Replay_DebugTeleport,
	SER_FD(target));

static void write_header(File& f)
{
	f.write(stream_header.data(), stream_header.size());
	f.w32L(stream_version);
	
	auto ps = get_full_platform_version();
	f.w8( ps.size() );
	f.write( ps.data(), ps.size() + 1 );
}
static void check_header(File& f)
{
	for (auto& c : stream_header)
	{
		char r;
		f.read(&r, 1);
		if (c != r)
			throw std::runtime_error("Invalid file header");
	}
	
	auto vers = f.r32L();
	if		(vers < stream_version) throw std::runtime_error("Unsupported file version (old)");
	else if (vers > stream_version) throw std::runtime_error("Unsupported file version (new)");
	
	std::string ps;
	ps.resize( f.r8() );
	f.read( ps.data(), ps.size() + 1 );
	if (ps != get_full_platform_version())
		VLOGW("ReplayReader:: incompatible platform - {}", ps);
}
static std::unique_ptr<File> net_init(const char *addr, const char *port, bool is_server)
{
	VLOGD("Replay:: net_init - {}, [{}]:{}", is_server?"server":"client", addr, port);
	if (is_server)
	{
		std::unique_ptr<TCP_Server> serv(TCP_Server::create(addr, port));
		if (auto f = serv->accept()) return f;
		throw std::runtime_error("Network failed");
	}
	else
	{
		return std::unique_ptr<TCP_Socket>(TCP_Socket::connect(addr, port));
	}
}



struct Frame {
	PlayerController::State st;
	std::vector<ReplayEvent> evs;
	bool st_set = false; ///< Only for ReplayThread on writing
};
static void write(const PlayerController::State& st, File& f)
{
	f.w8(0);
	SerialFunc<PlayerController::State>::write(st, f);
}
static void write(const ReplayEvent& ev, File& f)
{
	f.w8(1);
	SerialFunc<ReplayEvent>::write(ev, f);
}
static bool read(Frame& frm, File& f)
{
	try {
		while (true)
		{
			int i = f.r8();
			if (i == 1) {
				auto& ev = frm.evs.emplace_back();
				SERIALFUNC_READ(ev, f);
			}
			else {
				SERIALFUNC_READ(frm.st, f);
				return true;
			}
		}
	}
	catch (std::exception& e) {
		VLOGE("DemoRecordReader:: read failed - {}", e.what());
		return false;
	}
}



struct ReplayThread
{
	static ReplayThread mk_write(File* f)
	{
		return ReplayThread([f](ReplayThread* t_ptr)
		{
			auto& t = *t_ptr;
			while (true)
			{
				std::unique_lock lock(t.mut);
				t.sig.wait(lock, [&]{
					return (!t.q.empty() && t.q.front().st_set) || t.close;
				});
				if (t.close) break;
				
				auto q = std::move(t.q.front());
				t.q.pop();
				lock.unlock();
				
				try {
					for (auto& e : q.evs) ::write(e, *f);
					::write(q.st, *f);
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
			auto& t = *t_ptr;
			while (!t.close)
			{
				Frame frm;
				try {
					if (!::read(frm, *f)) {
						VLOGE("ReplayThread:: ended");
						t.error = true;
						break;
					}
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
	
	void write(ReplayEvent ev)
	{
		if (error) throw std::runtime_error("ReplayThread:: failed");
		std::unique_lock lock(mut);
		
		if (q.empty() || q.back().st_set) q.emplace();
		q.back().evs.emplace_back(std::move(ev));
		
		sig.notify_one();
	}
	void write(PlayerController::State st)
	{
		if (error) throw std::runtime_error("ReplayThread:: failed");
		std::unique_lock lock(mut);
		
		if (q.empty() || q.back().st_set) q.emplace();
		q.back().st = std::move(st);
		q.back().st_set = true;
		
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
	Ret update_server(PlayerController& pc) override
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
		
		auto evs = std::move(frms.front().evs);
		pc.force_state( frms.front().st );
		frms.pop();
		
		if		(frms.size() > fn_skip_thr) return RET_OK{ 0.f, std::move(evs) };
		else if (frms.size() > fn_ffwd_thr)
			return RET_OK{ 1.f - inv_lerp<float,float>(fn_ffwd_thr, fn_skip_thr, frms.size()), std::move(evs) };
		
		return RET_OK{ {}, std::move(evs) };
	}
};
ReplayReader* ReplayReader::read_net(ReplayInitData& dat, const char *addr, const char *port, bool is_server)
{
	auto f = net_init(addr, port, is_server);
	check_header(*f);
	
	SERIALFUNC_READ(dat, *f);
	return new Replay_NetReader(std::move(f));
}



class Replay_NetWriter : public ReplayWriter
{
public:
	std::unique_ptr<File> f;
	ReplayThread thr;
	
	Replay_NetWriter(std::unique_ptr<File> f)
	    : f(std::move(f)), thr(ReplayThread::mk_write(this->f.get()))
	{}
	void add_event(ReplayEvent ev) override {thr.write(std::move(ev));}
	void update_client(PlayerController& pc) override {thr.write(pc.get_state());}
};
ReplayWriter* ReplayWriter::write_net(ReplayInitData dat, const char *addr, const char *port, bool is_server)
{
	auto f = net_init(addr, port, is_server);
	write_header(*f);
	
	SERIALFUNC_WRITE(dat, *f);
	return new Replay_NetWriter(std::move(f));
}



class Replay_File : public ReplayWriter, public ReplayReader
{
public:
	std::unique_ptr<File> f;
	
	Replay_File(std::unique_ptr<File> f): f(std::move(f)) {}
	void add_event    (ReplayEvent   ev)     override {write(ev, *f);}
	void update_client(PlayerController& pc) override {write(pc.get_state(), *f);}
	
	Ret update_server(PlayerController& pc) override
	{
		if (f->tell() == f->get_size())
			return RET_END{};
		
		Frame frm;
		read(frm, *f);
		pc.force_state(std::move(frm.st));
		
		return RET_OK{{}, std::move(frm.evs)};
	}
};
ReplayWriter* ReplayWriter::write_file(ReplayInitData dat, const char *filename)
{
	auto f = File::open_ptr(filename, File::OpenCreate);
	write_header(*f);
	
	SERIALFUNC_WRITE(dat, *f);
	return new Replay_File(std::move(f));
}
ReplayReader* ReplayReader::read_file(ReplayInitData& dat, const char *filename)
{
	auto f = File::open_ptr(filename);
	check_header(*f);
	
	SERIALFUNC_READ(dat, *f);
	return new Replay_File(std::move(f));
}
