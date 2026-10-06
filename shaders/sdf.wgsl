// Signed distance fields on the GPU (ADR-027): the interpreter of the packed node program that
// spatial::packSdfTree emits and spatial::evaluatePacked runs on the CPU (src/spatial/sdf.cpp).
// Same formulas, same operation order, same stack discipline, so the two agree within float
// rounding (tests/rendering/test_sdf_gpu.cpp compares them within 1e-4, noise 1e-3).
//
// The including module declares the bindings itself, e.g.
//   @group(1) @binding(2) var<storage, read> sdfNodes: array<SdfNodeGpu>;
//   @group(1) @binding(3) var<uniform> fieldBlock: FieldBlock;
// This file includes fields.wgsl (which includes noise.wgsl); do not include either again.
//
// Program (records sdfNodes[offset .. offset + count)): a linear post-order program executed with
//   dist[8]  distance stack (sp entries), pts[8] saved points (pp entries), cur = current point.
//   primitive     childCount == 0       push d(kind, cur)
//   combination   childCount == n       n == 0: push FAR; else fold the top n entries in order
//                                       (d = dist[sp-n]; d = op(d, dist[sp-n+j])), drop them, push d
//   unary BEGIN   childCount == 0xFFFF  pts[pp++] = cur; domain ops warp cur (scale divides)
//   unary END     childCount == 1       cur = pts[--pp]; scale multiplies dist[sp-1] by scale,
//                                       displacements add their term at cur, others do nothing
// Stack over/underflow returns FAR (1e9), as the CPU does. Record layout: kind, childCount,
// fieldSlot, seed; p0 = (radius, height, rounding, offset); p1 = (size.xyz, scale); p2 =
// (axis.xyz, amount); p3 = (translation.xyz, smooth); p4 = forward rotation quaternion (xyzw) for
// rotate, else (frequency, 0, 0, 0); p5 = (frequency, speed, float(count), 0).
//
// DisplaceField samples fieldScalar(slot, worldPoint): fields live in world space, so the local
// point is taken to world with the object's localToWorld matrix (identity on the CPU/mesh path,
// which samples at the tree-local point).
#include "fields.wgsl"
#include "sdf_program.wgsl"
