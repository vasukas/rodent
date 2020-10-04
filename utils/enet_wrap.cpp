#include "enet_wrap.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_time.hpp"
#include <enet/enet.h>

constexpr uint32_t Greeting = 0xdead9a75;

static void net_init() {
	static bool inited = false;
	if (inited)
		return;
	inited = true;
	
	VLOGI("ENet compiled: {}.{}.{}", ENET_VERSION_MAJOR, ENET_VERSION_MINOR, ENET_VERSION_PATCH);
	if (enet_initialize()) {
		THROW_FMTSTR("enet_initialize() failed");
	}
	auto linked = enet_linked_version();
	VLOGI("ENet linked:   {}.{}.{}",
		  ENET_VERSION_GET_MAJOR(linked), ENET_VERSION_GET_MINOR(linked), ENET_VERSION_GET_PATCH(linked));
	
	std::atexit([] {
		enet_deinitialize();
	});
}
static ENetAddress resolve(const char *addr, const char *port) {
	ENetAddress na;
	if (enet_address_set_host(&na, addr)) {
		THROW_FMTSTR("resolve() enet_address_set_host() failed - \"{}\"", addr);
	}
	if ((na.port = std::atoi(port)) < 1024) {
		THROW_FMTSTR("resolve() invalid port < 1024 - \"{}\"", port);
	}
	return na;
}

class ENet_Socket_Impl : public ENet_Socket {
public:
	ENetHost* host = nullptr;
	ENetPeer* peer = nullptr;
	bool is_ok = false;
	
	std::vector<uint8_t> buf_read;
	std::vector<uint8_t> buf_write;
	size_t i_read = 0;
	
	~ENet_Socket_Impl() {
		enet_peer_disconnect_now(peer, 0);
		enet_peer_reset(peer);
		if (host) {
			enet_host_destroy(host);
		}
	}
	size_t read(void *buf, size_t buf_size) override {
		if (i_read + buf_size > buf_read.size()) {
			THROW_FMTSTR("socket read() buffer is empty");
		}
		std::memcpy(buf, buf_read.data() + i_read, buf_size);
		
		i_read += buf_size;
		constexpr size_t shift = 64 * 1024;
		while (i_read >= shift) {
			i_read -= shift;
			buf_read.erase(buf_read.begin(), buf_read.begin() + shift);
		}
		
		return buf_size;
	}
	size_t write(const void *buf, size_t buf_size) override {
		auto b = static_cast<const uint8_t *>(buf);
		buf_write.insert(buf_write.end(), b, b + buf_size);
		return buf_size;
	}
	void flush_packet() override {
		enet_peer_send(peer, 0, enet_packet_create(buf_write.data(), buf_write.size(), ENET_PACKET_FLAG_RELIABLE));
		buf_write.clear();
	}
	bool has_packets() override {
		return i_read < buf_read.size();
	}
	void on_packet(ENetPacket* pkt) {
		buf_read.insert(buf_read.end(), pkt->data, pkt->data + pkt->dataLength);
		enet_packet_destroy(pkt);
	}
	void update() override {
		while (true) {
			ENetEvent ev;
			int ret = enet_host_service(host, &ev, 0);
			if (ret <= 0) {
				if (ret) {
					THROW_FMTSTR("enet_host_service() failed");
				}
				break;
			}
			switch (ev.type) {
				case ENET_EVENT_TYPE_CONNECT:
					is_ok = true;
					break;
				case ENET_EVENT_TYPE_DISCONNECT:
					THROW_FMTSTR("Socket disconnect event");
					break;
				case ENET_EVENT_TYPE_RECEIVE:
					on_packet(ev.packet);
					break;
				case ENET_EVENT_TYPE_NONE:
					break;
			}
		}
	}
};
ENet_Socket* ENet_Socket::connect(const char *addr, const char *port) {
	net_init();
	ENetAddress na = resolve(addr, port);
	
	ENetHost* host = enet_host_create(0, 32, 1, 0, 0);
	if (!host) {
		THROW_FMTSTR("enet_host_create() failed (socket)");
	}
	
	ENetPeer* peer = enet_host_connect(host, &na, 1, Greeting);
	if (!peer) {
		THROW_FMTSTR("enet_host_connect() failed");
	}
	
	auto p = new ENet_Socket_Impl;
	p->host = host;
	p->peer = peer;
	
	VLOGD("ENet_Socket::connect() waiting");
	while (!p->is_ok) {
		p->update();
		sleep(TimeSpan::ms(50));
	}
	return p;
}

class ENet_Server_Impl : public ENet_Server {
public:
	ENetHost* host = nullptr;
	std::vector<ENet_Socket_Impl*> conns;
	
	~ENet_Server_Impl() {
		enet_host_destroy(host);
	}
	std::unique_ptr<ENet_Socket> accept() override {
		while (true) {
			for (auto& c : conns) {
				if (!c->is_ok) {
					c->is_ok = true;
					return std::unique_ptr<ENet_Socket>(c);
				}
			}
			update();
			sleep(TimeSpan::ms(50));
		}
	}
	void update() override {
		while (true) {
			ENetEvent ev;
			int ret = enet_host_service(host, &ev, 0);
			if (ret <= 0) {
				if (ret) {
					THROW_FMTSTR("enet_host_service() failed");
				}
				break;
			}
			switch (ev.type) {
				case ENET_EVENT_TYPE_CONNECT:
					if (ev.data != Greeting) {
						VLOGE("Invalid net greeting");
						enet_peer_reset(ev.peer);
					}
					else {
						auto c = conns.emplace_back(new ENet_Socket_Impl);
						c->host = nullptr;
						c->peer = ev.peer;
					}
					break;
				case ENET_EVENT_TYPE_DISCONNECT:
					THROW_FMTSTR("Socket disconnect event");
					break;
				case ENET_EVENT_TYPE_RECEIVE:
					for (auto& c : conns) {
						if (c->peer == ev.peer) {
							c->on_packet(ev.packet);
							break;
						}
					}
					break;
				case ENET_EVENT_TYPE_NONE:
					break;
			}
		}
	}
};
ENet_Server* ENet_Server::create(const char *addr, const char *port) {
	net_init();
	ENetAddress na = resolve(addr, port);
	
	ENetHost* host = enet_host_create(&na, 4, 1, 0, 0);
	if (!host) {
		THROW_FMTSTR("enet_host_create() failed (server)");
	}
	
	auto p = new ENet_Server_Impl;
	p->host = host;
	return p;
}
