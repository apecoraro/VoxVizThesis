#ifndef VOX_RENDERER_H
#define VOX_RENDERER_H

#include "VoxVizCore/Referenced.h"
#include "VoxVizCore/Camera.h"

#include <map>

class NvUIWindow;
class NvUIButton;
class NvUIValueBar;
class NvUISlider;
class NvUIValueText;
class NvUIText;

namespace vox
{
    class SceneObject;

    class Renderer : public Referenced
    {
    private:
        void* m_pGlCtx;
        void* m_pBckGlCtx;
        bool m_reloadShaders;
        int m_drawDebug;
        bool m_drawGUI;
        bool m_enableLighting;
		bool m_computeLighting;
		float m_lodScalar;
    public:
        Renderer(const std::string& algorithm);

        static Renderer* CreateRenderer(const std::string& algorithm);
        static void RegisterRendererAlgorithm(const std::string& algorithm, Renderer* pRenderer);

        void setRenderingGLContext(void* pGlCtx) { m_pGlCtx = pGlCtx; }
        void setBackgroundGLContext(void* pGlCtx) { m_pBckGlCtx = pGlCtx; }
        void* getRenderingGLContext() { return m_pGlCtx; }
        void* getBackgroundGLContext() { return m_pBckGlCtx; }

        void setReloadShaders(bool flag) { m_reloadShaders = flag; }
        bool getReloadShaders() const { return m_reloadShaders; }

        void setDrawDebug(int mode) { m_drawDebug = mode; }
        int getDrawDebug() const { return m_drawDebug; }

        void setDrawGUI(bool flag) { m_drawGUI = flag; }
        bool getDrawGUI() const { return m_drawGUI; }

        void setEnableLighting(bool flag) { m_enableLighting = flag; m_computeLighting = flag; }
        bool getEnableLighting() const { return m_enableLighting; }
		void setComputeLighting(bool flag) { if(m_enableLighting) m_computeLighting = flag; }
		bool getComputeLighting() const { return m_computeLighting; }

		void setLodScalar(float lodScalar) { m_lodScalar = lodScalar; }
		float getLodScalar() const { return m_lodScalar; }

        virtual void initGUI(NvUIWindow&) {}
        virtual void updateGUI(NvUIWindow&) {}

        virtual bool acceptsFileExtension(const std::string& ext) const;

        virtual void setup() {}
        virtual void init(vox::Camera& camera,
                          SceneObject& scene) {}

        virtual void draw(vox::Camera& camera,
                          SceneObject& scene)=0;

        virtual void update(vox::Camera& camera,
                            SceneObject& scene) {}
        
        virtual void postDraw(vox::Camera& camera,
                              SceneObject& scene) {}

        virtual void shutdown() {}

        static NvUIButton* CreateGUIButton(const std::string& label,
                                           float width,
                                           float height,
                                           int numStates,
                                           float textSize);

        static NvUISlider* CreateGUISlider(float width,
                                           float height,
                                           float maxValue,
                                           float minValue,
                                           float stepValue,
                                           float initialValue);

        static NvUIValueBar* CreateGUIValueBar(float width,
                                        float height,
                                        float maxValue,
                                        float minValue,
                                        float initialValue);

        static NvUIValueText* CreateGUIValueText(const std::string& valueName,
                                                 float textSize,
                                                 float value,
                                                 unsigned int decimalDigits);

        static NvUIValueText* CreateGUIValueText(const std::string& valueName,
                                                 float textSize,
                                                 unsigned int value);

        static NvUIText* CreateGUIText(const std::string& text,
                                       float textSize);
                                           
    };
}

#endif
