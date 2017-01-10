#include "VoxVizOpenGL/GLUtils.h"

#include "VoxVizOpenGL/GLExtensions.h"
#include "VoxVizOpenGL/GLShaderProgramManager.h"

#include "VoxVizCore/Camera.h"

#include <iostream>
#include <cmath>
#include <time.h>
#include <stdlib.h>

using namespace voxOpenGL;

static voxOpenGL::ShaderProgram* s_pClearBuffersProg = NULL;
static voxOpenGL::ShaderProgram* s_pScreenAlignedTexQuadProg = NULL;
static GLuint s_screenAlignedQuadVAO = 0;
static GLuint s_screenAlignedQuadVBO = 0;

static GLuint s_nearPlaneQuadVAO = 0;
static GLuint s_nearPlaneQuadVBO = 0;

struct IntVtx
{
    int x;
    int y;
    int z;
    IntVtx(int i, int j, int k) : x(i), y(j), z(k) {}
};

static IntVtx s_pScreenAlignedQuadVerts[] = 
{
    IntVtx(-1, -1, -1),
    IntVtx(1, -1, -1),
    IntVtx(-1, 1, -1),
    IntVtx(1, 1, -1),
};

void GLUtils::Initialize()
{
    s_pClearBuffersProg =
        voxOpenGL::ShaderProgramManager::instance().createShaderProgram("ClearBuffers.vert",
                                                                        "ClearBuffers.frag");

    s_pScreenAlignedTexQuadProg = 
        voxOpenGL::ShaderProgramManager::instance().createShaderProgram("ScreenAlignedTexturedQuad.vert",
                                                                        "ScreenAlignedTexturedQuad.frag");
}

const std::string& GLUtils::GetNearPlaneQuadVertexShaderFile()
{
    static std::string vtxShaderFile = "NearPlaneQuad.vert";

    return vtxShaderFile;
}

void GLUtils::InitScreenAlignedQuad()
{
    if(s_screenAlignedQuadVAO != 0)
        return;

    glGenVertexArrays(1, &s_screenAlignedQuadVAO);
    
    glBindVertexArray(s_screenAlignedQuadVAO);

    glGenBuffers(1, &s_screenAlignedQuadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, s_screenAlignedQuadVBO);

    glBufferData(GL_ARRAY_BUFFER,
                 4 * sizeof(IntVtx), 
                 s_pScreenAlignedQuadVerts, 
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_INT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void GLUtils::InitNearPlaneQuad(const vox::Camera& camera)
{
    if(s_nearPlaneQuadVBO != 0)
        return;

    glGenVertexArrays(1, &s_nearPlaneQuadVAO);
    
    glBindVertexArray(s_nearPlaneQuadVAO);

    glGenBuffers(1, &s_nearPlaneQuadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, s_nearPlaneQuadVBO);

    const double DEG2RAD = 3.14159265f / 180.0f;
    float fov = camera.getFieldOfView();

    float tangent = std::tan(fov / 2.0f * DEG2RAD);
    float nearPlane = camera.getNearPlaneDist();
    float height = nearPlane * tangent;
    float width = height * camera.getAspectRatio();

    float minX = -width;
    float maxX = width;
    float minY = -height;
    float maxY = height;
    float z = -nearPlane;

    //triangle strip
    std::vector<vox::Vec3f> nearPlaneQuadVerts;
    nearPlaneQuadVerts.push_back(vox::Vec3f(minX, minY, z));
    nearPlaneQuadVerts.push_back(vox::Vec3f(maxX, minY, z));
    nearPlaneQuadVerts.push_back(vox::Vec3f(minX, maxY, z));
    nearPlaneQuadVerts.push_back(vox::Vec3f(maxX, maxY, z));

    glBufferData(GL_ARRAY_BUFFER,
                 4 * sizeof(vox::Vec3f), 
                 &nearPlaneQuadVerts.front(), 
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void GLUtils::CreateVertexBuffer(const GLUtils::VertexArray& vertexArray,
                                 GLuint& vao,
                                 GLuint& vbo)
{
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(vox::Vec3f) * vertexArray.size(),
                 vertexArray.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vox::Vec3f), 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void GLUtils::CreateColoredVertexBuffer(const GLUtils::ColoredVertexArray& coloredVertexArray,
                                        GLuint& vao,
                                        GLuint& vbo)
{
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(GLUtils::ColoredVertex) * coloredVertexArray.size(),
                 coloredVertexArray.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GLUtils::ColoredVertex), 0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GLUtils::ColoredVertex), reinterpret_cast<void*>(sizeof(vox::Vec3f)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void GLUtils::CreateElementArrayBuffer(const GLUtils::IntArray& elementArray,
                                        GLuint& ebo)
{
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 sizeof(int) * elementArray.size(),
                 elementArray.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
}

void GLUtils::BindScreenAlignedTexturedQuadShader(int textureID)
{
    s_pScreenAlignedTexQuadProg->bind();

    s_pScreenAlignedTexQuadProg->setUniformValue("ColorSampler", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);
}

void GLUtils::UnbindScreenAlignedTexturedQuadShader()
{
    glBindTexture(GL_TEXTURE_2D, 0);

    s_pScreenAlignedTexQuadProg->release();
}

void GLUtils::DrawScreenAlignedQuad()
{
    glBindVertexArray(s_screenAlignedQuadVAO);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(0);
}

void GLUtils::DrawNearPlaneQuad()
{
    glBindVertexArray(s_nearPlaneQuadVAO);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(0);
}

void GLUtils::SetClearColorAndDepth(GLfloat r,
                                    GLfloat g,
                                    GLfloat b,
                                    GLfloat a,
                                    GLfloat depth)
{
    s_pClearBuffersProg->bind();

    s_pClearBuffersProg->setUniformValue("ClearColor", r, g, b, a);
    s_pClearBuffersProg->setUniformValue("ClearDepth", depth);

    s_pClearBuffersProg->release();
}

void GLUtils::ClearBuffers(const vox::Camera& camera)
{
    /*glEnable(GL_STENCIL_TEST);

    glStencilFunc(GL_ALWAYS, 0, 1);
    glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);*/

    s_pClearBuffersProg->bind();

    s_pClearBuffersProg->setUniformValue("ProjectionMatrix",
                                         camera.getProjectionMatrix());
    //GLUtils::DrawScreenAlignedQuad();
    GLUtils::DrawNearPlaneQuad();

    s_pClearBuffersProg->release();

    //glDisable(GL_STENCIL_TEST);
}

unsigned int GLUtils::Create3DTexture(const vox::VolumeDataSet& voxels)
{
    if(!glewIsSupported("GL_VERSION_1_3"))
    {
        std::cerr << "ERROR: glTexImage3D() not supported (requires GL_VERSION_1_3)." << std::endl;
        return 0;
    }

    unsigned int textureID;
    glGenTextures(1, &textureID);

    //create 3d texture from voxel data
    glEnable(GL_TEXTURE_3D);
    glBindTexture(GL_TEXTURE_3D, textureID);

    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage3D(GL_TEXTURE_3D, 
                 0, GL_LUMINANCE, 
                 voxels.dimX(), voxels.dimY(), voxels.dimZ(), 
                 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, voxels.getData());

    //glTexParameteri(GL_TEXTURE_3D, GL_GENERATE_MIPMAP, GL_TRUE);
    glGenerateMipmap(GL_TEXTURE_3D);
    //glTexParameteri(GL_TEXTURE_3D, GL_GENERATE_MIPMAP, GL_FALSE);
    //TODO figure out if only need specify once for this texture id rather than each draw
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    //glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

unsigned int GLUtils::Create3DTexture(GLint internalFormat,
                                      GLsizei width,  
                                      GLsizei height,  
                                      GLsizei depth,
                                      GLenum format,  
                                      GLenum type,
                                      const GLvoid *pData,
                                      bool genMipMaps,
                                      bool useInterpolation,
                                      bool clampToEdge/*=true*/)
{
    if(!glewIsSupported("GL_VERSION_1_3"))
    {
        std::cerr << "ERROR: glTexImage3D() not supported (requires GL_VERSION_1_3)." << std::endl;
        return 0;
    }

    unsigned int textureID;
    glGenTextures(1, &textureID);

    //create 3d texture from voxel data
    glEnable(GL_TEXTURE_3D);
    glBindTexture(GL_TEXTURE_3D, textureID);

    //if(format == GL_RED)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //else
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    
    GLUtils::CheckOpenGLError();

    glTexImage3D(GL_TEXTURE_3D, 
                 0, internalFormat, 
                 width, height, depth, 
                 0, format, type, pData);

    if(genMipMaps)
        //glTexParameteri(GL_TEXTURE_3D, GL_GENERATE_MIPMAP, GL_TRUE);
        glGenerateMipmap(GL_TEXTURE_3D);

	GLUtils::CheckOpenGLError();

    if(useInterpolation)
    {
        if(genMipMaps)
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        else
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    if(clampToEdge)
    {
	    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
    }

    return textureID;
}


void GLUtils::Upload3DTexture(unsigned int textureID,
                              GLint mipMapLevel,
                              GLint xOffset,
                              GLint yOffset,
                              GLint zOffset,
                              GLsizei width,
                              GLsizei height,
                              GLsizei depth,
                              GLenum format,
                              GLenum type,
                              const GLvoid* pData)
{
    glBindTexture(GL_TEXTURE_3D, textureID);
 
    //if(format == GL_RED)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //else
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    GLUtils::CheckOpenGLError();

    glTexSubImage3D(GL_TEXTURE_3D,
                    mipMapLevel,
                    xOffset, yOffset, zOffset,
                    width, height, depth,
                    format,
                    type,
                    pData);

    GLUtils::CheckOpenGLError();
}

unsigned int GLUtils::Create2DTexture(GLint internalFormat,
                                      GLsizei width,  
                                      GLsizei height,
                                      GLenum format,  
                                      GLenum type,
                                      const GLvoid *pData,
                                      bool genMipMaps,
                                      bool useInterpolation)



{
    unsigned int textureID;
    glGenTextures(1, &textureID);
    
    glBindTexture(GL_TEXTURE_2D, textureID);
    //if(format == GL_RED)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //else
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glTexImage2D(GL_TEXTURE_2D, 
                 0, internalFormat, 
                 width, height, 
                 0, format, type, pData);

    if(genMipMaps)
        //glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
        glGenerateMipmap(GL_TEXTURE_2D);

    GLUtils::CheckOpenGLError();

    if(useInterpolation)
    {
        if(genMipMaps)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        else
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLUtils::CheckOpenGLError();

    return textureID;
}

unsigned int GLUtils::Create2DJitterTexture(GLsizei width,  
                                            GLsizei height)



{
    unsigned int textureID;
    glGenTextures(1, &textureID);

    glBindTexture(GL_TEXTURE_2D, textureID);
    
    //if(format == GL_RED)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //else
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    
    GLUtils::CheckOpenGLError();

    time_t t;
    srand(time(&t));
    GLsizei count = width * height;
    unsigned char* pData = new unsigned char[count];
    for(GLsizei i = 0; i < count; ++i)
    {
        int random = rand();
        float float0to1 = static_cast<float>(random) / static_cast<float>(RAND_MAX);
        unsigned char randomUChar = static_cast<unsigned char>(255.0f * float0to1);
        if(randomUChar == 0)
            randomUChar += 1;
        pData[i] = randomUChar;
    }

    glTexImage2D(GL_TEXTURE_2D, 
                 0, GL_R8, 
                 width, height, 
                 0, GL_RED, GL_UNSIGNED_BYTE, pData);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLUtils::CheckOpenGLError();

    return textureID;
}

unsigned int GLUtils::Create2DPyramidTexture(GLint internalFormat,
                                             GLsizei width,  
                                             GLsizei height,
                                             GLenum format,  
                                             GLenum type,
                                             bool genMipMaps,
                                             bool useInterpolation,
                                             int& levelCount)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);

    glBindTexture(GL_TEXTURE_2D, textureID);
    //if(format == GL_RED)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //else
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    
    GLUtils::CheckOpenGLError();

    int pyramidLevel = 0;
    while(true)
    {
        glTexImage2D(GL_TEXTURE_2D, 
                     pyramidLevel, internalFormat, 
                     width, height, 
                     0, format, type, NULL);

        GLUtils::CheckOpenGLError();

        if(width == 1 && height == 1)
            break;

        if(width != 1)
            width >>= 1;
        if(height != 1)
            height >>= 1;

        ++pyramidLevel;
    }

    levelCount = pyramidLevel + 1;

    if(genMipMaps)
        //glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
        glGenerateMipmap(GL_TEXTURE_2D);

    GLUtils::CheckOpenGLError();

    if(useInterpolation)
    {
        if(genMipMaps)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        else
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    
    return textureID;
}

unsigned int GLUtils::Create2DTextureArray(GLint internalFormat,
                                           GLsizei width,  
                                           GLsizei height,  
                                           GLsizei depth,
                                           GLenum format,  
                                           GLenum type,
                                           const GLvoid *pData,
                                           bool genMipMaps,
                                           bool useInterpolation)
{
    if(!glewIsSupported("GL_VERSION_1_3"))
    {
        std::cerr << "ERROR: glTexImage3D() not supported (requires GL_VERSION_1_3)." << std::endl;
        return 0;
    }

    unsigned int textureID;
    glGenTextures(1, &textureID);

    GLUtils::CheckOpenGLError();

    glBindTexture(GL_TEXTURE_2D_ARRAY, textureID);

    //if(format == GL_RED)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //else
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 
                 0, internalFormat, 
                 width, height, depth, 
                 0, format, type, pData);
    if(genMipMaps)
        //glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_GENERATE_MIPMAP, GL_TRUE);
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    GLUtils::CheckOpenGLError();

    if(useInterpolation)
    {
        if(genMipMaps)
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        else
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    GLUtils::CheckOpenGLError();

    return textureID;
}

unsigned int GLUtils::Create1DTexture(unsigned char* pColors, size_t numColors)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);

    //create 3d texture from voxel data
    glEnable(GL_TEXTURE_1D);
    glBindTexture(GL_TEXTURE_1D, textureID);

    glTexImage1D(GL_TEXTURE_1D,
                 0, GL_RGBA8,
                 numColors, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pColors);

    //glTexParameteri(GL_TEXTURE_1D, GL_GENERATE_MIPMAP, GL_TRUE);
    glGenerateMipmap(GL_TEXTURE_1D);
    
	GLUtils::CheckOpenGLError();

    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

    GLUtils::CheckOpenGLError();

    return textureID;
}

static unsigned int ComputeNumComponents(GLenum pixelFormat)
{
    switch(pixelFormat)
    {
    case(GL_COLOR_INDEX): 
    case(GL_STENCIL_INDEX): 
    case(GL_DEPTH_COMPONENT): 
    case(GL_RED):
    case(GL_RED_INTEGER_EXT): 
    case(GL_GREEN): 
    case(GL_GREEN_INTEGER_EXT): 
    case(GL_BLUE): 
    case(GL_BLUE_INTEGER_EXT): 
    case(GL_ALPHA): 
    case(GL_ALPHA_INTEGER_EXT): 
    case(GL_ALPHA8I_EXT): 
    case(GL_ALPHA8UI_EXT): 
    case(GL_ALPHA16I_EXT): 
    case(GL_ALPHA16UI_EXT): 
    case(GL_ALPHA32I_EXT): 
    case(GL_ALPHA32UI_EXT): 
    case(GL_ALPHA16F_ARB): 
    case(GL_ALPHA32F_ARB):
    case(GL_INTENSITY): 
    case(GL_INTENSITY4): 
    case(GL_INTENSITY8): 
    case(GL_INTENSITY12): 
    case(GL_INTENSITY16): 
    case(GL_INTENSITY8UI_EXT): 
    case(GL_INTENSITY8I_EXT): 
    case(GL_INTENSITY16I_EXT): 
    case(GL_INTENSITY16UI_EXT): 
    case(GL_INTENSITY32I_EXT): 
    case(GL_INTENSITY32UI_EXT): 
    case(GL_INTENSITY16F_ARB): 
    case(GL_INTENSITY32F_ARB): 
    case(GL_LUMINANCE): 
    case(GL_LUMINANCE_INTEGER_EXT): 
    case(GL_LUMINANCE4): 
    case(GL_LUMINANCE8): 
    case(GL_LUMINANCE12): 
    case(GL_LUMINANCE16): 
    case(GL_LUMINANCE8I_EXT): 
    case(GL_LUMINANCE8UI_EXT): 
    case(GL_LUMINANCE16I_EXT): 
    case(GL_LUMINANCE16UI_EXT): 
    case(GL_LUMINANCE32I_EXT): 
    case(GL_LUMINANCE32UI_EXT): 
    case(GL_LUMINANCE16F_ARB): 
    case(GL_LUMINANCE32F_ARB): 
        return 1;
    case(GL_RG):
    case(GL_RG_INTEGER):
    case(GL_LUMINANCE4_ALPHA4): 
    case(GL_LUMINANCE6_ALPHA2): 
    case(GL_LUMINANCE8_ALPHA8): 
    case(GL_LUMINANCE12_ALPHA4): 
    case(GL_LUMINANCE12_ALPHA12): 
    case(GL_LUMINANCE16_ALPHA16): 
    case(GL_LUMINANCE_ALPHA): 
    case(GL_LUMINANCE_ALPHA_INTEGER_EXT): 
    case(GL_LUMINANCE_ALPHA8I_EXT): 
    case(GL_LUMINANCE_ALPHA8UI_EXT): 
    case(GL_LUMINANCE_ALPHA16I_EXT): 
    case(GL_LUMINANCE_ALPHA16UI_EXT): 
    case(GL_LUMINANCE_ALPHA32I_EXT): 
    case(GL_LUMINANCE_ALPHA32UI_EXT): 
    case(GL_LUMINANCE_ALPHA16F_ARB): 
    case(GL_LUMINANCE_ALPHA32F_ARB): 
    case(GL_HILO_NV): 
    case(GL_DSDT_NV): 
        return 2; 
    case(GL_RGB): 
    case(GL_BGR): 
    case(GL_BGR_INTEGER_EXT): 
    case(GL_RGB8I_EXT): 
    case(GL_RGB8UI_EXT): 
    case(GL_RGB16I_EXT): 
    case(GL_RGB16UI_EXT): 
    case(GL_RGB32I_EXT): 
    case(GL_RGB32UI_EXT): 
    case(GL_RGB16F_ARB): 
    case(GL_RGB32F_ARB): 
    case(GL_RGB_INTEGER_EXT): 
    case(GL_DSDT_MAG_NV): 
        return 3;
    case(GL_RGBA16F_ARB): 
    case(GL_RGBA32F_ARB): 
    case(GL_RGBA): 
    case(GL_BGRA): 
    case(GL_RGBA8): 
    case(GL_RGBA_INTEGER_EXT):
    case(GL_DSDT_MAG_VIB_NV): 
    case(GL_BGRA_INTEGER_EXT): 
    default:
        return 4;
    }
}

static unsigned int ComputePixelSizeInBits(GLenum format, GLenum type)
{
    switch(type)
    {
    case(GL_BITMAP): 
        return ComputeNumComponents(format);
    case(GL_BYTE):
    case(GL_UNSIGNED_BYTE): 
        return 8 * ComputeNumComponents(format);
    case(GL_HALF_FLOAT_NV):
    case(GL_SHORT):
    case(GL_UNSIGNED_SHORT): 
        return 16 * ComputeNumComponents(format);
    case(GL_INT):
    case(GL_UNSIGNED_INT):
    case(GL_FLOAT):
        return 32 * ComputeNumComponents(format);
    default:
        return 8 * 4;//return default 8bit by RGBA
    }    

}

static unsigned int ComputeRowWidthInBytes(int width, GLenum pixelFormat, GLenum type, int packing)
{
    unsigned int pixelSize = ComputePixelSizeInBits(pixelFormat, type);
    int widthInBits = width*pixelSize;
    int packingInBits = packing*8;
    
    return (widthInBits/packingInBits + ((widthInBits%packingInBits)?1:0))*packing;
}

unsigned char* GLUtils::LoadImageFromCurrentTexture(GLenum textureMode,
                                                    GLint& width,
                                                    GLint& height,
                                                    GLint& depth,
                                                    GLenum pixelFormat/* = GL_RGBA*/,
                                                    GLenum type/* = GL_UNSIGNED_BYTE*/,
                                                    GLint level/*=0*/,
                                                    unsigned char* pBuffer/*=NULL*/)
{
    if(textureMode==0) 
        return NULL;
    
    //sanity check
    //GLint internalFormat;
    //glGetTexLevelParameteriv(textureMode, level, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    //GLint internalDataType;
    //glGetTexLevelParameteriv(textureMode, level, GL_TEXTURE_RED_TYPE, &internalDataType);
    //GLint internalResolution;
    //glGetTexLevelParameteriv(textureMode, level, GL_TEXTURE_RED_SIZE, &internalResolution);

    if(pBuffer == NULL)
    {
        GLint packing;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &packing);
        glPixelStorei(GL_PACK_ALIGNMENT, packing);
        
        glGetTexLevelParameteriv(textureMode, level, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(textureMode, level, GL_TEXTURE_HEIGHT, &height);
        glGetTexLevelParameteriv(textureMode, level, GL_TEXTURE_DEPTH, &depth);
    
        unsigned int dataSize = ComputeRowWidthInBytes(width, pixelFormat, type, packing)*height*depth;
                
        pBuffer = new unsigned char[dataSize];
    }

    GLUtils::CheckOpenGLError();

    glGetTexImage(textureMode, level, pixelFormat, type, pBuffer);

    GLUtils::CheckOpenGLError();

    return pBuffer;
}

unsigned int GLUtils::CreatePixelBufferObject(GLenum pboType,
                                              GLsizeiptr size,
                                              const GLvoid* pData,
                                              GLenum usage)
{
    if(!glewIsSupported("GL_ARB_pixel_buffer_object"))
    {
        std::cerr << "ERROR: Pixel buffer objects not supported (requires GL_ARB_pixel_buffer_object)." << std::endl;
        return 0;
    }

    GLUtils::CheckOpenGLError();

    GLuint pboID;
    glGenBuffers(1, &pboID);

    GLUtils::CheckOpenGLError();

    if(size > 0)
    {
        glBindBuffer(pboType, pboID);

        glBufferData(pboType, size, pData, usage);

        glBindBuffer(pboType, 0);
    }

    GLUtils::CheckOpenGLError();

    return pboID;
}

void GLUtils::ComputeWorldXYZ(GLdouble winX,
                              GLdouble winY,
                              GLdouble winZ,
                              const GLdouble* modelViewMat,
                              const GLdouble* projMat,
                              const GLint* viewport,
                              GLdouble& worldX,
                              GLdouble& worldY,
                              GLdouble& worldZ)
{
    //GLdouble modelViewMat[16];
    //glGetDoublev(GL_MODELVIEW_MATRIX,
    //             modelViewMat);
    //GLdouble projMat[16];
    //glGetDoublev(GL_PROJECTION_MATRIX,
    //             projMat);
    //GLint viewport[4];
    //glGetIntegerv(GL_VIEWPORT, viewport);

    gluUnProject(winX, winY, winZ, 
                 modelViewMat, projMat, 
                 viewport, 
                 &worldX, &worldY, &worldZ);
}

void GLUtils::CheckOpenGLError()
{
	GLenum errCode = glGetError();
    //assert(errCode1 == 0);
	if(errCode != 0)
	{
		const GLubyte* error = glewGetErrorString(errCode);
		std::cerr << "*************" << std::endl;
		std::cerr << "OpenGL Error: "
				  << error 
				  << " code="
				  << std::hex << errCode
				  << std::endl;
		std::cerr << "*************" << std::endl;
	}
}