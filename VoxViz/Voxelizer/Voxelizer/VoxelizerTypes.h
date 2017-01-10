#ifndef VOXELIZER_TYPES_H
#define VOXELIZER_TYPES_H

#include <vector_types.h>

#include <glm/glm.hpp>
#include <vector>

namespace cuda
{
    //typedef glm::vec4 VoxColor;
    typedef uchar4 VoxColor;
    typedef glm::vec3 VoxNorm;

    typedef std::vector<VoxColor> VoxelColors;
    struct VoxelColorMipMap
    {
        glm::uvec3 dim;
        VoxelColors colors;
    };
    typedef std::vector<VoxelColorMipMap> VoxelColorMipMaps;
    typedef std::vector<VoxNorm> VoxelNormals;
    struct VoxelNormalMipMap
    {
        glm::uvec3 dim;
        VoxelNormals normals;
    };
    typedef std::vector<VoxelNormalMipMap> VoxelNormalMipMaps;
};

#endif
