#ifndef VOXVIZ_OPENGL_FRAMEBUFFEROBJECT_H
#define VOXVIZ_OPENGL_FRAMEBUFFEROBJECT_H

#include "VoxVizCore/Referenced.h"

#include "VoxVizOpenGL/GLExtensions.h"

#include <vector>

namespace voxOpenGL
{
    typedef std::vector<unsigned int> TextureIDs;

    class GLFrameBufferObject : public vox::Referenced
    {
    private:
        GLsizei m_width;
        GLsizei m_height;
        unsigned int m_fboID;
        unsigned int m_depthBufID;
        unsigned int m_depthStencilBufID;
        //GLsizei m_numRenderTargets;
        GLint m_maxRenderTargets;
    public:
        GLFrameBufferObject(GLsizei width, GLsizei height);

        void attachDepthBuffer();
        void attachDepthStencilBuffer();
        void attachColorBuffers(const TextureIDs& colorBufs,
                                int attachIndex);
        void attachColorBuffer(unsigned int colorBuf,
                               int attachIndex,
                               int level=0);

        void attach2DArrayLayerColorBuffer(unsigned int textureID,
                                           int attachIndex,
                                           GLint layerStart,
                                           int layerCount);

        void detachColorBuffer(int attachIndex=0);

        void detach2DArrayLayerColorBuffer(int attachIndex,
                                           int layerCount);

        void detachDepthBuffer();

        void detachDepthStencilBuffer();

        void setViewportWidthHeight(GLsizei width, GLsizei height);

        void getViewportWidthHeight(GLsizei& width, GLsizei& height)
        {
            width = m_width;
            height = m_height;
        }

        void bind();
        void release();
        void mapDrawBuffers(int numDrawBufs);

        void blitToBackBuffer();
    protected:
        ~GLFrameBufferObject();

    };
}

#endif
