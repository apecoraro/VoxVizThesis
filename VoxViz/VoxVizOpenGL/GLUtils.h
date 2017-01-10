#ifndef VOXVIZ_OPENGL_UTILS_H
#define VOXVIZ_OPENGL_UTILS_H

#include "VoxVizCore/VolumeDataSet.h"

#include "VoxVizOpenGL/GLExtensions.h"

namespace voxOpenGL
{
    class GLUtils
    {
    public:
        static void Initialize();

        static unsigned int Create3DTexture(const vox::VolumeDataSet& volumeDataSet);

        static unsigned int Create3DTexture(GLint internalFormat,
                                            GLsizei width,  
                                            GLsizei height,  
                                            GLsizei depth,
                                            GLenum format,  
                                            GLenum type,
                                            const GLvoid *pData,
                                            bool genMipMaps,
                                            bool useInterpolation,
                                            bool clampToEdge=true);

        static void Upload3DTexture(unsigned int textureID,
                                    GLint mipMapLevel,
                                    GLint xOffset,
                                    GLint yOffset,
                                    GLint zOffset,
                                    GLsizei width,
                                    GLsizei height,
                                    GLsizei depth,
                                    GLenum format,
                                    GLenum type,
                                    const GLvoid* pData);

        static unsigned int Create2DTexture(GLint internalFormat,
                                            GLsizei width,  
                                            GLsizei height, 
                                            GLenum format,  
                                            GLenum type,
                                            const GLvoid *pData,
                                            bool genMipMaps,
                                            bool useInterpolation);

        static unsigned int Create2DJitterTexture(GLsizei width,  
                                                 GLsizei height);

        static unsigned int Create2DPyramidTexture(GLint internalFormat,
                                                   GLsizei width,  
                                                   GLsizei height, 
                                                   GLenum format,  
                                                   GLenum type,
                                                   bool genMipMaps,
                                                   bool useInterpolation,
                                                   int& levelCount);

        static unsigned int Create2DTextureArray(GLint internalFormat,
                                                 GLsizei width,  
                                                 GLsizei height,  
                                                 GLsizei depth,
                                                 GLenum format,  
                                                 GLenum type,
                                                 const GLvoid *pData,
                                                 bool genMipMaps,
                                                 bool useInterpolation);

        static unsigned int Create1DTexture(unsigned char* pColorLUT, size_t numColors);

        static unsigned char* LoadImageFromCurrentTexture(GLenum textureMode,
                                                          GLint& width,
                                                          GLint& height,
                                                          GLint& depth,
                                                          GLenum pixelFormat = GL_RGBA,
                                                          GLenum type = GL_UNSIGNED_BYTE,
                                                          GLint level = 0,
                                                          unsigned char* pBuffer=NULL);

        static unsigned int CreatePixelBufferObject(GLenum pboType,
                                                    GLsizeiptr size,
                                                    const GLvoid* pData,
                                                    GLenum usage);

        static void InitScreenAlignedQuad();
        static void BindScreenAlignedTexturedQuadShader(int textureID);
        static void UnbindScreenAlignedTexturedQuadShader();
        static void DrawScreenAlignedQuad();

        static void SetClearColorAndDepth(GLfloat r,
                                          GLfloat g,
                                          GLfloat b,
                                          GLfloat a,
                                          float depth);
        static void ClearBuffers(const vox::Camera& camera);

        static const std::string& GetNearPlaneQuadVertexShaderFile();
        static void InitNearPlaneQuad(const vox::Camera& camera);
        static void DrawNearPlaneQuad();

        typedef std::vector<vox::Vec3f> VertexArray;
        static void CreateVertexBuffer(const VertexArray& vertexArray,
                                       GLuint& vao,
                                       GLuint& vbo);

        struct ColoredVertex
        {
            vox::Vec3f position;
            vox::Vec4ub color;
        };
        typedef std::vector<ColoredVertex> ColoredVertexArray;
        static void CreateColoredVertexBuffer(const ColoredVertexArray& coloredVertexArray,
                                              GLuint& vao,
                                              GLuint& vbo);

        typedef std::vector<int> IntArray;
        static void CreateElementArrayBuffer(const GLUtils::IntArray& elementArray,
                                             GLuint& ebo);

        static void ComputeWorldXYZ(GLdouble winX,
                              GLdouble winY,
                              GLdouble winZ,
                              const GLdouble* modelViewMat,
                              const GLdouble* projMat,
                              const GLint* viewport,
                              GLdouble& worldX,
                              GLdouble& worldY,
                              GLdouble& worldZ);

		static void CheckOpenGLError();
    };
};
#endif
