#include "VoxVizCore/Renderer.h"
#include "VoxVizCore/SmartPtr.h"
#include "VoxVizCore/SceneObject.h"

#include "NvUI/NvUI.h"
#include "NvUI/NvPackedColor.h"

using namespace vox;
        
typedef std::map<std::string, SmartPtr<Renderer> > Renderers;
static Renderers* s_pRenderers = nullptr;

Renderer::Renderer(const std::string& algorithm) : 
        m_reloadShaders(false),
        m_drawDebug(0),
        m_drawGUI(true),
        m_enableLighting(true),
		m_computeLighting(false),
		m_lodScalar(2.0f)
{
    RegisterRendererAlgorithm(algorithm, this);
}

Renderer* Renderer::CreateRenderer(const std::string& algorithm)
{
    Renderers::iterator findIt = s_pRenderers->find(algorithm);
    if(findIt == s_pRenderers->end())
        return NULL;

    return findIt->second.get();
}

void Renderer::RegisterRendererAlgorithm(const std::string& algorithm, Renderer* pRenderer)
{
    static Renderers renderers;
    s_pRenderers = &renderers;
    renderers[algorithm] = pRenderer;
}

bool Renderer::acceptsFileExtension(const std::string& ext) const
{
    return ext == "pvm" || ext == "voxt";
}

NvUIButton* Renderer::CreateGUIButton(const std::string& label,
                                      float width,
                                      float height,
                                      int numStates,
                                      float textSize)
{
    NvUIButton *pButton = NULL;  
    //float width = 50.0f;
    //float height = 40.0f;
        
    NvUIElement *pButtonStates[3];

    pButtonStates[0] = new NvUIGraphicFrame("btn_round_blue.dds", 18, 18);
    pButtonStates[0]->SetDimensions(width, height);
    
    if(numStates > 1)
    {
        pButtonStates[1] = new NvUIGraphicFrame("btn_round_pressed.dds", 18, 18);
        pButtonStates[1]->SetDimensions(width, height);
    }
    else
        pButtonStates[1] = NULL;

    if(numStates > 2)
    {
        pButtonStates[2] = new NvUIGraphicFrame("btn_box_blue.dds", 18, 18);
        pButtonStates[2]->SetDimensions(width, height);
    }
    else
        pButtonStates[2] = NULL;

    pButton = new NvUIButton(NvUIButtonType::PUSH, 0, pButtonStates, label.c_str(), textSize, true);
    pButton->SetSubCode(1);
    pButton->SetHitMargin(height*0.05f, height*0.05f);
    pButton->SetDrawState(0);
    pButton->SetDimensions(width, height); // reset for check/radio buttons...
        
    return pButton;
}

NvUISlider* Renderer::CreateGUISlider(float width,
                                      float height,
                                      float maxValue,
                                      float minValue,
                                      float stepValue,
                                      float initialValue)
{
    //float width = 250.0f;
    //float height = 10.0f;

    NvUIGraphicFrame *pEmptyBar = new NvUIGraphicFrame("slider_empty.dds", 8);
    NvUIGraphicFrame *pFullBar = new NvUIGraphicFrame("slider_full.dds", 8);
    pFullBar->SetAlpha(0.6f); // make the fill a bit see-through.
    pFullBar->SetColor(NV_PACKED_COLOR(0xFF, 0xE0, 0x50, 0xFF)); // yellow

    NvUIGraphic *pThumb = new NvUIGraphic("slider_thumb.dds");
    pThumb->SetDimensions(height, height);

    NvUISlider *pSlider = new NvUISlider(pEmptyBar, pFullBar, pThumb, 1u);
    pSlider->SetSmoothScrolling(true);
    pSlider->SetIntegral(false);
    pSlider->SetMaxValue(maxValue);
    pSlider->SetMinValue(minValue);
    pSlider->SetStepValue(stepValue);
    pSlider->SetValue(initialValue);
    pSlider->SetDimensions(width, height);
    pSlider->SetHitMargin(height, height);

    return pSlider;
}

NvUIValueBar* Renderer::CreateGUIValueBar(float width,
                                          float height,
                                          float maxValue,
                                          float minValue,
                                          float initialValue)
{
    //float width = 250.0f;
    //float height = 10.0f;

    NvUIGraphicFrame *pEmptyBar = new NvUIGraphicFrame("slider_empty.dds", 8);
    NvUIGraphicFrame *pFullBar = new NvUIGraphicFrame("slider_full.dds", 8);
    pFullBar->SetAlpha(0.6f); // make the fill a bit see-through.
    pFullBar->SetColor(NV_PACKED_COLOR(0xFF, 0xE0, 0x50, 0xFF)); // yellow

    NvUIValueBar *pSlider = new NvUIValueBar(pEmptyBar, pFullBar);
    pSlider->SetIntegral(false);
    pSlider->SetMaxValue(maxValue);
    pSlider->SetMinValue(minValue);
    pSlider->SetValue(initialValue);
    pSlider->SetDimensions(width, height);

    return pSlider;
}

NvUIValueText* Renderer::CreateGUIValueText(const std::string& valueName,
                                            float textSize,
                                            float value,
                                            unsigned int decimalDigits)
{
    NvUIValueText* pValueText = new NvUIValueText(valueName.c_str(),
                                                  NvUIFontFamily::SANS, 
                                                  textSize,//window-height/40.0f
                                                  NvUITextAlign::LEFT,
                                                  value,
                                                  decimalDigits,
                                                  NvUITextAlign::LEFT);

    pValueText->SetColor(NV_PACKED_COLOR(0x30,0xD0,0xD0,0xB0));
    pValueText->SetShadow();

    return pValueText;
}

NvUIValueText* Renderer::CreateGUIValueText(const std::string& valueName,
                                            float textSize,
                                            unsigned int value)
{
    NvUIValueText* pValueText = new NvUIValueText(valueName.c_str(),
                                                  NvUIFontFamily::SANS, 
                                                  textSize,//window-height/40.0f
                                                  NvUITextAlign::LEFT,
                                                  value,
                                                  NvUITextAlign::LEFT);

    pValueText->SetColor(NV_PACKED_COLOR(0x30,0xD0,0xD0,0xB0));
    pValueText->SetShadow();

    return pValueText;
}

NvUIText* Renderer::CreateGUIText(const std::string& text,
                                  float textSize)
{
    NvUIText* pText = new NvUIText(text.c_str(),
                                   NvUIFontFamily::SANS,
                                   textSize,
                                   NvUITextAlign::RIGHT);
    pText->SetColor(NV_PACKED_COLOR(0x30,0xD0,0xD0,0xB0));
    pText->SetShadow();

    return pText;
}