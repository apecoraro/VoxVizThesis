#ifndef RAY_CASTER_SHADER_CODE_TESTER_H
#define RAY_CASTER_SHADER_CODE_TESTER_H

#include "VoxVizOpenGL/GLShaderProgramManager.h"

namespace rc
{
    class RayCasterShaderCodeTester
    {
    public:
        static void drawNearPlane(voxOpenGL::ShaderProgram* pNearPlaneProg);
    };
}

#endif
