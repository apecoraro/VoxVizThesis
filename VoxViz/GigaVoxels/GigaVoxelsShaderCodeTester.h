#ifndef GIGAVOXELS_SHADER_CODE_TESTER_H
#define GIGAVOXELS_SHADER_CODE_TESTER_H

#include "VoxVizOpenGL/GLShaderProgramManager.h"
#include "GigaVoxels/GigaVoxelsOctTree.h"
#include "VoxVizCore/Array.h"

namespace gv
{
    class GigaVoxelsShaderCodeTester
    {
    public:
        static void drawVolume(voxOpenGL::ShaderProgram* pShaderProgram,
                               GigaVoxelsOctTree* pOctTree,
                               const vox::FloatArray& vertexArray,
                               float lookX,
                               float lookY,
                               float lookZ);

        static void generateNodeUsageSelectionMask(voxOpenGL::ShaderProgram* pShaderProgram,
                                                   unsigned int nodeUsageTexture);

        static void computeActiveTexels(unsigned int selectionMaskTextureID);

        static void generateSelectionMaskHistoPyramid(int renderTargetWidth,
                                                      int renderTargetHeight,
                                                      unsigned int histoPyramidTextureID,
                                                      int histoPyramidTextureLevel);

        static void compressNodeUsageList(voxOpenGL::ShaderProgram* pShaderProgram,
                                          int renderTargetWidth,
                                          int renderTargetHeight,
                                          unsigned int histoPyramidTextureID,
                                          unsigned int nodeUsageTexture,
                                          unsigned int selectionMaskTexture);
    };
};

#endif
