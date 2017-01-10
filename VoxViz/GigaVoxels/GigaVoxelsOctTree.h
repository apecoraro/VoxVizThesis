#ifndef GV_GIGAVOXELS_OCTREE_H
#define GV_GIGAVOXELS_OCTREE_H

#include "VoxVizCore/Referenced.h"
#include "VoxVizCore/VolumeDataSet.h"

#include "VoxVizOpenGL/GLExtensions.h"

#include <QtCore/qthread.h>
#include <QtCore/qmutex.h>
#include <QtCore/qwaitcondition.h>
#include <QtGui/qvector4d.h>

#include <vector>

namespace gv
{
    class OctTreeNodePool;
    class BrickPool;
    class NodeTree;

    struct MipMap
    {
        size_t dimX;
        size_t dimY;
        size_t dimZ;
        
        vox::Vec4ub* pData;
        vox::Vec3f* pGradientData;

        MipMap() : dimX(0), dimY(0), dimZ(0), pData(NULL) {}
    };

    class GigaVoxelsOctTree : public vox::Referenced
    {
    public:
        
        typedef std::vector<MipMap> MipMaps;

        class NodeTexturePointer : public vox::Referenced
        {
        private:
            //static const size_t s_sizeOfChildNodeBlock = 8 * 8;//8 bytes (64 bits) per node * 8 children
            unsigned char m_childNodeBlock[64];
        public:
            NodeTexturePointer()
            {
                memset(m_childNodeBlock, 0, sizeof(m_childNodeBlock));
            }
            
            NodeTexturePointer(const NodeTexturePointer& copy)
            {
                memcpy(m_childNodeBlock, copy.m_childNodeBlock, sizeof(m_childNodeBlock));
            }

            virtual ~NodeTexturePointer(){}

            const NodeTexturePointer& operator=(const NodeTexturePointer& lhs)
            {
                if(this == &lhs)
                    return *this;

                memcpy(m_childNodeBlock, lhs.m_childNodeBlock, sizeof(m_childNodeBlock));
            }

            unsigned char* data() { return m_childNodeBlock; }
        };

        class Node : public vox::Referenced
        {
        private:
            struct GpuDataStruct
            {
                unsigned int red;
                unsigned int green;
                GpuDataStruct() : red(0), green(0) {}
            };
            //pointer to the data for this Node that is
            //stored in the 3D texture both on the GPU and CPU
            GpuDataStruct* m_pGpuDataStruct;
            unsigned char* m_pData;
            vox::SmartPtr<NodeTexturePointer> m_spTexturePtr;

            //specifies the index into the node pool for this Node's children
            //children are stored as 2x2x2 blocks in a 3d texture
            size_t m_childNodeBlockIndex;
            //brick texture
            const MipMap* m_pMipMap;//mip map that contains brick data
            size_t m_mipMapStartX;
            size_t m_mipMapEndX;
            size_t m_mipMapStartY;
            size_t m_mipMapEndY;
            size_t m_mipMapStartZ;
            size_t m_mipMapEndZ;
            size_t m_mipMapDepth;
            size_t m_brickDimX;
            size_t m_brickDimY;
            size_t m_brickDimZ;
            size_t m_brickBorderX;
            size_t m_brickBorderY;
            size_t m_brickBorderZ;
            size_t m_borderVoxels;

            char* m_pBrick;
            char* m_pBrickGradients;

            size_t m_3dTextureX;
            size_t m_3dTextureY;
            size_t m_3dTextureZ;
            volatile bool m_brickIsPendingUpload;

            Node* m_pParent;
            OctTreeNodePool* m_pOctTreeNodePool;

            size_t m_colorsDataSize;
            bool m_colorsCompressed;

            size_t m_gradientsDataSize;
            bool m_gradientsCompressed;

            void* m_pUserData;
        public:
            Node() : 
              m_pGpuDataStruct(NULL),
              m_pData(NULL),
              m_childNodeBlockIndex(0),//zero indicates leaf node
              m_pMipMap(NULL),
              m_mipMapStartX(0),
              m_mipMapEndX(0),
              m_mipMapStartY(0),
              m_mipMapEndY(0),
              m_mipMapStartZ(0),
              m_mipMapEndZ(0),
              m_mipMapDepth(0),
              m_brickDimX(0),
              m_brickDimY(0),
              m_brickDimZ(0),
              m_brickBorderX(0),
              m_brickBorderY(0),
              m_brickBorderZ(0),
              m_borderVoxels(2),
              m_pBrick(NULL),
              m_pBrickGradients(NULL),
              m_3dTextureX(0),
              m_3dTextureY(0),
              m_3dTextureZ(0),
              m_brickIsPendingUpload(false),
              m_pParent(NULL),
              m_pOctTreeNodePool(NULL),
              m_colorsDataSize(0),
              m_colorsCompressed(false),
              m_gradientsDataSize(0),
              m_gradientsCompressed(false),
              m_pUserData(NULL)
            {
            }

        protected:
            ~Node()
            {
                deleteBrickData();
            }

            void deleteBrickData()
            {
                if(m_pBrick != NULL)
                    delete [] m_pBrick;
                if(m_pBrickGradients != NULL)
                    delete [] m_pBrickGradients;
            }

            void setDataPtr(unsigned char* pData) 
            { 
                m_pData = pData;
                m_pGpuDataStruct = (GpuDataStruct*)pData; 
            }

            void setTexturePtr(NodeTexturePointer* pTexPtr)
            {
                m_spTexturePtr = pTexPtr;
            }

            GpuDataStruct* getDataPtr() { return m_pGpuDataStruct; }

            friend class OctTreeNodePool;
            friend class BrickPool;
        public:
            //bool getMaxSubDivisionFlag();
            //void setMaxSubDivisionFlag(bool flag);

            bool getBrickIsOnGpuFlag();
            void setBrickIsOnGpuFlag(bool flag);

            enum NodeType
            {
                CONSTANT_NODE=0,
                NON_CONSTANT_NODE
            };
            NodeType getNodeTypeFlag();
            void setNodeTypeFlag(NodeType nodeType);
        public:

            void getChildNodesPtr(unsigned int& x, unsigned int& y, unsigned int& z) const;
            void setChildNodesPtr(unsigned int x, unsigned int y, unsigned int z);

            void getBrickPtr(unsigned int& x, unsigned int& y, unsigned int& z) const;
            void setBrickPtr(unsigned int x, unsigned int y, unsigned int z);

            const vox::Vec4ub& getConstantValue() const;
            NodeType computeNodeType(const MipMap& mipMap,
                                 size_t x, size_t y, size_t z,
                                 size_t brickDimX, size_t brickDimY, size_t brickDimZ);
            void setConstantValue(float red, float green, float blue, float alpha);

            void setBrickColorsPtr(bool isCompressed, size_t dataSize, char* pVoxelColors);
            void setBrickGradientsPtr(bool isCompressed, size_t dataSize, char* pVoxelGradients);

            size_t getBrickColorsSize() const;
            size_t getBrickGradientsSize() const;

            void setChildNodeBlockIndex(size_t childIndex)
            {
                m_childNodeBlockIndex = childIndex;
            }

            size_t getChildNodeBlockIndex() const { return m_childNodeBlockIndex; }

            void copyMipMapToBrick(vox::Vec4ub* pBrick,
                                   vox::Vec3f* pBrickGradients);

            const char* getBrick() const { return m_pBrick; }
            const char* getBrickGradients() const { return m_pBrickGradients; }

            void allocateBrick(size_t brickSize);
            void allocateBrickGradients(size_t gradSize);

            char* takeBrick()
            { 
                //release brick memory for use by other node
                char* pBrick = m_pBrick;
                m_pBrick = NULL;
                return pBrick; 
            }

            char* takeBrickGradients() 
            { 
                //release brick grads memory for use by other node
                char* pBrickGradients = m_pBrickGradients;
                m_pBrickGradients = NULL;
                return pBrickGradients; 
            }

            void getBrickData(size_t& brickDimX,
                              size_t& brickDimY,
                              size_t& brickDimZ,
                              size_t& brickBorderX,
                              size_t& brickBorderY,
                              size_t& brickBorderZ) const
            {
                brickDimX = m_brickDimX;
                brickDimY = m_brickDimY;
                brickDimZ = m_brickDimZ;

                brickBorderX = m_brickBorderX;
                brickBorderY = m_brickBorderY;
                brickBorderZ = m_brickBorderZ;
            }

            void setBrickData(size_t brickDimX,
                              size_t brickDimY,
                              size_t brickDimZ,
                              size_t brickBorderX,
                              size_t brickBorderY,
                              size_t brickBorderZ)
            {
                m_brickDimX = brickDimX;
                m_brickDimY = brickDimY;
                m_brickDimZ = brickDimZ;

                m_brickBorderX = brickBorderX;
                m_brickBorderY = brickBorderY;
                m_brickBorderZ = brickBorderZ;
            }

            void get3DTexturePtr(size_t& x, size_t& y, size_t& z) const
            {
                x = m_3dTextureX;
                y = m_3dTextureY;
                z = m_3dTextureZ;
            }

            void set3DTexturePtr(size_t& x, size_t& y, size_t& z)
            {
                m_3dTextureX = x;
                m_3dTextureY = y;
                m_3dTextureZ = z;
            }

            void setMipMapXYZDepth(size_t mipMapX,
                                   size_t mipMapY,
                                   size_t mipMapZ,
                                   size_t depth)
            {
                m_mipMapStartX = mipMapX;
                m_mipMapStartY = mipMapY;
                m_mipMapStartZ = mipMapZ;
                m_mipMapDepth = depth;
            }

            bool getBrickIsPendingUpload() const { return m_brickIsPendingUpload; }
            void setBrickIsPendingUpload(bool flag) { m_brickIsPendingUpload = flag; }

            const Node* getParent() const { return m_pParent; }
            Node* getParent() { return m_pParent; }
            void setParent(Node* pParent) { m_pParent = pParent; }

            const OctTreeNodePool* getNodePool() const { return m_pOctTreeNodePool; }
            OctTreeNodePool* getNodePool() { return m_pOctTreeNodePool; }
            void setNodePool(OctTreeNodePool* pPool) { m_pOctTreeNodePool = pPool; }

            void setUserData(void* pUserData)
            {
                m_pUserData = pUserData;
            }

            void* getUserData()
            {
                return m_pUserData;
            }
        };

        typedef std::list< vox::SmartPtr<GigaVoxelsOctTree::Node> > UploadRequestList;

    private:
        vox::SmartPtr<NodeTree> m_spNodeTree;
        OctTreeNodePool* m_pOctTreeNodePool;
        size_t m_depth;

        unsigned int m_numPBOs;
        unsigned int m_pboIDs[3];
        GLsizeiptr m_pboBufferSizes[3];
        unsigned int m_nodeUsageListLengths[3];
        unsigned int m_pboNUDownloadIndex;
        unsigned int m_pboNUReadIndex;

        size_t m_updateCount;

        unsigned int m_nodeUsageTextureID;

    public:
        struct NodeUsageListParams
        {
            unsigned int textureID;
            unsigned int listLength;
            int width;
            int height;
            NodeUsageListParams() :
                textureID(0), listLength(0), width(0), height(0) {}
        };

    private:
        NodeUsageListParams m_compressedNodeUsageList[2];
        unsigned int m_compressedNodeUsageListWrite;
        unsigned int m_compressedNodeUsageListRead;
        
        unsigned int m_selectionMaskTextureID;

        struct HistoPyramidTexture
        {
            unsigned int textureID;
            int levelCount;
            unsigned int pboID;
            mutable bool getTexImageError;
        };

        HistoPyramidTexture m_histoPyramidTextures[2];
        unsigned int m_pboHPDownloadIndex;
        unsigned int m_pboHPReadIndex;

        MipMaps m_mipMaps;
        vox::SmartPtr<vox::SceneObject> m_spSceneObject;
        
        size_t m_brickDimX;
        size_t m_brickDimY;
        size_t m_brickDimZ;
        bool m_brickDataIsCompressed;
        bool m_brickGradientsAreUnsigned;
        float m_rayStepSize;//ray step size in voxel texture space (0 - 1)
        std::string m_filename;
    public:
        static void KillNodeUsageListProcessors();
        static size_t GetMaxNumNodeUsageListProcessors();
        static void GetNodeUsageListProcessorsStatus(std::vector<bool>& workerStates,
                                                     std::vector<size_t>& workerProcessListSizes);
        static void UpdateBrickPool();

        GigaVoxelsOctTree();

        void setFilename(const std::string& filename) { m_filename = filename; }
        const std::string& getFilename() const { return m_filename; }

        void build(const vox::VolumeDataSet* pVoxels,
                   const vox::VolumeDataSet::ColorLUT& colorLUT);

        void createNodeUsageTextures(int width, int height);

        void uploadInitialBricks();

        void initNodePoolAndNodeUsageListProcessor();
        
        void update();
        void updateNodePool();

        unsigned int getTreeTextureID() const;
        
        unsigned int getBrickTextureID() const;

        unsigned int getBrickGradientTextureID() const;

        unsigned int getNodeUsageTextureID() const
        {
            return m_nodeUsageTextureID;
        }

        unsigned int getSelectionMaskTextureID() const;
        unsigned int getHistoPyramidTextureID() const;

        void asyncDownloadCompressedNodeUsageListLength(int histoPyramidRenderLevel);
        unsigned int getDownloadedCompressedNodeUsageListLength(unsigned int& histoPyramidReadID);

        NodeUsageListParams& getWriteCompressedNodeUsageList()
        {
            return m_compressedNodeUsageList[m_compressedNodeUsageListWrite];
        }

        QVector3D getBrickDimension() const;

        bool getBrickDataIsCompressed() const
        {
            return  m_brickDataIsCompressed;
        }

        bool getBrickGradientsAreUnsigned() const
        {
            return m_brickGradientsAreUnsigned;
        }

        void getBrickParams(size_t& brickDimX,
                            size_t& brickDimY,
                            size_t& brickDimZ,
                            bool& isCompressed,
                            bool& brickGradientsAreUnsigned)
        {  
            brickDimX = m_brickDimX;
            brickDimY = m_brickDimY;
            brickDimZ = m_brickDimZ;
            isCompressed = m_brickDataIsCompressed;
            brickGradientsAreUnsigned = brickGradientsAreUnsigned;
        }

        float getRayStepSize() const { return m_rayStepSize; }
        void setRayStepSize(float rayStepSize) { m_rayStepSize = rayStepSize; }

        void setBrickParams(size_t brickDimX,
                            size_t brickDimY,
                            size_t brickDimZ,
                            bool isCompressed=false,
                            bool gradientsAreUnsigned=false)
        {
            m_brickDimX = brickDimX;
            m_brickDimY = brickDimY;
            m_brickDimZ = brickDimZ;
            m_brickDataIsCompressed = isCompressed;
            m_brickGradientsAreUnsigned = gradientsAreUnsigned;
        }

        QVector3D getBrickPoolDimension() const;

        size_t getDepth() const
        {
            return m_depth;
        }

        void setDepth(size_t depth) 
        { 
            m_depth = depth; 
        }

        bool getMipMapDimensions(size_t level, size_t& xDim, size_t& yDim, size_t& zDim)
        {
            if(level < m_mipMaps.size())
            {
                const MipMap& mipMap = m_mipMaps.at(level);
                xDim = mipMap.dimX;
                yDim = mipMap.dimY;
                zDim = mipMap.dimZ;
                return true;
            }
            return false;
        }

        Node* getRootNode();
        
        vox::SceneObject* getSceneObject() { return m_spSceneObject; }
        const vox::SceneObject* getSceneObject() const { return m_spSceneObject; }

        void setSceneObject(vox::SceneObject* pSceneObj) { m_spSceneObject = pSceneObj; }

        OctTreeNodePool* getNodePool() { return m_pOctTreeNodePool; }
    protected:
        ~GigaVoxelsOctTree();

        void updateNodeUsageListProcessor();
    };
}
#endif
