#ifndef VS3D_VOLUMESLICER_RENDERER_H
#define VS3D_VOLUMESLICER_RENDERER_H

#include "VoxVizCore/Renderer.h"
#include "VoxVizCore/VoxSampler.h"
#include "VoxVizCore/Array.h"

namespace vs3D
{
    class VolumeSlicer3DRenderer : public vox::Renderer
    {
    private:
        size_t m_numSlicePlanes;
    public:
		static void RegisterRenderer();

        VolumeSlicer3DRenderer(size_t numSlicePlanes=32) : vox::Renderer("vs"), m_numSlicePlanes(numSlicePlanes) {}

        size_t getNumSlicePlanes() { return m_numSlicePlanes; }
        void setNumSlicePlanes(size_t numSlicePlanes) { m_numSlicePlanes = numSlicePlanes; }

        virtual void setup();
        virtual void init(vox::Camera& camera,
                          vox::SceneObject& scene);

        virtual void draw(vox::Camera& camera,
                          vox::SceneObject& scene);

    };
};
#endif
