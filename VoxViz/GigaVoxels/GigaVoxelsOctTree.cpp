#include "GigaVoxels/GigaVoxelsOctTree.h"

#include "GigaVoxels/GigaVoxelsOctTreeNodePool.h"
#include "GigaVoxels/GigaVoxelsBrickPool.h"

#include "VoxVizOpenGL/GLUtils.h"
#include "VoxVizCore/Referenced.h"

#include <QtCore/QMutex>
#include <QtCore/QWaitCondition>

#include <QtCore/QElapsedTimer>

#include <cmath>
#include <iostream>
#include <sstream>

using namespace gv;

namespace gv
{
    class NodeTree : public vox::Referenced
    {
    private:
        size_t m_dimX;
        size_t m_dimY;
        size_t m_dimZ;
        size_t m_size;

        struct NodePtrs
        {
            vox::SmartPtr<GigaVoxelsOctTree::Node> spNode;
            NodePtrs() {}
        };

        NodePtrs* m_pNodePtrsBuf;

    protected:
        ~NodeTree()
        {
            delete [] m_pNodePtrsBuf;
        }

    public:
        NodeTree(size_t dimX,
                 size_t dimY,
                 size_t dimZ) :
            m_dimX(dimX),
            m_dimY(dimY),
            m_dimZ(dimZ),
            m_size(dimX * dimY * dimZ),
            m_pNodePtrsBuf(new NodePtrs[m_size])
        {
        }

        void getNodeXYZ(unsigned int nodeID, size_t& x, size_t& y, size_t& z)
        {
            //subtract one because in the shader we added one
            //to each node xyz so that we could identify difference
            //between no node usage (i.e. ray missed volume)
            //and root node usage
            x = ((nodeID >> 20u) & 0x000003FFu) - 1;
            y = ((nodeID >> 10u) & 0x000003FFu) - 1;
            z = (nodeID & 0x000003FFu) - 1;
        }

        GigaVoxelsOctTree::Node* getNode(size_t x, size_t y, size_t z)
        {
            size_t index = (z * m_dimY * m_dimX) + (y * m_dimX) + x;
            if(index < m_size)
                return m_pNodePtrsBuf[index].spNode.get();
            return NULL;
        }

        void initNode(GigaVoxelsOctTree::Node* pNode)
        {
            size_t x, y, z;
            pNode->get3DTexturePtr(x, y, z);
            m_pNodePtrsBuf[(z * m_dimY * m_dimX) + (y * m_dimX) + x].spNode = pNode;
        }
    };
}

GigaVoxelsOctTree::GigaVoxelsOctTree() :
    m_spNodeTree(NULL),
    m_pOctTreeNodePool(new OctTreeNodePool()),
    m_depth(0),
    m_numPBOs(3),
    m_pboNUDownloadIndex(0),
    m_pboNUReadIndex(1),
    m_updateCount(0),
    m_nodeUsageTextureID(0),
    m_compressedNodeUsageListWrite(1),
    m_compressedNodeUsageListRead(0),
    m_selectionMaskTextureID(0),
    m_pboHPDownloadIndex(0),
    m_pboHPReadIndex(1),
    m_brickDimX(0),
    m_brickDimY(0),
    m_brickDimZ(0),
    m_brickDataIsCompressed(false),
    m_brickGradientsAreUnsigned(false),
    m_rayStepSize(0.0f)
{
    memset(m_pboIDs, 0, sizeof(m_pboIDs));

    memset(m_nodeUsageListLengths, 0, sizeof(m_nodeUsageListLengths));

    memset(m_histoPyramidTextures, 0, sizeof(m_histoPyramidTextures));
}

void GigaVoxelsOctTree::createNodeUsageTextures(int width, int height)
{
    //this texture tracks the node usage
    m_nodeUsageTextureID = 
            voxOpenGL::GLUtils::Create2DTextureArray(GL_RGBA32UI,
                                                     width,
                                                     height,
                                                     3,
                                                     GL_RGBA_INTEGER,
                                                     GL_UNSIGNED_INT,
                                                     NULL,
                                                     false,
                                                     false);

    for(unsigned int i = 0; i < m_numPBOs; ++i)
    {
        m_pboIDs[i] = voxOpenGL::GLUtils::CreatePixelBufferObject(GL_PIXEL_PACK_BUFFER,
                                                                  0,
                                                                  NULL,
                                                                  GL_STREAM_READ);

        m_pboBufferSizes[i] = 0;
    }

    for(int i = 0; i < 2; ++i)
    {
        //create 1x1 texture for now, we'll resize it using the histopyramid texture generation results
        m_compressedNodeUsageList[i].width = 1;
        m_compressedNodeUsageList[i].height = 1;
        m_compressedNodeUsageList[i].listLength = 0;
        m_compressedNodeUsageList[i].textureID =
            voxOpenGL::GLUtils::Create2DTexture(GL_RGBA32UI, 
                                                m_compressedNodeUsageList[i].width,
                                                m_compressedNodeUsageList[i].height,
                                                GL_RGBA_INTEGER, GL_UNSIGNED_INT, NULL,
                                                false, false);

        //the next two shaders render/generate the mip map levels
        //for this texture, Create2DPyramidTexture allocates the memory
        //for each level of the mip map chain
        m_histoPyramidTextures[i].textureID =
                voxOpenGL::GLUtils::Create2DPyramidTexture(GL_R32UI,
                                                           width,
                                                           height,
                                                           GL_RED_INTEGER,
                                                           GL_UNSIGNED_INT,
                                                           false,
                                                           false,
                                                           m_histoPyramidTextures[i].levelCount);

        m_histoPyramidTextures[i].pboID = 
                voxOpenGL::GLUtils::CreatePixelBufferObject(GL_PIXEL_PACK_BUFFER,
                                                            sizeof(unsigned int),
                                                            NULL,
                                                            GL_STREAM_READ);

        m_histoPyramidTextures[i].getTexImageError = false;
    }

    //this texture is rendered to by selection mask generator
    m_selectionMaskTextureID = 
            voxOpenGL::GLUtils::Create2DTexture(GL_RGBA32UI,
                                                width,
                                                height,
                                                GL_RGBA_INTEGER,
                                                GL_UNSIGNED_INT,
                                                NULL,
                                                false,
                                                false);
}

struct NodeUsageListTexture
{
    //4 octree nodes x,y,z encoded in 30 bits of 
    //red, green, blue, alpha 32ui texture
    unsigned int nodes[4];
};

class NodeUsageListProcessor : public QThread
{
private:
    struct NodeUsageList
    {
        NodeUsageListTexture* pList;
        vox::SmartPtr<NodeTree> spNodeTree;//3D array of node's to allow quick updating of last access
        unsigned int size;
        unsigned int length;
        size_t frameIndex;
        NodeUsageList() : 
            pList(NULL),
            size(0), 
            length(0),
            frameIndex(0) {}
    };

    typedef std::list<NodeUsageList> NodeUsageListQueue;
    NodeUsageListQueue m_nodeUsageListFreePool;
    NodeUsageListQueue m_nodeUsageListProcessPool;
    QMutex m_sharedNodeUsageListMutex;
    QWaitCondition m_waitForListUpdate;
    volatile size_t m_numListsInProcessPool;

    QMutex m_uploadRequestListMutex;
    GigaVoxelsOctTree::UploadRequestList m_uploadRequestList;

    typedef std::map<GigaVoxelsOctTree::Node*, vox::SmartPtr<GigaVoxelsOctTree::Node> > UniqueNodeUsageList;

    volatile bool m_notDone;
    volatile bool m_exited;
    QWaitCondition m_waitForExit;

    void initNodeUsageListFreePool(size_t count)
    {
        m_nodeUsageListFreePool.resize(count);
        for(NodeUsageListQueue::iterator itr = m_nodeUsageListFreePool.begin();
            itr != m_nodeUsageListFreePool.end();
            ++itr)
        {
            NodeUsageList& nodeUsageList = *itr;
            nodeUsageList.size = 32768;
            nodeUsageList.pList = new NodeUsageListTexture[nodeUsageList.size];
        }
    }

    enum State
    {
        STARTING,
        IDLE,
        PROCESSING,
        EXITING
    };

    volatile State m_state;
public:
    NodeUsageListProcessor() :
        m_numListsInProcessPool(0),
        m_notDone(true),
        m_exited(false),
        m_state(STARTING)
    {
        initNodeUsageListFreePool(10);
    }

    NodeUsageListProcessor(const NodeUsageListProcessor& copy) :
        m_numListsInProcessPool(0),
        m_notDone(true),
        m_exited(false),
        m_state(copy.STARTING)
    {
        initNodeUsageListFreePool(10);
    }

    virtual ~NodeUsageListProcessor()
    {
        m_sharedNodeUsageListMutex.lock();

        for(auto freeItr = m_nodeUsageListFreePool.begin();
            freeItr != m_nodeUsageListFreePool.end();
            ++freeItr)
        {
            if(freeItr->pList != nullptr)
                delete [] freeItr->pList;
        }

        for(auto procItr = m_nodeUsageListProcessPool.begin();
            procItr != m_nodeUsageListProcessPool.end();
            ++procItr)
        {
            if(procItr->pList != nullptr)
                delete [] procItr->pList;
        }

        m_sharedNodeUsageListMutex.unlock();
    }

    NodeUsageListProcessor& operator=(const NodeUsageListProcessor& lhs)
    {
        //do nothing
        return *this;
    }

    void getStatus(bool& workerState,
                   size_t& workerProcessListSize) const
    {
        switch(m_state)
        {
        case STARTING:
        case IDLE:
        case EXITING:
            workerState = false;
            break;
        case PROCESSING:
            workerState = true;
            break;
        }

        workerProcessListSize = m_numListsInProcessPool;
    }

    void setDone() 
    { 
        m_notDone = false; 

        m_waitForListUpdate.wakeAll();
    }

    void waitForExit()
    {
        QMutex mutex;
        mutex.lock();

        while(m_exited == false)
        {
            m_waitForExit.wait(&mutex, 10); 
        }

        mutex.unlock();
    }

    void addNodeUsageList(NodeTree* pNodeTree,
                          unsigned int nodeUsageListLength,
                          size_t frameIndex)
    {
        if(m_sharedNodeUsageListMutex.tryLock())
        {
            if(m_nodeUsageListFreePool.size() == 0)
            {
                //take NodeUsageList from front of the active/process pool
                //and move it over to the free pool. The back of the active
                //pool is the most recently added list, so stuff at the front
                //is not as fresh
                m_nodeUsageListFreePool.splice(m_nodeUsageListFreePool.end(),
                                                m_nodeUsageListProcessPool,
                                                m_nodeUsageListProcessPool.begin());
            }

            NodeUsageList& nodeUsageList = m_nodeUsageListFreePool.front();

            //memcpy(nodeUsageList.pList, pTexture, sizeof(NodeUsageListTexture) * nodeUsageListLength);
            glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, sizeof(NodeUsageListTexture) * nodeUsageListLength, nodeUsageList.pList);

            GLenum errCode = glGetError();
            if(errCode != 0)
            {
                std::cerr << "ERROR reading node usage list via glGetBufferSubData. OpenGLError=" 
                              << glewGetErrorString(errCode)
                              << " code=" << std::hex << errCode
                              << std::endl;
            }
            else
            {
                nodeUsageList.spNodeTree = pNodeTree;
                nodeUsageList.length = nodeUsageListLength;
                nodeUsageList.frameIndex = frameIndex;

                m_nodeUsageListProcessPool.splice(m_nodeUsageListProcessPool.end(),
                                                  m_nodeUsageListFreePool,
                                                  m_nodeUsageListFreePool.begin());
                ++m_numListsInProcessPool;

                m_sharedNodeUsageListMutex.unlock();

                m_waitForListUpdate.wakeAll();
            }
        }
    }

    void removeNodeUsageLists(NodeTree* pRemoveNodeTree)
    {
        if(m_sharedNodeUsageListMutex.tryLock())
        {
            for(NodeUsageListQueue::iterator itr = m_nodeUsageListProcessPool.begin();
                itr != m_nodeUsageListProcessPool.end();
                )
            {
                NodeUsageList& nodeUsageList = *itr;
                if(nodeUsageList.spNodeTree.get() == pRemoveNodeTree)
                {
                    itr = m_nodeUsageListProcessPool.erase(itr);
                    --m_numListsInProcessPool;
                }
                else
                {
                    ++itr;
                }
            }

            m_sharedNodeUsageListMutex.unlock();
        }
    }

    void getUploadRequests(GigaVoxelsOctTree::UploadRequestList& requestList)
    {
        if(m_uploadRequestListMutex.tryLock())
        {
            requestList.splice(requestList.end(),
                               m_uploadRequestList,
                               m_uploadRequestList.begin(),
                               m_uploadRequestList.end());

            m_uploadRequestListMutex.unlock();
        }
    }

    NodeUsageListQueue::iterator getBackIterator(NodeUsageListQueue& nodeUsageList)
    {
        NodeUsageListQueue::iterator itr = nodeUsageList.begin();
        NodeUsageListQueue::iterator end = nodeUsageList.end();
        NodeUsageListQueue::iterator retItr = itr;
        while(itr != end)
        {
            retItr = itr;
            ++itr;
        }

        return retItr;
    }

    virtual void run()
    {
        GigaVoxelsOctTree::UploadRequestList localUploadRequestList;
        UniqueNodeUsageList localUniqueNodeUsageList;
        NodeUsageList nodeUsageList;
        nodeUsageList.size = 32768;//32x32x32 - max number of nodes in 6 level tree
        nodeUsageList.pList = new NodeUsageListTexture[nodeUsageList.size];

        while(m_notDone)
        {
            m_sharedNodeUsageListMutex.lock();

            //don't wait if we have items to process
            if(m_nodeUsageListProcessPool.size() == 0)
            {
                m_state = IDLE;
                m_waitForListUpdate.wait(&m_sharedNodeUsageListMutex);

                if(m_nodeUsageListProcessPool.size() == 0)
                {
                    m_sharedNodeUsageListMutex.unlock();
                    continue;
                }
            }
            
            m_state = PROCESSING;

            NodeUsageListQueue::iterator backItr = getBackIterator(m_nodeUsageListProcessPool);
            NodeUsageList& sharedNodeUsageList = *backItr;
            //if(nodeUsageList.size < sharedNodeUsageList.length)
            //{
            //    if(nodeUsageList.pList != NULL)
            //        delete [] nodeUsageList.pList;

            //    nodeUsageList.pList = new NodeUsageListTexture[sharedNodeUsageList.length];
            //    nodeUsageList.size = sharedNodeUsageList.length;
            //}

            //copy the node usage list to my own copy
            nodeUsageList.spNodeTree = sharedNodeUsageList.spNodeTree.get();
            //eliminate reference to the node tree so it doesn't leak
            sharedNodeUsageList.spNodeTree = NULL;
            nodeUsageList.length = sharedNodeUsageList.length;
            nodeUsageList.frameIndex = sharedNodeUsageList.frameIndex;
            memcpy(nodeUsageList.pList, sharedNodeUsageList.pList, sizeof(NodeUsageListTexture) * sharedNodeUsageList.length);

            --m_numListsInProcessPool;
            //stick back on the free pool
            m_nodeUsageListFreePool.splice(m_nodeUsageListFreePool.end(),
                                           m_nodeUsageListProcessPool,
                                           backItr);

            m_sharedNodeUsageListMutex.unlock();

            if(m_notDone != true)
                break;

            //if the node tree has been unloaded then go back to top
            if(nodeUsageList.spNodeTree->referenceCount() == 1)
            {
                nodeUsageList.spNodeTree = NULL;
                continue;
            }
            
            unsigned int index = 0;
            for(; index < nodeUsageList.length; ++index)
            {
                //if the node tree gets unloaded while we are still processing it then just quit out
                if(nodeUsageList.spNodeTree->referenceCount() == 1)
                {
                    nodeUsageList.spNodeTree = NULL;
                    localUniqueNodeUsageList.clear();
                    localUploadRequestList.clear();
                    break;
                }

                const NodeUsageListTexture& nodeUsage = nodeUsageList.pList[index];

                size_t x, y, z;
                if(nodeUsage.nodes[0] != 0)
                {
                    nodeUsageList.spNodeTree->getNodeXYZ(nodeUsage.nodes[0], x, y, z);
                    GigaVoxelsOctTree::Node* pNode = nodeUsageList.spNodeTree->getNode(x, y, z);
                    if(pNode)
                    {
                        localUniqueNodeUsageList[pNode] = pNode;
                        if(pNode->getNodeTypeFlag() == GigaVoxelsOctTree::Node::NON_CONSTANT_NODE
                           && !pNode->getBrickIsOnGpuFlag() 
                           && !pNode->getBrickIsPendingUpload())
                        {
                            pNode->setBrickIsPendingUpload(true);//this flag indicates that the node is on the upload request list
                            localUploadRequestList.push_back(pNode);
                        }
                    }
                    else
                    {
                        std::cout << "WTF at [" << index << "][0] of " << nodeUsageList.length <<std::endl;
                        break;
                    }
                }

                if(nodeUsage.nodes[1] != 0)
                {
                    nodeUsageList.spNodeTree->getNodeXYZ(nodeUsage.nodes[1], x, y, z);
                    GigaVoxelsOctTree::Node* pNode = nodeUsageList.spNodeTree->getNode(x, y, z);
                    if(pNode)
                    {
                        localUniqueNodeUsageList[pNode] = pNode;
                        if(pNode->getNodeTypeFlag() == GigaVoxelsOctTree::Node::NON_CONSTANT_NODE
                           && !pNode->getBrickIsOnGpuFlag() 
                           && !pNode->getBrickIsPendingUpload())
                        {
                            pNode->setBrickIsPendingUpload(true);//this flag indicates that the node is on the upload request list
                            localUploadRequestList.push_back(pNode);
                        }
                    }
                    else
                    {
                        std::cout << "WTF at [" << index << "][1] of " << nodeUsageList.length <<std::endl;
                        break;
                    }
                }

                if(nodeUsage.nodes[2] != 0)
                {
                    nodeUsageList.spNodeTree->getNodeXYZ(nodeUsage.nodes[2], x, y, z);
                    GigaVoxelsOctTree::Node* pNode = nodeUsageList.spNodeTree->getNode(x, y, z);
                    if(pNode)
                    {
                        localUniqueNodeUsageList[pNode] = pNode;
                        if(pNode->getNodeTypeFlag() == GigaVoxelsOctTree::Node::NON_CONSTANT_NODE
                           && !pNode->getBrickIsOnGpuFlag() 
                           && !pNode->getBrickIsPendingUpload())
                        {
                            pNode->setBrickIsPendingUpload(true);//this flag indicates that the node is on the upload request list
                            localUploadRequestList.push_back(pNode);
                        }
                    }
                    else
                    {
                        std::cout << "WTF at [" << index << "][2] of " << nodeUsageList.length <<std::endl;
                        break;
                    }
                }

                if(nodeUsage.nodes[3] != 0)
                {
                    nodeUsageList.spNodeTree->getNodeXYZ(nodeUsage.nodes[3], x, y, z);
                    GigaVoxelsOctTree::Node* pNode = nodeUsageList.spNodeTree->getNode(x, y, z);
                    if(pNode)
                    {
                        localUniqueNodeUsageList[pNode] = pNode;
                        if(pNode->getNodeTypeFlag() == GigaVoxelsOctTree::Node::NON_CONSTANT_NODE
                           && !pNode->getBrickIsOnGpuFlag() 
                           && !pNode->getBrickIsPendingUpload())
                        {
                            pNode->setBrickIsPendingUpload(true);//this flag indicates that the node is on the upload request list
                            localUploadRequestList.push_back(pNode);
                        }
                    }
                    else
                    {
                        std::cout << "WTF at [" << index << "][3] of " << nodeUsageList.length <<std::endl;
                        break;
                    }
                }

                if(localUniqueNodeUsageList.size() > 50)
                {
                    if(localUploadRequestList.size() > 0)
                    {
                        m_uploadRequestListMutex.lock();

                        m_uploadRequestList.splice(m_uploadRequestList.end(), 
                                                   localUploadRequestList,
                                                   localUploadRequestList.begin(),
                                                   localUploadRequestList.end());

                        m_uploadRequestListMutex.unlock();
                    }

                    for(UniqueNodeUsageList::iterator itr = localUniqueNodeUsageList.begin();
                        itr != localUniqueNodeUsageList.end();
                        ++itr)
                    {
                        if(itr->first->getBrickIsOnGpuFlag() 
                            && itr->first->referenceCount() > 1)//if only ref'd by list then it is deleted
                            BrickPool::instance().notifyUsed(itr->first);
                    }

                    localUniqueNodeUsageList.clear();
                }
            }

            nodeUsageList.spNodeTree = NULL;

            if(localUniqueNodeUsageList.size() > 0)
            {
                if(localUploadRequestList.size() > 0)
                {
                    m_uploadRequestListMutex.lock();

                    m_uploadRequestList.splice(m_uploadRequestList.end(), 
                                                localUploadRequestList,
                                                localUploadRequestList.begin(),
                                                localUploadRequestList.end());

                    m_uploadRequestListMutex.unlock();
                }

                for(UniqueNodeUsageList::iterator itr = localUniqueNodeUsageList.begin();
                    itr != localUniqueNodeUsageList.end();
                    ++itr)
                {
                    if(itr->first->getBrickIsOnGpuFlag() 
                        && itr->first->referenceCount() > 1)//if only ref'd by list then it is deleted
                        BrickPool::instance().notifyUsed(itr->first);
                }

                localUniqueNodeUsageList.clear();
            }
        }

        delete [] nodeUsageList.pList;
        m_exited = true;
        m_state = EXITING;
        m_waitForExit.wakeAll();
    }
};


static size_t s_maxNumNodeUsageListProcessorThreads = std::min(QThread::idealThreadCount() - 1, 4);

class NodeUsageListProcessorManager
{
private:
    typedef std::vector<NodeUsageListProcessor> Workers;
    Workers m_workers;
    size_t m_addToIndex;
    static NodeUsageListProcessorManager* s_pManagerInstance;
    NodeUsageListProcessorManager() : m_addToIndex(0) 
    {
        m_workers.reserve(s_maxNumNodeUsageListProcessorThreads);
    }

    ~NodeUsageListProcessorManager()
    {
        killAll();
        s_pManagerInstance = NULL;
    }
public:
    static NodeUsageListProcessorManager& instance()
    {
        if(s_pManagerInstance == NULL)
            s_pManagerInstance = new NodeUsageListProcessorManager();

        return *s_pManagerInstance;
    }

    static void deleteInstance()
    {
        if(s_pManagerInstance != NULL)
            delete s_pManagerInstance;
        s_pManagerInstance = NULL;
    }

    void spawnNodeUsageListProcessor()
    {
        m_workers.push_back(NodeUsageListProcessor());
        m_workers.back().start();
    }

    void addNodeUsageList(NodeTree* pNodeTree,
                          unsigned int nodeUsageListLength,
                          size_t frameIndex)
    {
        m_workers[m_addToIndex].addNodeUsageList(pNodeTree, 
                                                 nodeUsageListLength, 
                                                 frameIndex);
        ++m_addToIndex;
        if(m_addToIndex == m_workers.size())
            m_addToIndex = 0;
    }

    void removeNodeUsageLists(NodeTree* pRemoveNodeTree)
    {
        for(auto itr = m_workers.begin();
            itr != m_workers.end();
            ++itr)
        {
            itr->removeNodeUsageLists(pRemoveNodeTree);
        }
    }
                                  
    void getUploadRequestsAndUpdateNodeLastAccess(GigaVoxelsOctTree::UploadRequestList& requestList)
    {
        for(auto itr = m_workers.begin();
            itr != m_workers.end();
            ++itr)
        {
            itr->getUploadRequests(requestList);
        }
    }

    size_t getWorkerCount() const { return m_workers.size(); }

    void killAll()
    {
        for(auto itr = m_workers.begin();
            itr != m_workers.end();
            ++itr)
        {
            itr->setDone();
            itr->waitForExit();
        }

        m_workers.clear();
        m_addToIndex = 0;
    }

    void getStatus(std::vector<bool>& workerStates,
                   std::vector<size_t>& workerProcessListSizes) const
    {
        for(auto itr = m_workers.begin();
            itr != m_workers.end();
            ++itr)
        {
            bool workerState;
            size_t workerProcessListSize;
            itr->getStatus(workerState, workerProcessListSize);
            workerStates.push_back(workerState);
            workerProcessListSizes.push_back(workerProcessListSize);
        }
    }
};

NodeUsageListProcessorManager* NodeUsageListProcessorManager::s_pManagerInstance = NULL;

void GigaVoxelsOctTree::KillNodeUsageListProcessors()
{
    NodeUsageListProcessorManager::instance().killAll();
    NodeUsageListProcessorManager::deleteInstance();
}

void GigaVoxelsOctTree::GetNodeUsageListProcessorsStatus(std::vector<bool>& workerStates,
                                                         std::vector<size_t>& workerProcessListSizes)
{
    NodeUsageListProcessorManager::instance().getStatus(workerStates,
                                                        workerProcessListSizes);
}

size_t GigaVoxelsOctTree::GetMaxNumNodeUsageListProcessors()
{
    return s_maxNumNodeUsageListProcessorThreads;
}

GigaVoxelsOctTree::~GigaVoxelsOctTree()
{
    NodeUsageListProcessorManager::instance().removeNodeUsageLists(m_spNodeTree.get());
    m_spNodeTree = NULL;

    delete m_pOctTreeNodePool;

    glDeleteBuffers(m_numPBOs, m_pboIDs);

    if(m_nodeUsageTextureID != 0)
    {
        glDeleteTextures(1, &m_nodeUsageTextureID);
        glDeleteTextures(1, &m_compressedNodeUsageList[0].textureID);
        glDeleteTextures(1, &m_compressedNodeUsageList[1].textureID);

        glDeleteTextures(1, &m_selectionMaskTextureID);
        glDeleteTextures(1, &m_histoPyramidTextures[0].textureID);
        glDeleteBuffers(1, &m_histoPyramidTextures[0].pboID);
        glDeleteTextures(1, &m_histoPyramidTextures[1].textureID);
        glDeleteBuffers(1, &m_histoPyramidTextures[1].pboID);
    }
}

static void GetBoxFilterAverage(vox::Vec4ub& avgUChar,
                                vox::Vec3f& avgGrad,
                                const vox::Vec4ub* pVoxels,
                                const vox::Vec3f* pVoxelGrads,
                                size_t dimX, size_t dimY, size_t dimZ,
                                size_t startX, size_t startY, size_t startZ,
                                size_t sampleX, size_t sampleY, size_t sampleZ)
{
    struct Vec4ui
    {
        unsigned int r;
        unsigned int g;
        unsigned int b;
        unsigned int a;
        Vec4ui() : r(0), g(0), b(0), a(0) {}
    };

    size_t endZ = startZ + sampleZ;
    if(endZ > dimZ)
        endZ = dimZ;

    size_t endY = startY + sampleY;
    if(endY > dimY)
        endY = dimY;

    size_t endX = startX + sampleX;
    if(endX > dimX)
        endX = dimX;

    float sampleCount = 0.0f;

    Vec4ui avg;
    for(size_t z = startZ;
        z < endZ;
        ++z)
    {
        for(size_t y = startY;
            y < endY;
            ++y)
        {
            for(size_t x = startX;
                x < endX;
                ++x)
            {
                const vox::Vec4ub& voxelColor = pVoxels[(z * dimY * dimX) + (y * dimX) + x];

                avg.r += voxelColor.r;
                avg.g += voxelColor.g;
                avg.b += voxelColor.b;
                avg.a += voxelColor.a;

                const vox::Vec3f& voxelGrad = pVoxelGrads[(z * dimY * dimX) + (y * dimX) + x];

                avgGrad.x += voxelGrad.x;
                avgGrad.y += voxelGrad.y;
                avgGrad.z += voxelGrad.z;

                sampleCount += 1.0f;
            }
        }
    }

    static const float oneDivSampleCount = 1.0f / sampleCount;

    avgUChar.r = static_cast<unsigned char>(std::floor((static_cast<float>(avg.r)*oneDivSampleCount) + 0.5f));
    avgUChar.g = static_cast<unsigned char>(std::floor((static_cast<float>(avg.g)*oneDivSampleCount) + 0.5f));
    avgUChar.b = static_cast<unsigned char>(std::floor((static_cast<float>(avg.b)*oneDivSampleCount) + 0.5f));
    avgUChar.a = static_cast<unsigned char>(std::floor((static_cast<float>(avg.a)*oneDivSampleCount) + 0.5f));

    avgGrad.x *= oneDivSampleCount;
    avgGrad.y *= oneDivSampleCount;
    avgGrad.z *= oneDivSampleCount;
}

vox::Vec3f Normalize(vox::Vec3f& vec)
{
    QVector3D qvec(vec.x, vec.y, vec.z);

    qvec.normalize();

    vec.x = qvec.x();
    vec.y = qvec.y();
    vec.z = qvec.z();

    return vec;
}

static float GetAlpha(vox::Vec4ub* pVoxelColors, 
                size_t dimX, size_t dimY, 
                size_t x, size_t y, size_t z)
{
    size_t index = (z * dimY * dimX) + (y * dimX) + x;
    unsigned char alpha = pVoxelColors[index].a;

    return static_cast<float>(alpha) / 255.0f;
}

static void GenerateMipMap(const MipMap& curLevelMipMap,
                           size_t curDimX, size_t curDimY, size_t curDimZ,
                           size_t nxtDimX, size_t nxtDimY, size_t nxtDimZ,
                           MipMap& nextLevelMipMap)
{
    const vox::Vec4ub* pCurLevel = curLevelMipMap.pData;
    const vox::Vec3f* pCurLevelGrads = curLevelMipMap.pGradientData;

    size_t nxtLvlSize = nxtDimX * nxtDimY * nxtDimZ;

    vox::Vec4ub* pNextLevel = 
        nextLevelMipMap.pData =
            new vox::Vec4ub[nxtLvlSize];
    vox::Vec3f* pNextLevelGrads = 
        nextLevelMipMap.pGradientData =
            new vox::Vec3f[nxtLvlSize];

    float scaleX = static_cast<float>(curDimX) / static_cast<float>(nxtDimX);
    float scaleY = static_cast<float>(curDimY) / static_cast<float>(nxtDimY);
    float scaleZ = static_cast<float>(curDimZ) / static_cast<float>(nxtDimZ);

    for(size_t nxtZ = 0;
        nxtZ < nxtDimZ;
        ++nxtZ)
    {
        size_t startZ = static_cast<size_t>(nxtZ*scaleZ);
        for(size_t nxtY = 0;
            nxtY < nxtDimY;
            ++nxtY)
        {
            size_t startY = static_cast<size_t>(nxtY*scaleY);
            for(size_t nxtX = 0;
                nxtX < nxtDimX;
                ++nxtX)
            {
                vox::Vec4ub avg;
                vox::Vec3f avgGrad;

                size_t startX = static_cast<size_t>(nxtX*scaleX);

                GetBoxFilterAverage(avg, avgGrad,
                                    pCurLevel, 
                                    pCurLevelGrads,
                                    curDimX, curDimY, curDimZ,
                                    startX, startY, startZ,
                                    static_cast<size_t>(scaleX),
                                    static_cast<size_t>(scaleY),
                                    static_cast<size_t>(scaleZ));

                pNextLevel[(nxtZ * nxtDimY * nxtDimX) + (nxtY * nxtDimX) + nxtX] = avg;
                pNextLevelGrads[(nxtZ * nxtDimY * nxtDimX) + (nxtY * nxtDimX) + nxtX] = Normalize(avgGrad);
            }
        }
    }
}

void Scale3DImage(MipMap& fullMipMap,
                  size_t scaleX, size_t scaleY, size_t scaleZ)
{
    vox::Vec4ub* pScaledData = new vox::Vec4ub[scaleX * scaleY * scaleZ];
    vox::Vec3f* pScaledGradients = new vox::Vec3f[scaleX * scaleY * scaleZ];

    float scalePctX = static_cast<float>(scaleX) / static_cast<float>(fullMipMap.dimX);
    float scalePctY = static_cast<float>(scaleY) / static_cast<float>(fullMipMap.dimY);
    float scalePctZ = static_cast<float>(scaleZ) / static_cast<float>(fullMipMap.dimZ);

    float invScalePctX = 1.0f / scalePctX;
    float invScalePctY = 1.0f / scalePctY;
    float invScalePctZ = 1.0f / scalePctZ;

    for(size_t z = 0; z < scaleZ; ++z)
    {
        size_t nearZ = static_cast<size_t>(std::floor((z * invScalePctZ) + 0.49f));
        if(nearZ >= fullMipMap.dimZ)
            nearZ = fullMipMap.dimZ-1;

        for(size_t y = 0; y < scaleY; ++y)
        {
            size_t nearY = static_cast<size_t>(std::floor((y * invScalePctY) + 0.49f));
            if(nearY >= fullMipMap.dimY)
                nearY = fullMipMap.dimY-1;

            for(size_t x = 0; x < scaleX; ++x)
            {
                size_t nearX = static_cast<size_t>(std::floor((x * invScalePctX) + 0.49f));
                if(nearX >= fullMipMap.dimX)
                    nearX = fullMipMap.dimX-1;

                size_t nearIndex = (nearZ * fullMipMap.dimY * fullMipMap.dimX) 
                                   + (nearY * fullMipMap.dimX) 
                                   + nearX;

                pScaledData[(z * scaleY * scaleX) + (y * scaleX) + x] = 
                    fullMipMap.pData[nearIndex];

                pScaledGradients[(z * scaleY * scaleX) + (y * scaleX) + x] =
                    fullMipMap.pGradientData[nearIndex];
            }
        }
    }

    delete [] fullMipMap.pData;
    delete [] fullMipMap.pGradientData;

    fullMipMap.pData = pScaledData;
    fullMipMap.pGradientData = pScaledGradients;

    fullMipMap.dimX = scaleX;
    fullMipMap.dimY = scaleY;
    fullMipMap.dimZ = scaleZ;
}

static void ScaleForBrick(size_t& volDim, size_t brickDim)
{
    //now scale it to be a multiple of brick dimension and to
    //be less than or equal to max texture dimensions for this card
    size_t mod = volDim % brickDim;
    if(mod != 0)
    {
        volDim -= mod;
        volDim += brickDim;
    }
}

static void GenerateMipMaps(const vox::VolumeDataSet* pVoxels,
                            const  vox::VolumeDataSet::ColorLUT& colorLUT,
                            size_t& brickDimX,
                            size_t& brickDimY,
                            size_t& brickDimZ,
                            GigaVoxelsOctTree::MipMaps& mipMaps)
{
    MipMap fullMipMap;
    
    size_t dimX = pVoxels->dimX();

    size_t dimY = pVoxels->dimY();

    size_t dimZ = pVoxels->dimZ();

    fullMipMap.dimX = dimX;
    fullMipMap.dimY = dimY;
    fullMipMap.dimZ = dimZ;

    size_t voxelCount = fullMipMap.dimX * fullMipMap.dimY * fullMipMap.dimZ;
    fullMipMap.pData = new vox::Vec4ub[voxelCount];
    fullMipMap.pGradientData = new vox::Vec3f[voxelCount];
    
    pVoxels->convert(colorLUT, 
                     fullMipMap.pData,
                     fullMipMap.pGradientData);

    //make sure these are a multiple of 2
    size_t xMod = (dimX % 2);
    if(xMod != 0)
    {
        dimX -= xMod;
        dimX += 2;
    }

    size_t yMod = (dimY % 2);
    if(yMod != 0)
    {
        dimY -= (dimY % 2);
        dimY += 2;
    }

    size_t zMod = (dimZ % 2);
    if(zMod != 0)
    {
        dimZ -= (dimZ % 2);
        dimZ += 2;
    }

    GLint maxTexDimInt;
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &maxTexDimInt);

    size_t maxTexDim = static_cast<size_t>(maxTexDimInt);

    static const size_t k_MinBrickDim = 2;
    static const size_t k_DblMinBrickDim = k_MinBrickDim << 1;
    static const size_t k_MaxBrickDim = 16;
    static const size_t k_DblMaxBrickDim = k_MaxBrickDim << 1;

    if(dimX > maxTexDim)
        dimX = maxTexDim;
    else if(dimX < k_DblMinBrickDim)
        dimX = k_DblMinBrickDim;

    if(dimY > maxTexDim)
        dimY = maxTexDim;
    else if(dimY < k_DblMinBrickDim)
        dimY = k_DblMinBrickDim;

    if(dimZ > maxTexDim)
        dimZ = maxTexDim;
    else if(dimZ < k_DblMinBrickDim)
        dimZ = k_DblMinBrickDim;

    if(dimX >= dimY && dimX >= dimZ)
    {
        brickDimX = dimX < k_DblMaxBrickDim ? k_MinBrickDim : k_MaxBrickDim;
        ScaleForBrick(dimX, brickDimX);
        size_t nodeDim = dimX / brickDimX;
        //want our tree to be cubical
        brickDimY = dimY / nodeDim;
        ScaleForBrick(dimY, brickDimY);

        brickDimZ = dimZ / nodeDim;
        ScaleForBrick(dimZ, brickDimZ);
    }
    else if(dimY >= dimX && dimY >= dimZ)
    {
        brickDimY = dimY < k_DblMaxBrickDim ? k_MinBrickDim : k_MaxBrickDim;
        ScaleForBrick(dimY, brickDimY);
        size_t nodeDim = dimY / brickDimY;
        //want our tree to be cubical
        brickDimX = dimX / nodeDim;
        ScaleForBrick(dimX, brickDimX);

        brickDimZ = dimZ / nodeDim;
        ScaleForBrick(dimZ, brickDimZ);
    }
    else
    {
        brickDimZ = dimZ < k_DblMaxBrickDim ? k_MinBrickDim : k_MaxBrickDim;
        ScaleForBrick(dimZ, brickDimZ);

        size_t nodeDim = dimZ / brickDimZ;
        //want our tree to be cubical
        brickDimX = dimX / nodeDim;
        ScaleForBrick(dimX, brickDimX);

        brickDimY = dimY / nodeDim;
        ScaleForBrick(dimY, brickDimY);
    }

    size_t nodeDimX = dimX / brickDimX;
    size_t nodeDimY = dimY / brickDimY;
    size_t nodeDimZ = dimZ / brickDimZ;
    if(nodeDimX != nodeDimZ || nodeDimX != nodeDimY)
        std::cerr << "ERROR brick dimensions do not result in oct tree." << std::endl;

    if(dimX != fullMipMap.dimX ||
       dimY != fullMipMap.dimY ||
       dimZ != fullMipMap.dimZ)
    {
        Scale3DImage(fullMipMap,
                     dimX, dimY, dimZ);
    }

    mipMaps.push_back(fullMipMap);
    
    size_t minDim = std::min(dimX,
                             std::min(dimY, dimZ));

    size_t minBrickDim = std::min(brickDimX,
                                  std::min(brickDimY, brickDimZ));

    while(minDim > minBrickDim)
    {
        MipMap nextMipMap;

        size_t nxtDimX = dimX;
        size_t nxtDimY = dimY;
        size_t nxtDimZ = dimZ;

        nxtDimX >>= 1;
        nxtDimY >>= 1;
        nxtDimZ >>= 1;

        GenerateMipMap(mipMaps.back(),
                       dimX, dimY, dimZ,
                       nxtDimX, nxtDimY, nxtDimZ,
                       nextMipMap);
        
        //update dimensions for current level
        dimX = nxtDimX;
        dimY = nxtDimY;
        dimZ = nxtDimZ;

        nextMipMap.dimX = dimX;
        nextMipMap.dimY = dimY;
        nextMipMap.dimZ = dimZ;

        mipMaps.push_back(nextMipMap);

        minDim >>= 1;
    }
}

static size_t BuildOctTreeChildren(OctTreeNodePool& octTreeNodePool,
                                 GigaVoxelsOctTree::MipMaps& mipMaps,
                                 size_t brickDimX, size_t brickDimY, size_t brickDimZ,
                                 size_t startMipS, size_t startMipT, size_t startMipR,
                                 size_t mipMapLevel,
                                 size_t childBlockIndex)
{
    size_t numNonConstLeafNodes = 0;

    const MipMap& mipMap = mipMaps.at(mipMapLevel);

    size_t nextMipMapLevel;
    if(mipMapLevel != 0)
        nextMipMapLevel = mipMapLevel - 1;

    size_t childIndex = 0;
    size_t mipR = startMipR;
    for(size_t z = 0; z < 2; ++z)
    {
        size_t mipT = startMipT;
        for(size_t y = 0; y < 2; ++y)
        {
            size_t mipS = startMipS;
            for(size_t x = 0; x < 2; ++x)
            {
                GigaVoxelsOctTree::Node* pChild = octTreeNodePool.getChild(childBlockIndex + childIndex);
                
                if(pChild->computeNodeType(mipMap, 
                                           mipS, mipT, mipR,
                                           brickDimX, brickDimY, brickDimZ) == GigaVoxelsOctTree::Node::NON_CONSTANT_NODE 
                   && mipMapLevel == 0)
                {
                    ++numNonConstLeafNodes;
                }

                if(mipMapLevel != 0)
                {
                    size_t subChildIndex;
                    octTreeNodePool.allocateChildNodeBlock(pChild, subChildIndex);
                    
                    size_t childMipS = mipS << 1;
                    size_t childMipT = mipT << 1;
                    size_t childMipR = mipR << 1;
                    //child.setMaxSubDivisionFlag(false);
                    numNonConstLeafNodes += 
                        BuildOctTreeChildren(octTreeNodePool, 
                                             mipMaps,
                                             brickDimX, 
                                             brickDimY,
                                             brickDimZ,
                                             childMipS,
                                             childMipT,
                                             childMipR,
                                             nextMipMapLevel,
                                             subChildIndex);
                }
                
                //else
                //    child.setMaxSubDivisionFlag(true);

                mipS += brickDimX;

                ++childIndex;
            }

            mipT += brickDimY;
        }

        mipR += brickDimZ;
    }

    return numNonConstLeafNodes;
}

static GigaVoxelsOctTree::Node* BuildOctTree(OctTreeNodePool& octTreeNodePool,
                                            GigaVoxelsOctTree::MipMaps& mipMaps,
                                            size_t brickDimX,
                                            size_t brickDimY,
                                            size_t brickDimZ,
                                            size_t& numNonConstLeafNodes)
{
    numNonConstLeafNodes = mipMaps.size() == 1 ? 1 : 0;

    size_t rootIndexIsZero;
    octTreeNodePool.allocateChildNodeBlock(NULL, rootIndexIsZero);
    GigaVoxelsOctTree::Node* pRoot = octTreeNodePool.getChild(rootIndexIsZero);
    
    pRoot->computeNodeType(mipMaps.at(mipMaps.size()-1),
                           0, 0, 0,
                           brickDimX, brickDimY, brickDimZ);

    if(mipMaps.size() > 1)
    {
        //pRoot->setMaxSubDivisionFlag(false);

        size_t childBlockIndex;        
        octTreeNodePool.allocateChildNodeBlock(pRoot, childBlockIndex);

        size_t nextMipMapLevel = mipMaps.size()-2;

        numNonConstLeafNodes = 
            BuildOctTreeChildren(octTreeNodePool,
                                mipMaps,
                                brickDimX, brickDimY, brickDimZ,
                                0, 0, 0,
                                nextMipMapLevel,
                                childBlockIndex);
    }

    return pRoot;
}

//bool GigaVoxelsOctTree::Node::getMaxSubDivisionFlag()
bool GigaVoxelsOctTree::Node::getBrickIsOnGpuFlag()
{
    unsigned int& bits = *reinterpret_cast<unsigned int*>(&m_pData[0]);

    return (bits & 0x80000000) != 0;
}

//void GigaVoxelsOctTree::Node::setMaxSubDivisionFlag(bool flag)
void GigaVoxelsOctTree::Node::setBrickIsOnGpuFlag(bool flag)
{
    unsigned int& bits = *reinterpret_cast<unsigned int*>(&m_pData[0]);

    if(flag)
        bits |= 0x80000000;
    else
        bits &= ~(0x80000000);
}

GigaVoxelsOctTree::Node::NodeType GigaVoxelsOctTree::Node::getNodeTypeFlag()
{
    unsigned int& bits = *reinterpret_cast<unsigned int*>(&m_pData[0]);

    return (bits & 0x40000000) != 0 ? CONSTANT_NODE : NON_CONSTANT_NODE;
}

void GigaVoxelsOctTree::Node::setNodeTypeFlag(NodeType nodeType)
{
    unsigned int& bits = *reinterpret_cast<unsigned int*>(&m_pData[0]);

    if(nodeType == CONSTANT_NODE)
        bits |= 0x40000000;
    else
        bits &= ~(0x40000000);
}

void GigaVoxelsOctTree::Node::getChildNodesPtr(unsigned int& x, unsigned int& y, unsigned int& z) const
{
    unsigned int& bits = *reinterpret_cast<unsigned int*>(&m_pData[0]);

    x = static_cast<unsigned int>((bits >> 20) & 1023);//high 10 bits are the x coordinate
    y = static_cast<unsigned int>(((bits >> 10) & 1023));//middle 10 bits are the y coordinate
    z = static_cast<unsigned int>((bits & 1023));//low 10 bits are the z coordinate
}

void GigaVoxelsOctTree::Node::setChildNodesPtr(unsigned int x, unsigned int y, unsigned int z)
{
    unsigned int& bits = *reinterpret_cast<unsigned int*>(&m_pData[0]);

    bits |= (x << 20);
    bits |= (y << 10);
    bits |= z;
}

void GigaVoxelsOctTree::Node::getBrickPtr(unsigned int& x, unsigned int& y, unsigned int& z) const
{
    unsigned int& bits = *reinterpret_cast<unsigned int*>(&m_pData[4]);

    x = ((bits >> 20) & 1023);
    y = ((bits >> 10) & 1023);
    z = (bits & 1023);
}

void GigaVoxelsOctTree::Node::setBrickPtr(unsigned int x, unsigned int y, unsigned int z)
{
    unsigned int& bits = *reinterpret_cast<unsigned int*>(&m_pData[4]);

    bits |= (x << 20);
    bits |= (y << 10);
    bits |= z;
}

const vox::Vec4ub& GigaVoxelsOctTree::Node::getConstantValue() const
{
    return *reinterpret_cast<vox::Vec4ub*>(&m_pData[4]);
}

//static void NormalizeGradients(size_t count, vox::Vec3f* pGradients)
//{
//    for(size_t i = 0; i < count; ++i)
//    {
//        vox::Vec3f& grad = pGradients[i];
//        grad.x = (grad.x + 1.0f) * 0.5f;
//        grad.y = (grad.y + 1.0f) * 0.5f;
//        grad.z = (grad.z + 1.0f) * 0.5f;
//    }
//}

void GigaVoxelsOctTree::Node::setBrickColorsPtr(bool isCompressed, size_t dataSize, char* pVoxelColors)
{
    m_colorsCompressed = isCompressed;
    m_colorsDataSize = dataSize;
    m_pBrick = pVoxelColors;
}

void GigaVoxelsOctTree::Node::setBrickGradientsPtr(bool isCompressed, size_t dataSize, char* pVoxelGradients)
{
    m_gradientsCompressed = isCompressed;
    m_gradientsDataSize = dataSize;
    m_pBrickGradients = pVoxelGradients;
}

size_t GigaVoxelsOctTree::Node::getBrickColorsSize() const
{
    if(m_colorsDataSize > 0)
        return m_colorsDataSize;

    size_t count = m_brickDimX * m_brickDimY * m_brickDimZ;
    return sizeof(vox::Vec4ub) * count;
}

size_t GigaVoxelsOctTree::Node::getBrickGradientsSize() const
{
    if(m_gradientsDataSize > 0)
        return m_gradientsDataSize;

    size_t count = m_brickDimX * m_brickDimY * m_brickDimZ;
    return sizeof(vox::Vec3f) * count;
}

void GigaVoxelsOctTree::Node::copyMipMapToBrick(vox::Vec4ub* pBrick,
                                                vox::Vec3f* pBrickGradients)
{
    const MipMap& mipMap = *m_pMipMap;

    m_pBrick = (char*)pBrick;
    m_pBrickGradients = (char*)pBrickGradients;

    vox::Vec4ub* pWriteTexture = pBrick;
    vox::Vec3f* pWriteGradTexture = pBrickGradients;

    if(m_mipMapStartX == 0)
    {
        pWriteTexture += 1;
        pWriteGradTexture += 1;
    }

    size_t brickDimX = m_mipMapEndX - m_mipMapStartX;

    if(m_mipMapStartY == 0)
    {
        pWriteTexture += m_brickDimX;
        pWriteGradTexture += m_brickDimX;
    }

    if(m_mipMapStartZ == 0)
    {
        pWriteTexture += (m_brickDimY * m_brickDimX);
        pWriteGradTexture += (m_brickDimY * m_brickDimX);
    }

    for(size_t zIdx = m_mipMapStartZ; zIdx < m_mipMapEndZ; ++zIdx)
    {
        vox::Vec4ub* pSubWriteTexture = pWriteTexture;
        vox::Vec3f* pSubWriteGradTexture = pWriteGradTexture;

        for(size_t yIdx = m_mipMapStartY; yIdx < m_mipMapEndY; ++yIdx)
        {
            size_t dataIndex = (zIdx * mipMap.dimY * mipMap.dimX) 
                                + (yIdx * mipMap.dimX) 
                                + m_mipMapStartX;

            const vox::Vec4ub* pData = &mipMap.pData[dataIndex];

            memcpy(pSubWriteTexture, 
                   pData, sizeof(vox::Vec4ub) * brickDimX);

            pSubWriteTexture += m_brickDimX;

            vox::Vec3f* pGradData = &mipMap.pGradientData[dataIndex];
                
            memcpy(pSubWriteGradTexture, 
                   pGradData, sizeof(vox::Vec3f) * brickDimX);

            pSubWriteGradTexture += m_brickDimX;
        }

        pWriteTexture += (m_brickDimX * m_brickDimY);
        pWriteGradTexture += (m_brickDimX * m_brickDimZ);
    }
}

GigaVoxelsOctTree::Node::NodeType 
     GigaVoxelsOctTree::Node::computeNodeType(const MipMap& mipMap,
                                              size_t mipMapStartX, 
                                              size_t mipMapStartY, 
                                              size_t mipMapStartZ,
                                              size_t brickDimX, size_t brickDimY, size_t brickDimZ)
{
    m_mipMapStartX = mipMapStartX;
    m_mipMapStartY = mipMapStartY;
    m_mipMapStartZ = mipMapStartZ;

    m_brickBorderX = m_borderVoxels >> 1;
    m_brickBorderY = m_borderVoxels >> 1;
    m_brickBorderZ = m_borderVoxels >> 1;
    
    m_mipMapEndX = m_mipMapStartX + brickDimX;

    if(m_mipMapStartX != 0)
    {
        m_mipMapStartX -= m_brickBorderX;
    }

    if(m_mipMapEndX <= mipMap.dimX - m_brickBorderX)
        m_mipMapEndX += m_brickBorderX;

    if(m_mipMapEndX > mipMap.dimX)
    {
        std::cerr << "ERROR: m_mipMapEndX too large." << std::endl;
        m_mipMapEndX = mipMap.dimX;
    }

    m_mipMapEndY = m_mipMapStartY + brickDimY;
    
    if(m_mipMapStartY != 0)
    {
        m_mipMapStartY -= m_brickBorderY;
    }

    if(m_mipMapEndY <= mipMap.dimY - m_brickBorderY)
        m_mipMapEndY += m_brickBorderY;

    if(m_mipMapEndY > mipMap.dimY)
    {
        std::cerr << "ERROR: m_mipMapEndY too large." << std::endl;
        m_mipMapEndY = mipMap.dimY;
    }

    m_mipMapEndZ = m_mipMapStartZ + brickDimZ;
    
    if(m_mipMapStartZ != 0)
    {
        m_mipMapStartZ -= m_brickBorderZ;
    }

    if(m_mipMapEndZ <= mipMap.dimZ - m_brickBorderZ)
        m_mipMapEndZ += m_brickBorderZ;

    if(m_mipMapEndZ > mipMap.dimZ)
    {
        std::cerr << "ERROR: m_mipMapEndZ too large." << std::endl;
        m_mipMapEndZ = mipMap.dimZ;
    }

    vox::Vec4ub constValue = 
        mipMap.pData[(m_mipMapStartZ * mipMap.dimX * mipMap.dimY) + (m_mipMapStartY * mipMap.dimX) + m_mipMapStartX];

    bool isEdgeVoxel = m_mipMapStartX == 0 
                       || m_mipMapStartY == 0 
                       || m_mipMapStartZ == 0 
                       || m_mipMapEndX == mipMap.dimX 
                       || m_mipMapEndY == mipMap.dimY
                       || m_mipMapEndZ == mipMap.dimZ;

    if(isEdgeVoxel)
    {
        //if edge voxel then the border of the voxel is empty space
        //so compare against completely transparent value
        constValue.r = constValue.g = constValue.b = constValue.a = 0;
    }

    for(size_t z = m_mipMapStartZ; z < m_mipMapEndZ; ++z)
    {
        for(size_t y = m_mipMapStartY; y < m_mipMapEndY; ++y)
        {
            for(size_t x = m_mipMapStartX; x < m_mipMapEndX; ++x)
            {
                const vox::Vec4ub& curValue = 
                    mipMap.pData[(z * mipMap.dimX * mipMap.dimY) + (y * mipMap.dimX) + x];

                //if both values are completely transparent then it does
                //not matter that the rgb does not match
                if(curValue.a == 0 && constValue.a == 0)
                    continue;

                if(curValue.r != constValue.r
                    || curValue.g != constValue.g
                    || curValue.b != constValue.b
                    || curValue.a != constValue.a)
                {
                    setNodeTypeFlag(NON_CONSTANT_NODE);//non const node

                    m_pMipMap = &mipMap;

                    m_brickDimX = brickDimX + m_borderVoxels;
                    m_brickDimY = brickDimY + m_borderVoxels;
                    m_brickDimZ = brickDimZ + m_borderVoxels;

                    return NON_CONSTANT_NODE;
                }
            }
        }
    }

    unsigned char* pConstBits = reinterpret_cast<unsigned char*>(&m_pData[4]);
    memcpy(pConstBits, &constValue, sizeof(vox::Vec4ub));

    setNodeTypeFlag(CONSTANT_NODE);//constant node

    return CONSTANT_NODE;
}

void GigaVoxelsOctTree::Node::setConstantValue(float red, float green, float blue, float alpha)
{
    vox::Vec4ub constValue;

    constValue.r = static_cast<unsigned char>(red * 255.0f);
    constValue.g = static_cast<unsigned char>(green * 255.0f);
    constValue.b = static_cast<unsigned char>(blue * 255.0f);
    constValue.a = static_cast<unsigned char>(alpha * 255.0f);

    setNodeTypeFlag(CONSTANT_NODE);
}

void GigaVoxelsOctTree::Node::allocateBrick(size_t brickSize)
{
    if(m_pBrick != NULL)
        delete [] m_pBrick;

    m_pBrick = (char*)new vox::Vec4ub[brickSize];
}

void GigaVoxelsOctTree::Node::allocateBrickGradients(size_t gradSize)
{
    if(m_pBrickGradients != NULL)
        delete [] m_pBrickGradients;

    m_pBrickGradients = (char*)new vox::Vec3f[gradSize];
}

void BuildNodeTree(GigaVoxelsOctTree::Node* pNode, 
                   OctTreeNodePool& octTreeNodePool, 
                   NodeTree* pNodeTree)
{
    pNodeTree->initNode(pNode);

    size_t childBlockIndex = pNode->getChildNodeBlockIndex();
    if(childBlockIndex != 0)
    {
        for(int i = 0; i < 8; ++i)
        {
            GigaVoxelsOctTree::Node& child = *octTreeNodePool.getChild(childBlockIndex + i);
            BuildNodeTree(&child, octTreeNodePool, pNodeTree);
        }
    }
}

void GigaVoxelsOctTree::build(const vox::VolumeDataSet* pVoxels,
                              const vox::VolumeDataSet::ColorLUT& colorLUT)
{
    size_t brickDimX;
    size_t brickDimY;
    size_t brickDimZ;

    GenerateMipMaps(pVoxels, colorLUT, brickDimX, brickDimY, brickDimZ, m_mipMaps);
    //const_cast<vox::VolumeDataSet*>(pVoxels)->freeVoxels();

    setBrickParams(brickDimX, brickDimY, brickDimZ);

    m_depth = m_mipMaps.size();
    
    size_t nonConstLeafNodeCount;
    BuildOctTree(*m_pOctTreeNodePool, 
                m_mipMaps, 
                brickDimX, brickDimY, brickDimZ, 
                nonConstLeafNodeCount);

    /*for(MipMaps::iterator itr = m_mipMaps.begin();
        itr != m_mipMaps.end();
        ++itr)
    {
        delete [] itr->pData;
        delete [] itr->pGradientData;
    }*/

    size_t nonConstLeafNodeParentCount = nonConstLeafNodeCount >> 3;
    size_t gpuBricks = nonConstLeafNodeCount 
                       + nonConstLeafNodeParentCount 
                       + ((nonConstLeafNodeParentCount==1) ? 0 : 1);
    size_t cpuBricks = std::min(static_cast<size_t>(1000u), gpuBricks);
    size_t pboBricks = cpuBricks == gpuBricks ? cpuBricks : 5000ul;
    size_t borderVoxels = 2u;
    BrickPool::instance().init(brickDimX, 
                               brickDimY, 
                               brickDimZ, 
                               gpuBricks,
                               cpuBricks,
                               pboBricks,
                               borderVoxels);

    uploadInitialBricks();
    initNodePoolAndNodeUsageListProcessor();
}

void GigaVoxelsOctTree::uploadInitialBricks()
{
    BrickPool::instance().initBricks(getRootNode(), *m_pOctTreeNodePool, 1);
}

void GigaVoxelsOctTree::initNodePoolAndNodeUsageListProcessor()
{
    m_pOctTreeNodePool->create3DTexture();
    
    size_t dimX, dimY, dimZ;
    m_pOctTreeNodePool->get3DTextureDimensions(dimX, dimY, dimZ);

    m_spNodeTree = new NodeTree(dimX, dimY, dimZ);

    BuildNodeTree(getRootNode(), 
                  *m_pOctTreeNodePool, 
                  m_spNodeTree.get());

    if(NodeUsageListProcessorManager::instance().getWorkerCount() < s_maxNumNodeUsageListProcessorThreads)
        NodeUsageListProcessorManager::instance().spawnNodeUsageListProcessor();
}

unsigned int GigaVoxelsOctTree::getHistoPyramidTextureID() const
{
    return m_histoPyramidTextures[m_pboHPDownloadIndex].textureID;
}

unsigned int GigaVoxelsOctTree::getSelectionMaskTextureID() const
{
    return m_selectionMaskTextureID;
}

void GigaVoxelsOctTree::asyncDownloadCompressedNodeUsageListLength(int histoPyramidRenderLevel)
{   
    //start load of texture into download pixel buffer object
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_histoPyramidTextures[m_pboHPDownloadIndex].pboID);
    
    //read back the last render level
    glGetTexImage(GL_TEXTURE_2D, histoPyramidRenderLevel, GL_RED_INTEGER, GL_UNSIGNED_INT, (GLvoid*)0);
    GLenum getTexErrCode = glGetError();
    if(getTexErrCode != 0)
    {
        std::cerr << "GigaVoxelsOctTree::asyncDownloadCompressedNodeUsageListLength: "
                  << "Failed to get texture image via glGetTexImage. " 
                  << "OpenGLError="
                  << glewGetErrorString(getTexErrCode)
                  << std::endl;
        m_histoPyramidTextures[m_pboHPDownloadIndex].getTexImageError = true;
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

unsigned int GigaVoxelsOctTree::getDownloadedCompressedNodeUsageListLength(unsigned int& histoPyramidReadID)
{
    unsigned int histoPyramidSum = 0;
    if(m_histoPyramidTextures[m_pboHPReadIndex].getTexImageError)
    {
        std::cerr << "GigaVoxelsOctTree::getDownloadedCompressedNodeUsageListLength: "
                  << "Skipping download because glGetTexImage failed. " 
                  << std::endl;
        m_histoPyramidTextures[m_pboHPReadIndex].getTexImageError = false;
        return histoPyramidSum;
    }

    histoPyramidReadID = m_histoPyramidTextures[m_pboHPReadIndex].textureID;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_histoPyramidTextures[m_pboHPReadIndex].pboID);

    QElapsedTimer mapTimer;
    mapTimer.start();

    glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, sizeof(unsigned int), &histoPyramidSum);
    qint64 mapTime = mapTimer.restart();

    GLenum errCode = glGetError();
    if(errCode != 0)
    {
        std::cerr << "ERROR reading HistoPyramidTexture via glGetBufferSubData. OpenGLError=" 
                  << glewGetErrorString(errCode)
                  << " code=" << std::hex << errCode
                  << std::endl;
    }

    //switch the read and download indices
    unsigned int readIndex = m_pboHPReadIndex;

    m_pboHPReadIndex = m_pboHPDownloadIndex;

    m_pboHPDownloadIndex = readIndex;

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0u);

    return histoPyramidSum;
}

#include <QtCore/QElapsedTimer>

void GigaVoxelsOctTree::UpdateBrickPool()
{
    QElapsedTimer timer;
    timer.start();
    NodeUsageListProcessorManager::instance().getUploadRequestsAndUpdateNodeLastAccess(BrickPool::instance().getUploadRequestList());
    qint64 get = timer.elapsed();
    //brick pool will submit nodes that need updating
    //to the node pool
    BrickPool::instance().update();
    qint64 tot = timer.elapsed();
    //if(tot > 0)
    //    std::cout << "TOT=" << tot << " get=" << get << std::endl;
}

void GigaVoxelsOctTree::updateNodePool()
{
    m_pOctTreeNodePool->update();
}

void GigaVoxelsOctTree::update()
{
    if(getRootNode()->getBrickIsOnGpuFlag() == false)
    {
        if(getRootNode()->getNodeTypeFlag() == Node::NON_CONSTANT_NODE)
        {
            std::cerr << "GigaVoxelsOctTree::update(): Root Brick is not on GPU?" << std::endl;
        }
        return;
    }

    QElapsedTimer timer;
    timer.start();

    updateNodePool();

    BrickPool::instance().notifyUsed(getRootNode());
    //this ensures that the root (i.e. least detailed) brick is always on GPU

    qint64 nodePool = timer.restart();

    updateNodeUsageListProcessor();

    qint64 nlp = timer.elapsed();

    if(nlp < nodePool)
        std::cout << "NLP==" << nlp << " < " << nodePool << "==nodePool" << std::endl;
}

#include <QtCore/QElapsedTimer>

void GigaVoxelsOctTree::updateNodeUsageListProcessor()
{
    NodeUsageListParams& writeList = m_compressedNodeUsageList[m_compressedNodeUsageListWrite];
    
    if(writeList.listLength == 0)
        return;

    QElapsedTimer totalTimer;
    totalTimer.start();

    m_compressedNodeUsageListRead = m_compressedNodeUsageListWrite;
    ++m_compressedNodeUsageListWrite;
    if(m_compressedNodeUsageListWrite == 2)
        m_compressedNodeUsageListWrite = 0;

    ++m_updateCount;

    unsigned int nodeUsageListTextureID = writeList.textureID;
    unsigned int nodeUsageListLength = writeList.listLength;

    int nodeUsageListWidth = writeList.width;
    int nodeUsageListHeight = writeList.height;

    //kick off download of current texture
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pboIDs[m_pboNUDownloadIndex]);
    GLsizeiptr dataSize = nodeUsageListWidth * 
                          nodeUsageListHeight * 
                          sizeof(unsigned int) * 
                          4;

    if(m_pboBufferSizes[m_pboNUDownloadIndex] < dataSize)
    {
        glBufferData(GL_PIXEL_PACK_BUFFER,
                     dataSize, NULL, GL_STREAM_READ);

        GLenum errCode = glGetError();
        if(errCode != 0)
        {
            std::cerr << "GigaVoxelsOctTree::updateNodeUsageListProcessor: "
                      << "Failed to allocate PBO buffer via glBufferData of size: " 
                      << dataSize 
                      << " OpenGLError="
                      << glewGetErrorString(errCode)
                      << std::endl;
            GLint64 bufferSize;
            glGetBufferParameteri64v(GL_PIXEL_PACK_BUFFER,
                                     GL_BUFFER_SIZE,
                                     &bufferSize);
            m_pboBufferSizes[m_pboNUDownloadIndex] = bufferSize;                                                                    
        }
        else
            m_pboBufferSizes[m_pboNUDownloadIndex] = dataSize;
    }
    
    glBindTexture(GL_TEXTURE_2D, nodeUsageListTextureID);

    GLenum bindTexErrCode = glGetError();
    if(bindTexErrCode != 0)
    {
        std::cerr << "Failed to bind texture image via glBindTexture. " 
                  << "OpenGLError="
                  << glewGetErrorString(bindTexErrCode)
				  << " error code=" 
				  << std::hex << bindTexErrCode
                  << std::endl;
        nodeUsageListLength = 0;
    }
	else
	{
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 0);

		GLenum getTexErrCode = glGetError();
		if(getTexErrCode != 0)
		{
			std::cerr << "Failed to get texture image via glGetTexImage. " 
					  << "OpenGLError="
					  << glewGetErrorString(getTexErrCode)
					  << " error code=" 
					  << std::hex << getTexErrCode
					  << std::endl;
			nodeUsageListLength = 0;
		}
	}
    glBindTexture(GL_TEXTURE_2D, 0);
    //this will be the size used in the read back step in the next frame
    m_nodeUsageListLengths[m_pboNUDownloadIndex] = nodeUsageListLength;

    //read back node usage list from last frame
    if(m_nodeUsageListLengths[m_pboNUReadIndex] > 0)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pboIDs[m_pboNUReadIndex]);
        
        GLenum errCode = glGetError();
        
        if(errCode == 0)// && pNodeUsageTexture != NULL)
        {
            if(m_nodeUsageListLengths[m_pboNUReadIndex] > 32768)
            {
                //std::cerr << "ERROR reading node usage list? Length="
                //          << m_nodeUsageListLengths[m_pboNUReadIndex]
                //          << std::endl;
                m_nodeUsageListLengths[m_pboNUReadIndex] = 32768;
            }
            //else
            {
                QElapsedTimer addTimer;
                addTimer.start();

                NodeUsageListProcessorManager::instance().addNodeUsageList(m_spNodeTree.get(),
                                                                           m_nodeUsageListLengths[m_pboNUReadIndex],
                                                                           m_updateCount);
                qint64 addTime = addTimer.restart();
            }
        }
        else
        {
            std::cerr << "ERROR reading node usage list via glGetBufferSubData. "
					  << "OpenGLError=" 
                      << glewGetErrorString(errCode) 
					  << ". error code=" 
					  << std::hex << errCode
                      << std::endl;
        }
    }
    //advance the read and download indices
    ++m_pboNUReadIndex;
    m_pboNUDownloadIndex = m_pboNUReadIndex - 1;
    if(m_pboNUReadIndex == m_numPBOs)
        m_pboNUReadIndex = 0;

    qint64 totalTime = totalTimer.elapsed();

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

unsigned int GigaVoxelsOctTree::getBrickTextureID() const
{    
    return BrickPool::instance().getColorTextureID();
}

unsigned int GigaVoxelsOctTree::getBrickGradientTextureID() const
{    
    return BrickPool::instance().getGradientTextureID();
}

unsigned int GigaVoxelsOctTree::getTreeTextureID() const
{
    return m_pOctTreeNodePool->getTextureID();
}

QVector3D GigaVoxelsOctTree::getBrickDimension() const
{
    return QVector3D(m_brickDimX, m_brickDimY, m_brickDimZ);
}

QVector3D GigaVoxelsOctTree::getBrickPoolDimension() const
{
    return QVector3D(BrickPool::instance().getDimX(),
                     BrickPool::instance().getDimY(),
                     BrickPool::instance().getDimZ());
}

GigaVoxelsOctTree::Node* GigaVoxelsOctTree::getRootNode() 
{
    size_t rootIndexIsZero = 0;
    return m_pOctTreeNodePool->getChild(rootIndexIsZero); 
}