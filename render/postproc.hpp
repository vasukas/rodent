#ifndef POSTPROC_HPP
#define POSTPROC_HPP

#include "vaslib/vas_time.hpp"

class Postproc
{
public:
	enum ChainIndex
	{
		CI_MAIN,
		CI_PARTS,
		
		CI_TOTAL_COUNT_INTERNAL
	};
	
	static Postproc* create_main_chain();
	
	virtual ~Postproc() = default;
	virtual void start(TimeSpan passed, ChainIndex i) = 0;
	virtual void finish(ChainIndex i) = 0; ///< Unbounds buffer
	virtual void render(ChainIndex i) = 0; ///< Draws effects
};

#endif // POSTPROC_HPP
