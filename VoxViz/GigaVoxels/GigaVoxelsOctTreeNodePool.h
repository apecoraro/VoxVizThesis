#ifndef GV_GIGAVOXELS_OCTREE_NODE_POOL_H
#define GV_GIGAVOXELS_OCTREE_NODE_POOL_H

#include "GigaVoxels/GigaVoxelsOctTree.h"

namespace gv
{
    class OctTreeNodePool
    {
    private:
        size_t m_dimX;//x dimension of the 3d texture that holds gpu nodes
        size_t m_dimY;//y dimension of the 3d texture that holds gpu nodes
        size_t m_dimZ;//z dimension of the 3d texture that holds gpu nodes
        //size_t m_sizeOfNodePoolTexture;
        unsigned int m_pboUploadID;
        unsigned int m_textureID;
        
        typedef std::vector< vox::SmartPtr<GigaVoxelsOctTree::NodeTexturePointer> > NodeTexturePointers;

        NodeTexturePointers m_nodeTexturePointers;

        typedef std::vector< vox::SmartPtr<GigaVoxelsOctTree::Node> > NodePool;
        NodePool m_nodePool;//nodes on main system memory

        typedef std::vector< vox::SmartPtr<GigaVoxelsOctTree::Node> > NodeUpdateList;
        NodeUpdateList m_nodeUpdateList;
    public:
        OctTreeNodePool();
        ~OctTreeNodePool();

        void allocateChildNodeBlock(GigaVoxelsOctTree::Node* pParent,
                                    size_t& blockIndex);

        GigaVoxelsOctTree::Node* getChild(size_t childIndex);

        void get3DTextureDimensions(size_t& x, size_t& y, size_t& z) const;

        void create3DTexture();
        void addToUpdateList(GigaVoxelsOctTree::Node* pNode);
        void update();

        unsigned int getTextureID() const;

        size_t getNodeCount() const;
    protected:
        void update3DTexture(GigaVoxelsOctTree::Node* pNode,
                             size_t writeOffset, size_t dataSize);
    };
};

#endif
