#include "RayCaster/RayCasterShaderCodeTester.h"

#include "VoxVizOpenGL/GLExtensions.h"
#include "VoxVizOpenGL/GLUtils.h"

//#include <glm/core/setup.hpp>
#define GLM_SWIZZLE GLM_SWIZZLE_FULL
#include <glm/glm.hpp>

#include <iostream>

using namespace glm;

#define uniform static
#define in
#define out
#define inout
#define discard return

static glm::vec4 gl_Position;

//vertex shader uniforms
uniform mat4 ProjectionMatrix;
uniform mat4 InverseModelViewMatrix;
uniform vec3 VolTranslation;
uniform vec3 VolScale;

void NearPlaneVertexShader(in vec4 glVertex, out vec4& RayPosition)
{
    //the near plane quad is already in view space
    gl_Position = ProjectionMatrix * glVertex;
    
    vec3 rayPosition = (InverseModelViewMatrix * glVertex).xyz;
    rayPosition -= VolTranslation;
    rayPosition /= VolScale;
    rayPosition += 0.5f;

    RayPosition = vec4(rayPosition, 1.0f);
}

void rc::RayCasterShaderCodeTester::drawNearPlane(voxOpenGL::ShaderProgram* pShaderProgram)
{
    pShaderProgram->getUniformValue("ProjectionMatrix", &ProjectionMatrix[0][0]);
    pShaderProgram->getUniformValue("InverseModelViewMatrix", &InverseModelViewMatrix[0][0]);
    pShaderProgram->getUniformValue("VolTranslation", &VolTranslation[0]);
    pShaderProgram->getUniformValue("VolScale", &VolScale[0]);

    vec4 rayPosition;
    NearPlaneVertexShader(vec4(0.0f, 0.0f, -1.0f, 1.0f), rayPosition);

    NearPlaneVertexShader(vec4(-0.5f, -0.4f, -1.0f, 1.0f), rayPosition);

    NearPlaneVertexShader(vec4(0.5f, 0.4f, -1.0f, 1.0f), rayPosition);
}
