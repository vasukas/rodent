#ifndef EV_SIGNAL_HPP
#define EV_SIGNAL_HPP

#include <functional>
#include <vector>



/// Enables class as event subscriber. Note: should be in the beginning of declaration
#define EVS_SUBSCR \
	ev_signal_detail::Subscr _ev_signal_subscr

/// Connects this member function with 0 arguments to signal
#define EVS_CONNECT0( SIG, FUNC_NAME )\
	EVS_CONNECT_N_TEMP( SIG, FUNC_NAME )

/// Connects this member function with 1 argument to signal
#define EVS_CONNECT1( SIG, FUNC_NAME )\
	EVS_CONNECT_N_TEMP( SIG, FUNC_NAME, std::placeholders::_1 )

/// Connects freestanding function to signal
#define EVS_FREEFUNC( SIG, FUNC )\
	SIG.connect(nullptr, FUNC)

/// Disconnects subscriber from everything
#define EVS_SUBSCR_UNSUB_ALL \
	_ev_signal_subscr.rem_all()



#define EVS_CONNECT_N_TEMP( SIG, FUNC_NAME, ... )\
	EVS_OBJECT_N_TEMP( SIG, this, FUNC_NAME, __VA_ARGS__ )

#define EVS_OBJECT_N_TEMP( SIG, OBJPTR, FUNC_NAME, ... )\
	SIG.connect(_ev_signal_subscr, std::bind(&std::remove_reference<decltype(*(OBJPTR))>::type::FUNC_NAME, (OBJPTR), ##__VA_ARGS__))

namespace ev_signal_detail
{
	struct Publr
	{
		virtual ~Publr() = default;
		virtual void rem(size_t i) = 0;
	};
	struct Subscr
	{
		std::vector<std::pair <Publr*, size_t>> rs;
		
		~Subscr() {
			for (auto& r : rs) r.first->rem(r.second);
		}
		void rem(Publr* p)
		{
			for (size_t i=0; i<rs.size(); )
				if (rs[i].first == p) rs.erase( rs.begin() + i );
				else ++i;
		}
		void rem_all()
		{
			for (auto& r : rs) r.first->rem(r.second);
			rs.clear();
		}
	};
}



template <typename... Args>
struct ev_signal : ev_signal_detail::Publr
{
	using Cb = std::function<void(Args...)>;
	
	~ev_signal() {
		for (auto& s : ss)
			if (s.first && s.second)
				s.second->rem(this);
	}
	void connect(Cb cb) {
		ss.emplace_back(std::move(cb), nullptr);
	}
	void connect(ev_signal_detail::Subscr& s, Cb cb) {
		s.rs.emplace_back(this, ss.size());
		ss.emplace_back(std::move(cb), &s);
	}
	void signal(const Args&... args) {
		for (auto& s : ss)
			if (s.first)
				s.first(args...);
	}
	bool has_conn() {
		for (auto& s : ss) if (s.first) return true;
		return false;
	}
	
private:
	std::vector<std::pair <Cb, ev_signal_detail::Subscr*>> ss;
	void rem(size_t i) {
		ss[i].first = {};
	}
};

#endif // EV_SIGNAL_HPP
