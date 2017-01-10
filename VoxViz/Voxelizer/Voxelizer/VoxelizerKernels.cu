#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include "device_functions.h"

#include <cmath>
#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>

#define GLM_FORCE_CUDA
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include "Voxelizer.h"

__device__ __host__ bool TrianglePlaneOverlapsVoxel(const glm::vec3& v0, 
                                const glm::vec3& normal,
                                const glm::vec3& p, 
                                const glm::vec3& deltaP,
                                const glm::vec3& uvw)//, 
                                //bool debug)
{
    glm::vec3 voxOffset = (uvw * deltaP);
    glm::vec3 p0 = p + voxOffset;//lower left
    glm::vec3 cornerDelta[] = {
        deltaP, //back upper right
        glm::vec3(deltaP.x, deltaP.y, 0.0f), //back lower right
        glm::vec3(deltaP.x, 0.0f, 0.0f), //front lower right
        glm::vec3(0.0f, 0.0f, deltaP.z), //front upper left
        glm::vec3(0.0f, deltaP.y, deltaP.z), //back upper left
        glm::vec3(deltaP.x, 0.0f, deltaP.z), //front upper right
        glm::vec3(0.0f, deltaP.y, 0.0f), //back lower left
    };
    float D = glm::dot(-normal, v0);
    float dist1 = glm::dot(normal, p0) + D;
    for(int i = 0; i < 7; ++i)
    {
        glm::vec3 p1 = p0 + cornerDelta[i];
        float dist2 = glm::dot(normal, p1) + D;

        /*if(debug)
        {
            printf("p0=[%f %f %f] p1=[%f %f %f] D=%f dist1=%f dist2=%f\n",
                   p0.x, p0.y, p0.z,
                   p1.x, p1.y, p1.z,
                   D, dist1, dist2);
        }*/
        if(dist1 * dist2 <= 0.0f)
            return true;
    }
    return false;
}

static const int X_AXIS = 0;
static const int Y_AXIS = 1;
static const int Z_AXIS = 2;

//bool TriangleOverlapsVoxelOnAxis(const glm::vec3* pVerts,
//                                 const glm::vec3* pEdges,
//                                 const glm::vec3* pEdgeNormals,
//                                 const glm::vec3& normal,
//                                 const glm::vec3& voxMin,
//                                 const glm::vec3& voxDelta,
//                                 int axisIndex1, int axisIndex2)
//{
//    static float epsilon = 0.0001f;
//    glm::vec2 voxMinProj = glm::vec2(voxMin[axisIndex1], voxMin[axisIndex2]);
//    //for each edge
//    for(int i = 0; i < 3; ++i)
//    {
//        const glm::vec2& edge = glm::vec2(pEdges[i][axisIndex1], pEdges[i][axisIndex2]);
//        glm::vec2 edgeNorm = glm::vec2(pEdgeNormals[i][axisIndex1], pEdgeNormals[i][axisIndex2]);
//        
//        if(glm::abs(edgeNorm.x) + glm::abs(edgeNorm.y) < epsilon)//skip degenerate normals
//            continue;
//        //compute critical point
//        glm::vec2 critPtDelta = glm::vec2((edgeNorm.x < 0.0f ? 0.0f : voxDelta.x),
//                                          (edgeNorm.y < 0.0f ? 0.0f : voxDelta.y));
//
//        glm::vec2 critPt = voxMinProj + critPtDelta;
//        
//        //compute edge function test result
//        float edgeFuncResult = ((critPt.x - pVerts[i][axisIndex1]) * edge.y)
//                                - ((critPt.y - pVerts[i][axisIndex2]) * edge.x);
//        if(edgeFuncResult > 0)
//            return false;
//    }
//
//    return true;
//}

__device__ __host__ bool TriangleOverlapsVoxelOnAxis(const glm::vec3* pVerts,
                                 const glm::vec3* pEdges,
                                 const glm::vec3& normal,
                                 const glm::vec3& p,
                                 const glm::vec3& deltaP,
                                 const glm::vec3& uvw,
                                 int axisIndex1, int axisIndex2, int otherAxisIndex)
{
    float projScalar = normal[otherAxisIndex] >= 0.0f ? 1.0f : -1.0f;
    glm::vec2 edgeNormsProjToPlane[3] = {
        (glm::vec2(-pEdges[0][axisIndex2], pEdges[0][axisIndex1]) * projScalar),
        (glm::vec2(-pEdges[1][axisIndex2], pEdges[1][axisIndex1]) * projScalar),
        (glm::vec2(-pEdges[2][axisIndex2], pEdges[2][axisIndex1]) * projScalar)
    };

    glm::vec2 voxProjToPlane[3] = {
        glm::vec2(edgeNormsProjToPlane[0].x * deltaP[axisIndex1], edgeNormsProjToPlane[0].y * deltaP[axisIndex2]),
        glm::vec2(edgeNormsProjToPlane[1].x * deltaP[axisIndex1], edgeNormsProjToPlane[1].y * deltaP[axisIndex2]),
        glm::vec2(edgeNormsProjToPlane[2].x * deltaP[axisIndex1], edgeNormsProjToPlane[2].y * deltaP[axisIndex2])
    };

    float deltaProjToPlane[3] = {
        (glm::dot(-edgeNormsProjToPlane[0], glm::vec2(pVerts[0][axisIndex1], pVerts[0][axisIndex2])))  
            + (glm::dot(edgeNormsProjToPlane[0], glm::vec2(p[axisIndex1], p[axisIndex2]))) 
            + (glm::max(0.0f, deltaP[axisIndex1] * edgeNormsProjToPlane[0].x))
            + (glm::max(0.0f, deltaP[axisIndex2] * edgeNormsProjToPlane[0].y)),
        (glm::dot(-edgeNormsProjToPlane[1], glm::vec2(pVerts[1][axisIndex1], pVerts[1][axisIndex2])))  
            + (glm::dot(edgeNormsProjToPlane[1], glm::vec2(p[axisIndex1], p[axisIndex2]))) 
            + (glm::max(0.0f, deltaP[axisIndex1] * edgeNormsProjToPlane[1].x))
            + (glm::max(0.0f, deltaP[axisIndex2] * edgeNormsProjToPlane[1].y)),
        (glm::dot(-edgeNormsProjToPlane[2], glm::vec2(pVerts[2][axisIndex1], pVerts[2][axisIndex2])))  
            + (glm::dot(edgeNormsProjToPlane[2], glm::vec2(p[axisIndex1], p[axisIndex2]))) 
            + (glm::max(0.0f, deltaP[axisIndex1] * edgeNormsProjToPlane[2].x))
            + (glm::max(0.0f, deltaP[axisIndex2] * edgeNormsProjToPlane[2].y))
    };

    float overlapTest0 = deltaProjToPlane[0] 
                            + (uvw[axisIndex2] * voxProjToPlane[0].y) 
                            + (uvw[axisIndex1] * voxProjToPlane[0].x);

                            
    float overlapTest1 = deltaProjToPlane[1] 
                            + (uvw[axisIndex2] * voxProjToPlane[1].y) 
                            + (uvw[axisIndex1] * voxProjToPlane[1].x);

                            
    float overlapTest2 = deltaProjToPlane[2] 
                            + (uvw[axisIndex2] * voxProjToPlane[2].y) 
                            + (uvw[axisIndex1] * voxProjToPlane[2].x);

    return (overlapTest0 >= 0.0f && overlapTest1 >= 0.0f && overlapTest2 >= 0.0f);
}

__device__ __host__ bool TriangleOverlapsVoxel(const glm::vec3* pVerts,
                           const glm::vec3* pEdges,
                           //const glm::vec3* pEdgeNormals,
                           const glm::vec3& normal,
                           const glm::vec3& p,
                           const glm::vec3& deltaP,
                           const glm::vec3& uvw)
{
    ////xy plane
    //if(!TriangleOverlapsVoxelOnAxis(pVerts, pEdges, pEdgeNormals, normal, p, deltaP, X_AXIS, Y_AXIS))
    //    return false;
    ////zx plane
    //if(!TriangleOverlapsVoxelOnAxis(pVerts, pEdges, pEdgeNormals, normal, p, deltaP, Z_AXIS, X_AXIS))
    //    return false;
    ////yz plane
    //return TriangleOverlapsVoxelOnAxis(pVerts, pEdges, pEdgeNormals, normal, p, deltaP, Y_AXIS, Z_AXIS);
    //xy plane
    if(!TriangleOverlapsVoxelOnAxis(pVerts, pEdges, normal, p, deltaP, uvw, X_AXIS, Y_AXIS, Z_AXIS))
        return false;
    //zx plane
    if(!TriangleOverlapsVoxelOnAxis(pVerts, pEdges, normal, p, deltaP, uvw, Z_AXIS, X_AXIS, Y_AXIS))
        return false;
    //yz plane
    return TriangleOverlapsVoxelOnAxis(pVerts, pEdges, normal, p, deltaP, uvw, Y_AXIS, Z_AXIS, X_AXIS);
}

void ComputeTriangleVoxelBounds(const glm::vec3* pVerts, 
                        const glm::vec3& p, 
                        const glm::vec3& deltaP, 
                        const glm::uvec3& voxDim, 
                        glm::uvec3& minVox, 
                        glm::uvec3& maxVox)
{
    const glm::ivec3 maxVoxIndex = glm::ivec3(voxDim) - glm::ivec3(1);
    const glm::ivec3 minVoxIndex = glm::ivec3(0);
    minVox = glm::uvec3(maxVoxIndex);
    maxVox = glm::uvec3(minVoxIndex);

    for(int i = 0; i < 3; ++i)
    {
        const glm::vec3& vert = pVerts[i];
        glm::vec3 diff = vert - p, p, maxP;

        glm::uvec3 uvw = glm::uvec3(glm::clamp(glm::ivec3(diff / deltaP),
                                    minVoxIndex, maxVoxIndex));
        if(uvw.x < minVox.x)
            minVox.x = uvw.x;
        else if(uvw.x > maxVox.x)
            maxVox.x = uvw.x;

        if(uvw.y < minVox.y)
            minVox.y = uvw.y;
        else if(uvw.y > maxVox.y)
            maxVox.y = uvw.y;

        if(uvw.z < minVox.z)
            minVox.z = uvw.z;
        else if(uvw.z > maxVox.z)
            maxVox.z = uvw.z;
    }
}

__global__ void ComputeFaceNormals(const glm::vec3* pVerts,
                                   glm::vec3* pNormals,
                                   size_t numVerts)
{
    glm::uint triIndex = (blockDim.x * blockIdx.x) + threadIdx.x;
    
    glm::uint vertIndex = triIndex * 3u;
    if((vertIndex+2) >= numVerts)
    {
        //printf("CEFNB: vertIndex=%d numVerts=%d\n", vertIndex, numVerts);
        return;
    }    

    glm::vec3 v0 = pVerts[vertIndex];
    glm::vec3 v1 = pVerts[vertIndex+1];
    glm::vec3 v2 = pVerts[vertIndex+2];

    glm::vec3 p0p1 = v1 - v0;
    glm::vec3 p0p2 = v2 - v0;
    pNormals[triIndex] = glm::normalize(glm::cross(p0p1, p0p2));
}

__global__ void ComputeEdgesFaceNormalsAndBounds(const glm::vec3* pVerts,
                                                 size_t numVerts, 
                                                 glm::vec3 p,
                                                 glm::vec3 deltaP,
                                                 glm::uvec3 voxDim,
                                                 glm::vec3* pEdges, 
                                                 glm::vec3* pNormals, 
                                                 cuda::VoxelBBox* pBounds)
{
    glm::uint triIndex = (blockDim.x * blockIdx.x) + threadIdx.x;
    
    glm::uint vertIndex = triIndex * 3u;
    if((vertIndex+2) >= numVerts)
    {
        //printf("CEFNB: vertIndex=%d numVerts=%d\n", vertIndex, numVerts);
        return;
    }    

    glm::vec3 v0 = pVerts[vertIndex];
    glm::vec3 v1 = pVerts[vertIndex+1];
    glm::vec3 v2 = pVerts[vertIndex+2];

    glm::vec3 edges[3] = {
        glm::vec3(v1 - v0),
        glm::vec3(v2 - v1),
        glm::vec3(v0 - v2)
    };

    pEdges[vertIndex] = edges[0];
    pEdges[vertIndex+1] = edges[1];
    pEdges[vertIndex+2] = edges[2];

    const glm::vec3& p0p1 = edges[0];
    glm::vec3 p0p2 = v2 - v0;
    pNormals[triIndex] = glm::normalize(glm::cross(p0p1, p0p2));

    //printf("%d=(%f %f %f)\n", 
    //       triIndex,
    //       pNormals[triIndex].x,
    //       pNormals[triIndex].y,
    //       pNormals[triIndex].z);

    const glm::ivec3 maxVoxIndex = glm::ivec3(voxDim) - glm::ivec3(1);
    const glm::ivec3 minVoxIndex = glm::ivec3(0);
    cuda::VoxelBBox bbox;
    bbox.minVox = glm::uvec3(maxVoxIndex);
    bbox.maxVox = glm::uvec3(minVoxIndex);

    {
        glm::vec3 diff = (v0 - p) / deltaP;

        glm::uvec3 uvw = glm::uvec3(glm::clamp(glm::ivec3(diff),
                                                minVoxIndex, maxVoxIndex));

        bbox.minVox = uvw;
        bbox.maxVox = uvw;
    }

    {
        glm::vec3 diff = (v1 - p) / deltaP;
        //printf("diff1=(%f %f %f)\n",
        //       diff.x,
        //       diff.y,
        //       diff.z);

        glm::uvec3 uvw = glm::uvec3(glm::clamp(glm::ivec3(diff),
                                                minVoxIndex, maxVoxIndex));
        if(uvw.x < bbox.minVox.x)
            bbox.minVox.x = uvw.x;
        else if(uvw.x > bbox.maxVox.x)
            bbox.maxVox.x = uvw.x;

        if(uvw.y < bbox.minVox.y)
            bbox.minVox.y = uvw.y;
        else if(uvw.y > bbox.maxVox.y)
            bbox.maxVox.y = uvw.y;

        if(uvw.z < bbox.minVox.z)
            bbox.minVox.z = uvw.z;
        else if(uvw.z > bbox.maxVox.z)
            bbox.maxVox.z = uvw.z;
    }

    {
        glm::vec3 diff = (v2 - p) / deltaP;
        //printf("diff2=(%f %f %f)\n",
        //       diff.x,
        //       diff.y,
        //       diff.z);

        glm::uvec3 uvw = glm::uvec3(glm::clamp(glm::ivec3(diff),
                                                minVoxIndex, maxVoxIndex));
        if(uvw.x < bbox.minVox.x)
            bbox.minVox.x = uvw.x;
        else if(uvw.x > bbox.maxVox.x)
            bbox.maxVox.x = uvw.x;

        if(uvw.y < bbox.minVox.y)
            bbox.minVox.y = uvw.y;
        else if(uvw.y > bbox.maxVox.y)
            bbox.maxVox.y = uvw.y;

        if(uvw.z < bbox.minVox.z)
            bbox.minVox.z = uvw.z;
        else if(uvw.z > bbox.maxVox.z)
            bbox.maxVox.z = uvw.z;
    }

    //printf("bounds[%d](%d %d %d) - (%d %d %d)\n", 
    //       triIndex, 
    //       bbox.minVox.x,
    //       bbox.minVox.y,
    //       bbox.minVox.z,
    //       bbox.maxVox.x,
    //       bbox.maxVox.y,
    //       bbox.maxVox.z);
    pBounds[triIndex] = bbox;
}

__global__ void ComputeVoxelization(const glm::vec3* pVerts,
                                    size_t triOffset, 
                                    const glm::vec3* pEdges, 
                                    const glm::vec3* pNormals, 
                                    const cuda::VoxelBBox* pBounds,
                                    glm::vec3 p,
                                    glm::vec3 deltaP,
                                    glm::uvec3 minVoxChunk,
                                    glm::uvec3 maxVoxChunk,
                                    glm::uint zOffset,
                                    cudaPitchedPtr pdevVoxelTriCounts,
                                    cudaPitchedPtr pdevVoxelTriIndices)
{
    //glm::uint triIndex = (blockDim.x * blockIdx.x) + threadIdx.x;
    glm::uint triIndex = blockIdx.z + triOffset;
    
    glm::uint vertIndex = triIndex * 3u;
    
    //if(triIndex == 5000 && threadIdx.x == 0u && threadIdx.y == 0u)
    //    printf("CEFNB: triIndex = %d blockIdx=(%d %d %d) blockDim=(%d %d %d)\n", triIndex, blockIdx.x, blockIdx.y, blockIdx.z, blockDim.x, blockDim.y, blockDim.z);
    
    glm::uint xIndex = minVoxChunk.x + ((blockIdx.x * blockDim.x) + threadIdx.x);
    glm::uint yIndex = minVoxChunk.y + ((blockIdx.y * blockDim.y) + threadIdx.y);
    glm::uint zIndex = zOffset + threadIdx.z;

    const cuda::VoxelBBox& bounds = pBounds[triIndex];
    glm::uvec3 minVox = glm::max(bounds.minVox, minVoxChunk);
    glm::uvec3 maxVox = glm::min(bounds.maxVox, maxVoxChunk);

    //check if current chunk is outside bounds of current triangle
    if(minVox.x > maxVox.x)
        return;

    if(minVox.y > maxVox.y)
        return;

    if(minVox.z > maxVox.z)
        return;

    //check if my voxel is outside bounds of triangle
    if(xIndex < minVox.x || xIndex > maxVox.x)
        return;

    if(yIndex < minVox.y || yIndex > maxVox.y)
        return;

    if(zIndex < minVox.z || zIndex > maxVox.z)
        return;

    glm::vec3 uvw(0.0f, 0.0f, 0.0f);

    const glm::vec3* pTriVerts = &pVerts[vertIndex];
    const glm::vec3* pTriEdges = &pEdges[vertIndex];
    const glm::vec3& normal = pNormals[triIndex];
    
    //if(threadIdx.z < 3)
    //{
    //    printf("%d %d - %d minVox=[%d %d %d] maxVox=[%d %d %d]\n",
    //           threadIdx.z,
    //           threadZStart,
    //           threadZEnd,
    //           minVox.x,
    //           minVox.y,
    //           minVox.z,
    //           maxVox.x,
    //           maxVox.y,
    //           maxVox.z);
    //}

    //if(zIndex == 141)
    //    printf("Checking tri=%d at voxel=%d %d %d\n", triIndex, xIndex, yIndex, zIndex);

    char* devPtrTriCounts = (char*)pdevVoxelTriCounts.ptr;
    size_t triCountsPitch = pdevVoxelTriCounts.pitch;
    size_t triCountsSlicePitch = triCountsPitch * pdevVoxelTriCounts.ysize;

    char* devPtrTriIndices = (char*)pdevVoxelTriIndices.ptr;
    size_t triIndicesPitch = pdevVoxelTriIndices.pitch;
    size_t triIndicesSlicePitch = triIndicesPitch * pdevVoxelTriIndices.ysize;

    //char* devPtrNormals = (char*)pdevVoxelNormals.ptr;
    //size_t normalsPitch = pdevVoxelNormals.pitch;
    //size_t normalsSlicePitch = normalsPitch * pdevVoxelNormals.ysize;

    //for(glm::uint w = minVox.z; w <= maxVox.z; ++w)
    glm::uint w = zIndex;
    {
        uvw.z = (float)w;
        glm::uint z = (w - minVoxChunk.z);

        char* sliceTriCounts = devPtrTriCounts + z * triCountsSlicePitch;
        char* sliceTriIndices = devPtrTriIndices + z * triIndicesSlicePitch;
        //char* sliceNormals = devPtrNormals + z * normalsSlicePitch;

        //printf("uvw.z=%f\n", uvw.z);

        //for(glm::uint v = minVox.y; v <= maxVox.y; ++v)
        {
            glm::uint v = yIndex;
            uvw.y = (float)v;
            glm::uint y = (v - minVoxChunk.y);
            glm::uint* voxelCounts = (glm::uint*)(sliceTriCounts + y * triCountsPitch);
            glm::lowp_ivec4* voxelTriIndices = (glm::lowp_ivec4*)(sliceTriIndices + y * triIndicesPitch);
            //glm::vec3* voxelNormals = (glm::vec3*)(sliceNormals + y * normalsPitch);
            //for(glm::uint u = minVox.x; u <= maxVox.x; ++u)
            {
                glm::uint u = xIndex;
                glm::uint x = (u - minVoxChunk.x);
                uvw.x = (float)u;

                /*bool debug = (triIndex == 8 &&
                       (x == 179 || x == 180) &&
                       y == 158 && 
                       (z >= 221 && z <= 225));*/
                /*if(triIndex == 8 &&
                       (x == 179 || x == 180) &&
                       y == 158 && 
                       (z >= 221 && z <= 225))
                {
                    printf("[%d %d %d] tri=%d v0=[%f %f %f] normal=[%f %f %f] uvw=[%f %f %f]\n",
                            x, y, z, triIndex,
                            pTriVerts[0].x, pTriVerts[0].y, pTriVerts[0].z,
                            normal.x, normal.y, normal.z,
                            uvw.x, uvw.y, uvw.z);
                }*/

                //voxelColors[x] = cuda::VoxColor(uvw, 1.0f);
                //voxelNormals[x] = glm::vec3(uvw);
                if(TrianglePlaneOverlapsVoxel(pTriVerts[0], normal, p, deltaP, uvw))//, debug))
                {
                    /*if(triIndex == 8 &&
                       (x == 179 || x == 180) &&
                       y == 158 && 
                       (z >= 221 && z <= 225))
                    {
                        printf("[%d %d %d] tri=%d passed plane overlap check\n",
                               x, y, z, triIndex);
                    }*/
                    if(!TriangleOverlapsVoxel(pTriVerts, 
                                              pTriEdges, 
                                              normal, 
                                              p, deltaP, uvw))
                    {
                        //continue;
                        return;
                    }
                }
                else
                {
                    //continue;
                    return;
                }

                //printf("[%d %d %d] tri=%d found overlap\n",
                //       x, y, z, triIndex);
                
                atomicAdd(&voxelCounts[x], 1u);
                
                if(voxelTriIndices[x][0] == -1)
                    voxelTriIndices[x][0] = triIndex;
                else if(voxelTriIndices[x][1] == -1)
                    voxelTriIndices[x][1] = triIndex;
                else if(voxelTriIndices[x][2] == -1)
                    voxelTriIndices[x][2] = triIndex;
                else
                    voxelTriIndices[x][3] = triIndex;

                //TODO get color from texture and mix rather than just set
                /*cuda::VoxColor color = cuda::VoxColor(std::abs(normal.x), std::abs(normal.y), std::abs(normal.z), 1.0f);
                atomicAdd(&voxelColors[x].x, color.x);
                atomicAdd(&voxelColors[x].y, color.y);
                atomicAdd(&voxelColors[x].z, color.z);
                voxelColors[x].w = color.w;
                */
                //atomicAdd(&voxelNormals[x].x, normal.x),
                //atomicAdd(&voxelNormals[x].y, normal.y),
                //atomicAdd(&voxelNormals[x].z, normal.z);
                //in next step we'll average and normalize
            }
        }
    }
}

//__global__ void ComputeVoxelizationAverages(cudaPitchedPtr pdevVoxelTriCounts,
//                                            cudaPitchedPtr pdevVoxelColors,
//                                            glm::uvec3 voxDim)
//{
//    glm::uint xIndex = (blockIdx.x * blockDim.x) + threadIdx.x;
//    if(xIndex >= voxDim.x)
//        return;
//    
//    glm::uint yIndex = (blockIdx.y * blockDim.y) + threadIdx.y;
//    if(yIndex >= voxDim.y)
//        return;
//
//    glm::uint zIndex = (blockIdx.z * blockDim.z) + threadIdx.z;
//    if(zIndex >= voxDim.z)
//        return;
//
//    char* devPtrTriCounts = (char*)pdevVoxelTriCounts.ptr;
//    size_t triCountsPitch = pdevVoxelTriCounts.pitch;
//    size_t triCountsSlicePitch = triCountsPitch * pdevVoxelTriCounts.ysize;
//
//    char* sliceTriCounts = devPtrTriCounts + zIndex * triCountsSlicePitch;
//
//    glm::uint* voxelCounts = (glm::uint*)(sliceTriCounts + yIndex * triCountsPitch);
//
//    glm::uint triCount = voxelCounts[xIndex];
//    if(triCount <= 1u)
//        return;
//
//    char* devPtrColors = (char*)pdevVoxelColors.ptr;
//    size_t colorsPitch = pdevVoxelColors.pitch;
//    size_t colorsSlicePitch = colorsPitch * pdevVoxelColors.ysize;
//
//    //char* devPtrNormals = (char*)pdevVoxelNormals.ptr;
//    //size_t normalsPitch = pdevVoxelNormals.pitch;
//    //size_t normalsSlicePitch = normalsPitch * pdevVoxelNormals.ysize;
//
//    char* sliceColors = devPtrColors + zIndex * colorsSlicePitch;
//    //char* sliceNormals = devPtrNormals + zIndex * normalsSlicePitch;
//
//    cuda::VoxColor* voxelColors = (cuda::VoxColor*)(sliceColors + yIndex * colorsPitch);
//    //glm::vec3* voxelNormals = (glm::vec3*)(sliceNormals + yIndex * normalsPitch);
//
//    float oneOverTriCount = 1.0f / static_cast<float>(triCount);
//
//    voxelColors[xIndex].x *= oneOverTriCount;
//    voxelColors[xIndex].y *= oneOverTriCount;
//    voxelColors[xIndex].z *= oneOverTriCount;
//    //leave the alpha component alone
//    //glm::vec3 normal = voxelNormals[xIndex];
//
//    //normal *= oneOverTriCount;
//    //voxelNormals[xIndex] = glm::normalize(normal);
//}

texture<uchar4, cudaTextureType2D, cudaReadModeNormalizedFloat> colorTex;

bool BindTextureToArray(cudaArray* pgImageArray,
                        cudaChannelFormatDesc imageDesc,
                        cudaTextureAddressMode addressMode0,
                        cudaTextureAddressMode addressMode1)
{
    colorTex.addressMode[0] = addressMode0;
    colorTex.addressMode[1] = addressMode1;
    colorTex.filterMode = cudaFilterModeLinear;
    colorTex.normalized = true;    // access with normalized texture coordinates

    // Bind the array to the texture
    return cudaBindTextureToArray(&colorTex, pgImageArray, &imageDesc) == cudaSuccess;
}

__device__ glm::vec2 ComputeVoxelBarycentricCoords(glm::vec2 P,
                                           glm::vec2 A,
                                           glm::vec2 B,
                                           glm::vec2 C)
{
    // Compute vectors        
    glm::vec2 v0 = C - A;
    glm::vec2 v1 = B - A;
    glm::vec2 v2 = P - A;

    // Compute dot products
    float dot00 = glm::dot(v0, v0);
    float dot01 = glm::dot(v0, v1);
    float dot02 = glm::dot(v0, v2);
    float dot11 = glm::dot(v1, v1);
    float dot12 = glm::dot(v1, v2);

    // Compute barycentric coordinates
    float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
    glm::vec2 uv(glm::clamp((dot11 * dot02 - dot01 * dot12) * invDenom,
                            0.0f, 1.0f),
                 glm::clamp((dot00 * dot12 - dot01 * dot02) * invDenom,
                            0.0f, 1.0f));

    return uv;
}

__device__ glm::vec2 ComputeVoxelTexCoords(glm::vec2 uv,
                                glm::vec2 uvA, 
                                glm::vec2 uvB, 
                                glm::vec2 uvC)//,
                                //bool debug=false)
{

    glm::vec2 V = (uvB - uvA) * uv.y;
    glm::vec2 U = (uvC - uvA) * uv.x;
    glm::vec2 interpUV = uvA + U + V;
    /*if(debug)
    {
        printf("u=%f v=%f U=[%f %f] V=[%f %f]\n", uv.x, uv.y, U.x, U.y, V.x, V.y);
    }*/
    return interpUV;
}

__device__ glm::vec3 ComputeVoxelNormal(glm::vec2 uv,
                             glm::vec3 nA, 
                             glm::vec3 nB, 
                             glm::vec3 nC)//,
                             //bool debug=false)
{
    glm::vec3 V = (nB - nA) * uv.y;
    glm::vec3 U = (nC - nA) * uv.x;
    glm::vec3 interpNorm = nA + U + V;
    /*if(debug)
    {
        printf("u=%f v=%f U=[%f %f %f] V=[%f %f %f]\n", 
               uv.x, uv.y, U.x, U.y, U.z, V.x, V.y, V.z);
    }*/
    return interpNorm;
}

__device__ void VoxColorToVec4(glm::vec4* pVec4, glm::vec4 color)
{
    *pVec4 = color;
}

__device__ void VoxColorToVec4(glm::vec4* pVec4, uchar4 color)
{
    pVec4->x = static_cast<float>(color.x) / 255.0f;
    pVec4->y = static_cast<float>(color.y) / 255.0f;
    pVec4->z = static_cast<float>(color.z) / 255.0f;
    pVec4->w = static_cast<float>(color.w) / 255.0f;
}

__device__ void Vec4ToVoxColor(glm::vec4* pVoxColor, glm::vec4 vec4Color)
{
    *pVoxColor = vec4Color;
}

__device__ void Vec4ToVoxColor(uchar4* pVoxColor, glm::vec4 vec4Color)
{
    pVoxColor->x = static_cast<unsigned char>(vec4Color.x * 255.0f);
    pVoxColor->y = static_cast<unsigned char>(vec4Color.y * 255.0f);
    pVoxColor->z = static_cast<unsigned char>(vec4Color.z * 255.0f);
    pVoxColor->w = static_cast<unsigned char>(vec4Color.w * 255.0f);
}

__global__ void ComputeColorsAndNormals(size_t triGrpOffset,
                                        const glm::vec3* pVerts,
                                        const glm::vec3* pVtxNormals,
                                        const glm::vec3* pFaceNormals,
                                        const glm::vec2* pUVs,
                                        bool isTerrain,
                                        glm::vec3 voxOrigin,
                                        glm::vec3 voxSizeMeters,
                                        cudaPitchedPtr voxelTriCountsDevPtr,
                                        cudaPitchedPtr voxelTriIndicesDevPtr,
                                        cudaPitchedPtr voxelColorsDevMipMapPtr,
                                        cudaPitchedPtr voxelNormalsDevMipMapPtr,
                                        glm::uvec3 voxWriteDim)
{
    glm::uint xIndex = (blockIdx.x * blockDim.x) + threadIdx.x;
    if(xIndex >= voxWriteDim.x)
        return;
    
    glm::uint yIndex = (blockIdx.y * blockDim.y) + threadIdx.y;
    if(yIndex >= voxWriteDim.y)
        return;

    glm::uint zIndex = (blockIdx.z * blockDim.z) + threadIdx.z;
    if(zIndex >= voxWriteDim.z)
        return;

    glm::vec3 voxCenter = voxOrigin
                        + glm::vec3(xIndex * voxSizeMeters.x,
                                    yIndex * voxSizeMeters.y,
                                    zIndex * voxSizeMeters.z)
                        + (voxSizeMeters * 0.5f);

    glm::lowp_ivec4* voxelReadTriIndices = NULL;
    glm::uint* voxelReadTriCounts = NULL;
    {
        char* devReadPtrTriIndices = (char*)voxelTriIndicesDevPtr.ptr;
        size_t triIndicesPitch = voxelTriIndicesDevPtr.pitch;
        size_t triIndicesSlicePitch = triIndicesPitch * voxelTriIndicesDevPtr.ysize;

        char* devReadPtrTriCounts = (char*)voxelTriCountsDevPtr.ptr;
        size_t normalsPitch = voxelTriCountsDevPtr.pitch;
        size_t normalsSlicePitch = normalsPitch * voxelTriCountsDevPtr.ysize;

        char* sliceReadTriIndices = devReadPtrTriIndices + zIndex * triIndicesSlicePitch;
        char* sliceReadTriCounts = devReadPtrTriCounts + zIndex * normalsSlicePitch;

        voxelReadTriIndices = (glm::lowp_ivec4*)(sliceReadTriIndices + yIndex * triIndicesPitch);
        voxelReadTriCounts = (glm::uint*)(sliceReadTriCounts + yIndex * normalsPitch);
    }

    cuda::VoxColor* voxelWriteColors = NULL;
    cuda::VoxNorm* voxelWriteNormals = NULL;

    {
        char* devWritePtrColors = (char*)voxelColorsDevMipMapPtr.ptr;
        size_t colorsPitch = voxelColorsDevMipMapPtr.pitch;
        size_t colorsSlicePitch = colorsPitch * voxelColorsDevMipMapPtr.ysize;

        char* devWritePtrNormals = (char*)voxelNormalsDevMipMapPtr.ptr;
        size_t normalsPitch = voxelNormalsDevMipMapPtr.pitch;
        size_t normalsSlicePitch = normalsPitch * voxelNormalsDevMipMapPtr.ysize;

        char* sliceWriteColors = devWritePtrColors + zIndex * colorsSlicePitch;
        char* sliceWriteNormals = devWritePtrNormals + zIndex * normalsSlicePitch;

        voxelWriteColors = (cuda::VoxColor*)(sliceWriteColors + yIndex * colorsPitch);
        voxelWriteNormals = (cuda::VoxNorm*)(sliceWriteNormals + yIndex * normalsPitch);
    }

    glm::vec4 color;
    VoxColorToVec4(&color, voxelWriteColors[xIndex]);

    cuda::VoxNorm normal = voxelWriteNormals[xIndex];
    glm::lowp_ivec4& triIndices = voxelReadTriIndices[xIndex];
    
    //if first item in triIndices is negative
    //then no new triangle intersections were found
    bool newTriIsects = (triIndices[0] >= 0);
    if(newTriIsects)
    {
        glm::uint triCount = glm::min(voxelReadTriCounts[xIndex], 4u);
        //reset this to one because after this execution the
        //stored color is worth one in the average
        voxelReadTriCounts[xIndex] = 1u;
        for(glm::uint i = 0u; i < 4u && triIndices[i] != -1; ++i)
        {
            short int triIndex = triIndices[i];
            triIndices[i] = -1;//reset this to -1 for next loop
            short int vertBaseIndex = triIndex * 3;
            const glm::vec3& v1 = pVerts[vertBaseIndex];
            const glm::vec3& v2 = pVerts[vertBaseIndex+1];
            const glm::vec3& v3 = pVerts[vertBaseIndex+2];

            short int triGrpTriIndex = (triIndex - triGrpOffset);
            short int triGrpBaseVtxIndex = triGrpTriIndex * 3;
            const glm::vec2& uv1 = pUVs[triGrpBaseVtxIndex];
            const glm::vec2& uv2 = pUVs[triGrpBaseVtxIndex+1];
            const glm::vec2& uv3 = pUVs[triGrpBaseVtxIndex+2];
            glm::vec2 bcCoords;
            //find dominant axis
            const glm::vec3& faceNormal = pFaceNormals[triIndex];
            float absFaceNormalX = std::abs(faceNormal.x);
            float absFaceNormalY = std::abs(faceNormal.y);
            float absFaceNormalZ = std::abs(faceNormal.z);
            //glm::vec3 absFaceNormal = glm::abs(faceNormal);
            if(absFaceNormalX > absFaceNormalY && absFaceNormalX > absFaceNormalZ)
            {
               bcCoords = ComputeVoxelBarycentricCoords(glm::vec2(voxCenter.z, voxCenter.y),
                                                 glm::vec2(v1.z, v1.y),
                                                 glm::vec2(v2.z, v2.y),
                                                 glm::vec2(v3.z, v3.y));
            }
            else if(absFaceNormalY > absFaceNormalZ)
            { 
               bcCoords = ComputeVoxelBarycentricCoords(glm::vec2(voxCenter.x, voxCenter.z),
                                                 glm::vec2(v1.x, v1.z),
                                                 glm::vec2(v2.x, v2.z),
                                                 glm::vec2(v3.x, v3.z));
            }
            else
            {
                bcCoords = ComputeVoxelBarycentricCoords(glm::vec2(voxCenter.x, voxCenter.y),
                                                         glm::vec2(v1.x, v1.y),
                                                         glm::vec2(v2.x, v2.y),
                                                         glm::vec2(v3.x, v3.y));
            }

            /*bool debug = false;
            if(xIndex == 25 && yIndex == 25)
                debug = true;*/
            if(pVtxNormals != nullptr)
            {
                const glm::vec3& n1 = pVtxNormals[triGrpBaseVtxIndex];
                const glm::vec3& n2 = pVtxNormals[triGrpBaseVtxIndex+1];
                const glm::vec3& n3 = pVtxNormals[triGrpBaseVtxIndex+2];
                normal += ComputeVoxelNormal(bcCoords,
                                            n1, n2, n3);//,
                                            //debug);
            }
            else
                normal += faceNormal;

            glm::vec2 texCoords = ComputeVoxelTexCoords(bcCoords,
                                                        uv1, uv2, uv3);//,
                                                        //debug);
            
            float4 texel = tex2D(colorTex, 
                                 static_cast<float>(texCoords.x), 
                                 static_cast<float>(texCoords.y));
            //don't apply completely translucent texels to the voxel
            if(texel.w <= 0.005)
                --triCount;
            else
            {
                texel.x *= texel.w;
                texel.y *= texel.w;
                texel.z *= texel.w;
                color.x += texel.x;
                color.y += texel.y;
                color.z += texel.z;
                color.w += texel.w;
            }
            //if((xIndex == 25 && yIndex == 25) || triCount > 2)
            //{
            //    printf("normal=[%f %f %f]\n", normal.x, normal.y, normal.z);
            //    printf("voxCenter=[%f %f %f]\n", voxCenter.x, voxCenter.y, voxCenter.z);
            //    printf("triVerts=[%f %f %f][%f %f %f][%f %f %f]\n", 
            //            v1.x, v1.y, v1.z,
            //            v2.x, v2.y, v2.z,
            //            v3.x, v3.y, v3.z);
            //    printf("uvs=[%f %f][%f %f][%f %f]\n", 
            //            uv1.x, uv1.y,
            //            uv2.x, uv2.y,
            //            uv3.x, uv3.y);
            //    printf("voxel=[%d %d %d] triIndices[%d]=[%d] texCoords=[%f %f] = texel=[%f %f %f %f] ... color=[%f %f %f %f] / triCount=%d\n", 
            //        xIndex, yIndex, zIndex, i,
            //        triIndex,
            //        texCoords.x, texCoords.y,
            //        texel.x, texel.y, texel.z, texel.w,
            //        color.x, color.y, color.z, color.w,
            //        triCount);
            //}
        }

        if(triCount > 0u)
        {
            color.x /= triCount;
            color.y /= triCount;
            color.z /= triCount;
            color.w = glm::min(color.w, 1.0f);//additive alpha blending
            normal.x /= triCount;
            normal.y /= triCount;
            normal.z /= triCount;
            normal = glm::normalize(normal);
        }
    }
    
    Vec4ToVoxColor(&voxelWriteColors[xIndex], color);
    voxelWriteNormals[xIndex] = normal;
    if(newTriIsects && isTerrain)
    {
        //fill in voxels under ground
        for(glm::uint groundZ = 1; groundZ < 20u; ++groundZ)
        {
            if(zIndex == 0u)
                break;
            --zIndex;

            char* devWritePtrColors = (char*)voxelColorsDevMipMapPtr.ptr;
            size_t colorsPitch = voxelColorsDevMipMapPtr.pitch;
            size_t colorsSlicePitch = colorsPitch * voxelColorsDevMipMapPtr.ysize;

            char* devWritePtrNormals = (char*)voxelNormalsDevMipMapPtr.ptr;
            size_t normalsPitch = voxelNormalsDevMipMapPtr.pitch;
            size_t normalsSlicePitch = normalsPitch * voxelNormalsDevMipMapPtr.ysize;

            char* sliceWriteColors = devWritePtrColors + zIndex * colorsSlicePitch;
            char* sliceWriteNormals = devWritePtrNormals + zIndex * normalsSlicePitch;

            voxelWriteColors = (cuda::VoxColor*)(sliceWriteColors + yIndex * colorsPitch);
            voxelWriteNormals = (cuda::VoxNorm*)(sliceWriteNormals + yIndex * normalsPitch);
            //if((xIndex == 25 && yIndex == 25))
            //{
            //    printf("Filling ground %d\n", zIndex);
            //}
            Vec4ToVoxColor(&voxelWriteColors[xIndex], color);
            voxelWriteNormals[xIndex] = normal;
        }
    }
}

__global__ void ComputeColorsAsNormals(size_t triGrpOffset,
                                       const glm::vec3* pVerts,
                                       const glm::vec3* pVtxNormals,
                                       const glm::vec3* pFaceNormals,
                                       glm::vec3 voxOrigin,
                                       glm::vec3 voxSizeMeters,
                                       cudaPitchedPtr voxelTriCountsDevPtr,
                                       cudaPitchedPtr voxelTriIndicesDevPtr,
                                       cudaPitchedPtr voxelColorsDevMipMapPtr,
                                       cudaPitchedPtr voxelNormalsDevMipMapPtr,
                                       glm::uvec3 voxWriteDim)
{
    glm::uint xIndex = (blockIdx.x * blockDim.x) + threadIdx.x;
    if(xIndex >= voxWriteDim.x)
        return;
    
    glm::uint yIndex = (blockIdx.y * blockDim.y) + threadIdx.y;
    if(yIndex >= voxWriteDim.y)
        return;

    glm::uint zIndex = (blockIdx.z * blockDim.z) + threadIdx.z;
    if(zIndex >= voxWriteDim.z)
        return;

    glm::vec3 voxCenter = voxOrigin
                        + glm::vec3(xIndex * voxSizeMeters.x,
                                    yIndex * voxSizeMeters.y,
                                    zIndex * voxSizeMeters.z)
                        + (voxSizeMeters * 0.5f);

    glm::lowp_ivec4* voxelReadTriIndices = NULL;
    glm::uint* voxelReadTriCounts = NULL;
    {
        char* devReadPtrTriIndices = (char*)voxelTriIndicesDevPtr.ptr;
        size_t triIndicesPitch = voxelTriIndicesDevPtr.pitch;
        size_t triIndicesSlicePitch = triIndicesPitch * voxelTriIndicesDevPtr.ysize;

        char* devReadPtrTriCounts = (char*)voxelTriCountsDevPtr.ptr;
        size_t normalsPitch = voxelTriCountsDevPtr.pitch;
        size_t normalsSlicePitch = normalsPitch * voxelTriCountsDevPtr.ysize;

        char* sliceReadTriIndices = devReadPtrTriIndices + zIndex * triIndicesSlicePitch;
        char* sliceReadTriCounts = devReadPtrTriCounts + zIndex * normalsSlicePitch;

        voxelReadTriIndices = (glm::lowp_ivec4*)(sliceReadTriIndices + yIndex * triIndicesPitch);
        voxelReadTriCounts = (glm::uint*)(sliceReadTriCounts + yIndex * normalsPitch);
    }

    cuda::VoxColor* voxelWriteColors = NULL;
    cuda::VoxNorm* voxelWriteNormals = NULL;

    {
        char* devWritePtrColors = (char*)voxelColorsDevMipMapPtr.ptr;
        size_t colorsPitch = voxelColorsDevMipMapPtr.pitch;
        size_t colorsSlicePitch = colorsPitch * voxelColorsDevMipMapPtr.ysize;

        char* devWritePtrNormals = (char*)voxelNormalsDevMipMapPtr.ptr;
        size_t normalsPitch = voxelNormalsDevMipMapPtr.pitch;
        size_t normalsSlicePitch = normalsPitch * voxelNormalsDevMipMapPtr.ysize;

        char* sliceWriteColors = devWritePtrColors + zIndex * colorsSlicePitch;
        char* sliceWriteNormals = devWritePtrNormals + zIndex * normalsSlicePitch;

        voxelWriteColors = (cuda::VoxColor*)(sliceWriteColors + yIndex * colorsPitch);
        voxelWriteNormals = (cuda::VoxNorm*)(sliceWriteNormals + yIndex * normalsPitch);
    }

    glm::vec4 color;
    VoxColorToVec4(&color, voxelWriteColors[xIndex]);

    cuda::VoxNorm normal = voxelWriteNormals[xIndex];
    glm::lowp_ivec4& triIndices = voxelReadTriIndices[xIndex];
    
    //if first item in triIndices is negative
    //then no new triangle intersections were found
    if(triIndices[0] >= 0)
    {
        glm::uint triCount = voxelReadTriCounts[xIndex];
        //reset this to one because after this execution the
        //stored color is worth one in the average
        voxelReadTriCounts[xIndex] = 1u;
        for(glm::uint i = 0u; i < 4u && triIndices[i] != -1; ++i)
        {
            short int triIndex = triIndices[i];
            triIndices[i] = -1;//reset this to -1 for next loop
            short int vertBaseIndex = triIndex * 3;
            const glm::vec3& v1 = pVerts[vertBaseIndex];
            const glm::vec3& v2 = pVerts[vertBaseIndex+1];
            const glm::vec3& v3 = pVerts[vertBaseIndex+2];
            
            const glm::vec3& faceNormal = pFaceNormals[triIndex];
            if(pVtxNormals != nullptr)
            {
                short int triGrpTriIndex = (triIndex - triGrpOffset);
                short int triGrpBaseVtxIndex = triGrpTriIndex * 3;
                const glm::vec3& n1 = pVtxNormals[triGrpBaseVtxIndex];
                const glm::vec3& n2 = pVtxNormals[triGrpBaseVtxIndex+1];
                const glm::vec3& n3 = pVtxNormals[triGrpBaseVtxIndex+2];
                //this will be used to determine which plane
                //of coordinate system to compute barycentric
                //coordinates
                glm::vec2 bcCoords;
                //find dominant axis
                if(faceNormal.x > faceNormal.y && faceNormal.x > faceNormal.z)
                {
                   bcCoords = ComputeVoxelBarycentricCoords(glm::vec2(voxCenter.z, voxCenter.y),
                                                     glm::vec2(v1.z, v1.y),
                                                     glm::vec2(v2.z, v2.y),
                                                     glm::vec2(v3.z, v3.y));
                }
                else if(faceNormal.y > faceNormal.z)
                { 
                   bcCoords = ComputeVoxelBarycentricCoords(glm::vec2(voxCenter.x, voxCenter.z),
                                                     glm::vec2(v1.x, v1.z),
                                                     glm::vec2(v2.x, v2.z),
                                                     glm::vec2(v3.x, v3.z));
                }
                else
                {
                    bcCoords = ComputeVoxelBarycentricCoords(glm::vec2(voxCenter.x, voxCenter.y),
                                                             glm::vec2(v1.x, v1.y),
                                                             glm::vec2(v2.x, v2.y),
                                                             glm::vec2(v3.x, v3.y));
                }
                color += glm::vec4(ComputeVoxelNormal(bcCoords, n1, n2, n3), 1.0);
            }
            else
            {
                color += glm::vec4(faceNormal, 1.0f);
            }

            //if((xIndex > 25 && xIndex < 35) || triCount > 2)
            //{
            //    printf("voxCenter=[%f %f %f]\n", voxCenter.x, voxCenter.y, voxCenter.z);
            //    printf("triVerts=[%f %f %f][%f %f %f][%f %f %f]\n", 
            //            v1.x, v1.y, v1.z,
            //            v2.x, v2.y, v2.z,
            //            v3.x, v3.y, v3.z);
            //    printf("voxel=[%d %d %d] triIndices[%d]=[%d] color=[%f %f %f %f] / triCount=%d\n", 
            //        xIndex, yIndex, zIndex, i,
            //        triIndex,
            //        color.x, color.y, color.z, color.w,
            //        triCount);
            //}
        }
        
        color.x /= triCount;
        color.y /= triCount;
        color.z /= triCount;
        color.w = glm::min(color.w, 1.0f);//additive alpha blending
        
        normal = glm::normalize(glm::vec3(color));

        if(color.x < 0.0f)
            color.x *= -1.0f;
        if(color.y < 0.0f)
            color.y *= -1.0f;
        if(color.z < 0.0f)
            color.z *= -1.0f;
    }
    
    Vec4ToVoxColor(&voxelWriteColors[xIndex], color);
    voxelWriteNormals[xIndex] = normal;
}

__device__ float VectorLenSq(glm::vec3 vec)
{
     return vec.x * vec.x + vec.y * vec.y + vec.z * vec.z;
}

__global__ void ComputeVoxelMipMap64(
                        glm::uint voxInputDimX, glm::uint voxInputDimY, glm::uint voxInputDimZ,//input size
                        cudaPitchedPtr inputVoxelColors,//input colors
                        cudaPitchedPtr inputVoxelNormals,//input normals
                        glm::uint voxDimX, glm::uint voxDimY, glm::uint voxDimZ,//output size
                        cudaPitchedPtr mipMapColors,//output colors
                        cudaPitchedPtr mipMapNormals)//output normals
{
    glm::uint xIndex = (blockIdx.x * blockDim.x) + threadIdx.x;
    if(xIndex >= voxDimX)
        return;
    
    glm::uint yIndex = (blockIdx.y * blockDim.y) + threadIdx.y;
    if(yIndex >= voxDimY)
        return;

    glm::uint zIndex = (blockIdx.z * blockDim.z) + threadIdx.z;
    if(zIndex >= voxDimZ)
        return;

    glm::uint mmBaseX = xIndex * 4;
    glm::uint mmXPlus1 = glm::min(mmBaseX + 1, voxInputDimX-1);
    glm::uint mmXPlus2 = glm::min(mmBaseX + 2, voxInputDimX-1);
    glm::uint mmXPlus3 = glm::min(mmBaseX + 3, voxInputDimX-1);
    glm::uint mmBaseY = yIndex * 4;
    glm::uint mmYPlus1 = glm::min(mmBaseY + 1, voxInputDimY-1);
    glm::uint mmYPlus2 = glm::min(mmBaseY + 2, voxInputDimY-1);
    glm::uint mmYPlus3 = glm::min(mmBaseY + 3, voxInputDimY-1);
    glm::uint mmBaseZ = zIndex * 4;
    glm::uint mmZPlus1 = glm::min(mmBaseZ + 1, voxInputDimZ-1);
    glm::uint mmZPlus2 = glm::min(mmBaseZ + 2, voxInputDimZ-1);
    glm::uint mmZPlus3 = glm::min(mmBaseZ + 3, voxInputDimZ-1);

    char* pVoxels = (char*)inputVoxelColors.ptr;
    size_t pitch = inputVoxelColors.pitch;
    size_t slicePitch = inputVoxelColors.ysize * pitch;

    glm::uint mmBaseZOffset = mmBaseZ * slicePitch;
    glm::uint mmZPlus1Offset = mmZPlus1 * slicePitch;
    glm::uint mmZPlus2Offset = mmZPlus2 * slicePitch;
    glm::uint mmZPlus3Offset = mmZPlus3 * slicePitch;

    glm::uint mmBaseYOffset = mmBaseY * pitch;
    glm::uint mmYPlus1Offset = mmYPlus1 * pitch;
    glm::uint mmYPlus2Offset = mmYPlus2 * pitch;
    glm::uint mmYPlus3Offset = mmYPlus3 * pitch;

    cuda::VoxColor colorBox[64] = {
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmBaseX],//x, y, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmXPlus1],//x+1, y, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmXPlus2],//x+2, y, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmXPlus3],//x+3, y, z

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmBaseX],//x, y+1, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmXPlus1],//x+1, y+1, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmXPlus2],//x+2, y+1, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmXPlus3],//x+3, y+1, z

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus2Offset)[mmBaseX],//x, y+2, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus2Offset)[mmXPlus1],//x+1, y+2, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus2Offset)[mmXPlus2],//x+2, y+2, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus2Offset)[mmXPlus3],//x+3, y+2, z

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus3Offset)[mmBaseX],//x, y+3, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus3Offset)[mmXPlus1],//x+1, y+3, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus3Offset)[mmXPlus2],//x+2, y+3, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus3Offset)[mmXPlus3],//x+3, y+3, z

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmBaseX],//x, y, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmXPlus1],//x+1, y, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmXPlus2],//x+2, y, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmXPlus3],//x+3, y, z+1

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmBaseX],//x, y+1, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmXPlus1],//x+1, y+1, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmXPlus2],//x+2, y+1, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmXPlus3],//x+3, y+1, z+1

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus2Offset)[mmBaseX],//x, y+2, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus2Offset)[mmXPlus1],//x+1, y+2, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus2Offset)[mmXPlus2],//x+2, y+2, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus2Offset)[mmXPlus3],//x+3, y+2, z+1

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus3Offset)[mmBaseX],//x, y+3, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus3Offset)[mmXPlus1],//x+1, y+3, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus3Offset)[mmXPlus2],//x+2, y+3, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus3Offset)[mmXPlus3],//x+3, y+3, z+1

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmBaseYOffset)[mmBaseX],//x, y, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmBaseYOffset)[mmXPlus1],//x+1, y, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmBaseYOffset)[mmXPlus2],//x+2, y, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmBaseYOffset)[mmXPlus3],//x+3, y, z+2

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus1Offset)[mmBaseX],//x, y+1, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus1Offset)[mmXPlus1],//x+1, y+1, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus1Offset)[mmXPlus2],//x+2, y+1, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus1Offset)[mmXPlus3],//x+3, y+1, z+2

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus2Offset)[mmBaseX],//x, y+2, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus2Offset)[mmXPlus1],//x+1, y+2, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus2Offset)[mmXPlus2],//x+2, y+2, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus2Offset)[mmXPlus3],//x+3, y+2, z+2

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus3Offset)[mmBaseX],//x, y+3, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus3Offset)[mmXPlus1],//x+1, y+3, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus3Offset)[mmXPlus2],//x+2, y+3, z+2
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus2Offset + mmYPlus3Offset)[mmXPlus3],//x+3, y+3, z+2

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmBaseYOffset)[mmBaseX],//x, y, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmBaseYOffset)[mmXPlus1],//x+1, y, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmBaseYOffset)[mmXPlus2],//x+2, y, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmBaseYOffset)[mmXPlus3],//x+3, y, z+3

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus1Offset)[mmBaseX],//x, y+1, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus1Offset)[mmXPlus1],//x+1, y+1, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus1Offset)[mmXPlus2],//x+2, y+1, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus1Offset)[mmXPlus3],//x+3, y+1, z+3

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus2Offset)[mmBaseX],//x, y+2, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus2Offset)[mmXPlus1],//x+1, y+2, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus2Offset)[mmXPlus2],//x+2, y+2, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus2Offset)[mmXPlus3],//x+3, y+2, z+3

        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus3Offset)[mmBaseX],//x, y+3, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus3Offset)[mmXPlus1],//x+1, y+3, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus3Offset)[mmXPlus2],//x+2, y+3, z+3
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus3Offset + mmYPlus3Offset)[mmXPlus3]//x+3, y+3, z+3
    };

    float alphaSum = colorBox[0].w;
    alphaSum += colorBox[1].w;
    alphaSum += colorBox[2].w;
    alphaSum += colorBox[3].w;
    alphaSum += colorBox[4].w;
    alphaSum += colorBox[5].w;
    alphaSum += colorBox[6].w;
    alphaSum += colorBox[7].w;
    alphaSum += colorBox[8].w;
    alphaSum += colorBox[9].w;
    alphaSum += colorBox[10].w;
    alphaSum += colorBox[11].w;
    alphaSum += colorBox[12].w;
    alphaSum += colorBox[13].w;
    alphaSum += colorBox[14].w;
    alphaSum += colorBox[15].w;
    alphaSum += colorBox[16].w;
    alphaSum += colorBox[17].w;
    alphaSum += colorBox[18].w;
    alphaSum += colorBox[19].w;
    alphaSum += colorBox[20].w;
    alphaSum += colorBox[21].w;
    alphaSum += colorBox[22].w;
    alphaSum += colorBox[23].w;
    alphaSum += colorBox[24].w;
    alphaSum += colorBox[25].w;
    alphaSum += colorBox[26].w;
    alphaSum += colorBox[27].w;
    alphaSum += colorBox[28].w;
    alphaSum += colorBox[29].w;
    alphaSum += colorBox[30].w;
    alphaSum += colorBox[31].w;
    alphaSum += colorBox[32].w;
    alphaSum += colorBox[33].w;
    alphaSum += colorBox[34].w;
    alphaSum += colorBox[35].w;
    alphaSum += colorBox[36].w;
    alphaSum += colorBox[37].w;
    alphaSum += colorBox[38].w;
    alphaSum += colorBox[39].w;
    alphaSum += colorBox[40].w;
    alphaSum += colorBox[41].w;
    alphaSum += colorBox[42].w;
    alphaSum += colorBox[43].w;
    alphaSum += colorBox[44].w;
    alphaSum += colorBox[45].w;
    alphaSum += colorBox[46].w;
    alphaSum += colorBox[47].w;
    alphaSum += colorBox[48].w;
    alphaSum += colorBox[49].w;
    alphaSum += colorBox[50].w;
    alphaSum += colorBox[51].w;
    alphaSum += colorBox[52].w;
    alphaSum += colorBox[53].w;
    alphaSum += colorBox[54].w;
    alphaSum += colorBox[55].w;
    alphaSum += colorBox[56].w;
    alphaSum += colorBox[57].w;
    alphaSum += colorBox[58].w;
    alphaSum += colorBox[59].w;
    alphaSum += colorBox[60].w;
    alphaSum += colorBox[61].w;
    alphaSum += colorBox[62].w;
    alphaSum += colorBox[63].w;
    
    float alphaWeights[64] = { 
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
    };

    if(alphaSum > 0.0f)
    {
        alphaWeights[0] = colorBox[0].w / alphaSum;
        alphaWeights[1] = colorBox[1].w / alphaSum;
        alphaWeights[2] = colorBox[2].w / alphaSum;
        alphaWeights[3] = colorBox[3].w / alphaSum;
        alphaWeights[4] = colorBox[4].w / alphaSum;
        alphaWeights[5] = colorBox[5].w / alphaSum;
        alphaWeights[6] = colorBox[6].w / alphaSum;
        alphaWeights[7] = colorBox[7].w / alphaSum;
        alphaWeights[8] = colorBox[8].w / alphaSum;
        alphaWeights[9] = colorBox[9].w / alphaSum;

        alphaWeights[10] = colorBox[10].w / alphaSum;
        alphaWeights[11] = colorBox[11].w / alphaSum;
        alphaWeights[12] = colorBox[12].w / alphaSum;
        alphaWeights[13] = colorBox[13].w / alphaSum;
        alphaWeights[14] = colorBox[14].w / alphaSum;
        alphaWeights[15] = colorBox[15].w / alphaSum;
        alphaWeights[16] = colorBox[16].w / alphaSum;
        alphaWeights[17] = colorBox[17].w / alphaSum;
        alphaWeights[18] = colorBox[18].w / alphaSum;
        alphaWeights[19] = colorBox[19].w / alphaSum;
        
        alphaWeights[20] = colorBox[20].w / alphaSum;
        alphaWeights[21] = colorBox[21].w / alphaSum;
        alphaWeights[22] = colorBox[22].w / alphaSum;
        alphaWeights[23] = colorBox[23].w / alphaSum;
        alphaWeights[24] = colorBox[24].w / alphaSum;
        alphaWeights[25] = colorBox[25].w / alphaSum;
        alphaWeights[26] = colorBox[26].w / alphaSum;
        alphaWeights[27] = colorBox[27].w / alphaSum;
        alphaWeights[28] = colorBox[28].w / alphaSum;
        alphaWeights[29] = colorBox[29].w / alphaSum;
        
        alphaWeights[30] = colorBox[30].w / alphaSum;
        alphaWeights[31] = colorBox[31].w / alphaSum;
        alphaWeights[32] = colorBox[32].w / alphaSum;
        alphaWeights[33] = colorBox[33].w / alphaSum;
        alphaWeights[34] = colorBox[34].w / alphaSum;
        alphaWeights[35] = colorBox[35].w / alphaSum;
        alphaWeights[36] = colorBox[36].w / alphaSum;
        alphaWeights[37] = colorBox[37].w / alphaSum;
        alphaWeights[38] = colorBox[38].w / alphaSum;
        alphaWeights[39] = colorBox[39].w / alphaSum;
        
        alphaWeights[40] = colorBox[40].w / alphaSum;
        alphaWeights[41] = colorBox[41].w / alphaSum;
        alphaWeights[42] = colorBox[42].w / alphaSum;
        alphaWeights[43] = colorBox[43].w / alphaSum;
        alphaWeights[44] = colorBox[44].w / alphaSum;
        alphaWeights[45] = colorBox[45].w / alphaSum;
        alphaWeights[46] = colorBox[46].w / alphaSum;
        alphaWeights[47] = colorBox[47].w / alphaSum;
        alphaWeights[48] = colorBox[48].w / alphaSum;
        alphaWeights[49] = colorBox[49].w / alphaSum;
        
        alphaWeights[50] = colorBox[50].w / alphaSum;
        alphaWeights[51] = colorBox[51].w / alphaSum;
        alphaWeights[52] = colorBox[52].w / alphaSum;
        alphaWeights[53] = colorBox[53].w / alphaSum;
        alphaWeights[54] = colorBox[54].w / alphaSum;
        alphaWeights[55] = colorBox[55].w / alphaSum;
        alphaWeights[56] = colorBox[56].w / alphaSum;
        alphaWeights[57] = colorBox[57].w / alphaSum;
        alphaWeights[58] = colorBox[58].w / alphaSum;
        alphaWeights[59] = colorBox[59].w / alphaSum;
        
        alphaWeights[60] = colorBox[60].w / alphaSum;
        alphaWeights[61] = colorBox[61].w / alphaSum;
        alphaWeights[62] = colorBox[62].w / alphaSum;
        alphaWeights[63] = colorBox[63].w / alphaSum;
    }

    pVoxels = (char*)inputVoxelNormals.ptr;
    pitch = inputVoxelNormals.pitch;
    slicePitch = inputVoxelNormals.ysize * pitch;

    mmBaseZOffset = mmBaseZ * slicePitch;
    mmZPlus1Offset = mmZPlus1 * slicePitch;
    mmZPlus2Offset = mmZPlus2 * slicePitch;
    mmZPlus3Offset = mmZPlus3 * slicePitch;

    mmBaseYOffset = mmBaseY * pitch;
    mmYPlus1Offset = mmYPlus1 * pitch;
    mmYPlus2Offset = mmYPlus2 * pitch;
    mmYPlus3Offset = mmYPlus3 * pitch;

    cuda::VoxNorm normals[64] = 
    {
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmBaseX],//x, y, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmXPlus1],//x+1, y, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmXPlus2],//x+2, y, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmXPlus3],//x+3, y, z

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmBaseX],//x, y+1, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmXPlus1],//x+1, y+1, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmXPlus2],//x+2, y+1, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmXPlus3],//x+3, y+1, z

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus2Offset)[mmBaseX],//x, y+2, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus2Offset)[mmXPlus1],//x+1, y+2, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus2Offset)[mmXPlus2],//x+2, y+2, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus2Offset)[mmXPlus3],//x+3, y+2, z

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus3Offset)[mmBaseX],//x, y+3, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus3Offset)[mmXPlus1],//x+1, y+3, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus3Offset)[mmXPlus2],//x+2, y+3, z
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmBaseZOffset + mmYPlus3Offset)[mmXPlus3],//x+3, y+3, z

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmBaseX],//x, y, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmXPlus1],//x+1, y, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmXPlus2],//x+2, y, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmXPlus3],//x+3, y, z+1

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmBaseX],//x, y+1, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmXPlus1],//x+1, y+1, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmXPlus2],//x+2, y+1, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmXPlus3],//x+3, y+1, z+1

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus2Offset)[mmBaseX],//x, y+2, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus2Offset)[mmXPlus1],//x+1, y+2, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus2Offset)[mmXPlus2],//x+2, y+2, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus2Offset)[mmXPlus3],//x+3, y+2, z+1

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus3Offset)[mmBaseX],//x, y+3, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus3Offset)[mmXPlus1],//x+1, y+3, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus3Offset)[mmXPlus2],//x+2, y+3, z+1
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus1Offset + mmYPlus3Offset)[mmXPlus3],//x+3, y+3, z+1

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmBaseYOffset)[mmBaseX],//x, y, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmBaseYOffset)[mmXPlus1],//x+1, y, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmBaseYOffset)[mmXPlus2],//x+2, y, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmBaseYOffset)[mmXPlus3],//x+3, y, z+2

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus1Offset)[mmBaseX],//x, y+1, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus1Offset)[mmXPlus1],//x+1, y+1, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus1Offset)[mmXPlus2],//x+2, y+1, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus1Offset)[mmXPlus3],//x+3, y+1, z+2

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus2Offset)[mmBaseX],//x, y+2, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus2Offset)[mmXPlus1],//x+1, y+2, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus2Offset)[mmXPlus2],//x+2, y+2, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus2Offset)[mmXPlus3],//x+3, y+2, z+2

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus3Offset)[mmBaseX],//x, y+3, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus3Offset)[mmXPlus1],//x+1, y+3, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus3Offset)[mmXPlus2],//x+2, y+3, z+2
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus2Offset + mmYPlus3Offset)[mmXPlus3],//x+3, y+3, z+2

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmBaseYOffset)[mmBaseX],//x, y, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmBaseYOffset)[mmXPlus1],//x+1, y, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmBaseYOffset)[mmXPlus2],//x+2, y, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmBaseYOffset)[mmXPlus3],//x+3, y, z+3

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus1Offset)[mmBaseX],//x, y+1, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus1Offset)[mmXPlus1],//x+1, y+1, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus1Offset)[mmXPlus2],//x+2, y+1, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus1Offset)[mmXPlus3],//x+3, y+1, z+3

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus2Offset)[mmBaseX],//x, y+2, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus2Offset)[mmXPlus1],//x+1, y+2, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus2Offset)[mmXPlus2],//x+2, y+2, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus2Offset)[mmXPlus3],//x+3, y+2, z+3

        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus3Offset)[mmBaseX],//x, y+3, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus3Offset)[mmXPlus1],//x+1, y+3, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus3Offset)[mmXPlus2],//x+2, y+3, z+3
        reinterpret_cast<cuda::VoxNorm*>(pVoxels + mmZPlus3Offset + mmYPlus3Offset)[mmXPlus3]//x+3, y+3, z+3
    };

    glm::vec4 avgColor;
    VoxColorToVec4(&avgColor, colorBox[0]);
    avgColor.x *= alphaWeights[0];
    avgColor.y *= alphaWeights[0];
    avgColor.z *= alphaWeights[0];

    glm::vec4 curColor;

    cuda::VoxNorm avgNormal = normals[0];
    for(int i = 1; i < 64; ++i)
    {
        VoxColorToVec4(&curColor, colorBox[i]);
        curColor.x *= alphaWeights[i];
        curColor.y *= alphaWeights[i];
        curColor.z *= alphaWeights[i];

        avgColor += curColor;

        avgNormal += normals[i];
    }

    const float oneOverSixtyFour = 1.0f / 64.0f;

    avgColor.w *= oneOverSixtyFour;
    avgColor.w = glm::pow(avgColor.w, 1.0f - avgColor.w);
    avgColor.x *= avgColor.w;
    avgColor.y *= avgColor.w;
    avgColor.z *= avgColor.w;

    avgNormal *= oneOverSixtyFour;
    float len2 = glm::length2(avgNormal);
    if(len2 > 0.0f)
        avgNormal /= (glm::sqrt(len2));//normalize
    else
        avgNormal = glm::vec3(0.0f);

    pVoxels = (char*)mipMapColors.ptr;
    pitch = mipMapColors.pitch;
    slicePitch = mipMapColors.ysize * pitch;
    //if(xIndex >= 110 && xIndex <= 112 && yIndex == 7 && zIndex == 8)
    //    printf("[%d %d %d]=[%f %f %f %f]\n", xIndex-7, yIndex-7, zIndex-7, avgColor.x, avgColor.y, avgColor.z, avgColor.w);
    cuda::VoxColor voxColor;
    Vec4ToVoxColor(&voxColor, avgColor);
    reinterpret_cast<cuda::VoxColor*>(pVoxels + (zIndex * slicePitch) + (yIndex * pitch))[xIndex] = voxColor;
    //reinterpret_cast<cuda::VoxColor*>(pVoxels + (zIndex * slicePitch) + (yIndex * pitch))[xIndex] = cuda::VoxColor(xIndex, yIndex, zIndex, 0.0f);

    pVoxels = (char*)mipMapNormals.ptr;
    pitch = mipMapNormals.pitch;
    slicePitch = mipMapNormals.ysize * pitch;

    reinterpret_cast<glm::vec3*>(pVoxels + (zIndex * slicePitch) + (yIndex * pitch))[xIndex] = avgNormal;
    //reinterpret_cast<glm::vec3*>(pVoxels + (zIndex * slicePitch) + (yIndex * pitch))[xIndex] = glm::vec3(xIndex, yIndex, zIndex);
}

__global__ void ComputeVoxelMipMap8(
                        glm::uint voxInputDimX, glm::uint voxInputDimY, glm::uint voxInputDimZ,//input size
                        cudaPitchedPtr inputVoxelColors,//input colors
                        cudaPitchedPtr inputVoxelNormals,//input normals
                        glm::uint voxDimX, glm::uint voxDimY, glm::uint voxDimZ,//output size
                        cudaPitchedPtr mipMapColors,//output colors
                        cudaPitchedPtr mipMapNormals)//output normals
{
    glm::uint xIndex = (blockIdx.x * blockDim.x) + threadIdx.x;
    if(xIndex >= voxDimX)
        return;
    
    glm::uint yIndex = (blockIdx.y * blockDim.y) + threadIdx.y;
    if(yIndex >= voxDimY)
        return;

    glm::uint zIndex = (blockIdx.z * blockDim.z) + threadIdx.z;
    if(zIndex >= voxDimZ)
        return;

    glm::uint mmBaseX = xIndex * 2;
    glm::uint mmXPlus1 = glm::min(mmBaseX + 1, voxInputDimX-1);
    glm::uint mmBaseY = yIndex * 2;
    glm::uint mmYPlus1 = glm::min(mmBaseY + 1, voxInputDimY-1);
    glm::uint mmBaseZ = zIndex * 2;
    glm::uint mmZPlus1 = glm::min(mmBaseZ + 1, voxInputDimZ-1);

    char* pVoxels = (char*)inputVoxelColors.ptr;
    size_t pitch = inputVoxelColors.pitch;
    size_t slicePitch = inputVoxelColors.ysize * pitch;

    glm::uint mmBaseZOffset = mmBaseZ * slicePitch;
    glm::uint mmZPlus1Offset = mmZPlus1 * slicePitch;

    glm::uint mmBaseYOffset = mmBaseY * pitch;
    glm::uint mmYPlus1Offset = mmYPlus1 * pitch;

    cuda::VoxColor colorBox[8] = {
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmBaseX],//x, y, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmXPlus1],//x+1, y, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmBaseX],//x, y+1, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmXPlus1],//x+1, y+1, z
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmBaseX],//x, y, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmXPlus1],//x+1, y, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmBaseX],//x, y+1, z+1
        reinterpret_cast<cuda::VoxColor*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmXPlus1]//x+1, y+1, z+1
    };

    /*float alphaSum = colorBox[0].w;
    alphaSum += colorBox[1].w;
    alphaSum += colorBox[2].w;
    alphaSum += colorBox[3].w;
    alphaSum += colorBox[4].w;
    alphaSum += colorBox[5].w;
    alphaSum += colorBox[6].w;
    alphaSum += colorBox[7].w;
    float alphaWeights[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    if(alphaSum > 0.0f)
    {
        alphaWeights[0] = colorBox[0].w / alphaSum;
        alphaWeights[1] = colorBox[1].w / alphaSum;
        alphaWeights[2] = colorBox[2].w / alphaSum;
        alphaWeights[3] = colorBox[3].w / alphaSum;
        alphaWeights[4] = colorBox[4].w / alphaSum;
        alphaWeights[5] = colorBox[5].w / alphaSum;
        alphaWeights[6] = colorBox[6].w / alphaSum;
        alphaWeights[7] = colorBox[7].w / alphaSum;
    }*/

    pVoxels = (char*)inputVoxelNormals.ptr;
    pitch = inputVoxelNormals.pitch;
    slicePitch = inputVoxelNormals.ysize * pitch;

    mmBaseZOffset = mmBaseZ * slicePitch;
    mmZPlus1Offset = mmZPlus1 * slicePitch;

    mmBaseYOffset = mmBaseY * pitch;
    mmYPlus1Offset = mmYPlus1 * pitch;

    glm::vec3 normals[8] = 
    {
        reinterpret_cast<glm::vec3*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmBaseX],//x, y, z
        reinterpret_cast<glm::vec3*>(pVoxels + mmBaseZOffset + mmBaseYOffset)[mmXPlus1],//x+1, y, z
        reinterpret_cast<glm::vec3*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmBaseX],//x, y+1, z
        reinterpret_cast<glm::vec3*>(pVoxels + mmBaseZOffset + mmYPlus1Offset)[mmXPlus1],//x+1, y+1, z
        reinterpret_cast<glm::vec3*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmBaseX],//x, y, z+1
        reinterpret_cast<glm::vec3*>(pVoxels + mmZPlus1Offset + mmBaseYOffset)[mmXPlus1],//x+1, y, z+1
        reinterpret_cast<glm::vec3*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmBaseX],//x, y+1, z+1
        reinterpret_cast<glm::vec3*>(pVoxels + mmZPlus1Offset + mmYPlus1Offset)[mmXPlus1]//x+1, y+1, z+1
    };

    glm::vec4 avgColor;
    VoxColorToVec4(&avgColor, colorBox[0]);
    /*avgColor.x *= alphaWeights[0];
    avgColor.y *= alphaWeights[0];
    avgColor.z *= alphaWeights[0];*/

    glm::vec4 curColor;

    cuda::VoxNorm avgNormal = normals[0];
    for(int i = 1; i < 8; ++i)
    {
        VoxColorToVec4(&curColor, colorBox[i]);
        /*curColor.x *= alphaWeights[i];
        curColor.y *= alphaWeights[i];
        curColor.z *= alphaWeights[i];*/

        avgColor += curColor;

        avgNormal += normals[i];
    }

    const float oneOverEight = 1.0f / 8.0f;
    avgColor.w *= oneOverEight;
    //avgColor.w = glm::pow(avgColor.w, 1.0f - avgColor.w);
    avgColor.x *= oneOverEight;//avgColor.w;
    avgColor.y *= oneOverEight;//avgColor.w;
    avgColor.z *= oneOverEight;//avgColor.w;

    avgNormal *= oneOverEight;
    float len2 = glm::length2(avgNormal);
    if(len2 > 0.0f)
        avgNormal /= (glm::sqrt(len2));//normalize
    else
        avgNormal = glm::vec3(0.0f);

    pVoxels = (char*)mipMapColors.ptr;
    pitch = mipMapColors.pitch;
    slicePitch = mipMapColors.ysize * pitch;

    cuda::VoxColor voxColor;
    Vec4ToVoxColor(&voxColor, avgColor);
    //if(xIndex >= 110 && xIndex <= 112 && yIndex == 7 && zIndex == 8)
    //    printf("[%d %d %d]=[%f %f %f %f]\n", xIndex-7, yIndex-7, zIndex-7, avgColor.x, avgColor.y, avgColor.z, avgColor.w);
    reinterpret_cast<cuda::VoxColor*>(pVoxels + (zIndex * slicePitch) + (yIndex * pitch))[xIndex] = voxColor;
    //reinterpret_cast<cuda::VoxColor*>(pVoxels + (zIndex * slicePitch) + (yIndex * pitch))[xIndex] = cuda::VoxColor(xIndex, yIndex, zIndex, 0.0f);

    pVoxels = (char*)mipMapNormals.ptr;
    pitch = mipMapNormals.pitch;
    slicePitch = mipMapNormals.ysize * pitch;

    reinterpret_cast<glm::vec3*>(pVoxels + (zIndex * slicePitch) + (yIndex * pitch))[xIndex] = avgNormal;
    //reinterpret_cast<glm::vec3*>(pVoxels + (zIndex * slicePitch) + (yIndex * pitch))[xIndex] = glm::vec3(xIndex, yIndex, zIndex);
}

__device__ glm::uint EncodeColorAsUInt(glm::vec4 color)
{
    unsigned int r = static_cast<unsigned int>(color.x * 255.0f);
    unsigned int g = static_cast<unsigned int>(color.y * 255.0f);
    unsigned int b = static_cast<unsigned int>(color.z * 255.0f);
    unsigned int a = static_cast<unsigned int>(color.w * 255.0f);

    glm::uint bits = (r << 24);
    bits |= (g << 16);
    bits |= (b << 8);
    bits |= a;

    return bits;
}

__global__ void ComputeOctTreeNodeConstColor(cudaPitchedPtr devVoxels, 
                                             cuda::VoxColor* devOctTreeConstColors,
                                             glm::ivec3 voxOffset,
                                             glm::ivec3 offsetToBrickBorder,
                                             glm::ivec3 voxDim,
                                             glm::uvec3 octTreeChunkDim,
                                             glm::uvec3 octTreeDim,
                                             glm::ivec3 nodeOffset,
                                             glm::uvec3 brickDim)
{
    //compute x, y, z of my voxel in the chunk
    int xIndex = (blockIdx.x * blockDim.x) + threadIdx.x;
    if(xIndex >= octTreeChunkDim.x)
        return;
    
    int yIndex = (blockIdx.y * blockDim.y) + threadIdx.y;
    if(yIndex >= octTreeChunkDim.y)
        return;

    int zIndex = (blockIdx.z * blockDim.z) + threadIdx.z;
    if(zIndex >= octTreeChunkDim.z)
        return;

    //use that to compute node's x, y, z
    int brickX = xIndex + nodeOffset.x;
    int brickY = yIndex + nodeOffset.y;
    int brickZ = zIndex + nodeOffset.z;

    //compute x,y,z of first element of this node's brick
    int brickSampleX = (brickX * brickDim.x) - voxOffset.x + offsetToBrickBorder.x;
    int brickSampleY = (brickY * brickDim.y) - voxOffset.y + offsetToBrickBorder.y;
    int brickSampleZ = (brickZ * brickDim.z) - voxOffset.z + offsetToBrickBorder.z;

    if(brickSampleX < 0)
    {
        //use first available element
        //printf("brickSimpleX < 0 (%d => %d)", brickSampleX, ((brickX+1) * brickDim.x) - voxOffset.x + offsetToBrickBorder.x);
        brickSampleX = 0;
    }
    
    if(brickSampleY < 0)
    {
        //use first available element
        //printf("brickSimpleY < 0 (%d => %d)", brickSampleY, ((brickY+1) * brickDim.y) - voxOffset.y + offsetToBrickBorder.y);
        brickSampleY = 0;
    }
    
    if(brickSampleZ < 0)
    {
        //use first available element
        //printf("brickSimpleZ < 0 (%d => %d)", brickSampleZ, ((brickZ+1) * brickDim.z) - voxOffset.z + offsetToBrickBorder.z);
        brickSampleZ = 0;
    }

    if(brickSampleX >= voxDim.x)
    {
        //use last element from previous brick
        brickSampleX = voxDim.x - 1;
    }
    
    if(brickSampleY >= voxDim.y)
    {
        //use last element from previous brick
        brickSampleY = voxDim.y - 1;
    }

    if(brickSampleZ >= voxDim.z)
    {
        //use last element from previous brick
        brickSampleZ = voxDim.z - 1;
    }

    char* devPtrColors = (char*)devVoxels.ptr;
    size_t voxelsPitch = devVoxels.pitch;
    size_t voxelsSlicePitch = voxelsPitch * devVoxels.ysize;

    char* sliceVoxels = devPtrColors + (brickSampleZ * voxelsSlicePitch);
    cuda::VoxColor* voxelColors = (cuda::VoxColor*)(sliceVoxels + (brickSampleY * voxelsPitch));
    cuda::VoxColor constVal = voxelColors[brickSampleX];

    //if(brickX == 4 && brickY == 0 && brickZ == 0 && 
    //   octTreeDim.x == 8u && octTreeDim.y == 8u && octTreeDim.z == 8u)
    //{
    //    printf("constVal[%d %d %d] = vox[%d %d %d] = %f %f %f %f\n", 
    //           brickX, brickY, brickZ,
    //           brickSampleX, brickSampleY, brickSampleZ,
    //           constVal.x, constVal.y, constVal.z, constVal.w);

    //}
    devOctTreeConstColors[(brickZ * octTreeDim.x * octTreeDim.y)
                          + (brickY * octTreeDim.x)
                          + brickX] = constVal;
}

__device__ bool VEC4_EQUAL(const glm::vec4& v1, const glm::vec4& v2)
{
    const float epsilon = 0.001f;
    glm::vec4 diff = v1 - v2;

    return (diff.x > -epsilon && diff.x < epsilon) &&
           (diff.y > -epsilon && diff.y < epsilon) &&
           (diff.z > -epsilon && diff.z < epsilon) &&
           (diff.w > -epsilon && diff.w < epsilon);
}

__device__ bool VEC4_EQUAL(const uchar4& v1, const uchar4& v2)
{
    return v1.x == v2.x && v1.y == v2.y && v1.z == v2.z && v1.w == v2.w;
}

__device__ void ComputeNodeType(int xIndex,
                                int yIndex,
                                int zIndex,
                                int brickX,
                                int brickY, 
                                int brickZ,
                                //int brickSampleX,
                                //int brickSampleY,
                                //int brickSampleZ,
                                cudaPitchedPtr devVoxels,
                                glm::ivec3 voxDim,
                                glm::uint* devOctTreeNodes,
                                cuda::VoxColor* devOctTreeConstColors,
                                glm::uvec3 octTreeDim)
{
    if(xIndex < 0 || xIndex >= voxDim.x ||
       yIndex < 0 || yIndex >= voxDim.y ||
       zIndex < 0 || zIndex >= voxDim.z)
    {
        printf("Shit! xyzIndex=%d %d %d\n", xIndex, yIndex, zIndex);
        return;
    }

    //if(brickSampleX < 0 || brickSampleX >= voxDim.x ||
    //   brickSampleY < 0 || brickSampleY >= voxDim.y ||
    //   brickSampleZ < 0 || brickSampleZ >= voxDim.z)
    //{
    //    printf("SOB! xyzIndex=%d %d %d brickSampleXYZ=%d %d %d\n", 
    //           xIndex, yIndex, zIndex, 
    //           brickSampleX, brickSampleY, brickSampleZ);
    //    return;
    //}

    if(brickX < 0 || brickX >= octTreeDim.x ||
       brickY < 0 || brickY >= octTreeDim.y ||
       brickZ < 0 || brickZ >= octTreeDim.z)
    {
        printf("WTF? xyzIndex=%d %d %d brickXYZ=%d %d %d\n", 
               xIndex, yIndex, zIndex,
               brickX, brickY, brickZ);
        return;
    }

    char* devPtrColors = (char*)devVoxels.ptr;
    size_t voxelsPitch = devVoxels.pitch;
    size_t voxelsSlicePitch = voxelsPitch * devVoxels.ysize;

    //char* sliceVoxels = devPtrColors + (brickSampleZ * voxelsSlicePitch);
    //cuda::VoxColor* voxelColors = (cuda::VoxColor*)(sliceVoxels + (brickSampleY * voxelsPitch));
    //cuda::VoxColor constVal = voxelColors[brickSampleX];
    cuda::VoxColor constVal = devOctTreeConstColors[(brickZ * octTreeDim.x * octTreeDim.y)
                                               + (brickY * octTreeDim.x)
                                               + brickX];

    char* sliceVoxels = devPtrColors + (zIndex * voxelsSlicePitch);
    cuda::VoxColor* voxelColors = (cuda::VoxColor*)(sliceVoxels + (yIndex * voxelsPitch));
    cuda::VoxColor checkVal = voxelColors[xIndex];


    //if(xIndex == 12 &&
    //   yIndex == 110 &&
    //   zIndex == 100 &&
    //   octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
    //{
    //    printf("vox[%d %d %d] = %f %f %f %f == %f %f %f %f -> %d (node=%d %d %d)\n", 
    //           xIndex, yIndex, zIndex,
    //           checkVal.x, checkVal.y, checkVal.z, checkVal.w, 
    //           constVal.x, constVal.y, constVal.z, constVal.w,
    //           VEC4_FLOATS_EQUAL(constVal, checkVal) == false ? 1 : 0,
    //           brickX, brickY, brickZ);

    //}

    if(VEC4_EQUAL(constVal, checkVal) == false)
    {
        //if(brickX == 8u && brickY == 1u && brickZ == 31u && octTreeDim.x == 32u)
        /*if(xIndex == 127 &&
            yIndex == 30 &&
            zIndex == 209 &&
            octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
        {
            printf("Node[%d %d %d]==1 voxel[%d %d %d] (%f %f %f %f)=(%f %f %f %f)\n",
                    brickX, brickY, brickZ,
                    xIndex, yIndex, zIndex,
                    constVal.x,
                    constVal.y,
                    constVal.z,
                    constVal.w,
                    checkVal.x,
                    checkVal.y,
                    checkVal.z,
                    checkVal.w);
        }*/
        
        devOctTreeNodes[(brickZ * octTreeDim.x * octTreeDim.y)
                        + (brickY * octTreeDim.x)
                        + brickX] = 1u;//non-const node
        //if(xIndex == 29u && yIndex == 16u && zIndex == 20u)// && octTreeDim.x == 4u)
        //    printf("[%d %d %d]=%f %f %f %f == %f %f %f %f [%d %d %d]=[%d]=%d\n", 
        //       brickSampleX, brickSampleY, brickSampleZ,
        //       constVal.x, constVal.y, constVal.z, constVal.w,
        //       checkVal.x, checkVal.y, checkVal.z, checkVal.w,
        //       brickX, brickY, brickZ,
        //       (brickZ * octTreeDim.x * octTreeDim.y) + (brickY * octTreeDim.x) + brickX,
        //       devOctTreeNodes[(brickZ * octTreeDim.x * octTreeDim.y)
        //                + (brickY * octTreeDim.x)
        //                + brickX]);
    }
}

__device__ void HandleZBorder(int xIndex,
                              int yIndex,
                              int zIndex,
                              int brickX,
                              int brickY, 
                              int brickZ,
                              int brickBaseX,
                              int brickBaseY,
                              int brickBaseZ,
                              //glm::ivec3 brickSampleOffset,
                              glm::uvec3 brickDim,
                              cudaPitchedPtr devVoxels,
                              glm::ivec3 voxDim,
                              glm::uint* devOctTreeNodes,
                              cuda::VoxColor* devOctTreeConstColors,
                              glm::uvec3 octTreeDim)
{
    //check if z is one past end of prev brick
    if(zIndex == brickBaseZ && brickZ > 0)
    {
        int borderBrickZ = brickZ - 1;
        
        /*if(xIndex == 127 &&
            yIndex == 30 &&
            zIndex == 209 &&
            octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
        {
            printf("In HandleZBorder subZ Node[%d %d %d]==1 voxel[%d %d %d]\n",
                brickX, brickY, borderBrickZ,
                xIndex, yIndex, zIndex);
        }*/
        ComputeNodeType(xIndex, yIndex, zIndex,
                        brickX, brickY, borderBrickZ,
                        //brickBaseX + brickSampleOffset.x, 
                        //brickBaseY + brickSampleOffset.y, 
                        //borderBrickBaseZ + brickSampleOffset.z,
                        devVoxels,
                        voxDim,
                        devOctTreeNodes,
                        devOctTreeConstColors,
                        octTreeDim);
    }
    else if(zIndex == (brickBaseZ + brickDim.z - 1) && brickZ < (octTreeDim.z - 1))//one before start of next brick
    {
        int borderBrickZ = brickZ + 1;

        /*if(xIndex == 127 &&
            yIndex == 30 &&
            zIndex == 209 &&
            octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
        {
            printf("In HandleZBorder addZ Node[%d %d %d]==1 voxel[%d %d %d]\n",
                brickX, brickY, borderBrickZ,
                xIndex, yIndex, zIndex);
        }*/
        ComputeNodeType(xIndex, yIndex, zIndex,
                        brickX, brickY, borderBrickZ,
                        //brickBaseX + brickSampleOffset.x, 
                        //brickBaseY + brickSampleOffset.y, 
                        //borderBrickBaseZ + brickSampleOffset.z,
                        devVoxels,
                        voxDim,
                        devOctTreeNodes,
                        devOctTreeConstColors,
                        octTreeDim);
    }
}

__device__ void HandleYBorder(int xIndex,
                              int yIndex,
                              int zIndex,
                              int brickX,
                              int brickY, 
                              int brickZ,
                              int brickBaseX,
                              int brickBaseY,
                              int brickBaseZ,
                              //glm::ivec3 brickSampleOffset,
                              glm::uvec3 brickDim,
                              cudaPitchedPtr devVoxels,
                              glm::ivec3 voxDim,
                              glm::uint* devOctTreeNodes,
                              cuda::VoxColor* devOctTreeConstColors,
                              glm::uvec3 octTreeDim)
{
    //check if y is one past end of prev brick
    if(yIndex == brickBaseY && brickY > 0)
    {
        int borderBrickY = brickY - 1;
        int borderBrickBaseY = brickBaseY - brickDim.y;

        /*if(xIndex == 127 &&
            yIndex == 30 &&
            zIndex == 209 &&
            octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
        {
            printf("In HandleYBorder sub Node[%d %d %d]==1 voxel[%d %d %d]\n",
                brickX, borderBrickY, brickZ,
                xIndex, yIndex, zIndex);
        }*/

        ComputeNodeType(xIndex, yIndex, zIndex,
                        brickX, borderBrickY, brickZ,
                        //brickBaseX + brickSampleOffset.x, 
                        //borderBrickBaseY + brickSampleOffset.y, 
                        //brickBaseZ + brickSampleOffset.z,
                        devVoxels,
                        voxDim,
                        devOctTreeNodes,
                        devOctTreeConstColors,
                        octTreeDim);

        if((zIndex == brickBaseZ && brickZ > 0) || 
           (zIndex == (brickBaseZ + brickDim.z - 1) && brickZ < (octTreeDim.z - 1)))
        {
            HandleZBorder(xIndex, yIndex, zIndex,
                          brickX, borderBrickY, brickZ,
                          brickBaseX, borderBrickBaseY, brickBaseZ,
                          //brickSampleOffset,
                          brickDim,
                          devVoxels,
                          voxDim,
                          devOctTreeNodes,
                          devOctTreeConstColors,
                          octTreeDim);
        }
    }
    else if(yIndex == (brickBaseY + brickDim.y - 1) && brickY < (octTreeDim.y - 1))//one before start of next brick
    {
        int borderBrickY = brickY + 1;
        int borderBrickBaseY = brickBaseY + brickDim.y;

        /*if(xIndex == 127 &&
            yIndex == 30 &&
            zIndex == 209 &&
            octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
        {
            printf("In HandleYBorder add Node[%d %d %d]==1 voxel[%d %d %d]\n",
                brickX, borderBrickY, brickZ,
                xIndex, yIndex, zIndex);
        }*/
                
        ComputeNodeType(xIndex, yIndex, zIndex,
                        brickX, borderBrickY, brickZ,
                        //brickBaseX + brickSampleOffset.x, 
                        //borderBrickBaseY + brickSampleOffset.y, 
                        //brickBaseZ + brickSampleOffset.z,
                        devVoxels,
                        voxDim,
                        devOctTreeNodes,
                        devOctTreeConstColors,
                        octTreeDim);

        if((zIndex == brickBaseZ && brickZ > 0) || 
           (zIndex == (brickBaseZ + brickDim.z - 1) && brickZ < (octTreeDim.z - 1)))
        {
            HandleZBorder(xIndex, yIndex, zIndex,
                          brickX, borderBrickY, brickZ,
                          brickBaseX, borderBrickBaseY, brickBaseZ,
                          //brickSampleOffset,
                          brickDim,
                          devVoxels,
                          voxDim,
                          devOctTreeNodes,
                          devOctTreeConstColors,
                          octTreeDim);
        }
    }
}

__device__ void HandleXBorder(int xIndex,
                              int yIndex,
                              int zIndex,
                              int brickX,
                              int brickY, 
                              int brickZ,
                              int brickBaseX,
                              int brickBaseY,
                              int brickBaseZ,
                              //glm::ivec3 brickSampleOffset,
                              glm::uvec3 brickDim,
                              cudaPitchedPtr devVoxels,
                              glm::ivec3 voxDim,
                              glm::uint* devOctTreeNodes,
                              cuda::VoxColor* devOctTreeConstColors,
                              glm::uvec3 octTreeDim)
{
    //if(xIndex == 15u && yIndex == 0u && zIndex == 0u && voxDim.x == 18u && octTreeDim.x == 8u)
    //{
    //    printf("WTF %d %d %d\n", xIndex, brickBaseX, (brickBaseX + brickDim.x - 1));
    //}
    //if xIndex is one voxel past the end of the previous brick in X dim
    if((xIndex == brickBaseX && brickX > 0))
    {
        int borderBrickX = brickX - 1;
        int borderBrickBaseX = brickBaseX - brickDim.x;

        /*if(xIndex == 127 &&
            yIndex == 30 &&
            zIndex == 209 &&
            octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
        {
            printf("In HandleXBorder sub Node[%d %d %d]==1 voxel[%d %d %d]\n",
                borderBrickX, brickY, brickZ,
                xIndex, yIndex, zIndex);
        }*/

        ComputeNodeType(xIndex, yIndex, zIndex,
                        borderBrickX, brickY, brickZ,
                        //borderBrickBaseX + brickSampleOffset.x,
                        //brickBaseY + brickSampleOffset.y, 
                        //brickBaseZ + brickSampleOffset.z,
                        devVoxels,
                        voxDim,
                        devOctTreeNodes,
                        devOctTreeConstColors,
                        octTreeDim);
        
        if((yIndex == brickBaseY && brickY > 0) || 
           (yIndex == (brickBaseY + brickDim.y - 1) && brickY < (octTreeDim.y - 1)))
        {
            HandleYBorder(xIndex, yIndex, zIndex,
                          borderBrickX, brickY, brickZ,
                          borderBrickBaseX, brickBaseY, brickBaseZ,
                          //brickSampleOffset,
                          brickDim,
                          devVoxels,
                          voxDim,
                          devOctTreeNodes,
                          devOctTreeConstColors,
                          octTreeDim);
        }

        if((zIndex == brickBaseZ && brickZ > 0) || 
           (zIndex == (brickBaseZ + brickDim.z - 1) && brickZ < (octTreeDim.z - 1)))
        {
            //if(xIndex == 12 &&
            //   yIndex == 110 &&
            //   zIndex == 100 &&
            //   octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
            //{
            //    printf("In HandleXBorder, about to HandleZBorder node=%d %d %d base=%d %d %d\n",
            //           borderBrickX, brickY, brickZ,
            //           borderBrickBaseX, brickBaseY, brickBaseZ);
            //}
            HandleZBorder(xIndex, yIndex, zIndex,
                          borderBrickX, brickY, brickZ,
                          borderBrickBaseX, brickBaseY, brickBaseZ,
                          //brickSampleOffset,
                          brickDim,
                          devVoxels,
                          voxDim,
                          devOctTreeNodes,
                          devOctTreeConstColors,
                          octTreeDim);
        }
    }
    //if xIndex is one voxel before start of next brick
    else if(xIndex == (brickBaseX + brickDim.x - 1) && brickX < (octTreeDim.x - 1))
    {
        int borderBrickX = brickX + 1;
        int borderBrickBaseX = brickBaseX + brickDim.x;
        
        /*if(xIndex == 127 &&
            yIndex == 30 &&
            zIndex == 209 &&
            octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
        {
            printf("In HandleXBorder add Node[%d %d %d]==1 voxel[%d %d %d]\n",
                borderBrickX, brickY, brickZ,
                xIndex, yIndex, zIndex);
        }*/

        ComputeNodeType(xIndex, yIndex, zIndex,
                        borderBrickX, brickY, brickZ,
                        //borderBrickBaseX + brickSampleOffset.x,
                        //brickBaseY + brickSampleOffset.y, 
                        //brickBaseZ + brickSampleOffset.z,
                        devVoxels,
                        voxDim,
                        devOctTreeNodes,
                        devOctTreeConstColors,
                        octTreeDim);
        
        if((yIndex == brickBaseY && brickY > 0) || 
           (yIndex == (brickBaseY + brickDim.y - 1) && brickY < (octTreeDim.y - 1)))
        {
            HandleYBorder(xIndex, yIndex, zIndex,
                          borderBrickX, brickY, brickZ,
                          borderBrickBaseX, brickBaseY, brickBaseZ,
                          //brickSampleOffset,
                          brickDim,
                          devVoxels,
                          voxDim,
                          devOctTreeNodes,
                          devOctTreeConstColors,
                          octTreeDim);
        }

        if((zIndex == brickBaseZ && brickZ > 0) ||
           (zIndex == (brickBaseZ + brickDim.z - 1) && brickZ < (octTreeDim.z - 1)))
        {
            HandleZBorder(xIndex, yIndex, zIndex,
                          borderBrickX, brickY, brickZ,
                          borderBrickBaseX, brickBaseY, brickBaseZ,
                          //brickSampleOffset,
                          brickDim,
                          devVoxels,
                          voxDim,
                          devOctTreeNodes,
                          devOctTreeConstColors,
                          octTreeDim);
        }
    }
}

__global__ void ComputeOctTreeNodeType(cudaPitchedPtr devVoxels, 
                                       glm::uint* devOctTreeNodes,
                                       cuda::VoxColor* devOctTreeConstColors,
                                       glm::ivec3 voxOffset,
                                       glm::ivec3 offsetToBrickBorder,
                                       //glm::ivec3 brickSampleOffset,
                                       glm::ivec3 voxDim,
                                       glm::ivec3 fullVoxDim,
                                       glm::uvec3 octTreeDim,
                                       glm::uvec3 brickDim,
                                       int xOffset)
{
    //compute x, y, z of my voxel in the chunk
    int xIndex = (blockIdx.x * blockDim.x) + threadIdx.x + xOffset;
    if(xIndex >= voxDim.x)
        return;
    
    int yIndex = (blockIdx.y * blockDim.y) + threadIdx.y;
    if(yIndex >= voxDim.y)
        return;

    int zIndex = (blockIdx.z * blockDim.z) + threadIdx.z;
    if(zIndex >= voxDim.z)
        return;

    glm::ivec3 fullVoxXYZ(xIndex + voxOffset.x - offsetToBrickBorder.x,
                          yIndex + voxOffset.y - offsetToBrickBorder.y,
                          zIndex + voxOffset.z - offsetToBrickBorder.z);

    //if(xIndex == 12 &&
    //   yIndex == 110 &&
    //   zIndex == 100 &&
    //   octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
    //{
    //    printf("vox=[%d %d %d]->[%d %d %d]\n", 
    //               xIndex, yIndex, zIndex, 
    //               fullVoxXYZ.x, fullVoxXYZ.y, fullVoxXYZ.z);
    //}
    //use that to compute node's x, y, z
    if(fullVoxXYZ.x < -1 || fullVoxXYZ.x > fullVoxDim.x ||
       fullVoxXYZ.y < -1 || fullVoxXYZ.y > fullVoxDim.y ||
       fullVoxXYZ.z < -1 || fullVoxXYZ.z > fullVoxDim.z)
    {
        //if(octTreeDim.x == 8u && octTreeDim.y == 8u && octTreeDim.z == 8u)
        //{
        //    printf("skipping vox=[%d %d %d]->[%d %d %d]\n", 
        //           xIndex, yIndex, zIndex, 
        //           fullVoxXYZ.x, fullVoxXYZ.y, fullVoxXYZ.z);
        //}
        return;//this means we are in the extra border area
    }

    int brickX = glm::min(static_cast<int>(static_cast<float>(fullVoxXYZ.x) / static_cast<float>(brickDim.x)),
                          static_cast<int>(octTreeDim.x - 1u));
    int brickY = glm::min(static_cast<int>(static_cast<float>(fullVoxXYZ.y) / static_cast<float>(brickDim.y)),
                          static_cast<int>(octTreeDim.y - 1u));;
    int brickZ = glm::min(static_cast<int>(static_cast<float>(fullVoxXYZ.z) / static_cast<float>(brickDim.z)),
                          static_cast<int>(octTreeDim.z - 1u));

    //compute x,y,z of first element of this node's brick
    int brickBaseX = (brickX * brickDim.x) - voxOffset.x + offsetToBrickBorder.x;
    int brickBaseY = (brickY * brickDim.y) - voxOffset.y + offsetToBrickBorder.y;
    int brickBaseZ = (brickZ * brickDim.z) - voxOffset.z + offsetToBrickBorder.z;

    /*if(octTreeDim.x == 32u && xIndex == 127 && yIndex == 30 && zIndex == 209)
    {
        printf("Base Node[%d %d %d]==[%d %d %d] voxel[%d %d %d]\n",
                brickX, brickY, brickZ,
                fullVoxXYZ.x, fullVoxXYZ.y, fullVoxXYZ.z,
                xIndex, yIndex, zIndex);
    }*/
    
    ComputeNodeType(xIndex, yIndex, zIndex,
                    brickX, brickY, brickZ,
                    //brickBaseX + brickSampleOffset.x,
                    //brickBaseY + brickSampleOffset.y,
                    //brickBaseZ + brickSampleOffset.z,
                    devVoxels,
                    voxDim,
                    devOctTreeNodes,
                    devOctTreeConstColors,
                    octTreeDim);
    //else if(brickX == -1 && brickY == -1 && brickZ == -1)
    //{
    //    printf("BrickXYZ=%d %d %d\n    xyzIndex=%d %d %d\n    brickBaseXYZ=%d %d %d\n    brickBaseXYZ+brickDim.x-1=%d %d %d\n", 
    //           brickX, brickY, brickZ, 
    //           xIndex, yIndex, zIndex, 
    //           brickBaseX, brickBaseY, brickBaseZ,
    //           brickBaseX + brickDim.x - 1, 
    //           brickBaseY + brickDim.y - 1, 
    //           brickBaseZ + brickDim.z - 1);
    //}

    //if(xIndex == 12 &&
    //   yIndex == 110 &&
    //   zIndex == 100 &&
    //   octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
    //{
    //    printf("minBorderX=%d maxBorderX=%d minBorderZ=%d maxBorderZ=%d\n", 
    //               brickBaseX, 
    //               (brickBaseX + brickDim.x - 1),
    //               brickBaseZ,
    //               (brickBaseZ + brickDim.z - 1));
    //}

    if((xIndex == brickBaseX && brickX > 0) || 
       (xIndex == (brickBaseX + brickDim.x - 1) && brickX < (octTreeDim.x - 1)))
    {
        //if(xIndex == 12 &&
        //   yIndex == 110 &&
        //   zIndex == 100 &&
        //   octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
        //{
        //    printf("About to HandleXBorder\n");
        //}
        HandleXBorder(xIndex, yIndex, zIndex,
                      brickX, brickY, brickZ,
                      brickBaseX, brickBaseY, brickBaseZ,
                      //brickSampleOffset,
                      brickDim,
                      devVoxels,
                      voxDim,
                      devOctTreeNodes,
                      devOctTreeConstColors,
                      octTreeDim);
    }
    
    if((yIndex == brickBaseY && brickY > 0) || 
       (yIndex == (brickBaseY + brickDim.y - 1) && brickY < (octTreeDim.y - 1)))
    {
        HandleYBorder(xIndex, yIndex, zIndex,
                      brickX, brickY, brickZ,
                      brickBaseX, brickBaseY, brickBaseZ,
                      //brickSampleOffset,
                      brickDim,
                      devVoxels,
                      voxDim,
                      devOctTreeNodes,
                      devOctTreeConstColors,
                      octTreeDim);
    }
    
    if((zIndex == brickBaseZ && brickZ > 0) ||
       (zIndex == (brickBaseZ + brickDim.z - 1) && brickZ < (octTreeDim.z - 1)))
    {
        //if(xIndex == 12 &&
        //   yIndex == 110 &&
        //   zIndex == 100 &&
        //   octTreeDim.x == 32u && octTreeDim.y == 32u && octTreeDim.z == 32u)
        //{
        //        printf("About to HandleZBorder\n");
        //}
        //printf("HandleZBorder %d %d %d\n", xIndex, yIndex, zIndex);
        HandleZBorder(xIndex, yIndex, zIndex,
                      brickX, brickY, brickZ,
                      brickBaseX, brickBaseY, brickBaseZ,
                      //brickSampleOffset,
                      brickDim,
                      devVoxels,
                      voxDim,
                      devOctTreeNodes,
                      devOctTreeConstColors,
                      octTreeDim);
    }
}                                                                              

//void cpuTest(const glm::vec3* verts, 
//             int numVerts,
//             const glm::vec3& p,
//             const glm::vec3& deltaP,
//             int *voxels,
//             const glm::uvec3& voxDim)
//{
//    std::vector< glm::vec3 > edges(numVerts);
//    //std::vector< glm::vec3 > edgeNormals(numVerts);
//    std::vector< glm::vec3 > normals( numVerts / 3);
//    
//    for(int i = 0, int j = 0; i < numVerts; i+=3, ++j)
//    {
//        edges[i] = glm::vec3(verts[i+1] - verts[i]);
//        edges[i+1] = glm::vec3(verts[i+2] - verts[i+1]);
//        edges[i+2] = glm::vec3(verts[i] - verts[i+2]);
//
//        const glm::vec3& p0p1 = edges[i];
//        glm::vec3 p0p2 = verts[i+2] - verts[i];
//        normals[j] = glm::normalize(glm::cross(p0p1, p0p2));
//
//        //edgeNormals[i] = glm::cross(normals[j], edges[i]);
//        //edgeNormals[i+1] = glm::cross(normals[j], edges[i+1]);
//        //edgeNormals[i+2] = glm::cross(normals[j], edges[i+2]);
//    };
//
//    //int index = 0;
//    //for(int tri = 0; tri < 8; ++tri)
//    for(int tri = 0; tri < 2; ++tri)
//    {
//        glm::uvec3 minVox;
//        glm::uvec3 maxVox;
//        int vindex = tri * 3;
//        ComputeTriangleVoxelBounds(&verts[vindex], p, deltaP, voxDim, minVox, maxVox);
//        glm::vec3 uvw(0.0f, 0.0f, 0.0f);
//
//        const glm::vec3& normal = normals[tri];
//        //int thinProjAxis1, thinProjAxis2, thinOtherAxis;
//        //if(thinVoxelization)
//        //{
//        //    glm::vec3 normAbs = glm::abs(normal);
//        //    if(normAbs.z > normAbs.x)
//        //    {
//        //        if(normAbs.z > normAbs.y)
//        //        {
//        //            //z is dominant axis, so test projection onto xy axis
//        //            thinProjAxis1 = X_AXIS;
//        //            thinProjAxis2 = Y_AXIS;
//        //            thinOtherAxis = Z_AXIS;
//        //        }
//        //        else
//        //        {
//        //            //y is dominant axis, so test projection onto zx axis
//        //            thinProjAxis1 = Z_AXIS;
//        //            thinProjAxis2 = X_AXIS;
//        //            thinOtherAxis = Y_AXIS;
//        //        }
//        //    }
//        //    else if(normAbs.x > normAbs.y)
//        //    {
//        //        //x is dominant axis, so test projection onto yz axis
//        //        thinProjAxis1 = Y_AXIS;
//        //        thinProjAxis2 = Z_AXIS;
//        //        thinOtherAxis = X_AXIS;
//        //    }
//        //    else
//        //    {
//        //        //y is dominant axis, so test projection onto zx axis
//        //        thinProjAxis1 = Z_AXIS;
//        //        thinProjAxis2 = X_AXIS;
//        //        thinOtherAxis = Y_AXIS;
//        //    }
//        //}
//
//        for(glm::uint u = minVox.x; u <= maxVox.x; ++u)
//        {
//            uvw.x = (float)u;
//            for(glm::uint v = minVox.y; v <= maxVox.y; ++v)
//            {
//                uvw.y = (float)v;
//                for(glm::uint w = minVox.z; w <= maxVox.z; ++w)
//                {
//                    glm::uint voxelIndex = (w * voxDim.x * voxDim.y) + (v * voxDim.x) + u;
//                    if(voxels[voxelIndex] == 1)
//                        continue;
//
//                    uvw.z = (float)w;
//                    glm::vec3 voxMin = p;// + (uvw * deltaP);
//                    if(TrianglePlaneOverlapsVoxel(verts[vindex], normals[tri], p, deltaP, uvw))
//                    {
//                        ////if(thinVoxelization)
//                        //{
//                        //    //if(!TriangleOverlapsVoxelOnAxis(&verts[vindex], 
//                        //    //                                &edges[vindex], 
//                        //    //                                normals[tri], 
//                        //    //                                p, deltaP*0.5f, uvw,
//                        //    //                                thinProjAxis1, thinProjAxis2, thinOtherAxis))
//                        //    //{
//                        //    //    continue;
//                        //    //}
//                        //    if(!TriangleOverlapsVoxel(&verts[vindex], 
//                        //                              &edges[vindex], 
//                        //                              &edgeNormals[vindex],
//                        //                              normals[tri], 
//                        //                              voxMin, deltaP * 0.5f, uvw))
//                        //    {
//                        //        continue;
//                        //    }
//                        //}
//                        //else 
//                        if(!TriangleOverlapsVoxel(&verts[vindex], 
//                                                       &edges[vindex], 
//                                                       //&edgeNormals[vindex],
//                                                       normals[tri], 
//                                                       voxMin, deltaP, uvw))
//                        {
//                            continue;
//                        }
//                    }
//                    else
//                        continue;
//
//                    voxels[voxelIndex] = 1;
//                }
//            }
//        }
//    }
//}
