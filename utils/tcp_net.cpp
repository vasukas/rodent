#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "tcp_net.hpp"



#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "vaslib/wincompat.hpp"

#define IS_POSIX_CONNECT 0

#define LASTERR			WSAGetLastError()
#define EC(x)			WSA ## x
#define SHUT_RDWR		SD_BOTH
#define WSAEAGAIN		WSAEWOULDBLOCK
#define MSG_NOSIGNAL	0
#define MSG_MORE		0

#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV	0x0400
#define AI_V4MAPPED		0x0008
#define AI_ADDRCONFIG	0x0020
#endif

static const char *net_low_errmsg (int err = LASTERR)
{
	static std::string s;
	s = winc_error(err);
	return s.data();
}
static bool net_init_internal()
{
	WSAData wd;
	if (int ret = WSAStartup(0x202, &wd)) {
		VLOGE("net_init() WSAStartup: {}", net_low_errmsg(ret));
		return false;
	}
	return true;
}
static void net_deinit_internal()
{
	bool r = (WSACleanup() == 0);
	if (!r) VLOGE("net_deinit() WSACleanup: {}", net_low_errmsg());
}

#else

#include <cstring>
#include <cerrno>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

// there should be some additional headers for Macs?

#define IS_POSIX_CONNECT 1

#define LASTERR			errno // btw, this is thread-safe
#define EC(x)			(x)
#define SOCKET			int
#define INVALID_SOCKET	-1
#define closesocket		close

static const char *net_low_errmsg (int err = LASTERR)
{
	static std::string s;
	s = FMT_FORMAT("[{}] {}", err, strerror(err));
	return s.data();
}
static bool net_init_internal() {return true;}
static void net_deinit_internal() {}

#endif



static RAII_Guard net_init_ref()
{
	static int net_init_refcount = 0;
	
	if (!net_init_refcount && !net_init_internal())
		return {};
	
	++net_init_refcount;
	return RAII_Guard([](){ if (--net_init_refcount == 0) net_deinit_internal(); });
}
struct NetAddr
{
	static constexpr socklen_t max_size = 128;
	uint8_t data[ max_size ];
	socklen_t size = 0;
	int fam = 0; ///< AF_* value
	
	bool set( const char *addr, const char *port );
	std::string get() const;
};
bool NetAddr::set( const char *addr, const char *port )
{
	if (!strcmp(addr, "0"))
		addr = {};
	
	addrinfo hint = {};
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	hint.ai_protocol = IPPROTO_TCP;
	hint.ai_flags = AI_ADDRCONFIG | AI_NUMERICHOST | AI_NUMERICSERV;
	if (!addr) hint.ai_flags |= AI_PASSIVE;
	
	addrinfo* res;
	int ret = getaddrinfo( addr, port, &hint, &res );
	if (ret)
	{
		VLOGE("NetAddr::set() getaddrinfo failed for \"{}\" - [{}] {}", addr, ret, gai_strerror(ret));
		return false;
	}
	
	if (res->ai_addrlen > max_size)
	{
		VLOGE("NetAddr::set() resolved address is too big ({} vs {})", res->ai_addrlen, max_size);
		freeaddrinfo(res);
		return false;
	}
	
	fam = res->ai_family;
	size = res->ai_addrlen;
	memcpy( data, res->ai_addr, res->ai_addrlen );
	
	freeaddrinfo(res);
	return true;
}
std::string NetAddr::get() const
{
	char host[128], serv[8];
	
	int ret = getnameinfo(
		(sockaddr*) data, size,
		host, 128,
		serv, 8,
		NI_NUMERICHOST | NI_NUMERICSERV
	);
	if (ret)
	{
		VLOGE("NetAddr::set() getnameinfo failed - [{}] {}", ret, gai_strerror(ret));
		return "UNKNOWN";
	}
	return std::string("[") + host + "]:" + serv;
}

struct SocketInternal
{
	SOCKET s = INVALID_SOCKET;
	
	SocketInternal(SocketInternal&& si) noexcept {
		std::swap(si.s, s);
	}
	~SocketInternal() {
		if (s != INVALID_SOCKET) {
			shutdown(s, SHUT_RDWR);
			if (closesocket(s))
				VLOGE("closesocket failed - {}", net_low_errmsg());
		}
	}
	
	operator bool() const {return s != INVALID_SOCKET;}
	
	static SocketInternal create_from(SOCKET s)
	{
		SocketInternal si(s);
		if (!si.setup()) return INVALID_SOCKET;
		return si;
	}
	static SocketInternal create_new(int fam)
	{
		SOCKET s = socket(fam, SOCK_STREAM, 0);
		if (s == INVALID_SOCKET)
		{
			VLOGE("socket failed - {}", net_low_errmsg());
			return INVALID_SOCKET;
		}
		
		// enable IPv4 for IPv6
		if (fam == AF_INET6)
		{
#ifdef _WIN32
			DWORD flag = 0;
#else
			int flag = 0;
#endif
			if (setsockopt( s, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &flag, sizeof(flag) ))
				VLOGW("setsockopt failed - IPV6_V6ONLY - {}", net_low_errmsg());
		}
		
		return create_from(s);
	}
	
private:
	SocketInternal(SOCKET s): s(s) {}
	bool setup()
	{
		// disable Nagle's algorithm
//		{
//#ifdef _WIN32
//			BOOL flag = 1;
//#else
//			int flag = 1;
//#endif
//			if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag) ))
//			{
//				VLOGE("setsockopt failed - TCP_NODELAY - {}", net_low_errmsg());
//				return false;
//			}
//		}
		return true;
	}
};



class TCP_Socket_Impl : public TCP_Socket
{
public:
	RAII_Guard netref;
	SocketInternal sock;
//	bool is_batch = false;
	
	
	TCP_Socket_Impl(RAII_Guard netref, SocketInternal sock)
		: netref(std::move(netref)), sock(std::move(sock))
	{}
	size_t read(void *buf, size_t buf_size) override
	{
		int flags = MSG_NOSIGNAL;
		
		size_t ptr = 0;
		while (ptr != buf_size)
		{
			int n = ::recv( sock.s, (char *) buf + ptr, buf_size - ptr, flags );
			if (n > 0)
			{
				ptr += n;
				continue;
			}
			
			if (!n)
			{
				VLOGE("TCP_Socket:: connection closed");
				if (error_throw) throw std::runtime_error("TCP_Socket::read() connection closed");
				return 0;
			}
			
			int err = LASTERR;
			if (err != EC(EAGAIN) && err != EC(EWOULDBLOCK) && err != EC(EINPROGRESS) && err != EC(EINTR))
			{
				VLOGE("TCP_Socket:: recv failed - {}", net_low_errmsg(err));
				return 0;
			}
		}
		return buf_size;
	}
	size_t write(const void *buf, size_t buf_size) override
	{
		int flags = MSG_NOSIGNAL;
//		if (is_batch) flags |= MSG_MORE;
		
		while (true)
		{
			int n = ::send( sock.s, (const char *) buf, buf_size, flags );
			if (n != -1) break;
			
			int err = LASTERR;
			if (err == EC(EINTR)) continue;
			if (err == EC(EAGAIN) || err == EC(EWOULDBLOCK) || err == EC(ENOBUFS))
				continue;
			
			if (err != EC(EINPROGRESS))
			{
				VLOGE("TCP_Socket:: send failed - {}", net_low_errmsg(err));
				if (error_throw) throw std::runtime_error("TCP_Socket::write() failed");
				return 0;
			}
		}
		return buf_size;
	}
};
TCP_Socket* TCP_Socket::connect(const char *addr, const char *port)
{
	RAII_Guard netref = net_init_ref();
	if (!netref)
		THROW_FMTSTR("TCP_Socket::connect() failed for [{}]:{}", addr, port);
	
	NetAddr na;
	if (!na.set( addr, port ))
		THROW_FMTSTR("TCP_Socket::connect() failed for [{}]:{}", addr, port);
	
	SocketInternal sock = SocketInternal::create_new(na.fam);
	if (!sock)
		THROW_FMTSTR("TCP_Socket::connect() failed for [{}]:{}", addr, port);
	
	VLOGI("TCP_Socket:: connecting to \"{}\"", na.get());
	if (::connect( sock.s, (sockaddr*) na.data, na.size ))
	{
		VLOGE("connect failed - {}", net_low_errmsg());
		THROW_FMTSTR("TCP_Socket::connect() failed for [{}]:{}", addr, port);
	}
	
	VLOGI("TCP_Socket:: connected");
	return new TCP_Socket_Impl( std::move(netref), std::move(sock) );
}



class TCP_Server_Impl : public TCP_Server
{
public:
	RAII_Guard netref;
	SocketInternal sock;
	
	
	TCP_Server_Impl(RAII_Guard netref, SocketInternal sock)
		: netref(std::move(netref)), sock(std::move(sock))
	{}
	std::unique_ptr<TCP_Socket> accept() override
	{
		NetAddr na;
		na.size = na.max_size;
		
		SOCKET s = ::accept( sock.s, (sockaddr*) na.data, &na.size );
		if (s == INVALID_SOCKET)
		{
			int err = LASTERR;
			if (err != EC(EAGAIN) && err != EC(EWOULDBLOCK) && err != EC(ECONNABORTED) && err != EC(EINTR))
				VLOGE("accept failed - {}", net_low_errmsg( err ));
			return nullptr;
		}
		VLOGI("TCP_Server:: accepted \"{}\"", na.get());
		
		SocketInternal ret = SocketInternal::create_from(s);
		if (!ret) {
			VLOGE("TCP_Server::accept() failed");
			return nullptr;
		}
		
		return std::make_unique<TCP_Socket_Impl>( net_init_ref(), std::move(ret) );
	}
};
TCP_Server* TCP_Server::create(const char *addr, const char *port)
{
	RAII_Guard netref = net_init_ref();
	if (!netref)
		THROW_FMTSTR("TCP_Server::create() failed for [{}]:{}", addr, port);
	
	NetAddr na;
	if (!na.set( addr, port ))
		THROW_FMTSTR("TCP_Server::create() failed for [{}]:{}", addr, port);
	
	SocketInternal sock = SocketInternal::create_new(na.fam);
	if (!sock)
		THROW_FMTSTR("TCP_Server::create() failed for [{}]:{}", addr, port);
	
	if (bind( sock.s, (sockaddr*) na.data, na.size ))
	{
		VLOGE("bind failed - {}", net_low_errmsg());
		THROW_FMTSTR("TCP_Server::create() failed for [{}]:{}", addr, port);
	}
	if (listen( sock.s, 4 ))
	{
		VLOGE("listen failed - {}", net_low_errmsg());
		THROW_FMTSTR("TCP_Server::create() failed for [{}]:{}", addr, port);
	}
	
	VLOGI("TCP_Server:: listening on \"{}\"", na.get());
	return new TCP_Server_Impl( std::move(netref), std::move(sock) );
}
