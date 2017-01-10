#include "GigaVoxels/GigaVoxelsDatabasePager.h"

#include "GigaVoxels/GigaVoxelsReader.h"
#include "GigaVoxels/GigaVoxelsBrickPool.h"
#include "GigaVoxels/GigaVoxelsDebugRenderer.h"

#include "VoxVizOpenGL/GLExtensions.h"
#include "VoxVizOpenGL/GLUtils.h"

#include <QtOpenGL/QGLPixelBuffer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>

#include <iostream>

using namespace gv;

namespace gv
{
    struct DatabaseRequest
    {
        DatabaseRequest() : distToNearPlane(0.0f) {}
        DatabaseRequest(PagedOctTreeNode& node, float dist) :
            spPagedOctTree(&node), distToNearPlane(dist) {}
        vox::SmartPtr<PagedOctTreeNode> spPagedOctTree;
        float distToNearPlane;
    };
    typedef std::list<DatabaseRequest> DatabaseRequestList;
    typedef std::set< vox::SmartPtr<gv::PagedOctTreeNode> > LoadedOctTrees;

    class DatabasePagerThread : public QThread
    {
    private:
        volatile bool m_done;
        volatile bool m_exited;
        QWaitCondition m_waitForExit;
        QMutex m_requestListMutex;
        QMutex m_handledRequestListMutex;
        QWaitCondition m_waitForListUpdate;
        DatabaseRequestList m_freeRequestList;
        DatabaseRequestList m_databaseRequestList;
        DatabaseRequestList m_handledRequestList;
        QMutex m_brickUploadRequestListMutex;
        GigaVoxelsOctTree::UploadRequestList m_brickUploadRequestList;
        size_t m_numPendingRequests;
        LoadedOctTrees m_loadedOctTrees;
        QGLPixelBuffer* m_pGlCtx;
        int m_viewportWidth;
        int m_viewportHeight;
        //stats
        DatabasePager::State m_state;
        size_t m_numPendingLoad;
        size_t m_numLoaded;
        size_t m_maxLoadTimeMs;
        size_t m_minLoadTimeMs;
        float m_avgLoadTimeMs;
    public:
        DatabasePagerThread() : 
            m_done(false),
            m_exited(false),
            m_numPendingRequests(0),
            m_pGlCtx(nullptr),
            m_viewportWidth(0),
            m_viewportHeight(0),
            m_state(DatabasePager::INIT),
            m_numPendingLoad(0),
            m_numLoaded(0),
            m_maxLoadTimeMs(0),
            m_minLoadTimeMs(UINT_MAX),
            m_avgLoadTimeMs(0.0f)
        {
            DatabaseRequest req;
            m_freeRequestList.emplace_front(req);
            m_freeRequestList.emplace_front(req);
            m_freeRequestList.emplace_front(req);
            m_freeRequestList.emplace_front(req);
            m_freeRequestList.emplace_front(req);
        }

        ~DatabasePagerThread()
        {
            setDone();
            waitForExit();
            if(m_pGlCtx != nullptr)
                delete m_pGlCtx;
        }

        void setGLContext(QGLPixelBuffer* pGlCtx)
        {
            m_pGlCtx = pGlCtx;
        }

        void setViewport(int width, int height)
        {
            m_viewportWidth = width;
            m_viewportHeight = height;
        }

        virtual void run() override;
        void setDone() { m_done = true; }
        void waitForExit()
        {
            m_requestListMutex.lock();
            m_waitForListUpdate.wakeAll();
            m_requestListMutex.unlock();

            QMutex mutex;
            mutex.lock();
            while(m_exited == false)
            {
                m_waitForExit.wait(&mutex, 10);
            }
            mutex.unlock();
        }

        void requestLoadOctTree(PagedOctTreeNode& node, float distToNearPlane);
        void mergeHandledRequests(GigaVoxelsDebugRenderer* pDebugRenderer);
        void unloadStalePagedOctTreeNodes(size_t curFrameIndex,
                                          GigaVoxelsDebugRenderer* pDebugRenderer);
        void getBrickUploadRequests(GigaVoxelsOctTree::UploadRequestList& brickUploadRequestList);

        void getStatus(DatabasePager::State& state,
                       size_t& numPendingLoad,
                       size_t& numLoaded,
                       size_t& maxLoadTimeMs,
                       size_t& minLoadTimeMs,
                       float& avgLoadTimeMs) const;
    };
}

static vox::SmartPtr<DatabasePager> s_spDBPagerInstance;

DatabasePager& DatabasePager::instance()
{
    if(s_spDBPagerInstance.get() == nullptr)
    {
        s_spDBPagerInstance = new DatabasePager();
    }

    return *s_spDBPagerInstance.get();
}

DatabasePager::DatabasePager() :
    m_pPagerThread(new DatabasePagerThread())
{   
}

DatabasePager::DatabasePager(const DatabasePager&) :
    m_pPagerThread(new DatabasePagerThread())
{
}

DatabasePager::~DatabasePager()
{
    delete m_pPagerThread;
}

void DatabasePager::start(void* pBackgroundCtx,
                          int vpWidth, int vpHeight)
{
    if(m_pPagerThread->isRunning())
        return;
    //voxOpenGL::GLUtils::CheckOpenGLError();
    m_pPagerThread->setGLContext((QGLPixelBuffer*)pBackgroundCtx);
    //voxOpenGL::GLUtils::CheckOpenGLError();
    m_pPagerThread->setViewport(vpWidth, vpHeight);
    m_pPagerThread->start();
}

void DatabasePager::getStatus(DatabasePager::State& state,
                              size_t& numPendingLoad,
                              size_t& numLoaded,
                              size_t& maxLoadTimeMs,
                              size_t& minLoadTimeMs,
                              float& avgLoadTimeMs) const
{
    m_pPagerThread->getStatus(state,
                              numPendingLoad,
                              numLoaded,
                              maxLoadTimeMs,
                              minLoadTimeMs,
                              avgLoadTimeMs);
}

void DatabasePager::getBrickUploadRequests(GigaVoxelsOctTree::UploadRequestList& brickUploadRequestList)
{
    m_pPagerThread->getBrickUploadRequests(brickUploadRequestList);
}

void DatabasePagerThread::getBrickUploadRequests(GigaVoxelsOctTree::UploadRequestList& brickUploadRequestList)
{
    if(m_brickUploadRequestListMutex.tryLock())
    {
        brickUploadRequestList.splice(brickUploadRequestList.end(),
                                      m_brickUploadRequestList,
                                      m_brickUploadRequestList.begin(),
                                      m_brickUploadRequestList.end());

        m_brickUploadRequestListMutex.unlock();
    }
    else
        std::cerr << "Failed to get lock on brick upload request list." << std::endl;
}

void DatabasePagerThread::getStatus(DatabasePager::State& state,
                                    size_t& numPendingLoad,
                                    size_t& numLoaded,
                                    size_t& maxLoadTimeMs,
                                    size_t& minLoadTimeMs,
                                    float& avgLoadTimeMs) const
{
    state = m_state;
    numPendingLoad = m_numPendingLoad + m_numPendingRequests;
    numLoaded = m_numLoaded;
    maxLoadTimeMs = m_maxLoadTimeMs;
    minLoadTimeMs = m_minLoadTimeMs;
    avgLoadTimeMs = m_avgLoadTimeMs;
}

static bool SortByDistAndFrameIndex(const DatabaseRequest& req1, 
                                    const DatabaseRequest& req2)
{
    if(req1.spPagedOctTree->getLastAccess() > req2.spPagedOctTree->getLastAccess())
        return true;
    else if(req1.spPagedOctTree->getLastAccess() == req2.spPagedOctTree->getLastAccess())
        return req1.distToNearPlane < req2.distToNearPlane;
    else
        return false;
}

void DatabasePagerThread::run()
{
    if(m_pGlCtx != nullptr)
        m_pGlCtx->makeCurrent();

    DatabaseRequestList requestList;
    while(!m_done)
    {
        //voxOpenGL::GLUtils::CheckOpenGLError();
        m_requestListMutex.lock();

        m_state = DatabasePager::WAITING;
        if(m_numPendingRequests == 0)
            m_waitForListUpdate.wait(&m_requestListMutex);

        requestList.splice(requestList.end(),
                           m_databaseRequestList,
                           m_databaseRequestList.begin(),
                           m_databaseRequestList.end());

        m_numPendingRequests = 0;

        m_numPendingLoad = requestList.size();

        m_state = DatabasePager::LOADING;

        m_requestListMutex.unlock();

        requestList.sort(SortByDistAndFrameIndex);

        for(DatabaseRequestList::iterator reqItr = requestList.begin();
            reqItr != requestList.end();
            ++reqItr)
        {
            --m_numPendingLoad;

            DatabaseRequest& curReq = *reqItr;

            const std::string& file = curReq.spPagedOctTree->getOctTreeFile();

            if(//m_currentFrameIndex - curReq.spPagedOctTree->getLastAccess() > 10 ||
               curReq.spPagedOctTree->getOctTree() != nullptr ||
               curReq.spPagedOctTree->octTreeLoaded())
            {
                std::cout << "Skipping loaded oct-tree: " << file << std::endl;
                continue;
            }
            
            QElapsedTimer timer;
            timer.start();

            GigaVoxelsOctTree* pOctTree = GigaVoxelsReader::LoadOctTreeFile(file);

            qint64 elapsed = timer.elapsed();
            float avgLoadTimeMs = m_avgLoadTimeMs;
            avgLoadTimeMs += elapsed;
            if(m_avgLoadTimeMs > 0.0f)
                avgLoadTimeMs *= 0.5f;
            m_avgLoadTimeMs = avgLoadTimeMs;

            if(elapsed > m_maxLoadTimeMs)
                m_maxLoadTimeMs = elapsed;
            if(elapsed < m_minLoadTimeMs)
                m_minLoadTimeMs = elapsed;

            if(pOctTree != nullptr)
            {
                //if(m_pGlCtx != nullptr)
                    //pOctTree->createNodeUsageTextures(m_viewportWidth, m_viewportHeight);
                    //pOctTree->initNodePoolAndNodeUsageListProcessor();
                curReq.spPagedOctTree->setLoadedOctTree(pOctTree);
                //add root node to brick upload request list
                if(pOctTree->getRootNode()->getNodeTypeFlag() == gv::GigaVoxelsOctTree::Node::NON_CONSTANT_NODE)
                {
                    m_brickUploadRequestListMutex.lock();
                    pOctTree->getRootNode()->setBrickIsPendingUpload(true);
                    m_brickUploadRequestList.push_back(pOctTree->getRootNode());
                    m_brickUploadRequestListMutex.unlock();
                }
            }
            else
            {
                std::cout << "ERROR: failed to load " << file << std::endl;
                continue;
            }

            m_handledRequestListMutex.lock();
            m_handledRequestList.push_back(curReq);
            m_handledRequestListMutex.unlock();

            m_requestListMutex.lock();
            //give these list nodes back for reuse
            m_freeRequestList.push_back(curReq);
            m_requestListMutex.unlock();
        }

        requestList.clear();
        //voxOpenGL::GLUtils::CheckOpenGLError();
    }

    if(m_pGlCtx != nullptr)
        m_pGlCtx->doneCurrent();

    m_state = DatabasePager::EXITING;
    m_exited = true;
    m_waitForExit.wakeAll();
}

void DatabasePager::kill()
{
    if(s_spDBPagerInstance.get() != nullptr)
    {
        s_spDBPagerInstance = nullptr;
    }
}

void DatabasePager::requestLoadOctTree(PagedOctTreeNode& node, float distToNearPlane)
{
    m_pPagerThread->requestLoadOctTree(node, distToNearPlane);
}

void DatabasePagerThread::requestLoadOctTree(PagedOctTreeNode& node, float distToNearPlane)
{
    if(node.isOnLoadRequestList())
        return;//already on request list

    if(m_requestListMutex.tryLock())
    {
        ++m_numPendingRequests;

        node.setIsOnLoadRequestList(true);

        if(m_freeRequestList.begin() != m_freeRequestList.end())
        {
            m_freeRequestList.begin()->spPagedOctTree = &node;
            m_freeRequestList.begin()->distToNearPlane = distToNearPlane;
            m_databaseRequestList.splice(m_databaseRequestList.end(),
                                         m_freeRequestList,
                                         m_freeRequestList.begin());
        }
        else
        {
            DatabaseRequest req(node, distToNearPlane);
            m_databaseRequestList.push_back(req);
        }

        m_waitForListUpdate.wakeAll();

        m_requestListMutex.unlock();
    }
}

void DatabasePager::mergeHandledRequests(GigaVoxelsDebugRenderer* pDebugRenderer)
{
    m_pPagerThread->mergeHandledRequests(pDebugRenderer);
}

void DatabasePagerThread::mergeHandledRequests(GigaVoxelsDebugRenderer* pDebugRenderer)
{
    if(m_handledRequestListMutex.tryLock())
    {
        DatabaseRequestList handledRequests;
        handledRequests.splice(handledRequests.end(),
                               m_handledRequestList,
                               m_handledRequestList.begin(),
                               m_handledRequestList.end());

        m_handledRequestListMutex.unlock();

        for(DatabaseRequestList::iterator itr = handledRequests.begin();
            itr != handledRequests.end();
            ++itr)
        {
            itr->spPagedOctTree->mergeLoadedOctTree();
            itr->spPagedOctTree->getOctTree()->getSceneObject()->createVAO();
            itr->spPagedOctTree->getOctTree()->createNodeUsageTextures(m_viewportWidth, m_viewportHeight);
            itr->spPagedOctTree->getOctTree()->initNodePoolAndNodeUsageListProcessor();
            itr->spPagedOctTree->setIsOnLoadRequestList(false);
            //upload updates to the node pool texture that were triggered
            //when the brick pool was updated
            itr->spPagedOctTree->getOctTree()->updateNodePool();
            pDebugRenderer->notifyPagedNodeIsActive(*itr->spPagedOctTree.get());
            m_loadedOctTrees.insert((*itr).spPagedOctTree.get());
            ++m_numLoaded;
        }
    }
}

void DatabasePager::unloadStalePagedOctTreeNodes(size_t curFrameIndex,
                                                 GigaVoxelsDebugRenderer* pDebugRenderer)
{
    m_pPagerThread->unloadStalePagedOctTreeNodes(curFrameIndex,
                                                 pDebugRenderer);
}

void DatabasePagerThread::unloadStalePagedOctTreeNodes(size_t curFrameIndex,
                                                       GigaVoxelsDebugRenderer* pDebugRenderer)
{
    static size_t staleFrameCount = 100;
    //if(curFrameIndex % 10 == 0)
        //std::cout << "Num loaded: " << m_loadedOctTrees.size() << std::endl;

    for(LoadedOctTrees::iterator itr = m_loadedOctTrees.begin();
        itr != m_loadedOctTrees.end();
        )
    {
        size_t staleFrames = curFrameIndex - itr->get()->getLastAccess();
    
        //if(curFrameIndex % 10 == 0)
        //    std::cout << "Stale Frames: " << staleFrames << std::endl;

        if(staleFrames > staleFrameCount)
        {
            std::cout << "Unloading OctTree" << std::endl;

            itr->get()->setOctTree(nullptr);
            pDebugRenderer->notifyPagedNodeIsInactive(*itr->get());
            itr = m_loadedOctTrees.erase(itr);
            --m_numLoaded;
        }
        else
            ++itr;
    }
}