#ifndef MC_MARCHING_CUBES_RENDERER_H
#define MC_MARCHING_CUBES_RENDERER_H

#include "VoxVizCore/Renderer.h"
#include "VoxVizCore/VoxSampler.h"
#include "VoxVizCore/Array.h"

namespace mc
{
    class MarchingCubesRenderer : public vox::Renderer
    {
    private:
        float m_isoLevel;
    public:
		static void RegisterRenderer();

        MarchingCubesRenderer() : vox::Renderer("mc"), m_isoLevel(0.5f) {}

        virtual void setup();
        virtual void init(vox::Camera& camera,
                          vox::SceneObject& scene);

        virtual void draw(vox::Camera& camera,
                          vox::SceneObject& scene);

    protected:

        int polygonalize(const vox::VoxSample& gridCell,
                         vox::FloatArray& vertices,
                         vox::FloatArray& normals);
    };
};
#endif
