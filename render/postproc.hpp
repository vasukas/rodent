#ifndef POSTPROC_HPP
#define POSTPROC_HPP

#include "utils/color_manip.hpp"
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"

class Texture;

class Postproc
{
public:
	static Postproc& get(); ///< Returns singleton
	
	virtual void tint_reset() = 0; ///< Resets sequence, keeps current state
	virtual void tint_seq(TimeSpan time_to_reach, FColor target_mul, FColor target_add = FColor(0,0,0,0)) = 0;
	void tint_default(TimeSpan time_to_reach) {tint_seq(time_to_reach, FColor(1,1,1,1), FColor(0,0,0,0));}
	
	virtual void capture_begin(Texture* tex) = 0; ///< Texture must have same size as window
	virtual void capture_end() = 0;
	
protected:
	friend class RenderControl_Impl;
	static Postproc* init(); ///< Creates singleton
	virtual void render() = 0;
	virtual ~Postproc();
};

#endif // POSTPROC_HPP
