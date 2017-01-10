#ifndef GIGA_VOXELS_PAGER_H
#define GIGA_VOXELS_PAGER_H

#include "GigaVoxels/GigaVoxelsSceneGraph.h"
#include "GigaVoxels/GigaVoxelsOctTree.h"
#include "GigaVoxels/GigaVoxelsDebugRenderer.h"

#include "VoxVizCore/Referenced.h"
#include "VoxVizCore/SmartPtr.h"

#include <set>
#include <list>

#include <QtCore/QThread>
#include <QtCore/QMutex>
#include <QtCore/QWaitCondition>

namespace gv
{
    class DatabasePagerThread;

    class DatabasePager : public vox::Referenced
    {
    public:
        static DatabasePager& instance();

        void start(void* pRenderingCtx,
                   int vpWidth, int vpHeight);
        static void kill();

        void requestLoadOctTree(PagedOctTreeNode& node, float distToNearPlane);
        void mergeHandledRequests(GigaVoxelsDebugRenderer* pDebugRenderer);
        void unloadStalePagedOctTreeNodes(size_t curFrameIndex,
                                          GigaVoxelsDebugRenderer* pDebugRenderer);
        void getBrickUploadRequests(GigaVoxelsOctTree::UploadRequestList& brickUploadRequestList);

        enum State
        {
            INIT,
            WAITING,
            LOADING,
            EXITING
        };

        void getStatus(DatabasePager::State& state,
                       size_t& numPendingLoad,
                       size_t& numActive,
                       size_t& maxLoadTimeMs,
                       size_t& minLoadTimeMs,
                       float& avgLoadTimeMs) const;
    protected:
        DatabasePager();
        DatabasePager(const DatabasePager&);
        ~DatabasePager();
    private:
        DatabasePagerThread* m_pPagerThread;
    };
}

#endif