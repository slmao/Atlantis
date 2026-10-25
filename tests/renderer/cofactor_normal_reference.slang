// BEGIN cofactorNormal -- ADR-0134 D1 (Spec 0067 R1). Byte-identical in all
// eleven lit vertex shaders, held to tests/renderer/cofactor_normal_reference.slang
// by tests/renderer/cofactor_normal_tests.cpp; change all twelve together.
// The normal's direction under any invertible world 3x3 M is
// sign(det M) * cof(M) * n, where cof(M) = [c1 x c2 | c2 x c0 | c0 x c1] =
// det(M) * transpose(inverse(M)) for M's columns c0, c1, c2. For a conformal
// M (rotation times uniform scale) it has the direction of M * n.
float3 cofactorNormal(float4x4 objectToWorld, float3 n) {
    float3x3 columns = transpose((float3x3)objectToWorld);
    float3 c0 = columns[0];
    float3 c1 = columns[1];
    float3 c2 = columns[2];
    float3 c1xc2 = cross(c1, c2);
    float3 normal = c1xc2 * n.x + cross(c2, c0) * n.y + cross(c0, c1) * n.z;
    return dot(c0, c1xc2) < 0.0 ? -normal : normal;
}
// END cofactorNormal
