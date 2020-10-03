#ifndef ENET_WRAP_HPP
#define ENET_WRAP_HPP

#include "vaslib/vas_file.hpp"

class ENet_Socket : public File {
public:
	static ENet_Socket* connect(const char *addr, const char *port); ///< Blocking. Throws on error
	virtual ~ENet_Socket() = default;
	
	// Note: read blocks until all data is received
	
	int64_t seek(int64_t, SeekWhence) override {return -1;}
	
	virtual void flush_packet() = 0; ///< Must be used on write
	virtual bool has_packets() = 0; ///< Receiving
	
	virtual void update() = 0; ///< Must be called on client
};

class ENet_Server {
public:
	static ENet_Server* create(const char *addr, const char *port); ///< Blocking. Throws on error
	virtual ~ENet_Server() = default;
	
	// Note: must exist longer than accepted connections
	
	virtual std::unique_ptr<ENet_Socket> accept() = 0; ///< Blocking. Returns null on error
	
	virtual void update() = 0;
};

#endif // ENET_WRAP_HPP
