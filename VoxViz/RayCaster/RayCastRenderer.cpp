#include "RayCaster/RayCastRenderer.h"
#include "RayCaster/RayCasterShaderCodeTester.h"
#include "VoxVizCore/VolumeDataSet.h"
#include "VoxVizCore/BoundingVolumes.h"

#include "VoxVizOpenGL/GLExtensions.h"
#include "VoxVizOpenGL/GLUtils.h"
#include "VoxVizOpenGL/GLShaderProgramManager.h"

#include <QtGui/qvector3d.h>
#include <QtOpenGL/qgl.h>

#include <GL/gl.h>
#include <GL/glu.h>

#ifdef max
    #undef max
#endif

#include <iostream>
#include <algorithm>
#include <cmath>

using namespace rc;

static const GLint ONE_GB = 1024000000;

static voxOpenGL::ShaderProgram* s_pShaderProg = NULL;
static voxOpenGL::ShaderProgram* s_pCameraNearPlaneProg = NULL;

void RayCastRenderer::RegisterRenderer()
{
	static vox::SmartPtr<RayCastRenderer> s_spRenderer = new RayCastRenderer();
}

void RayCastRenderer::setup()
{
    //create shader program
    s_pShaderProg = voxOpenGL::ShaderProgramManager::instance().createShaderProgram("RayCaster.vert",
                                                                                    "RayCaster.frag");

    s_pCameraNearPlaneProg = 
        voxOpenGL::ShaderProgramManager::instance().createShaderProgram(voxOpenGL::GLUtils::GetNearPlaneQuadVertexShaderFile(),
                                                                        "RayCaster.frag");
}

static void AddVertex(vox::FloatArray& vertexArray,
                      double x, double y, double z)
{
    vertexArray.push_back(static_cast<float>(x));
    vertexArray.push_back(static_cast<float>(y));
    vertexArray.push_back(static_cast<float>(z));
}

void RayCastRenderer::init(vox::Camera& camera,
                             vox::SceneObject& scene)
{
    vox::VolumeDataSet* pVoxels = dynamic_cast<vox::VolumeDataSet*>(&scene);

    if(pVoxels == NULL)
        return;

    m_numSamples = pVoxels->getNumSamples();
    if(m_numSamples == 0)
    {
        m_numSamples = static_cast<size_t>(std::sqrt(static_cast<float>(
                               (pVoxels->dimX() * pVoxels->dimX())
                             + (pVoxels->dimY() * pVoxels->dimY())
                             + (pVoxels->dimZ() * pVoxels->dimZ())))) + 2;
    }

    if(m_numSamples < 32)
        m_numSamples = 32;

    vox::TextureIDArray& textureIDs = pVoxels->getTextureIDArray();

    textureIDs.reserve(2);
    textureIDs.resize(2);

    vox::Vec4f* pVoxelColorsF = pVoxels->getColorsF();
    if(pVoxelColorsF != NULL)
    {
        textureIDs[0] = voxOpenGL::GLUtils::Create3DTexture(GL_RGBA32F,
                                                            pVoxels->dimX(),
                                                            pVoxels->dimY(),
                                                            pVoxels->dimZ(),
                                                            GL_RGBA,
                                                            GL_FLOAT,
                                                            pVoxelColorsF,
                                                            false,
                                                            true,
                                                            false);
        delete [] pVoxelColorsF;
        pVoxelColorsF = NULL;
        pVoxels->setColors(pVoxelColorsF);
    }
    else if(pVoxels->subVolumeCount() > 0)
    {
		GLint pixelSize = sizeof(float) * 4;
		GLint pixelFormat = GL_FLOAT;
		GLint internalFormat = GL_RGBA32F;
		if(pVoxels->subVolumeFormat() == vox::VolumeDataSet::SubVolume::FORMAT_UBYTE_RGBA)
		{
			pixelSize = sizeof(unsigned char) * 4;
			internalFormat = GL_RGBA8;
			pixelFormat = GL_UNSIGNED_BYTE;
		}

		size_t textureSize = pixelSize * pVoxels->dimX() * pVoxels->dimY() * pVoxels->dimZ();
		if(textureSize > ONE_GB)
		{
			internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
		}
        textureIDs[0] =
			voxOpenGL::GLUtils::Create3DTexture(internalFormat,
                                                pVoxels->dimX(),
                                                pVoxels->dimY(),
                                                pVoxels->dimZ(),
                                                GL_RGBA,
                                                pixelFormat,
                                                NULL,
                                                false,
                                                true,
                                                false);
		GLint compressedSize;
		glGetTexLevelParameteriv(GL_TEXTURE_3D, 
								 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE_ARB, &compressedSize);

		voxOpenGL::GLUtils::CheckOpenGLError();

        while(pVoxels->subVolumeCount() > 0)
        {
            vox::VolumeDataSet::SubVolume subVol;
            pVoxels->popSubVolume(subVol);
            voxOpenGL::GLUtils::Upload3DTexture(textureIDs[0], 
                                                0, 
                                                subVol.rangeStartX(),
                                                subVol.rangeStartY(),
                                                subVol.rangeStartZ(),
                                                subVol.rangeEndX() - subVol.rangeStartX(),
                                                subVol.rangeEndY() - subVol.rangeStartY(),
                                                subVol.rangeEndZ() - subVol.rangeStartZ(),
                                                GL_RGBA,
                                                (subVol.getFormat() == vox::VolumeDataSet::SubVolume::FORMAT_UBYTE_RGBA ?
                                                 GL_UNSIGNED_BYTE : GL_FLOAT),
                                                subVol.data());

			voxOpenGL::GLUtils::CheckOpenGLError();
        }
    }
    else
    {
        vox::Vec4ub* pVoxelColors = pVoxels->getColorsUB();
        if(pVoxelColors == NULL)
        {
            qreal intensity = 64.0/255.0;
            qreal alpha = 64.0/255.0;
            QVector4D colors[] =
            {
                 QVector4D(0,  0,  0,  0),//transparent black
                 QVector4D(0, intensity,  0, alpha),//red
                 QVector4D(intensity,  0,  0, alpha),//green
                 QVector4D(0,  0, intensity, alpha) //blue
            };

            vox::VolumeDataSet::ColorLUT colorLUT(&colors[0], &colors[4]);
            size_t voxelCount = pVoxels->dimX() * pVoxels->dimY() * pVoxels->dimZ();
            pVoxelColors = new vox::Vec4ub[voxelCount];
    
            pVoxels->convert(colorLUT, pVoxelColors);
        }

        textureIDs[0] = voxOpenGL::GLUtils::Create3DTexture(GL_RGBA8,
                                                            pVoxels->dimX(),
                                                            pVoxels->dimY(),
                                                            pVoxels->dimZ(),
                                                            GL_RGBA,
                                                            GL_UNSIGNED_BYTE,
                                                            pVoxelColors,
                                                            false,
                                                            true,
                                                            false);
        delete [] pVoxelColors;
        pVoxelColors = NULL;
        pVoxels->setColors(pVoxelColors);

		voxOpenGL::GLUtils::CheckOpenGLError();
    }

    GLsizei jitterTexSize = 32;
    textureIDs[1] = voxOpenGL::GLUtils::Create2DJitterTexture(jitterTexSize, jitterTexSize);

    if(s_pShaderProg->bind())
    {

        s_pShaderProg->setUniformValue("NumSamples", static_cast<GLint>(m_numSamples));

        s_pShaderProg->setUniformValue("VoxelSampler", 0);

        s_pShaderProg->setUniformValue("JitterTexSampler", 1);

        s_pShaderProg->setUniformValue("JitterTexSize", jitterTexSize);

		s_pShaderProg->setUniformValue("ComputeLighting", true);

        s_pShaderProg->release();
    }

    if(s_pCameraNearPlaneProg->bind())
    {
        s_pCameraNearPlaneProg->setUniformValue("NumSamples", static_cast<GLint>(m_numSamples));

        s_pCameraNearPlaneProg->setUniformValue("VoxelSampler", 0);

        s_pCameraNearPlaneProg->setUniformValue("JitterTexSampler", 1);

        s_pCameraNearPlaneProg->setUniformValue("JitterTexSize", jitterTexSize);

		s_pCameraNearPlaneProg->setUniformValue("ComputeLighting", true);

        s_pShaderProg->release();
    }

    //glEnableClientState(GL_VERTEX_ARRAY);
    //glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    vox::FloatArray& vertexArray = pVoxels->getFloatVertexArray();
    
    const vox::BoundingBox& bbox = pVoxels->getBoundingBox();

    ////front
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMax());

    ////back
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMin());

    ////left
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMin());

    ////right
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMax());

    //top
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMin());

    //bottom
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMin());
    
    pVoxels->setPrimitiveType(GL_QUADS);//TODO switch to GL_TRIANGLES
    pVoxels->createVAO();

    voxOpenGL::GLUtils::InitNearPlaneQuad(camera);

	voxOpenGL::GLUtils::CheckOpenGLError();
}

void RayCastRenderer::draw(vox::Camera& camera,
                             vox::SceneObject& scene)
{
    vox::VolumeDataSet* pVoxels = dynamic_cast<vox::VolumeDataSet*>(&scene);

    if(pVoxels == NULL)
        return;

	voxOpenGL::GLUtils::CheckOpenGLError();

    glEnable(GL_CULL_FACE);

    glEnable(GL_STENCIL_TEST);
    //use the stencil to make sure that only one pixel modifies the color buffer
    glStencilFunc(GL_EQUAL, 0, 0xFFFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    s_pShaderProg->bind();

    const QMatrix4x4& projMtx = camera.getProjectionMatrix();
    //glGetDoublev(GL_PROJECTION_MATRIX, projMtx.data());

    s_pShaderProg->setUniformValue("ProjectionMatrix", projMtx);

    const QMatrix4x4& modelViewMtx = camera.getViewMatrix();
    //glGetDoublev(GL_MODELVIEW_MATRIX, modelViewMtx.data());

    s_pShaderProg->setUniformValue("ModelViewMatrix", modelViewMtx);

    const vox::BoundingBox& bbox = pVoxels->getBoundingBox();

    static float uvrStepSize = sqrtf(3.0f) / m_numSamples;

    s_pShaderProg->setUniformValue("RayStepSize", uvrStepSize);

    QVector3D volTranslation = bbox.center();
    s_pShaderProg->setUniformValue("VolTranslation",
                                   volTranslation.x(),
                                   volTranslation.y(),
                                   volTranslation.z());

    //scale
    QVector3D volScale = QVector3D(bbox.xMax() - bbox.xMin(), 
                                   bbox.yMax() - bbox.yMin(), 
                                   bbox.zMax() - bbox.zMin());

    s_pShaderProg->setUniformValue("VolScale", 
                                   volScale.x(),
                                   volScale.y(),
                                   volScale.z());

    QVector3D volExtentMin = bbox.minimum() - volTranslation;
    volExtentMin.setX((volExtentMin.x() / volScale.x()) + 0.5f);
    volExtentMin.setY((volExtentMin.y() / volScale.y()) + 0.5f);
    volExtentMin.setZ((volExtentMin.z() / volScale.z()) + 0.5f);
    s_pShaderProg->setUniformValue("VolExtentMin", volExtentMin);

    QVector3D volExtentMax = bbox.maximum() - volTranslation;
    volExtentMax.setX((volExtentMax.x() / volScale.x()) + 0.5f);
    volExtentMax.setY((volExtentMax.y() / volScale.y()) + 0.5f);
    volExtentMax.setZ((volExtentMax.z() / volScale.z()) + 0.5f);
    s_pShaderProg->setUniformValue("VolExtentMax", volExtentMax);

    QVector3D cameraPosition = camera.getPosition() - volTranslation;
    cameraPosition.setX((cameraPosition.x() / volScale.x()) + 0.5f);
    cameraPosition.setY((cameraPosition.y() / volScale.y()) + 0.5f);
    cameraPosition.setZ((cameraPosition.z() / volScale.z()) + 0.5f);
    s_pShaderProg->setUniformValue("CameraPosition", cameraPosition);

    QVector3D lightPosition(0.0f, 100.0f, 0.0f);
    lightPosition.setX((lightPosition.x() / volScale.x()) + 0.5f);
    lightPosition.setY((lightPosition.y() / volScale.y()) + 0.5f);
    lightPosition.setZ((lightPosition.z() / volScale.z()) + 0.5f);
    s_pShaderProg->setUniformValue("LightPosition", lightPosition);

	bool computeLighting = getComputeLighting();
	s_pShaderProg->setUniformValue("ComputeLighting", computeLighting);

    vox::TextureIDArray& textureIDs = pVoxels->getTextureIDArray();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, textureIDs[0]);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureIDs[1]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	voxOpenGL::GLUtils::CheckOpenGLError();

    pVoxels->draw();

    s_pShaderProg->release();

	voxOpenGL::GLUtils::CheckOpenGLError();

    s_pCameraNearPlaneProg->bind();

    s_pCameraNearPlaneProg->setUniformValue("ProjectionMatrix", projMtx);
    s_pCameraNearPlaneProg->setUniformValue("ModelViewMatrix", modelViewMtx);
    s_pCameraNearPlaneProg->setUniformValue("InverseModelViewMatrix", modelViewMtx.inverted());
    s_pCameraNearPlaneProg->setUniformValue("RayStepSize", uvrStepSize);
    s_pCameraNearPlaneProg->setUniformValue("VolTranslation",
                                            volTranslation.x(),
                                            volTranslation.y(),
                                            volTranslation.z());
    s_pCameraNearPlaneProg->setUniformValue("VolScale", 
                                            volScale.x(),
                                            volScale.y(),
                                            volScale.z());
    s_pCameraNearPlaneProg->setUniformValue("VolExtentMin", volExtentMin);
    s_pCameraNearPlaneProg->setUniformValue("VolExtentMax", volExtentMax);
    s_pCameraNearPlaneProg->setUniformValue("CameraPosition", cameraPosition);
    s_pCameraNearPlaneProg->setUniformValue("LightPosition", lightPosition);
	s_pCameraNearPlaneProg->setUniformValue("ComputeLighting", computeLighting);

    /*bool runTest = false;
    if(runTest)
        rc::RayCasterShaderCodeTester::drawNearPlane(s_pCameraNearPlaneProg);*/

    voxOpenGL::GLUtils::DrawNearPlaneQuad();

    s_pCameraNearPlaneProg->release();

	voxOpenGL::GLUtils::CheckOpenGLError();

    glDisable(GL_STENCIL_TEST);
}
