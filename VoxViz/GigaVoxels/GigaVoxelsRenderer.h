#ifndef GV_GIGAVOXELS_RENDERER_H
#define GV_GIGAVOXELS_RENDERER_H

#include "VoxVizCore/Renderer.h"
#include "VoxVizCore/SmartPtr.h"
#include "VoxVizCore/VolumeDataSet.h"

#include "VoxVizOpenGL/GLFrameBufferObject.h"

#include "GigaVoxels/GigaVoxelsOctTree.h"
#include "GigaVoxels/GigaVoxelsDatabasePager.h"
#include "GigaVoxels/GigaVoxelsDebugRenderer.h"

#include "NvUI/NvUI.h"

#include <QtCore/QElapsedTimer>

#include <list>

namespace gv
{
    class GigaVoxelsRenderer : public vox::Renderer
    {
    private:        
        float m_lodScalar;
        unsigned int m_colorTextureID;
        unsigned int m_jitterTextureID;
        vox::SmartPtr<voxOpenGL::GLFrameBufferObject> m_spFrameBufferObject;
    public:
        struct Drawable
        {
            float distToNearPlane;
            vox::SmartPtr<GigaVoxelsOctTree> spOctTree;
            Drawable* pNext;
            Drawable() : distToNearPlane(0.0f), pNext(nullptr) {}
        };
        typedef std::vector<Drawable> DrawablePool;
    private:
        voxOpenGL::ShaderProgram* m_pDrawVolumeShader;
        voxOpenGL::ShaderProgram* m_pCameraNearPlaneShader;
        DrawablePool m_octTreeDrawablePool;
        Drawable* m_pRenderList;
        Drawable* m_pUpdateList;
        DatabasePager* m_pDatabasePager;
        GigaVoxelsDebugRenderer m_debugRenderer;

        //gui parameters
        float m_stateBtnTextSize;
        float m_valueTextSize;
        //database pager UI elements
        NvUIText* m_pDbPagerUILabel;
        NvUIButton* m_pDbPagerStateBtn;
        NvUIText* m_pDbPagerNumPendingLoadLabel;
        NvUIValueBar* m_pDbPagerNumPendingLoadValueBar;
        NvUIText* m_pDbPagerNumPendingLoadText;
        NvUIText* m_pDbPagerNumActiveLabel;
        NvUIValueBar* m_pDbPagerNumActiveValueBar;
        NvUIText* m_pDbPagerNumActiveText;
        //NvUIText* m_pDbPagerMaxLoadTimeValueText;
        //NvUIText* m_pDbPagerMinLoadTimeValueText;
        //NvUIText* m_pDbPagerAvgLoadTimeValueText;

        //gpu node usage list processor UI elements
        NvUIText* m_pNodeListProcUILabel;
        float m_nodeListProcUILeft;
        std::vector< std::pair<NvUIButton*, bool> > m_nodeListProcUIStateBtns;
        float m_nodeListProcUIStateBtnTop;
        float m_nodeListProcUIElementIncr;
        
        //brick upload list
        NvUIText* m_pBrickUploadUILabel;
        NvUIValueBar* m_pBrickUploadListValueBar;
        NvUIText* m_pBrickUploadListText;
        NvUIValueText* m_pDrawTimeUI;
        NvUIValueText* m_pCullTimeUI;
        NvUIValueText* m_pUpdateTimeUI;
        NvUIValueText* m_pGPUTimeUI;
        QElapsedTimer m_gpuTimer;
    public:
		static void RegisterRenderer();

        GigaVoxelsRenderer();

        virtual bool acceptsFileExtension(const std::string& ext) const override;

        virtual void setup();
        virtual void init(vox::Camera& camera,
                          vox::SceneObject& scene) override;

        virtual void initGUI(NvUIWindow& guiWindow) override;
        virtual void updateGUI(NvUIWindow& guiWindow) override;

        virtual void draw(vox::Camera& camera,
                          vox::SceneObject& scene) override;

        virtual void update(vox::Camera& camera,
                          vox::SceneObject& scene) override;

        virtual void postDraw(vox::Camera& camera,
                              vox::SceneObject& scene) override;

        virtual void shutdown();

    protected:
        ~GigaVoxelsRenderer();

        void setDebugShader(bool flag);

        void drawVolume(vox::Camera& camera,
                        vox::SceneObject& voxels,
                        GigaVoxelsOctTree& octTree,
                        size_t numSamples,
                        bool cameraIsPossiblyInside);

        void generateNodeUsageSelectionMask(GigaVoxelsOctTree& octTree,
                                            const vox::Camera& camera);

        void generateSelectionMaskHistoPyramid(GigaVoxelsOctTree& octTree,
                                               vox::Camera& camera);

        unsigned int downloadCompressedNodeUsageListLength(unsigned int& histoPyramidReadID);

        void compressNodeUsageList(GigaVoxelsOctTree& octTree,
                                   vox::Camera& camera);
    };
};
#endif
