#include "GigaVoxels/GigaVoxelsBrickPool.h"

#include "GigaVoxels/GigaVoxelsOctTreeNodePool.h"

#include <iostream>
#include <cmath>

using namespace gv;

static bool s_initialized = false;
static BrickPool* s_pInstance = NULL;

BrickPool& BrickPool::instance()
{
    if(s_pInstance == NULL)
        s_pInstance = new BrickPool();

    return *s_pInstance;
}

void BrickPool::deleteInstance()
{
    if(s_pInstance != NULL)
        delete s_pInstance;
    s_pInstance = NULL;
}

bool BrickPool::initialized()
{
    return s_initialized;
}

BrickPool::BrickPool() :
    m_maxGpuBricks(0),
    m_maxCpuBricks(0),
    m_bricksUploaded(0),
    m_pboSize(0),
    m_brickDimX(0),
    m_brickDimY(0),
    m_brickDimZ(0),
    m_dimX(0),
    m_dimY(0),
    m_dimZ(0),
    m_uploadPBO(0),
    m_internalTexFmtColors(GL_RGBA8),
    m_pixelFmtColors(GL_UNSIGNED_BYTE),
    m_internalTexFmtGrads(GL_RGBA8),
    m_pixelFmtGrads(GL_FLOAT),
    m_isCompressed(false),
    m_lightingEnabled(true),
    m_numBricksUploaded(0)
{
}

bool BrickPool::init(size_t brickDimX,
                     size_t brickDimY,
                     size_t brickDimZ,
                     size_t numGpuBricks,
                     size_t numCpuBricks,
                     size_t numPboBricks,
                     size_t borderVoxels)
{
    if(s_initialized)
        return true;

    s_initialized = true;

    m_maxGpuBricks = numGpuBricks;
    m_cpuBrickPool.resize(numCpuBricks);
    m_maxCpuBricks = numCpuBricks;
    m_brickDimX = brickDimX;
    m_brickDimY = brickDimY;
    m_brickDimZ = brickDimZ;

    size_t cubeRoot = static_cast<size_t>(std::pow(static_cast<double>(m_maxGpuBricks), 1.0 / 3.0));

    size_t dimX = cubeRoot;
    size_t dimY = cubeRoot;
    size_t dimZ = cubeRoot;
    while(dimX * dimY * dimZ < m_maxGpuBricks)
    {
        ++dimX;
    }

    m_maxGpuBricks = dimX * dimY * dimZ;

    m_borderVoxels = borderVoxels;

    size_t colorTextureBrickSize =  (m_brickDimX + m_borderVoxels) 
                                  * (m_brickDimY + m_borderVoxels) 
                                  * (m_brickDimZ + m_borderVoxels) 
                                  * sizeof(vox::Vec4ub);
    
    size_t gradientTextureBrickSize =  (m_brickDimX + m_borderVoxels) 
                                     * (m_brickDimY + m_borderVoxels) 
                                     * (m_brickDimZ + m_borderVoxels) 
                                     * sizeof(vox::Vec3f);

    m_dimX = (m_brickDimX + m_borderVoxels) * dimX;
    m_dimY = (m_brickDimY + m_borderVoxels) * dimY;
    m_dimZ = (m_brickDimZ + m_borderVoxels) * dimZ;

    //size_t colorTextureSize = m_dimX * m_dimY * m_dimZ * sizeof(vox::Vec4ub);
    //size_t gradientTextureSize = m_dimX * m_dimY * m_dimZ * sizeof(vox::Vec3f);

    m_colorTextureID = voxOpenGL::GLUtils::Create3DTexture(m_internalTexFmtColors,
													       m_dimX,
													       m_dimY,
													       m_dimZ,
													       GL_RGBA,
													       m_pixelFmtColors,
													       NULL,
													       false,
                                                           true);

    m_gradientTextureID = voxOpenGL::GLUtils::Create3DTexture(m_internalTexFmtGrads,
													          m_dimX,
													          m_dimY,
													          m_dimZ,
													          GL_RGB,
													          m_pixelFmtGrads,
													          NULL,
													          false,
                                                              true);

    m_pboSize = (colorTextureBrickSize + gradientTextureBrickSize) * numPboBricks;
    //use this to initialize the pbo
    unsigned char* pZero = new unsigned char[m_pboSize];
    memset(pZero, 0, m_pboSize);

    m_uploadPBO = voxOpenGL::GLUtils::CreatePixelBufferObject(GL_PIXEL_UNPACK_BUFFER, 
                                                              m_pboSize,
                                                              pZero, 
                                                              GL_STREAM_DRAW);
    delete [] pZero;

    for(size_t i = 0; i < m_maxCpuBricks; ++i)
    {
        vox::SmartPtr<GigaVoxelsOctTree::Node>& spEmptyNode = m_cpuBrickPool.at(i);

        spEmptyNode = new GigaVoxelsOctTree::Node();
        spEmptyNode->allocateBrick(colorTextureBrickSize);
        spEmptyNode->allocateBrickGradients(gradientTextureBrickSize);

        m_freeBricks.push_back(spEmptyNode.get());
    }

    return m_colorTextureID != 0 &&
        m_gradientTextureID != 0 &&
        m_uploadPBO != 0;
}

bool BrickPool::initSpecial(GLint internalTexFmtColors,
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
                            bool lightingEnabled)
{
    if(s_initialized)
        return true;

    s_initialized = true;

    m_maxCpuBricks = 0;
    m_brickDimX = brickDimX;
    m_brickDimY = brickDimY;
    m_brickDimZ = brickDimZ;

    m_maxGpuBricks = dimX * dimY * dimZ;

    m_borderVoxels = borderVoxels;

    m_dimX = (m_brickDimX + m_borderVoxels) * dimX;
    m_dimY = (m_brickDimY + m_borderVoxels) * dimY;
    m_dimZ = (m_brickDimZ + m_borderVoxels) * dimZ;

    m_internalTexFmtColors = internalTexFmtColors;
    m_pixelFmtColors = pixelFmtColors;
    m_internalTexFmtGrads = internalTexFmtGrads;
    m_pixelFmtGrads = pixelFmtGrads;
    m_isCompressed = isCompressed;

    m_lightingEnabled = lightingEnabled;

    m_colorTextureID = voxOpenGL::GLUtils::Create3DTexture(m_internalTexFmtColors,
													       m_dimX,
													       m_dimY,
													       m_dimZ,
													       GL_RGBA,
													       m_pixelFmtColors,
													       NULL,
													       false,
                                                           true);

    GLint compressedSizeColors;
	glGetTexLevelParameteriv(GL_TEXTURE_3D, 
                             0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE_ARB, &compressedSizeColors);

    if(lightingEnabled)
    {
        m_gradientTextureID = voxOpenGL::GLUtils::Create3DTexture(m_internalTexFmtGrads,
													              m_dimX,
													              m_dimY,
													              m_dimZ,
													              GL_RGB,
													              m_pixelFmtGrads,
													              NULL,
													              false,
                                                                  true);
        GLint compressedSizeNormals;
		glGetTexLevelParameteriv(GL_TEXTURE_3D, 
                                 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE_ARB, &compressedSizeNormals);
    }
    else
    {
        m_gradientTextureID = 0;/*voxOpenGL::GLUtils::Create3DTexture(m_internalTexFmtGrads,
													              4,
													              4,
													              4,
													              GL_RGB,
													              m_pixelFmtGrads,
													              NULL,
													              false,
                                                                  true);*/
    }

    m_pboSize = pboSize;
    //use this to initialize the pbo
    unsigned char* pZero = new unsigned char[m_pboSize];
    memset(pZero, 0, m_pboSize);

    m_uploadPBO = voxOpenGL::GLUtils::CreatePixelBufferObject(GL_PIXEL_UNPACK_BUFFER, 
                                                              m_pboSize,
                                                              pZero, 
                                                              GL_STREAM_DRAW);
    delete [] pZero;

    return m_colorTextureID != 0 &&
           (lightingEnabled == false || m_gradientTextureID != 0) &&
           m_uploadPBO != 0;
}

BrickPool::~BrickPool()
{
    glDeleteTextures(1, &m_colorTextureID);
    if(m_gradientTextureID != 0)
        glDeleteTextures(1, &m_gradientTextureID);
    glDeleteBuffers(1, &m_uploadPBO);
    //if(m_spEmptyNode != NULL)
        //delete m_pEmptyNode;

     for(LoadedBricks::iterator itr = m_loadedBricks.begin();
        itr != m_loadedBricks.end();
        ++itr)
    {
        BrickData* pData = *itr;
        delete pData;
    }
}

void BrickPool::initBricks(GigaVoxelsOctTree::Node* pRoot,
                           OctTreeNodePool& nodePool,
                           size_t initUploadCount)
{   
    NodeQueue nodeQueue;
    //add nodes to queue for initialization
    //leaf nodes will be at back
    nodeQueue.push_back(pRoot);//just to get it started
    for(size_t i = 0; 
        i < nodeQueue.size() && nodeQueue.size() < initUploadCount; 
        ++i)
    {
        GigaVoxelsOctTree::Node* pCurNode = nodeQueue.at(i).get();
        size_t childNodeBlockIndex = pCurNode->getChildNodeBlockIndex();
        if(childNodeBlockIndex != 0)
        {
            for(size_t childIndex = 0; childIndex < 8; ++childIndex)
            {
                GigaVoxelsOctTree::Node* pChild = nodePool.getChild(childNodeBlockIndex + childIndex);
                nodeQueue.push_back(pChild);
            }
        }
    }
    nodeQueue.push_back(pRoot);//upload root node first - we always want the root node

    if(m_loadedBricks.size() == m_maxGpuBricks)
    {
        int uploadCount = 0;
        //brick pool is fully loaded so add to upload request queue
        for(size_t i = nodeQueue.size()-1; 
            i > 0 && uploadCount < initUploadCount; 
            --i, ++uploadCount)
        {
            nodeQueue.at(i)->setBrickIsPendingUpload(true);
            m_uploadRequestList.push_back(nodeQueue.at(i).get());
        }
    }
    else
    {
        GLint xOffset = 0;
        GLint yOffset = 0;
        GLint zOffset = 0;
    
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_uploadPBO);
        int pboOffset = 0;
        //start from the back of the queue, which are the leaf nodes
        //and upload as many as possible 
        //(stop before zero because root node is at index 0 too)
        for(size_t i = nodeQueue.size()-1; i > 0; --i)
        {
            GigaVoxelsOctTree::Node* pCurNode = nodeQueue.at(i).get();
            if(pCurNode->getNodeTypeFlag() == GigaVoxelsOctTree::Node::NON_CONSTANT_NODE)
            {
                int nodePboOffset = pCurNode->getBrickColorsSize();
                nodePboOffset += pCurNode->getBrickGradientsSize();;

                if((pboOffset + nodePboOffset) >= (int)m_pboSize)
                {
                    uploadPBOToTextures();
                    pboOffset = 0;
                }

                initBrick(pCurNode, 
                          nodePool, 
                          pboOffset,
                          xOffset, 
                          yOffset,
                          zOffset);

                pboOffset += nodePboOffset;

                m_loadedBricks.push_back(new BrickData(pCurNode, xOffset, yOffset, zOffset, m_loadedBricks.end()));
                m_loadedBricks.back()->listIter = --m_loadedBricks.end();
                
                xOffset += m_brickDimX + m_borderVoxels;
                if(xOffset >= (GLint)m_dimX)
                {
                    xOffset = 0;
                    yOffset += m_brickDimY + m_borderVoxels;
                    if(yOffset >= (GLint)m_dimY)
                    {
                        yOffset = 0;
                        zOffset += m_brickDimZ + m_borderVoxels;
                    }
                }

                if(m_loadedBricks.size() == m_maxGpuBricks
                   || m_loadedBricks.size() == initUploadCount)
                {
                    break;
                }
            }
        }

        uploadPBOToTextures();

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        //insert dummy node for non-initial bricks
        initEmptyBricks(xOffset, yOffset, zOffset);
    }
}

void BrickPool::initEmptyBricks(GLint xOffset/* = 0*/,
                                GLint yOffset/* = 0*/,
                                GLint zOffset/* = 0*/)
{
    if(m_loadedBricks.size() != m_maxGpuBricks)
    {
        m_spEmptyNode = new GigaVoxelsOctTree::Node();
        static GigaVoxelsOctTree::Node::GpuDataStruct dataBlock;//the data ptr has to be valid for Node::set* functions
        m_spEmptyNode->setDataPtr((unsigned char*)&dataBlock);
        for(size_t j = m_loadedBricks.size(); j < m_maxGpuBricks; ++j)
        {
            m_loadedBricks.push_back(new BrickData(m_spEmptyNode,
                                                   xOffset, 
                                                   yOffset, 
                                                   zOffset,
                                                   m_loadedBricks.end()));
            m_loadedBricks.back()->listIter = --m_loadedBricks.end();

            xOffset += m_brickDimX + m_borderVoxels;
            if(xOffset >= (GLint)m_dimX)
            {
                xOffset = 0;
                yOffset += m_brickDimY + m_borderVoxels;
                if(yOffset >= (GLint)m_dimY)
                {
                    yOffset = 0;
                    zOffset += m_brickDimZ + m_borderVoxels;
                }
            }
        }
    }
}

void BrickPool::notifyUsed(GigaVoxelsOctTree::Node* pNode)
{
    BrickData* pBrickData = (BrickData*)pNode->getUserData();
    if(pBrickData != NULL)
    {
        if(pBrickData->getNode() == pNode)
        {
            m_loadedBricksMutex.lock();
            //move the node to the end of the list. items on the front of the list
            //are the least recently used items
            m_loadedBricks.splice(m_loadedBricks.end(), m_loadedBricks, pBrickData->listIter);
            m_loadedBricksMutex.unlock();
        }
        else
        {
            std::cerr << "ERROR: notifyUsed has node mismatch." << std::endl;
        }
    }
}

void BrickPool::notifyDeleted(GigaVoxelsOctTree::Node* pNode)
{
    BrickData* pBrickData = (BrickData*)pNode->getUserData();
    if(pBrickData != NULL)
    {
        if(pBrickData->getNode() == pNode)
        {
            LoadedBricks::iterator listItr = pBrickData->listIter;
            (*listItr)->spBrickNode->setUserData(NULL);
            (*listItr)->spBrickNode = m_spEmptyNode.get();
            m_loadedBricksMutex.lock();

            //move the node to the front of the list. items on the front of the list
            //are the least recently used items
            m_loadedBricks.splice(m_loadedBricks.begin(), m_loadedBricks, pBrickData->listIter);
        
            m_loadedBricksMutex.unlock();
        }
        else
        {
            std::cerr << "ERROR: notifyDeleted has node mismatch." << std::endl;
        }
    }
}

void BrickPool::uploadPBOToTextures()
{
    glBindTexture(GL_TEXTURE_3D, m_colorTextureID);
    //copy updated pbo ranges to texture
    //TODO test performance of multiple copies vs just copying
    //the entire pbo to the texture

	voxOpenGL::GLUtils::CheckOpenGLError();

    for(CopyOperations::iterator itr = m_colorTextureCopyOps.begin();
        itr != m_colorTextureCopyOps.end();
        ++itr)
    {
        //std::cout << "upload=" << (m_colorTextureCopyOps.end() - itr) << std::endl;
        CopyOperation& copyOp = *itr;
        if(m_isCompressed)
        {
            glCompressedTexSubImage3D(GL_TEXTURE_3D,
                                      0,
                                      copyOp.xOffset,
                                      copyOp.yOffset,
                                      copyOp.zOffset,
                                      copyOp.xSize,
                                      copyOp.ySize,
                                      copyOp.zSize,
                                      m_internalTexFmtColors,
                                      copyOp.dataSize,
                                      (const GLvoid*)copyOp.pboOffset);
        }
        else
        {
            glTexSubImage3D(GL_TEXTURE_3D, 
                            0,
                            copyOp.xOffset,
                            copyOp.yOffset,
                            copyOp.zOffset,
                            copyOp.xSize,
                            copyOp.ySize,
                            copyOp.zSize,
                            GL_RGBA,
                            GL_UNSIGNED_BYTE,
                            (const GLvoid*)copyOp.pboOffset);
        }
    }

    m_bricksUploaded += m_colorTextureCopyOps.size();
    std::cout << "Uploaded " 
              << m_colorTextureCopyOps.size() 
              << " total uploaded = " 
              << m_bricksUploaded 
              << " max bricks = " 
              << m_maxGpuBricks << std::endl;

    m_colorTextureCopyOps.clear();

	voxOpenGL::GLUtils::CheckOpenGLError();

    if(!m_lightingEnabled)
    {
        glBindTexture(GL_TEXTURE_3D, 0);
        return;
    }

    glBindTexture(GL_TEXTURE_3D, m_gradientTextureID);
    //copy updated pbo ranges to texture
    //TODO test performance of multiple copies vs just copying
    //the entire pbo to the texture
    for(CopyOperations::iterator itr = m_gradTextureCopyOps.begin();
        itr != m_gradTextureCopyOps.end();
        ++itr)
    {
        //std::cout << "upload=" << (m_gradTextureCopyOps.end() - itr) << std::endl;
        CopyOperation& copyOp = *itr;
        if(m_isCompressed)
        {
            glCompressedTexSubImage3D(GL_TEXTURE_3D,
                                      0,
                                      copyOp.xOffset,
                                      copyOp.yOffset,
                                      copyOp.zOffset,
                                      copyOp.xSize,
                                      copyOp.ySize,
                                      copyOp.zSize,
                                      m_internalTexFmtGrads,
                                      copyOp.dataSize,
                                      (const GLvoid*)copyOp.pboOffset);
        }
        else
        {
            glTexSubImage3D(GL_TEXTURE_3D, 
                            0,
                            copyOp.xOffset,
                            copyOp.yOffset,
                            copyOp.zOffset,
                            copyOp.xSize,
                            copyOp.ySize,
                            copyOp.zSize,
                            GL_RGB,
                            GL_FLOAT,
                            (const GLvoid*)copyOp.pboOffset);
        }
    }

    m_gradTextureCopyOps.clear();

    voxOpenGL::GLUtils::CheckOpenGLError();

    glBindTexture(GL_TEXTURE_3D, 0);
}

void BrickPool::update()
{
    m_numBricksUploaded = m_uploadRequestList.size();
    if(m_uploadRequestList.size() == 0)
        return;
    //sort m_loadedBricks by last access (least recently used first)
    //then upload requested bricks into least recently used slots
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_uploadPBO);

    int pboOffset = 0;
    //call replace brick to map sub ranges into the upload pbo
    GigaVoxelsOctTree::UploadRequestList::iterator itr = m_uploadRequestList.begin();
    for( ; itr != m_uploadRequestList.end() ;//&& uploadItr != m_loadedBricks.end();
        ++itr)
    {
        GigaVoxelsOctTree::Node* pNode = *itr;
        if(pNode->referenceCount() == 1)
            continue;//if m_uploadRequestList is only thing referencing this node then no need to upload

        pNode->setBrickIsPendingUpload(false);//this flag indicates that it is on the upload request list

        //m_uploadRequestList.erase(itr);

        if(pNode->getNodeTypeFlag() == GigaVoxelsOctTree::Node::NON_CONSTANT_NODE
           && !pNode->getBrickIsOnGpuFlag())
        {
            
            m_loadedBricksMutex.lock();
            LoadedBricks::iterator uploadItr = m_loadedBricks.begin();
            
            (*uploadItr)->getNode()->setBrickIsOnGpuFlag(false);
            (*uploadItr)->getNode()->setUserData(NULL);

            m_loadedBricksMutex.unlock();

            GigaVoxelsOctTree::Node* pReplaceNode = (*uploadItr)->getNode();
            
            int uploadSize = pNode->getBrickColorsSize();
            if(m_lightingEnabled)
                uploadSize += pNode->getBrickGradientsSize();

            if(pboOffset + uploadSize >= (int)m_pboSize)
            {
                //if not enough room then upload what we have and
                //reset
                uploadPBOToTextures();
                pboOffset = 0;
            }

            replaceBrick(*(*uploadItr), pNode, pboOffset);

            pboOffset += uploadSize;

            pNode->getNodePool()->addToUpdateList(pNode);

            if(pReplaceNode != m_spEmptyNode.get())
            {
                pReplaceNode->getNodePool()->addToUpdateList(pReplaceNode);
            }

            //move to back of loaded list
            m_loadedBricksMutex.lock();

            pNode->setUserData(*uploadItr);

            m_loadedBricks.splice(m_loadedBricks.end(), m_loadedBricks, uploadItr);
            m_loadedBricksMutex.unlock();
            
            GigaVoxelsOctTree::Node* pParent = pNode->getParent();

            if(pParent != nullptr
               && pParent->getNodeTypeFlag() == GigaVoxelsOctTree::Node::NON_CONSTANT_NODE
               && !pParent->getBrickIsOnGpuFlag())
            {    
                m_loadedBricksMutex.lock();
                uploadItr = m_loadedBricks.begin();

                (*uploadItr)->getNode()->setBrickIsOnGpuFlag(false);
                (*uploadItr)->getNode()->setUserData(NULL);

                m_loadedBricksMutex.unlock();

                GigaVoxelsOctTree::Node* pReplaceNode = (*uploadItr)->getNode();

                uploadSize = pParent->getBrickColorsSize();
                if(m_lightingEnabled)
                    uploadSize += pParent->getBrickGradientsSize();

                if(pboOffset + uploadSize >= (int)m_pboSize)
                {
                    //if not enough room then upload what we have and
                    //reset
                    uploadPBOToTextures();
                    pboOffset = 0;
                }
            
                replaceBrick(*(*uploadItr), pParent, pboOffset);

                pboOffset += uploadSize;

                pParent->getNodePool()->addToUpdateList(pParent);

                if(pReplaceNode != m_spEmptyNode.get())
                {
                    pReplaceNode->getNodePool()->addToUpdateList(pReplaceNode);
                }

                m_loadedBricksMutex.lock();
                pParent->setUserData(*uploadItr);
                //move to back of loaded list
                m_loadedBricks.splice(m_loadedBricks.end(), m_loadedBricks, uploadItr);
                m_loadedBricksMutex.unlock();
            }
        }
    }
    //TODO implement time limits
    m_uploadRequestList.clear();
    
    uploadPBOToTextures();

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void BrickPool::initBrick(GigaVoxelsOctTree::Node* pRoot,
                          OctTreeNodePool& nodePool,
                          GLint pboOffset,
                          GLint xOffset,
                          GLint yOffset,
                          GLint zOffset)
{
    uploadBrick(pRoot, pboOffset, xOffset, yOffset, zOffset);

    //uploadBrick will pop a free brick off the end
    //so now that we are done using the brick for uploading
    //we can stick it back on the queue
    if(m_freeBricks.size() != 0)
    {
        //Don't use m_freeBricks when it is not initialized
        //m_freeBricks will be empty when the brick pool for native gv format data
        enQueueFreeBrick(pRoot);
    }
}

void BrickPool::replaceBrick(BrickData& lruBrick, 
                             GigaVoxelsOctTree::Node* pNode,
                             GLint pboOffset)
{
   uploadBrick(pNode,
                pboOffset,
                lruBrick.brickX, 
                lruBrick.brickY, 
                lruBrick.brickZ);

    GigaVoxelsOctTree::Node* pLruNode = lruBrick.getNode();
    if(m_freeBricks.size() != 0)
    //Don't use m_freeBricks when it is not initialized
    //m_freeBricks will be empty when the brick pool for native gv format data
    //because m_freeBricks are the cpu-side-bricks
    {
        if(pLruNode->getBrick() != NULL)
            enQueueFreeBrick(pLruNode);
        else
            enQueueFreeBrick(pNode);
    }

    lruBrick.spBrickNode = pNode;
}

bool BrickPool::uploadBrick(GigaVoxelsOctTree::Node* pNode,
                            GLint writeOffset,
                            GLint xOffset,
                            GLint yOffset,
                            GLint zOffset)
{
    //GLint mipMapLevelZero = 0;
    if(!pNode->getBrick())
    //This code never used for native gv format data
    {
        GigaVoxelsOctTree::Node* pFreeBrickNode = deQueueFreeBrick();

        vox::Vec4ub* pBrick = (vox::Vec4ub*)pFreeBrickNode->takeBrick();
        memset(pBrick, 0, pFreeBrickNode->getBrickColorsSize());

        vox::Vec3f* pBrickGradients = (vox::Vec3f*)pFreeBrickNode->takeBrickGradients();
        memset(pBrickGradients, 0, pFreeBrickNode->getBrickGradientsSize());

        pNode->copyMipMapToBrick(pBrick, pBrickGradients);
    }

    //copy brick into PBO memory
    const char* pReadPtr = pNode->getBrick();
    const char* pReadGradsPtr = pNode->getBrickGradients();

    size_t brickDimX;
    size_t brickDimY;
    size_t brickDimZ;
    size_t brickBorderX;
    size_t brickBorderY;
    size_t brickBorderZ;
    pNode->getBrickData(brickDimX,
                        brickDimY,
                        brickDimZ,
                        brickBorderX,
                        brickBorderY,
                        brickBorderZ);

    size_t brickSize = pNode->getBrickColorsSize();
    copyToPBO(writeOffset,  
              brickSize,
              pReadPtr);

    m_colorTextureCopyOps.push_back(CopyOperation(writeOffset, 
                                                  xOffset, 
                                                  yOffset, 
                                                  zOffset,
                                                  brickDimX,
                                                  brickDimY,
                                                  brickDimZ,
                                                  brickSize));

    writeOffset += brickSize;

    if(m_lightingEnabled)
    {
        brickSize = pNode->getBrickGradientsSize();
        copyToPBO(writeOffset,
                  brickSize,
                  pReadGradsPtr);

        m_gradTextureCopyOps.push_back(CopyOperation(writeOffset,
                                                     xOffset, 
                                                     yOffset, 
                                                     zOffset,
                                                     brickDimX,
                                                     brickDimY,
                                                     brickDimZ,
                                                     brickSize));
    }
       
    GLint brickXOffset = xOffset + brickBorderX;
    
    GLint brickYOffset = yOffset + brickBorderY;

    GLint brickZOffset = zOffset + brickBorderZ;

    pNode->setBrickPtr(brickXOffset, 
                       brickYOffset, 
                       brickZOffset);

    pNode->setBrickIsOnGpuFlag(true);

    return true;
}

unsigned int BrickPool::getColorTextureID() const
{
    return m_colorTextureID;
}

unsigned int BrickPool::getGradientTextureID() const
{
    return m_gradientTextureID;
}

void BrickPool::copyToPBO(int writeOffset, 
                          size_t dataSize,
                          const GLvoid* pData)
{
    GLvoid* pWritePtr = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER,
                                         writeOffset, dataSize,
                                         GL_MAP_WRITE_BIT |
                                         GL_MAP_INVALIDATE_RANGE_BIT |
                                         GL_MAP_UNSYNCHRONIZED_BIT);
    if(pWritePtr == NULL)
    {
        GLenum errCode = glGetError();
        const GLubyte* error = glewGetErrorString(errCode);
        std::cerr << "ERROR: glMapBufferRange failed due to " 
                  << errCode 
                  << " " 
                  << error 
                  << std::endl;
    }
    else
        memcpy(pWritePtr, pData, dataSize);

    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
}

void BrickPool::enQueueFreeBrick(GigaVoxelsOctTree::Node* pNode)
{
    if(pNode->getBrick() == NULL || pNode->getBrickGradients() == NULL)
        std::cerr << "ERROR: enqueueing null brick node." << std::endl;

    m_freeBricks.push_front(pNode);
}

GigaVoxelsOctTree::Node* BrickPool::deQueueFreeBrick()
{
    GigaVoxelsOctTree::Node* pNode = m_freeBricks.back();
    m_freeBricks.pop_back();

    return pNode;
}