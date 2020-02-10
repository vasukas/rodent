#ifndef TCP_NET_HPP
#define TCP_NET_HPP

#include "vaslib/vas_file.hpp"

class TCP_Socket : public File
{
public:
	static TCP_Socket* connect(const char *addr, const char *port); ///< Blocking. Throws on error
	virtual ~TCP_Socket() = default;
	
	// Note: read blocks until all data is received
	
	int64_t seek(int64_t, SeekWhence) override {return -1;}
};

class TCP_Server
{
public:
	static TCP_Server* create(const char *addr, const char *port); ///< Blocking. Throws on error
	virtual ~TCP_Server() = default;
	
	virtual std::unique_ptr<TCP_Socket> accept() = 0; ///< Blocking. Returns null on error
};

#endif // TCP_NET_HPP
