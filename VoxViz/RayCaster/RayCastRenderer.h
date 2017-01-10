#ifndef RC_RAYCAST_RENDERER_H
#define RC_RAYCAST_RENDERER_H

#include "VoxVizCore/Renderer.h"

namespace rc
{
    class RayCastRenderer : public vox::Renderer
    {
    private:
        size_t m_numSamples;
    public:
		static void RegisterRenderer();

        RayCastRenderer(size_t numSamples=32) : vox::Renderer("rc"), m_numSamples(numSamples) {}

        size_t getNumSamples() { return m_numSamples; }
        void setNumSamples(size_t numSamples) { m_numSamples = numSamples; }

        virtual void setup();
        virtual void init(vox::Camera& camera,
                          vox::SceneObject& scene);

        virtual void draw(vox::Camera& camera,
                          vox::SceneObject& scene);

    };
};
#endif
