#ifndef POSTPROC_HPP
#define POSTPROC_HPP

#include "vaslib/vas_time.hpp"

class Postproc
{
public:
	static Postproc* create_main_chain();
	
	virtual ~Postproc() = default;
	virtual void start(TimeSpan passed) = 0;
	virtual void finish() = 0;
};

#endif // POSTPROC_HPP
