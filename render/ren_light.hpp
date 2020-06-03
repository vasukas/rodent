#ifndef REN_LIGHT_HPP
#define REN_LIGHT_HPP

#include "utils/color_manip.hpp"
#include "vaslib/vas_math.hpp"

struct LevelTerrain;

struct RenLightRef
{
	RenLightRef() = default;
	RenLightRef(RenLightRef&&) noexcept;
	RenLightRef& operator=(RenLightRef&&) noexcept;
	~RenLightRef();
	
	void set_type(vec2fp ctr, float radius, float angle);
	void set_color(FColor clr);
	
private:
	friend class RenLight_Impl;
	int i = -1;
};

class RenLight
{
public:
	bool enabled = true;
	
	static RenLight& get(); ///< Returns singleton
	virtual void gen_wall_mask(const LevelTerrain& lt) = 0;
	
protected:
	friend class Postproc_Impl;
	static RenLight* init();
	virtual ~RenLight();
	
	virtual void render() = 0;
};

#endif // REN_LIGHT_HPP
