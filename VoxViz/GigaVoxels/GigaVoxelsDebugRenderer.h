#ifndef GV_GIGAVOXELS_DEBUG_RENDERER_H
#define GV_GIGAVOXELS_DEBUG_RENDERER_H

#include "VoxVizCore/SmartPtr.h"
#include "VoxVizOpenGL/GLShaderProgramManager.h"

#include "GigaVoxels/GigaVoxelsOctTree.h"
#include "GigaVoxels/GigaVoxelsSceneGraph.h"

#include <map>

namespace gv
{
    class GigaVoxelsDebugRenderer
    {
    public:
        enum PagedNodeState
        {
            PAGED_NODE_ACTIVE=0,
            PAGED_NODE_LOADING=1,
            PAGED_NODE_INACTIVE=2,
            PAGED_NODE_NUM_STATES=3
        };
        struct PagedNodeDrawable
        {
            vox::SmartPtr<gv::PagedOctTreeNode> m_spNode;
            PagedNodeState m_state;
            PagedNodeDrawable() : m_state(PAGED_NODE_INACTIVE) {}
            PagedNodeDrawable(gv::PagedOctTreeNode& node) :
                m_spNode(&node), m_state(PAGED_NODE_INACTIVE) {}
        };
        typedef std::map<gv::PagedOctTreeNode*, PagedNodeDrawable> PagedNodes;
    private:
        bool m_initialized;
        int m_viewPortX;
        int m_viewPortY;
        int m_viewPortWidth;
        int m_viewPortHeight;
        unsigned int m_colorTextureID;//object colors
        //draw the state of the camera, the draw list,
        unsigned int m_frustumVAO;
        unsigned int m_frustumVBO;
        unsigned int m_frustumEBO;
        //and the state of the pager
        unsigned int m_pagedNodeVAO;
        unsigned int m_pagedNodeVBO;
        unsigned int m_pagedNodeEBO;
        PagedNodes m_pagedNodeDrawables;
    public:
        GigaVoxelsDebugRenderer();
        ~GigaVoxelsDebugRenderer();

        void init(vox::Camera& camera,
                  Node& sceneRoot,
                  voxOpenGL::ShaderProgram* pShader);

        void draw(vox::Camera& camera);

        void notifyPagedNodeIsActive(gv::PagedOctTreeNode& node);
        void notifyPagedNodeIsLoading(gv::PagedOctTreeNode& node);
        void notifyPagedNodeIsInactive(gv::PagedOctTreeNode& node);
    };
};
#endif
