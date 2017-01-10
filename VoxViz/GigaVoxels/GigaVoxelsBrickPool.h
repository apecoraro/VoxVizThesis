#ifndef GIGA_VOXELS_BRICK_POOL_H
#define GIGA_VOXELS_BRICK_POOL_H

#include "GigaVoxels/GigaVoxelsOctTree.h"

#include "VoxVizOpenGL/GLUtils.h"

#include <set>
#include <list>
#include <unordered_map>

namespace gv
{
    class OctTreeNodePool;

    class BrickPool
    {
    public:        
        typedef std::vector< vox::SmartPtr<GigaVoxelsOctTree::Node> > NodeQueue;
        typedef NodeQueue UploadRequestList;

        class BrickData;
        typedef std::list<BrickData*> LoadedBricks;

        struct BrickData
        {
            vox::SmartPtr<GigaVoxelsOctTree::Node> spBrickNode;
            int brickX;
            int brickY;
            int brickZ;
            LoadedBricks::iterator listIter;

            BrickData(GigaVoxelsOctTree::Node* pNode, 
                      int x, int y, int z,
                      LoadedBricks::iterator itr) :
                spBrickNode(pNode),
                brickX(x),
                brickY(y),
                brickZ(z),
                listIter(itr)
            {
                if(spBrickNode.get() != NULL)
                    spBrickNode->setUserData(this);
            }

            void get3DTexturePtr(int& x, int& y, int& z) const
            {
                x = brickX;
                y = brickY;
                z = brickZ;
            }

            GigaVoxelsOctTree::Node* getNode() { return spBrickNode.get(); }
            const GigaVoxelsOctTree::Node* getNode() const { return spBrickNode.get(); }

            ~BrickData()
            {
                if(spBrickNode.get() != NULL)
                    spBrickNode->setUserData(NULL);
            }
        };

    private:
       
        LoadedBricks m_loadedBricks;//nodes whose bricks are loaded on the GPU
        QMutex m_loadedBricksMutex;
        
        typedef std::list< vox::SmartPtr<GigaVoxelsOctTree::Node> > FreeBricks;
        FreeBricks m_freeBricks;
        typedef std::vector< vox::SmartPtr<GigaVoxelsOctTree::Node> > CpuBrickPool;
        CpuBrickPool m_cpuBrickPool;

        size_t m_maxGpuBricks;
        size_t m_maxCpuBricks;
        size_t m_bricksUploaded;
        size_t m_pboSize;
        size_t m_brickDimX;
        size_t m_brickDimY;
        size_t m_brickDimZ;
        size_t m_borderVoxels;
        size_t m_dimX;
        size_t m_dimY;
        size_t m_dimZ;

        GigaVoxelsOctTree::UploadRequestList m_uploadRequestList;

        unsigned int m_colorTextureID;
        unsigned int m_gradientTextureID;

        unsigned int m_uploadPBO;
        
        struct CopyOperation
        {
            int pboOffset;
            int xOffset; 
            int yOffset; 
            int zOffset;
            size_t xSize;
            size_t ySize;
            size_t zSize;
            size_t dataSize;

            CopyOperation(int offset, 
                          int x, int y, int z,
                          size_t sizeX,
                          size_t sizeY,
                          size_t sizeZ,
                          size_t sizeData) :
                pboOffset(offset), 
                xOffset(x), yOffset(y), zOffset(z), 
                xSize(sizeX),
                ySize(sizeY),
                zSize(sizeZ),
                dataSize(sizeData) {}
        };

        typedef std::vector<CopyOperation> CopyOperations;
        CopyOperations m_colorTextureCopyOps;
        CopyOperations m_gradTextureCopyOps;

        vox::SmartPtr<GigaVoxelsOctTree::Node> m_spEmptyNode;//used for brick slots that aren't currently loaded with a brick

        GLint m_internalTexFmtColors;
        GLenum m_pixelFmtColors;
        GLint m_internalTexFmtGrads;
        GLenum m_pixelFmtGrads;
        bool m_isCompressed;

        bool m_lightingEnabled;

        size_t m_numBricksUploaded;

        BrickPool();
        ~BrickPool();
    public:
        static BrickPool& instance();
        static void deleteInstance();

        static bool initialized();
        
        bool init(size_t brickDimX,
                  size_t brickDimY,
                  size_t brickDimz,
                  size_t numGpuBricks,
                  size_t numCpuBricks,
                  size_t numPboBricks,
                  size_t borderVoxels=2u);
        
        bool initSpecial(GLint internalTexFmtColors,
                         GLenum pixelFmtColors,
                         GLint internalTexFmtGrads,
                         GLenum pixelFmtGrads,
                         bool isCompressed,
                         size_t brickDimX,
                         size_t brickDimY,
                         size_t brickDimZ,
                         size_t dimX,
                         size_t dimY,
                         size_t dimZ,
                         size_t pboSize,
                         size_t borderVoxels,
                         bool lightingEnabled=true);

        void notifyUsed(GigaVoxelsOctTree::Node* pNode);
        void notifyDeleted(GigaVoxelsOctTree::Node* pNode);

        void update();

        void initBricks(GigaVoxelsOctTree::Node* pRoot,
                        OctTreeNodePool& nodePool,
                        size_t initCount);
        
        void initEmptyBricks(GLint xOffset = 0,
                             GLint yOffset = 0,
                             GLint zOffset = 0);

        unsigned int getColorTextureID() const;
        unsigned int getGradientTextureID() const;

        size_t getBrickDimX() const { return m_brickDimX; }
        size_t getBrickDimY() const { return m_brickDimY; }
        size_t getBrickDimZ() const { return m_brickDimZ; }

        size_t getDimX() const { return m_dimX; }
        size_t getDimY() const { return m_dimY; }
        size_t getDimZ() const { return m_dimZ; }

        GigaVoxelsOctTree::UploadRequestList& 
            getUploadRequestList() { return m_uploadRequestList; }

        size_t getNumBricksUploaded() const { return m_numBricksUploaded; }
    protected:

        void initBrick(GigaVoxelsOctTree::Node* pRoot,
                       OctTreeNodePool& nodePool,
                       GLint pboOffset,
                       GLint xOffset,
                       GLint yOffset,
                       GLint zOffset);

        void replaceBrick(BrickData& lruBrick,
                          GigaVoxelsOctTree::Node* pNewNode,
                          GLint pboOffset);

        bool uploadBrick(GigaVoxelsOctTree::Node* pNode,
                         GLint writeOffset,
                         GLint xOffset,
                         GLint yOffset,
                         GLint zOffset);

        void copyToPBO(int writeOffset, 
                               size_t dataSize,
                               const GLvoid* pData);

        void uploadPBOToTextures();

        void enQueueFreeBrick(GigaVoxelsOctTree::Node* pNode);
        GigaVoxelsOctTree::Node* deQueueFreeBrick();
    };
};

#endif
