#include "s_objs.hpp"


EWall::EWall(const std::vector<std::vector<vec2fp>>& walls):
    phy(this, b2BodyDef{}),
    ren(this, MODEL_LEVEL_STATIC, FColor(0.75, 0.75, 0.75, 1)),
    ren_back(this, MODEL_LEVEL_GRID, FColor(0, 0.8, 1, 0.3))
{
	dbg_name = "static_walls";
	
	std::vector<b2Vec2> verts;
	for (auto& w : walls)
	{
		verts.clear();
		verts.reserve(w.size());
		for (auto& p : w) verts.push_back(conv(p));
		
		bool loop = w.front().equals( w.back(), 1e-5 );
		
		b2ChainShape shp;
		if (loop) shp.CreateLoop(verts.data(), verts.size() - 1);
		else shp.CreateChain(verts.data(), verts.size());
		
		b2FixtureDef fd;
		fd.friction = 0.15;
		fd.restitution = 0.1;
		fd.shape = &shp;
		phy.body->CreateFixture(&fd);
	}
}


static b2BodyDef EPhyBox_bd(vec2fp at)
{
	b2BodyDef bd;
	bd.type = b2_dynamicBody;
	bd.position = conv(at);
	return bd;
}
EPhyBox::EPhyBox(vec2fp at):
    phy(this, EPhyBox_bd(at)),
    ren(this, MODEL_BOX_SMALL, FColor(1, 0.4, 0, 1)),
    hlc(this, 200)
{
	b2FixtureDef fd;
	fd.friction = 0.1;
	fd.restitution = 0.5;
	phy.add_box(fd, vec2fp::one(GameConst::hsz_box_small), 8.f);
	
	hlc.hook(phy);
}
