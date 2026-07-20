#include "stdafx.h"
#include "../../xrEngine/cl_intersect.h"
#include "../xrRender/du_cone.h"

//extern Fvector du_cone_vertices			[DU_CONE_NUMVERTEX];

BOOL tri_vs_sphere_intersect(Fvector& SC, float R, Fvector& v0, Fvector& v1, Fvector& v2)
{
	Fvector e0, e1;
	return CDB::TestSphereTri(SC, R, v0, e0.sub(v1, v0), e1.sub(v2, v0));
}

void CRenderTarget::enable_dbt_bounds(light* L)
{
	if (!RImplementation.o.nvdbt) return;
	if (!ps_r2_ls_flags.test(R2FLAG_USE_NVDBT)) return;

	u32 mask = 0xffffffff;
	EFC_Visible vis = RImplementation.ViewBase.testSphere(L->SpatialComponent->spatial.sphere.P, L->SpatialComponent->spatial.sphere.R * 1.01f, mask);
	if (vis != fcvFully) return;

	// xform BB
	Fbox BB;
	Fvector rr;
	rr.set(L->SpatialComponent->spatial.sphere.R, L->SpatialComponent->spatial.sphere.R, L->SpatialComponent->spatial.sphere.R);
	BB.setb(L->SpatialComponent->spatial.sphere.P, rr);

	Fbox bbp;
	bbp.invalidate();
	for (u32 i = 0; i < 8; i++)
	{
		Fvector pt;
		BB.getpoint(i, pt);
		Device.mFullTransform.transform(pt);
		bbp.modify(pt);
	}
	u_DBT_enable(bbp.min.z, bbp.max.z);
}

// nv-DBT
BOOL CRenderTarget::u_DBT_enable(float zMin, float zMax)
{
	if (!RImplementation.o.nvdbt) return FALSE;
	if (!ps_r2_ls_flags.test(R2FLAG_USE_NVDBT)) return FALSE;

	return FALSE;

	//	TODO: DX10: Check if DX10 supports this feature
	// enable cheat
	//HW.pDevice->SetRenderState(D3DRS_ADAPTIVETESS_X,MAKEFOURCC('N','V','D','B'));
	//HW.pDevice->SetRenderState(D3DRS_ADAPTIVETESS_Z,*(DWORD*)&zMin);
	//HW.pDevice->SetRenderState(D3DRS_ADAPTIVETESS_W,*(DWORD*)&zMax); 

	//return TRUE;
}

void CRenderTarget::u_DBT_disable()
{
	//	TODO: DX10: Check if DX10 supports this feature
	//if (RImplementation.o.nvdbt && ps_r2_ls_flags.test(R2FLAG_USE_NVDBT))	
	//	HW.pDevice->SetRenderState(D3DRS_ADAPTIVETESS_X,0);
}

BOOL CRenderTarget::enable_scissor(light* L) // true if intersects near plane
{
	// Msg	("%d: %x type(%d), pos(%f,%f,%f)",Device.dwFrame,u32(L),u32(L->flags.type),VPUSH(L->position));

	// Near plane intersection
	BOOL near_intersect = FALSE;
	{
		Fmatrix& M = Device.mFullTransform;
		Fvector4 plane;
		plane.x = -(M._14 + M._13);
		plane.y = -(M._24 + M._23);
		plane.z = -(M._34 + M._33);
		plane.w = -(M._44 + M._43);
		float denom = -1.0f / _sqrt(_sqr(plane.x) + _sqr(plane.y) + _sqr(plane.z));
		plane.mul(denom);
		Fplane P;
		P.n.set(plane.x, plane.y, plane.z);
		P.d = plane.w;
		float p_dist = P.classify(L->SpatialComponent->spatial.sphere.P) - L->SpatialComponent->spatial.sphere.R;
		near_intersect = (p_dist <= 0);
	}
#ifdef DEBUG
	if (1)
	{
		Fsphere		S;	S.set	(L->SpatialComponent->spatial.sphere.P,L->SpatialComponent->spatial.sphere.R);
		dbg_spheres.push_back	(mk_pair(S,L->color));
	}
#endif

	RCache.set_Scissor(nullptr);

	const Fsphere& sphere = L->SpatialComponent->spatial.sphere;
	if (L->flags.type == IRender_Light::REFLECTED || near_intersect ||
		!_valid(sphere.P.x) || !_valid(sphere.P.y) || !_valid(sphere.P.z) ||
		!_valid(sphere.R) || sphere.R <= 0.f ||
		Device.vCameraPosition.distance_to_sqr(sphere.P) <= _sqr(sphere.R) ||
		dwWidth == 0 || dwHeight == 0)
	{
		return near_intersect;
	}

	float min_x = flt_max;
	float min_y = flt_max;
	float max_x = -flt_max;
	float max_y = -flt_max;

	const auto project_point = [&](const Fvector& point)
	{
		Fvector4 world;
		world.set(point.x, point.y, point.z, 1.f);
		Fvector4 clip;
		Device.mFullTransform.transform(clip, world);
		if (!_valid(clip.x) || !_valid(clip.y) || !_valid(clip.z) || !_valid(clip.w) ||
			clip.w <= EPS_S || clip.z < 0.f)
			return false;

		const float inv_w = 1.f / clip.w;
		const float screen_x = (clip.x * inv_w * .5f + .5f) * float(dwWidth);
		const float screen_y = (-clip.y * inv_w * .5f + .5f) * float(dwHeight);
		if (!_valid(screen_x) || !_valid(screen_y))
			return false;

		min_x = _min(min_x, screen_x);
		min_y = _min(min_y, screen_y);
		max_x = _max(max_x, screen_x);
		max_y = _max(max_y, screen_y);
		return true;
	};

	if (L->flags.type == IRender_Light::SPOT)
	{
		for (u32 i = 0; i < DU_CONE_NUMVERTEX; ++i)
		{
			Fvector point;
			L->m_xform.transform_tiny(point, du_cone_vertices[i]);
			if (!project_point(point))
				return near_intersect;
		}
	}
	else
	{
		Fvector radius;
		radius.set(sphere.R, sphere.R, sphere.R);
		Fbox bounds;
		bounds.setb(sphere.P, radius);

		for (u32 i = 0; i < 8; ++i)
		{
			Fvector point;
			bounds.getpoint(i, point);
			if (!project_point(point))
				return near_intersect;
		}
	}

	const float width = float(dwWidth);
	const float height = float(dwHeight);
	if (max_x <= 0.f || max_y <= 0.f || min_x >= width || min_y >= height)
		return near_intersect;

	Irect rect;
	rect.x1 = min_x <= 0.f ? 0 : clampr(iFloor(min_x) - 1, 0, int(dwWidth));
	rect.y1 = min_y <= 0.f ? 0 : clampr(iFloor(min_y) - 1, 0, int(dwHeight));
	rect.x2 = max_x >= width ? int(dwWidth) : clampr(iCeil(max_x) + 1, 0, int(dwWidth));
	rect.y2 = max_y >= height ? int(dwHeight) : clampr(iCeil(max_y) + 1, 0, int(dwHeight));
	if (rect.x1 < rect.x2 && rect.y1 < rect.y2 &&
		(rect.x1 != 0 || rect.y1 != 0 || rect.x2 != int(dwWidth) || rect.y2 != int(dwHeight)))
	{
		RCache.set_Scissor(&rect);
	}

	return near_intersect;

#if 0
	CSector*	S	= (CSector*)L->spatial.sector;
	_scissor	bb	= S->r_scissor_merged;
	Irect		R;
	R.x1		= clampr	(iFloor	(bb.min.x*Device.dwWidth),	int(0),int(Device.dwWidth));
	R.x2		= clampr	(iCeil	(bb.max.x*Device.dwWidth),	int(0),int(Device.dwWidth));
	R.y1		= clampr	(iFloor	(bb.min.y*Device.dwHeight),	int(0),int(Device.dwHeight));
	R.y2		= clampr	(iCeil	(bb.max.y*Device.dwHeight),	int(0),int(Device.dwHeight));
	if	( (Device.dwWidth==u32(R.right - R.left)) && (Device.dwHeight==u32(R.bottom-R.top)) )
	{
		// full-screen -> do nothing
	} else {
		// test if light-volume intersects scissor-rectangle
	// if it does - do nothing, if doesn't - we look on light through portal

		// 1. convert rect into -1..1 space
		Fbox2		b_pp	= bb;
		b_pp.min.x			= b_pp.min.x * 2 - 1;
		b_pp.max.x			= b_pp.max.x * 2 - 1;
		b_pp.min.y			= (1-b_pp.min.y) * 2 - 1;
		b_pp.max.y			= (1-b_pp.max.y) * 2 - 1;

		// 2. construct scissor rectangle in post-projection space
		Fvector3	s_points_pp			[4];
		s_points_pp[0].set	(bb.min.x,bb.min.y,bb.depth);	// LT
		s_points_pp[1].set	(bb.max.x,bb.min.y,bb.depth);	// RT
		s_points_pp[2].set	(bb.max.x,bb.max.y,bb.depth);	// RB
		s_points_pp[3].set	(bb.min.x,bb.max.y,bb.depth);	// LB

		// 3. convert it into world space
		Fvector3	s_points			[4];
		Fmatrix&	iVP					= Device.mInvFullTransform;
		iVP.transform	(s_points[0],s_points_pp[0]);
		iVP.transform	(s_points[1],s_points_pp[1]);
		iVP.transform	(s_points[2],s_points_pp[2]);
		iVP.transform	(s_points[3],s_points_pp[3]);

		// 4. check intersection of two triangles with (spatial, enclosing) sphere
		BOOL	bIntersect	= tri_vs_sphere_intersect	(
			L->spatial.sphere.P,L->spatial.sphere.R,
			s_points[0],s_points[1],s_points[2]
			);
		if (!bIntersect)	bIntersect	= tri_vs_sphere_intersect	(
				L->spatial.sphere.P,L->spatial.sphere.R,
				s_points[2],s_points[3],s_points[0]
				);
		if (!bIntersect)	{
			// volume doesn't touch scissor - enable mask
			RCache.set_Scissor(&R);
			//CHK_DX		(HW.pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
			//CHK_DX		(HW.pDevice->SetScissorRect(&R));
		} else {
			// __asm int 3;
			RCache.set_Scissor(NULL);
		}
	}

	return near_intersect;
#endif
}

/*
{
	Fmatrix& M						= RCache.xforms.m_wvp;
	BOOL	bIntersect				= FALSE;
	for (u32 vit=0; vit<DU_CONE_NUMVERTEX; vit++)	{
		Fvector&	v	= du_cone_vertices[vit];
		float _z = v.x*M._13 + v.y*M._23 + v.z*M._33 + M._43;
		float _w = v.x*M._14 + v.y*M._24 + v.z*M._34 + M._44;
		if (_z<=0 || _w<=0)	{
			bIntersect	= TRUE;
			break;
		}
	}
}
*/
