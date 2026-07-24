// occRasterizer.cpp: implementation of the occRasterizer class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "occRasterizer.h"
#include "xrRender_console.h"
#include <emmintrin.h>

#if DEBUG
#include "dxRenderDeviceRender.h"
#endif

occRasterizer Raster;

void __stdcall fillDW_8x(void* _p, u32 size, u32 value)
{
	LPDWORD ptr = LPDWORD(_p);
	LPDWORD end = ptr + size;
	for (; ptr != end; ptr += 2)
	{
		ptr[0] = value;
		ptr[1] = value;
	}
}

IC __m128i max_s32(__m128i left, __m128i right)
{
	const __m128i mask = _mm_cmpgt_epi32(left, right);
	return _mm_or_si128(_mm_and_si128(mask, left), _mm_andnot_si128(mask, right));
}

IC void propagade_depth(occD* dest, const occD* src, int dim)
{
	for (int y = 0; y < dim; y++)
	{
		const occD* row0 = src + y * 2 * dim * 2;
		const occD* row1 = row0 + dim * 2;
		occD* output = dest + y * dim;
		for (int x = 0; x < dim; x += 4)
		{
			const int source_x = x * 2;
			__m128i first = max_s32(
				_mm_loadu_si128((const __m128i*)(row0 + source_x)),
				_mm_loadu_si128((const __m128i*)(row1 + source_x)));
			__m128i second = max_s32(
				_mm_loadu_si128((const __m128i*)(row0 + source_x + 4)),
				_mm_loadu_si128((const __m128i*)(row1 + source_x + 4)));

			first = max_s32(first, _mm_shuffle_epi32(first, _MM_SHUFFLE(2, 3, 0, 1)));
			second = max_s32(second, _mm_shuffle_epi32(second, _MM_SHUFFLE(2, 3, 0, 1)));
			first = _mm_shuffle_epi32(first, _MM_SHUFFLE(2, 0, 2, 0));
			second = _mm_shuffle_epi32(second, _MM_SHUFFLE(2, 0, 2, 0));
			_mm_storeu_si128((__m128i*)(output + x), _mm_unpacklo_epi64(first, second));
		}
	}
}

static bool validate_propagade_depth()
{
	constexpr int source_dim = 8;
	constexpr int dest_dim = source_dim / 2;
	occD source[source_dim * source_dim];
	occD dest[dest_dim * dest_dim];
	for (int i = 0; i < source_dim * source_dim; ++i)
		source[i] = (i & 1) ? -i * 7 : i * 11;

	propagade_depth(dest, source, dest_dim);
	for (int y = 0; y < dest_dim; ++y)
	{
		for (int x = 0; x < dest_dim; ++x)
		{
			const int source_x = x * 2;
			const occD* row0 = source + y * 2 * source_dim;
			const occD* row1 = row0 + source_dim;
			const occD expected = _max(_max(row0[source_x], row0[source_x + 1]),
				_max(row1[source_x], row1[source_x + 1]));
			if (dest[y * dest_dim + x] != expected)
				return false;
		}
	}
	return true;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

occRasterizer::occRasterizer()
#if DEBUG
:dbg_HOM_draw_initialized(false)
#endif
{
}

occRasterizer::~occRasterizer()
{
}

void occRasterizer::clear()
{
	const bool collect_stats = !!ps_r__portal_traverse_stats;
	const u64 started = collect_stats ? CPU::QPC() : 0;
	std::fill_n(&bufFrame[0][0], occ_dim * occ_dim, nullptr);
	std::fill_n(&bufDepth[0][0], occ_dim * occ_dim, 1.f);
	if (collect_stats)
	{
		u64 elapsed = CPU::QPC() - started;
		if (elapsed > CPU::qpc_overhead)
			elapsed -= CPU::qpc_overhead;
		stats_clear_ticks.store(elapsed, std::memory_order_relaxed);
	}
}

IC BOOL shared(occTri* T1, occTri* T2)
{
	if (T1 == T2) return TRUE;
	if (T1->adjacent[0] == T2) return TRUE;
	if (T1->adjacent[1] == T2) return TRUE;
	if (T1->adjacent[2] == T2) return TRUE;
	return FALSE;
}

void occRasterizer::propagade()
{
	// Clip-and-propagade zero level
	occTri** pFrame = get_frame();
	float* pDepth = get_depth();
	for (int y = 0; y < occ_dim_0; y++)
	{
		for (int x = 0; x < occ_dim_0; x++)
		{
			int ox = x + 2, oy = y + 2;

			// Y2-connect
			int pos = oy * occ_dim + ox;
			int pos_up = pos - occ_dim;
			if (pos_up < 0) pos_up = pos;
			int pos_down = pos + occ_dim;
			if (pos_down >= occ_dim_0 * occ_dim_0) pos_down = pos;
			int pos_down2 = pos_down + occ_dim;
			if (pos_down2 >= occ_dim_0 * occ_dim_0) pos_down2 = pos_down;

			occTri* Tu1 = pFrame[pos_up];
			if (Tu1)
			{
				// We has pixel 1scan up
				if (pFrame[pos_down] && shared(Tu1, pFrame[pos_down]))
				{
					// We has pixel 1scan down
					float ZR = (pDepth[pos_up] + pDepth[pos_down]) / 2;
					if (ZR < pDepth[pos])
					{
						pFrame[pos] = Tu1;
						pDepth[pos] = ZR;
					}
				}
				else if (pFrame[pos_down2] && shared(Tu1, pFrame[pos_down2]))
				{
					// We has pixel 2scan down
					float ZR = (pDepth[pos_up] + pDepth[pos_down2]) / 2;
					if (ZR < pDepth[pos])
					{
						pFrame[pos] = Tu1;
						pDepth[pos] = ZR;
					}
				}
			}

			//
			float d = pDepth[pos];
			clamp(d, -1.99f, 1.99f);
			bufDepth_0[y][x] = df_2_s32(d);
		}
	}

	// Propagate other levels
	const bool collect_stats = !!ps_r__portal_traverse_stats;
	const u64 started = collect_stats ? CPU::QPC() : 0;
	propagade_depth(&bufDepth_1[0][0], &bufDepth_0[0][0], occ_dim_1);
	propagade_depth(&bufDepth_2[0][0], &bufDepth_1[0][0], occ_dim_2);
	propagade_depth(&bufDepth_3[0][0], &bufDepth_2[0][0], occ_dim_3);
	if (collect_stats)
	{
		u64 elapsed = CPU::QPC() - started;
		if (elapsed > CPU::qpc_overhead)
			elapsed -= CPU::qpc_overhead;
		stats_mip_ticks.store(elapsed, std::memory_order_relaxed);
		stats_mip_validation_failures.store(validate_propagade_depth() ? 0 : 1, std::memory_order_relaxed);
	}
}

void occRasterizer::on_dbg_render()
{
#if DEBUG
	if( !ps_r2_ls_flags_ext.is(R_FLAGEXT_HOM_DEPTH_DRAW) )
	{
		dbg_HOM_draw_initialized = false;
		return;
	}

	for ( int i = 0; i< occ_dim_0; ++i)
	{
		for ( int j = 0; j< occ_dim_0; ++j)
		{
			if( bDebug )
			{
				Fvector quad,left_top,right_bottom,box_center,box_r;
				quad.set( (float)j-occ_dim_0/2.f, -((float)i-occ_dim_0/2.f), (float)bufDepth_0[i][j]/occQ_s32);
				Device.mProject;

				float z = -Device.mProject._43/(float)(Device.mProject._33-quad.z);
				left_top.set		( quad.x*z/Device.mProject._11/(occ_dim_0/2.f),		quad.y*z/Device.mProject._22/(occ_dim_0/2.f), z);
				right_bottom.set	( (quad.x+1)*z/Device.mProject._11/(occ_dim_0/2.f), (quad.y+1)*z/Device.mProject._22/(occ_dim_0/2.f), z);

				box_center.set		((right_bottom.x + left_top.x)/2, (right_bottom.y + left_top.y)/2, z);
				box_r = right_bottom;
				box_r.sub(box_center);

				Fmatrix inv;
				inv.invert(Device.mView);
				inv.transform( box_center );
				inv.transform_dir( box_r );

				pixel_box& tmp = dbg_pixel_boxes[ i*occ_dim_0+j];
				tmp.center	= box_center;
				tmp.radius	= box_r;
				tmp.z 		= quad.z;
				dbg_HOM_draw_initialized = true;
			}

			if( !dbg_HOM_draw_initialized )
				return;

			pixel_box& tmp = dbg_pixel_boxes[ i*occ_dim_0+j];
			Fmatrix Transform;
			Transform.identity();
			Transform.translate(tmp.center);

			// draw wire
			Device.SetNearer(TRUE);

			RCache.set_Shader	(dxRenderDeviceRender::Instance().m_SelectionShader);
			RCache.dbg_DrawOBB( Transform, tmp.radius, D3DCOLOR_XRGB(u32(255*pow(tmp.z,20.f)),u32(255*(1-pow(tmp.z,20.f))),0) );
			Device.SetNearer(FALSE);
		}
	}
#endif
}


struct occTestBounds
{
	int x0;
	int y0;
	int x1;
	int y1;
};

struct occHierarchyTestResult
{
	BOOL visible;
	u32 cells;
	u32 start_mip;
	BOOL coarse_reject;
	BOOL level0_fallback;
};

IC occTestBounds get_test_bounds(float _x0, float _y0, float _x1, float _y1)
{
	occTestBounds bounds;
	bounds.x0 = iFloor(_x0 * occ_dim_0 + .5f); clamp(bounds.x0, 0, occ_dim_0 - 1);
	bounds.x1 = iFloor(_x1 * occ_dim_0 + .5f); clamp(bounds.x1, bounds.x0, occ_dim_0 - 1);
	bounds.y0 = iFloor(_y0 * occ_dim_0 + .5f); clamp(bounds.y0, 0, occ_dim_0 - 1);
	bounds.y1 = iFloor(_y1 * occ_dim_0 + .5f); clamp(bounds.y1, bounds.y0, occ_dim_0 - 1);
	return bounds;
}

IC int select_start_mip(const occTestBounds& bounds)
{
	const int span_x = bounds.x1 - bounds.x0 + 1;
	const int span_y = bounds.y1 - bounds.y0 + 1;
	int span = span_x > span_y ? span_x : span_y;
	int level = 0;
	while (level < 3 && span > 4)
	{
		span = (span + 1) / 2;
		++level;
	}
	return level;
}

IC BOOL test_Level(occD* depth, int dim, int x0, int y0, int x1, int y1, occD z, u32* tested_cells)
{
	const u32 row_width = u32(x1 - x0 + 1);

	for (int y = y0; y <= y1; y++)
	{
		occD* base = depth + y * dim;
		occD* it = base + x0;
		occD* end = base + x1;
		for (; it <= end; it++)
			if (z < *it)
			{
				if (tested_cells)
					*tested_cells = u32(y - y0) * row_width + u32(it - (base + x0)) + 1;
				return TRUE;
			}
	}
	if (tested_cells)
		*tested_cells = u32(y1 - y0 + 1) * row_width;
	return FALSE;
}

static occHierarchyTestResult test_hierarchy(occRasterizer& raster, const occTestBounds& bounds, occD z, bool collect_stats)
{
	occHierarchyTestResult result = {};
	result.start_mip = select_start_mip(bounds);

	for (int level = int(result.start_mip); level >= 0; --level)
	{
		if (level == 0 && result.start_mip > 0)
			result.level0_fallback = TRUE;

		u32 level_cells = 0;
		const BOOL visible = test_Level(raster.get_depth_level(level), occ_dim_0 >> level,
			bounds.x0 >> level, bounds.y0 >> level, bounds.x1 >> level, bounds.y1 >> level, z,
			collect_stats ? &level_cells : nullptr);
		result.cells += level_cells;
		if (!visible)
		{
			result.coarse_reject = level > 0;
			return result;
		}
	}

	result.visible = TRUE;
	return result;
}


BOOL occRasterizer::test(float _x0, float _y0, float _x1, float _y1, float _z)
{
	const occD z = df_2_s32up(_z) + 1;
	const occTestBounds bounds = get_test_bounds(_x0, _y0, _x1, _y1);
	const bool collect_stats = !!ps_r__portal_traverse_stats;
	const u64 started = collect_stats ? CPU::QPC() : 0;
	occHierarchyTestResult hierarchy = {};
	u32 legacy_cells = 0;
	BOOL result;

	if (ps_r__hom_hierarchy == 0)
	{
		hierarchy.start_mip = 0;
		result = test_Level(get_depth_level(0), occ_dim_0, bounds.x0, bounds.y0, bounds.x1, bounds.y1, z,
			collect_stats ? &hierarchy.cells : nullptr);
	}
	else
	{
		hierarchy = test_hierarchy(*this, bounds, z, collect_stats);
		result = hierarchy.visible;
		if (ps_r__hom_hierarchy == 2)
			result = test_Level(get_depth_level(0), occ_dim_0, bounds.x0, bounds.y0, bounds.x1, bounds.y1, z,
				collect_stats ? &legacy_cells : nullptr);
	}

	if (!collect_stats)
		return result;

	u64 elapsed = CPU::QPC() - started;
	if (elapsed > CPU::qpc_overhead)
		elapsed -= CPU::qpc_overhead;

	stats_tests.fetch_add(1, std::memory_order_relaxed);
	stats_cells.fetch_add(hierarchy.cells, std::memory_order_relaxed);
	stats_ticks.fetch_add(elapsed, std::memory_order_relaxed);
	stats_start_mip[hierarchy.start_mip].fetch_add(1, std::memory_order_relaxed);
	if (hierarchy.coarse_reject)
		stats_coarse_rejects.fetch_add(1, std::memory_order_relaxed);
	if (hierarchy.level0_fallback)
		stats_level0_fallbacks.fetch_add(1, std::memory_order_relaxed);
	if (ps_r__hom_hierarchy == 2)
	{
		stats_legacy_cells.fetch_add(legacy_cells, std::memory_order_relaxed);
		if (hierarchy.visible != result)
		{
			stats_mismatches.fetch_add(1, std::memory_order_relaxed);
			if (!hierarchy.visible && result)
				stats_false_hidden.fetch_add(1, std::memory_order_relaxed);
		}
	}
	return result;
}

void occRasterizer::reset_stats()
{
	stats_tests.store(0, std::memory_order_relaxed);
	stats_cells.store(0, std::memory_order_relaxed);
	stats_ticks.store(0, std::memory_order_relaxed);
	stats_clear_ticks.store(0, std::memory_order_relaxed);
	stats_mip_ticks.store(0, std::memory_order_relaxed);
	stats_mip_validation_failures.store(0, std::memory_order_relaxed);
	for (auto& mip : stats_start_mip)
		mip.store(0, std::memory_order_relaxed);
	stats_coarse_rejects.store(0, std::memory_order_relaxed);
	stats_level0_fallbacks.store(0, std::memory_order_relaxed);
	stats_mismatches.store(0, std::memory_order_relaxed);
	stats_false_hidden.store(0, std::memory_order_relaxed);
	stats_legacy_cells.store(0, std::memory_order_relaxed);
}

occRasterizerStats occRasterizer::get_stats() const
{
	occRasterizerStats result = {};
	result.tests = stats_tests.load(std::memory_order_relaxed);
	result.cells = stats_cells.load(std::memory_order_relaxed);
	result.ticks = stats_ticks.load(std::memory_order_relaxed);
	result.clear_ticks = stats_clear_ticks.load(std::memory_order_relaxed);
	result.mip_ticks = stats_mip_ticks.load(std::memory_order_relaxed);
	result.mip_validation_failures = stats_mip_validation_failures.load(std::memory_order_relaxed);
	for (u32 level = 0; level < 4; ++level)
		result.start_mip[level] = stats_start_mip[level].load(std::memory_order_relaxed);
	result.coarse_rejects = stats_coarse_rejects.load(std::memory_order_relaxed);
	result.level0_fallbacks = stats_level0_fallbacks.load(std::memory_order_relaxed);
	result.mismatches = stats_mismatches.load(std::memory_order_relaxed);
	result.false_hidden = stats_false_hidden.load(std::memory_order_relaxed);
	result.legacy_cells = stats_legacy_cells.load(std::memory_order_relaxed);
	return result;
}
