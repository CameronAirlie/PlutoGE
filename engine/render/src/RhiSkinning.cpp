#include "RhiSkinning.h"

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Mesh.h"
#include <cmath>
#include <limits>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define PLUTO_SKINNING_SSE2 1
#endif

namespace PlutoGE::render
{
    // The RHI's shared vertex stream is consumed by lit, transparent, CSM and
    // virtual-shadow passes. Deform once per mesh/pose, rather than separately
    // in each material/pass. The supplied matrices already include inverse bind.
    RhiSkinningBounds SkinRhiVerticesInto(std::span<const MeshVertexData> source,
                                                   std::span<const glm::mat4> joints,
                                                   std::span<const BasicVertex> previous,
                                                   std::vector<BasicVertex> &result)
    {
        result.resize(source.size());
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        for (std::size_t index = 0; index < source.size(); ++index)
        {
            const auto &vertex = source[index];
            // Keep only the affine 3x4 portion for deformation below.
            float m[12]{};
#if defined(PLUTO_SKINNING_SSE2)
            // Matrix columns are contiguous in GLM. Blend four lanes at once;
            // unaligned loads impose no alignment requirement on the palette.
            __m128 columns[4] = {_mm_setzero_ps(), _mm_setzero_ps(), _mm_setzero_ps(), _mm_setzero_ps()};
#endif
            float totalWeight = 0;
            for (unsigned influence=0; influence<4; ++influence)
            {
                const int joint=vertex.joints[influence];
                const float weight=vertex.weights[influence];
                if (joint<0 || static_cast<std::size_t>(joint)>=joints.size() ||
                    !std::isfinite(weight) || weight<=0) continue;
                const float *bone=&joints[joint][0][0];
#if defined(PLUTO_SKINNING_SSE2)
                const __m128 weights = _mm_set1_ps(weight);
                for (unsigned column = 0; column < 4; ++column)
                    columns[column] = _mm_add_ps(columns[column], _mm_mul_ps(_mm_loadu_ps(bone + column * 4), weights));
#else
                for (unsigned column=0;column<4;++column)
                    for (unsigned row=0;row<3;++row)
                        m[column*3+row]+=bone[column*4+row]*weight;
#endif
                totalWeight+=weight;
            }
#if defined(PLUTO_SKINNING_SSE2)
            alignas(16) float blended[4];
            for (unsigned column = 0; column < 4; ++column)
            {
                _mm_store_ps(blended, columns[column]);
                for (unsigned row = 0; row < 3; ++row) m[column * 3 + row] = blended[row];
            }
#endif
            if (totalWeight>.0001f) {
                const float inverse=1/totalWeight;
                for (float &value:m) value*=inverse;
            } else {
                for (float &value:m) value=0;
                m[0]=m[4]=m[8]=1;
            }
            BasicVertex output{};
            for (unsigned row=0;row<3;++row)
                output.position[row]=m[row]*vertex.position[0]+m[3+row]*vertex.position[1]+m[6+row]*vertex.position[2]+m[9+row];
            // Cofactor columns give inverse-transpose normals. Normalization
            // cancels determinant magnitude; retain its sign for reflections.
            const float cofactor[9]={
                m[4]*m[8]-m[5]*m[7], m[5]*m[6]-m[3]*m[8], m[3]*m[7]-m[4]*m[6],
                m[7]*m[2]-m[8]*m[1], m[8]*m[0]-m[6]*m[2], m[6]*m[1]-m[7]*m[0],
                m[1]*m[5]-m[2]*m[4], m[2]*m[3]-m[0]*m[5], m[0]*m[4]-m[1]*m[3]};
            const float determinant=m[0]*cofactor[0]+m[1]*cofactor[1]+m[2]*cofactor[2];
            for (unsigned row=0;row<3;++row)
                output.normal[row]=std::abs(determinant)>1e-8f
                    ? (cofactor[row]*vertex.normal[0]+cofactor[3+row]*vertex.normal[1]+cofactor[6+row]*vertex.normal[2])/determinant
                    : vertex.normal[row];
            const auto normalize=[](float *v) {
                const float squared=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
                if (squared<=1e-12f || !std::isfinite(squared)) return false;
                const float inverse=1/std::sqrt(squared);
                for(unsigned c=0;c<3;++c) v[c]*=inverse;
                return true;
            };
            if(!normalize(output.normal.data())) output.normal={0,1,0};
            for(unsigned row=0;row<3;++row)
                output.tangent[row]=m[row]*vertex.tangent[0]+m[3+row]*vertex.tangent[1]+m[6+row]*vertex.tangent[2];
            const float projection=output.normal[0]*output.tangent[0]+output.normal[1]*output.tangent[1]+output.normal[2]*output.tangent[2];
            for(unsigned row=0;row<3;++row) output.tangent[row]-=output.normal[row]*projection;
            if(!normalize(output.tangent.data())) {
                const auto &n=output.normal;
                output.tangent=std::abs(n[1])<.99f ? std::array<float,4>{n[2],0,-n[0],0} : std::array<float,4>{0,-n[2],n[1],0};
                normalize(output.tangent.data());
            }
            output.tangent[3]=(vertex.tangent[3]==0 ? 1 : vertex.tangent[3])*(determinant<0 ? -1.0f : 1.0f);
            output.uv=vertex.uv;
            const auto &old = previous.size() == source.size() ? previous[index].position : output.position;
            output.previousPosition = {old[0], old[1], old[2], 1};
            result[index] = output;
            const glm::vec3 position(output.position[0], output.position[1], output.position[2]);
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        if (source.empty()) return {};
        return {(minimum + maximum) * 0.5f, glm::length(maximum - minimum) * 0.5f};
    }
}
