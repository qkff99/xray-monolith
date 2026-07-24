#include "stdafx.h"
#include "dxStatsRender.h"
#include "../../xrEngine/GameFont.h"
#include "dxRenderDeviceRender.h"
#include "occRasterizer.h"

void dxStatsRender::Copy(IStatsRender& _in)
{
	*this = *((dxStatsRender*)&_in);
}

void dxStatsRender::OutData1(CGameFont& F)
{
	F.OutNext("VERT:        %d/%d", RCache.stat.verts, RCache.stat.calls ? RCache.stat.verts / RCache.stat.calls : 0);
	F.OutNext("POLY:        %d/%d", RCache.stat.polys, RCache.stat.calls ? RCache.stat.polys / RCache.stat.calls : 0);
	F.OutNext("DIP/DP:      %d", RCache.stat.calls);
}

void dxStatsRender::OutData2(CGameFont& F)
{
#ifdef DEBUG
	F.OutNext	("SH/T/M/C:    %d/%d/%d/%d",RCache.stat.states,RCache.stat.textures,RCache.stat.matrices,RCache.stat.constants);
	F.OutNext	("RT/PS/VS:    %d/%d/%d",	RCache.stat.target_rt,RCache.stat.ps,RCache.stat.vs);
	F.OutNext	("DCL/VB/IB:   %d/%d/%d",   RCache.stat.decl,RCache.stat.vb,RCache.stat.ib);
#endif
}

void dxStatsRender::OutData3(CGameFont& F)
{
	F.OutNext("xforms:      %d", RCache.stat.xforms);
}

void dxStatsRender::OutData4(CGameFont& F)
{
	F.OutNext("static:        %3.1f/%d", RCache.stat.r.s_static.verts / 1024.f, RCache.stat.r.s_static.dips);
	F.OutNext("flora:         %3.1f/%d", RCache.stat.r.s_flora.verts / 1024.f, RCache.stat.r.s_flora.dips);
	F.OutNext("  flora_lods:  %3.1f/%d", RCache.stat.r.s_flora_lods.verts / 1024.f, RCache.stat.r.s_flora_lods.dips);
	F.OutNext("dynamic:       %3.1f/%d", RCache.stat.r.s_dynamic.verts / 1024.f, RCache.stat.r.s_dynamic.dips);
	F.OutNext("  dynamic_sw:  %3.1f/%d", RCache.stat.r.s_dynamic_sw.verts / 1024.f, RCache.stat.r.s_dynamic_sw.dips);
	F.OutNext("  dynamic_inst:%3.1f/%d", RCache.stat.r.s_dynamic_inst.verts / 1024.f,
	          RCache.stat.r.s_dynamic_inst.dips);
	F.OutNext("  dynamic_1B:  %3.1f/%d", RCache.stat.r.s_dynamic_1B.verts / 1024.f, RCache.stat.r.s_dynamic_1B.dips);
	F.OutNext("  dynamic_2B:  %3.1f/%d", RCache.stat.r.s_dynamic_2B.verts / 1024.f, RCache.stat.r.s_dynamic_2B.dips);
	F.OutNext("  dynamic_3B:  %3.1f/%d", RCache.stat.r.s_dynamic_3B.verts / 1024.f, RCache.stat.r.s_dynamic_3B.dips);
	F.OutNext("  dynamic_4B:  %3.1f/%d", RCache.stat.r.s_dynamic_4B.verts / 1024.f, RCache.stat.r.s_dynamic_4B.dips);
	F.OutNext("details:       %3.1f/%d", RCache.stat.r.s_details.verts / 1024.f, RCache.stat.r.s_details.dips);

	if (ps_r__portal_traverse_stats)
	{
		const PortalTraverseDebugStats& pstats = PortalTraverseDbg_Peek();
		const occRasterizerStats hom_stats = Raster.get_stats();
		const float hom_test_ms = 1000.f * float(double(hom_stats.ticks) / double(CPU::qpc_freq));
		const float hom_clear_ms = 1000.f * float(double(hom_stats.clear_ticks) / double(CPU::qpc_freq));
		const float hom_mip_ms = 1000.f * float(double(hom_stats.mip_ticks) / double(CPU::qpc_freq));
		const float portal_clip_ms = 1000.f * float(double(pstats.portal_clip_ticks) / double(CPU::qpc_freq));
		const float portal_hom_ms = 1000.f * float(double(pstats.portal_hom_ticks) / double(CPU::qpc_freq));
		const float packet_serial_sort_ms = 1000.f * float(double(pstats.packet_serial_sort_ticks) / double(CPU::qpc_freq));
		const float packet_parallel_sort_ms = 1000.f * float(double(pstats.packet_parallel_sort_ticks) / double(CPU::qpc_freq));
		const float scissor_coverage = pstats.scissor_sector_rects ?
			float(double(pstats.scissor_area_ppm) / double(pstats.scissor_sector_rects) / 10000.0) : 100.f;
		F.OutSkip();
		F.OutNext(" **** Visibility (%u) **** ", pstats.frame_id);
		F.OutNext("HOM: build[%2.2fms] test[%2.2fms/%u] cells[%llu/%llu]", Device.Statistic->RenderCALC_HOM.result,
			hom_test_ms, hom_stats.tests, hom_stats.cells, hom_stats.tests ? hom_stats.cells / hom_stats.tests : 0);
		F.OutNext("HOM build: clear[%2.3fms] mip[%2.3fms] simd-diff[%u]", hom_clear_ms, hom_mip_ms,
			hom_stats.mip_validation_failures);
		F.OutNext("HOM L%d: mip[%u/%u/%u/%u] reject[%u] l0[%u]", ps_r__hom_hierarchy, hom_stats.start_mip[0],
			hom_stats.start_mip[1], hom_stats.start_mip[2], hom_stats.start_mip[3], hom_stats.coarse_rejects,
			hom_stats.level0_fallbacks);
		if (ps_r__hom_hierarchy == 2)
			F.OutNext("HOM compare: diff[%u] false-hidden[%u] legacy-cells[%llu]", hom_stats.mismatches,
				hom_stats.false_hidden, hom_stats.legacy_cells);
		F.OutNext("cull: fr[%u] hom[%u] ssa[%u] sec[%u] shrecv[%u]", pstats.culled_frustum, pstats.culled_hom,
			pstats.culled_ssa, pstats.culled_sector, pstats.culled_shadow_receiver);
		F.OutNext("shadow: reject st[%u] dyn[%u] dt[%u] draw sun[%u] local[%u]", pstats.culled_shadow_receiver_static,
			pstats.culled_shadow_receiver_dynamic, pstats.culled_shadow_details, pstats.shadow_sun_draw_calls,
			pstats.shadow_local_draw_calls);
		F.OutNext("trv: all[%u] opt[%u] noopt[%u]", pstats.traverse_calls, pstats.traverse_calls_with_options, pstats.traverse_calls_without_options);
        F.OutNext("frustum: push[%u] o[%u] n[%u] max[%u] o[%u] n[%u]", pstats.frustums_pushed, pstats.frustums_pushed_opt,
            pstats.frustums_pushed_noopt, pstats.max_frustums_in_sector, pstats.max_frustums_in_sector_opt, pstats.max_frustums_in_sector_noopt);
        F.OutNext("portal: chk[%u] o[%u] n[%u] rec[%u] o[%u] n[%u]", pstats.portals_checked, pstats.portals_checked_opt,
            pstats.portals_checked_noopt, pstats.portals_recursed, pstats.portals_recursed_opt, pstats.portals_recursed_noopt);
        F.OutNext("portal: visSkip[%u]", pstats.portals_skipped_already_visited);
        F.OutNext("rej: sph[%u] sec[%u] ssa[%u] clip[%u] hom[%u]", pstats.portals_rejected_sphere, pstats.portals_rejected_sector,
            pstats.portals_rejected_ssa, pstats.portals_rejected_clip, pstats.portals_rejected_hom);
		F.OutNext("portal work: clip[%u/%2.2fms] hom[%u/%2.2fms]", pstats.portal_clip_tests, portal_clip_ms,
			pstats.portal_hom_tests, portal_hom_ms);
		F.OutNext("portal dedup: cmp[%u] exact[%u]", pstats.frustum_duplicate_comparisons,
			pstats.frustums_skipped_exact_duplicate);
		F.OutNext("portal debug: taken[%u] fixed-precedence[%u]", pstats.portals_debug_branch_taken,
			pstats.portals_debug_branch_precedence_hits);
		F.OutNext("scissor diag: candidates[%u] fallback[%u] restricted[%u/%u] cover[%2.1f%%]",
			pstats.scissor_candidates, pstats.scissor_near_fallbacks, pstats.scissor_restricted_sectors,
			pstats.scissor_sector_rects, scissor_coverage);
        F.OutNext("static: sec[%u] o[%u] n[%u] fr[%u] o[%u] n[%u] root[%u] o[%u] n[%u]", pstats.static_sector_nodes,
            pstats.static_sector_nodes_opt, pstats.static_sector_nodes_noopt, pstats.static_frustum_nodes, pstats.static_frustum_nodes_opt,
            pstats.static_frustum_nodes_noopt, pstats.static_add_root_calls, pstats.static_add_root_calls_opt, pstats.static_add_root_calls_noopt);
        F.OutNext("dynamic: sp[%u] o[%u] n[%u] tst[%u] o[%u] n[%u]", pstats.dynamic_spatials, pstats.dynamic_spatials_opt,
            pstats.dynamic_spatials_noopt, pstats.dynamic_frustum_tests, pstats.dynamic_frustum_tests_opt, pstats.dynamic_frustum_tests_noopt);
        F.OutNext("dynamic: hit[%u] o[%u] n[%u] rnd[%u] o[%u] n[%u]", pstats.dynamic_frustum_hits, pstats.dynamic_frustum_hits_opt,
            pstats.dynamic_frustum_hits_noopt, pstats.dynamic_rendered, pstats.dynamic_rendered_opt, pstats.dynamic_rendered_noopt);
		F.OutNext("submit: st[%u] o[%u] n[%u] dyn[%u] o[%u] n[%u]", pstats.queue_static_packets, pstats.queue_static_packets_opt,
            pstats.queue_static_packets_noopt, pstats.queue_dynamic_packets, pstats.queue_dynamic_packets_opt, pstats.queue_dynamic_packets_noopt);
		F.OutNext("packet sort: serial[%u/%llu/%2.2fms] parallel[%u/%llu/%2.2fms] skip[%u] max[%u]",
			pstats.packet_serial_sorts, pstats.packet_serial_sort_items, packet_serial_sort_ms,
			pstats.packet_parallel_sorts, pstats.packet_parallel_sort_items, packet_parallel_sort_ms,
			pstats.packet_sort_skipped, pstats.packet_sort_max_items);
		F.OutNext("packet state: binds[%u] key-collisions[%u]", pstats.packet_state_binds,
			pstats.packet_key_collisions);
        F.OutNext("dedup: seen[%u] o[%u] n[%u] skip[%u] o[%u] n[%u] fr[%u] tail[%u]", pstats.static_dedup_seen,
            pstats.static_dedup_seen_opt, pstats.static_dedup_seen_noopt, pstats.static_dedup_skipped,
            pstats.static_dedup_skipped_opt, pstats.static_dedup_skipped_noopt, pstats.static_frustum_tests,
            pstats.static_frustum_tail_skipped);
    }
}

void dxStatsRender::GuardVerts(CGameFont& F)
{
	if (RCache.stat.verts > 500000) F.OutNext("Verts     > 500k: %d", RCache.stat.verts);
}

void dxStatsRender::GuardDrawCalls(CGameFont& F)
{
	if (RCache.stat.calls > 1000) F.OutNext("DIP/DP    > 1k:   %d", RCache.stat.calls);
}

void dxStatsRender::SetDrawParams(IRenderDeviceRender* pRender)
{
	dxRenderDeviceRender* pR = (dxRenderDeviceRender*)pRender;

	RCache.set_xform_world(Fidentity);
	RCache.set_Shader(pR->m_SelectionShader);
	RCache.set_c("tfactor", 1, 1, 1, 1);
}
