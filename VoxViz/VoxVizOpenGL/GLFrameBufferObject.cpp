#include "VoxVizOpenGL/GLFrameBufferObject.h"
#include "VoxVizOpenGL/GLUtils.h"

#include <iostream>

using namespace voxOpenGL;

static GLenum s_colorAttachPts[] = 
{
    GL_COLOR_ATTACHMENT0,
    GL_COLOR_ATTACHMENT1,
    GL_COLOR_ATTACHMENT2,
    GL_COLOR_ATTACHMENT3,
    GL_COLOR_ATTACHMENT4,
    GL_COLOR_ATTACHMENT5,
    GL_COLOR_ATTACHMENT6,
    GL_COLOR_ATTACHMENT7,
    GL_COLOR_ATTACHMENT8,
    GL_COLOR_ATTACHMENT9,
    GL_COLOR_ATTACHMENT10,
    GL_COLOR_ATTACHMENT11,
    GL_COLOR_ATTACHMENT12,
    GL_COLOR_ATTACHMENT13,
    GL_COLOR_ATTACHMENT14,
    GL_COLOR_ATTACHMENT15,
};

GLFrameBufferObject::GLFrameBufferObject(GLsizei width, 
                                         GLsizei height) :
    m_width(width), 
    m_height(height), 
    m_fboID(0), 
    m_depthBufID(0),
    m_depthStencilBufID(0)
{
    if(!glewIsExtensionSupported("GL_ARB_framebuffer_object"))
    {
        std::cerr << "ERROR: OpenGL frame buffer object not supported (requires GL_ARB_framebuffer_object)." << std::endl;
        return;
    }
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &m_maxRenderTargets);

    glGenFramebuffers(1, &m_fboID);
}

void GLFrameBufferObject::attachColorBuffers(const TextureIDs& textureIDs,
                                             int attachIndex)
{
    if((int)textureIDs.size() > m_maxRenderTargets)
    {
        std::cerr << "ERROR: max color attachments is less than provided number of attachments." << std::endl;
        return;
    }

    //m_numRenderTargets = attachIndex + (int)textureIDs.size();
    
    GLUtils::CheckOpenGLError();

    int colorAttachPt = GL_COLOR_ATTACHMENT0 + attachIndex; 
    for(size_t index = 0; 
        index < textureIDs.size(); 
        ++colorAttachPt,
        ++index)
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, colorAttachPt,
                               GL_TEXTURE_2D, textureIDs.at(index), 0);
    }

    GLUtils::CheckOpenGLError();
}

void GLFrameBufferObject::attach2DArrayLayerColorBuffer(unsigned int textureID,
                                                        int attachIndex,
                                                        GLint layerStart,
                                                        int layerCount)
{
    //if(attachIndex + layerCount > m_numRenderTargets)
    //    m_numRenderTargets = attachIndex + layerCount;

    int colorAttachPt = GL_COLOR_ATTACHMENT0 + attachIndex;

    GLUtils::CheckOpenGLError();

    for(GLint layer = layerStart; 
        layer < layerStart + layerCount; 
        ++layer,
        ++colorAttachPt)
    {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, colorAttachPt,
                                  textureID, 0, layer);
    }

    GLUtils::CheckOpenGLError();
}

void GLFrameBufferObject::detach2DArrayLayerColorBuffer(int attachIndex,
                                                        int layerCount)
{
    int colorAttachPt = GL_COLOR_ATTACHMENT0 + attachIndex;
    for(int layer = 0; 
        layer < layerCount; 
        ++layer,
        ++colorAttachPt)
    {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, colorAttachPt,
                                  0, 0, 0);
    }
}

void GLFrameBufferObject::attachColorBuffer(unsigned int textureID,
                                            int attachIndex,
                                            int level/*=0*/)
{
    //if(attachIndex + 1 > m_numRenderTargets)
        //m_numRenderTargets = attachIndex + 1;

    GLUtils::CheckOpenGLError();

    int colorAttachPt = GL_COLOR_ATTACHMENT0 + attachIndex;
    glFramebufferTexture2D(GL_FRAMEBUFFER, colorAttachPt,
                           GL_TEXTURE_2D, textureID, level);

    GLUtils::CheckOpenGLError();
}

void GLFrameBufferObject::detachColorBuffer(int attachIndex/*=0*/)
{
    int colorAttachPt = GL_COLOR_ATTACHMENT0 + attachIndex;
    glFramebufferTexture2D(GL_FRAMEBUFFER, colorAttachPt,
                           GL_TEXTURE_2D, 0, 0);
}

void GLFrameBufferObject::attachDepthBuffer()
{
    GLUtils::CheckOpenGLError();

    if(m_depthBufID == 0)
    {
        glGenRenderbuffers(1, &m_depthBufID);
    
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthBufID);
    
        glRenderbufferStorage(GL_RENDERBUFFER, 
                              GL_DEPTH_COMPONENT,
                              m_width, m_height);

        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, m_depthBufID);

    GLUtils::CheckOpenGLError();
}

void GLFrameBufferObject::attachDepthStencilBuffer()
{
    GLUtils::CheckOpenGLError();

    if(m_depthStencilBufID == 0)
    {
        glGenRenderbuffers(1, &m_depthStencilBufID);
    
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthStencilBufID);
    
        glRenderbufferStorage(GL_RENDERBUFFER, 
                              GL_DEPTH24_STENCIL8,
                              m_width, m_height);

        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, m_depthStencilBufID);

    GLUtils::CheckOpenGLError();
}

void GLFrameBufferObject::detachDepthBuffer()
{
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, 0);
}

void GLFrameBufferObject::detachDepthStencilBuffer()
{
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, 0);
}

GLFrameBufferObject::~GLFrameBufferObject()
{
    if(m_fboID > 0)
        glDeleteFramebuffers(1, &m_fboID);

    if(m_depthBufID > 0)
        glDeleteRenderbuffers(1, &m_depthBufID);

    if(m_depthStencilBufID > 0)
        glDeleteRenderbuffers(1, &m_depthStencilBufID);
}

void GLFrameBufferObject::bind()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_fboID);
}

void GLFrameBufferObject::release()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLFrameBufferObject::setViewportWidthHeight(GLsizei width, GLsizei height)
{
    GLUtils::CheckOpenGLError();

    m_width = width;
    m_height = height; 

    glViewport(0, 0, m_width, m_height); 

    GLUtils::CheckOpenGLError();
}

void GLFrameBufferObject::mapDrawBuffers(int numDrawBufs)
{
    GLUtils::CheckOpenGLError();

    //glPushAttrib(GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT);
 
    glDrawBuffers(numDrawBufs, s_colorAttachPts); 

    GLUtils::CheckOpenGLError();
}

void GLFrameBufferObject::blitToBackBuffer()
{
    glDrawBuffer(GL_BACK);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fboID);

    GLUtils::CheckOpenGLError();

    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                      GL_LINEAR);

    GLUtils::CheckOpenGLError();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}
