#include <algorithm>
#include <functional>
#include <memory.h>
#include "vas_atlas_packer.hpp"
#include "vas_cpp_utils.hpp"
#include "vas_log.hpp"



bool AtlasPacker::InputInfo::operator <( const InputInfo& im )
{
	return w * h < im.w * im.h;
}
AtlasPacker::AtlasPacker()
{
	reset();
}
void AtlasPacker::reset()
{
	ims.clear();
}
void AtlasPacker::add( const InputInfo& info )
{
	for (auto &im : ims)
		if (im.id == info.id)
			THROW_FMTSTR( "AtlasPacker::add() ID {} already used", info.id );
	
	reserve_more_block( ims, 256 );
	auto &im = ims.emplace_back();
	
	im.id = info.id;
	im.w = info.w;
	im.h = info.h;
	im.spacing = info.spacing >= 0 ? info.spacing : space_size;
}
AtlasPacker::SpriteData* AtlasPacker::get( uint64_t id )
{
	for (auto &im : ims) if (im.id == id) return &im;
	return nullptr;
}
std::vector <AtlasPacker::AtlasInfo> AtlasPacker::build()
{
	// border size
	int border = (space_borders? space_size * 2 : 0);
	
	// calculate size without borders, in min-size
	int real_size = max_size / min_size - border;
	
	// sort sprites from biggest to least (by area)
	std::sort( ims.rbegin(), ims.rend() );
	
	// atlas builder info
	struct Atlas
	{
		size_t count = 0; // number of images in it
		std::vector <uint8_t> is_occ; // is_occupied flag (per min-size), without atlas borders
		int y_min = 0, y_max = 0; // current minimal and maximal height
		// y_min never modified :(
	};
	std::vector <Atlas> ats;
	
	auto occupied = [&]( Atlas& at, int x, int y ) -> uint8_t& { return at.is_occ[ y * real_size + x ];};
	
	// builder func
	std::function<bool(size_t, SpriteData&)> place =
	[&]( size_t at_ix, SpriteData& im ) -> bool
	{
		Atlas& at = ats[ at_ix ];
		
		// get sprite size in min-size
		int iw = im.w + im.spacing * 2;
		int ih = im.h + im.spacing * 2;
		
		iw = iw / min_size + (iw % min_size ? 1 : 0);
		ih = ih / min_size + (ih % min_size ? 1 : 0);
		
		// check for max height
		int req_ht = at.y_min + ih;
		if (req_ht > at.y_max)
		{
			if (req_ht > real_size) return false;
			at.y_max = req_ht;
		}
		
		// for all lines
		for (int y = at.y_min; y < at.y_max; ++y)
		for (int x = 0; x < real_size; ++x)
		{
			for (int iy = 0; iy < ih; ++iy)
			for (int ix = 0; ix < iw; ++ix)
			{
				if (occupied( at, x + ix, y + iy ))
					goto fail;
			}
			
			// found
			
			im.index = at_ix;
			im.x = x * min_size + im.spacing;
			im.y = y * min_size + im.spacing;
			
			for (int iy = 0; iy < ih; ++iy)
			for (int ix = 0; ix < iw; ++ix)
				occupied( at, x + ix, y + iy ) = 1;
			
			++at.count;
			return true;
			
			fail:;
		}
		
		// over-increase size
		if (req_ht > real_size)
		{
			if (at.y_max == real_size) return false;
			req_ht = real_size;
		}
		at.y_max = req_ht;
		return place( at_ix, im );
	};
	
	// placement loop
	for (auto& im : ims)
	{
		size_t at_ix = 0;
		for (; at_ix < ats.size(); ++at_ix)
		{
			if (place( at_ix, im ))
				break;
		}
		if (at_ix == ats.size())
		{
			// add new atlas
			ats.emplace_back().is_occ.resize( real_size * real_size );
			
			if (!place( at_ix, im ))
				THROW_FMTSTR( "AtlasPacker::build() sprite {} is bigger than max_size", im.id );
		}
	}
	
	// make output atlases
	std::vector <AtlasPacker::AtlasInfo> out_as;
	out_as.reserve( ats.size() );
	
	for (auto& at : ats)
	{
		int max_x = 0;
		int max_y = 0;
		
		for (int y = at.y_max - 1; y >= 0; --y)
		{
			int x = real_size;
			for (; x > 0; --x)
				if (occupied( at, x - 1, y ))
					break;
			
			if (!max_y && x) max_y = y + 1;
			max_x = std::max( max_x, x );
		}
		
		auto& out = out_as.emplace_back();
		out.w = max_x * min_size + border;
		out.h = max_y * min_size + border;
		out.sprs.reserve( at.count );
		
		for (auto& im : ims)
			if (im.index == out_as.size() - 1)
				out.sprs.push_back( im );
	}
	return out_as;
}



enum
{
	MEM_STATIC,
	MEM_NEW,
	MEM_MALLOC
};
AtlasBuilder::~AtlasBuilder()
{
	reset();
}
void AtlasBuilder::reset()
{
	if (pk) pk->reset();
	for (auto& d : ds)
	{
		if		(d.type == MEM_NEW) delete[] d.px;
		else if	(d.type == MEM_MALLOC) free( d.px );
	}
	ds.clear();
}
void AtlasBuilder::add_static( const AtlasPacker::InputInfo& info, const void *data, size_t pitch )
{
	add( info, data, pitch, MEM_STATIC );
}
void AtlasBuilder::add_copy( const AtlasPacker::InputInfo& info, const void *data, size_t pitch )
{
	ASSERT( pk, "AtlasBuilder null" );
	
	int wd = info.w * pk->bpp;
	uint8_t *px = new uint8_t [ info.h * wd ];
	
	if (!pitch) pitch = wd;
	auto src = static_cast< const uint8_t * >(data);
	
	for (int y = 0; y < info.h; ++y) memcpy( px + y * wd, src + y * pitch, wd );
	add( info, px, wd, MEM_NEW );
}
void AtlasBuilder::add_move_malloc( const AtlasPacker::InputInfo& info, void *data, size_t pitch )
{
	add( info, data, pitch, MEM_MALLOC );
}
void AtlasBuilder::add_move_new( const AtlasPacker::InputInfo& info, void *data, size_t pitch )
{
	add( info, data, pitch, MEM_NEW );
}
void AtlasBuilder::add( const AtlasPacker::InputInfo& info, const void *data, size_t pitch, int type )
{
	ASSERT( pk, "AtlasBuilder null" );
	pk->add( info );
	
	reserve_more_block( ds, 256 );
	auto& d = ds.emplace_back();
	
	d.id = info.id;
	d.pitch = pitch ? pitch : info.w * pk->bpp;
	d.px = static_cast< uint8_t* >( const_cast< void* >(data) );
	d.type = type;
}
std::vector <AtlasBuilder::AtlasData> AtlasBuilder::build()
{
	ASSERT( pk, "AtlasBuilder null" );
	auto as = pk->build();
	
	std::vector <AtlasBuilder::AtlasData> bs;
	bs.reserve( as.size() );
	
	for (auto& at : as)
	{
		auto& b = bs.emplace_back();
		b.info = std::move( at );
		b.px.resize( at.w * at.h * pk->bpp );
		
		for (auto& si : b.info.sprs)
		{
			auto di = std::find_if( ds.begin(), ds.end(), [&](auto&& e){ return e.id == si.id; } );
			if (di == ds.end()) continue;
			if (!di->px) continue; // for space glyph with zero size
			
			for (int y = 0; y < si.h; ++y)
			{
				size_t dst = pk->bpp * ((y + si.y) * at.w + si.x);
				size_t src = y * di->pitch;
				std::memcpy( b.px.data() + dst, di->px + src, si.w * pk->bpp );
			}
		}
	}
	return bs;
}
