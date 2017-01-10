#include "VolumeSlicer3D/VolumeSlicer3DRenderer.h"
#include "VoxVizCore/VolumeDataSet.h"
#include "VoxVizCore/BoundingVolumes.h"
#include "VoxVizCore/Plane.h"
#include "VoxVizCore/Ray.h"

#include "VoxVizOpenGL/GLExtensions.h"
#include "VoxVizOpenGL/GLShaderProgramManager.h"
#include "VoxVizOpenGL/GLUtils.h"

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

using namespace vs3D;

static const GLint ONE_GB = 1024000000;

void VolumeSlicer3DRenderer::RegisterRenderer()
{
	static vox::SmartPtr<VolumeSlicer3DRenderer> s_spRenderer = new VolumeSlicer3DRenderer();
}

static voxOpenGL::ShaderProgram* s_pShaderProg = NULL;

void VolumeSlicer3DRenderer::setup()
{
    //create shader program
    s_pShaderProg = voxOpenGL::ShaderProgramManager::instance().createShaderProgram("VolumeSlicer3D.vert",
                                                                                    "VolumeSlicer3D.frag");
}

static float s_sliceBBoxVerts[] = 
{
    //v0
    -0.5f, -0.5f, 0.5f,
    //v1
    0.5f, -0.5f, 0.5f,
    //v2
    -0.5f, -0.5f, -0.5f,
    //v3
    -0.5f, 0.5f, 0.5f,
    //v4
    0.5f, 0.5f, 0.5f,
    //v5
    0.5f, -0.5f, -0.5f,
    //v6
    -0.5f, 0.5f, -0.5f,
    //v7
    0.5f, 0.5f, -0.5f
};

int s_vertSequences[] =
{
    0, 1, 2, 3, 4, 5, 6, 7,
    1, 4, 5, 0, 3, 7, 2, 6,
    2, 5, 0, 6, 7, 1, 3, 4,
    3, 0, 6, 4, 1, 2, 7, 5,
    4, 3, 7, 1, 0, 6, 5, 2,
    5, 7, 1, 2, 6, 4, 0, 3,
    6, 2, 3, 7, 5, 0, 4, 1,
    7, 6, 4, 5, 2, 3, 1, 0
};

int s_vert1Indices[] =
{
    //p0 
    0, 1, 4, 4,
    //p1
    1, 0, 1, 4, 
    //p2
    0, 2, 5, 5,
    //p3
    2, 0, 2, 5,
    //p4
    0, 3, 6, 6,
    //p5
    3, 0, 3, 6
};

int s_vert2Indices[] =
{
    //p0
    1, 4, 7, 7,
    //p1
    5, 1, 4, 7,
    //p2
    2, 5, 7, 7,
    //p3
    6, 2, 5, 7,
    //p4
    3, 6, 7, 7,
    //p5
    4, 3, 6, 7
};

typedef QVector3D vec3;

QVector3D VertexShaderTest(const QVector3D& gl_Vertex,
                      const QVector3D& vecTranslate,
                      const QVector3D& vecScale,
                      size_t frontIdx,
                      const QVector3D& vecView)
{
    int vtxIndex = int(gl_Vertex.x());
    float planeD = gl_Vertex.y();

    vec3 Position = vec3(gl_Vertex.x(), gl_Vertex.y(), 0.0);

    int e = 0;
    for( ; e < 4; ++e)
    {
        int vidx1 = s_vertSequences[int(frontIdx * 8 + s_vert1Indices[vtxIndex * 4 + e])];
        int vidx2 = s_vertSequences[int(frontIdx * 8 + s_vert2Indices[vtxIndex * 4 + e])];

        vec3 vecV1 = vec3(s_sliceBBoxVerts[vidx1*3], s_sliceBBoxVerts[vidx1*3 + 1], s_sliceBBoxVerts[vidx1*3 + 2]);
        vec3 vecV2 = vec3(s_sliceBBoxVerts[vidx2*3], s_sliceBBoxVerts[vidx2*3 + 1], s_sliceBBoxVerts[vidx2*3 + 2]);

        vec3 rayStart = vecV1 * vecScale;
        rayStart += vecTranslate;

        vec3 rayEnd = vecV2 * vecScale;
        rayEnd += vecTranslate;
        
        vec3 rayDir = rayEnd - rayStart;
        
        float denom = QVector3D::dotProduct(rayDir, vecView);
        
        float lambda = (denom != 0) ? 
            /*1.0 - */((planeD - QVector3D::dotProduct(rayStart, vecView)) / denom) : -1.0;

        if(lambda >= 0.0 && lambda <= 1.0)
        {
            Position = rayStart + lambda * rayDir;
            break;
        }
    }

    if(e == 4)
        std::cout << "No intersection?" << std::endl;

    //vec3 texCoord = Position - vecTranslate;
    //texCoord.setX(texCoord.x() / vecScale.x());
    //texCoord.setY(texCoord.y() / vecScale.y());
    //texCoord.setZ(texCoord.z() / vecScale.z());

    //QVector3D str = vec3(texCoord.x() + 0.5, texCoord.y() + 0.5, texCoord.z() + 0.5);

    //return str;
    return Position;
}

void VolumeSlicer3DRenderer::init(vox::Camera& camera,
                                 vox::SceneObject& scene)
{
    vox::VolumeDataSet* pVoxels = dynamic_cast<vox::VolumeDataSet*>(&scene);

    if(pVoxels == NULL)
        return;

    m_numSlicePlanes = pVoxels->getNumSamples();
    if(m_numSlicePlanes == 0)
    {
        m_numSlicePlanes = static_cast<size_t>(std::sqrt(static_cast<float>(
                                   (pVoxels->dimX() * pVoxels->dimX())
                                 + (pVoxels->dimY() * pVoxels->dimY())
                                 + (pVoxels->dimZ() * pVoxels->dimZ())))) + 2;
        if(m_numSlicePlanes < 32)
            m_numSlicePlanes = 32;
    }

    if(s_pShaderProg->bind())
    {
        s_pShaderProg->setUniformValueArray("nSequence", s_vertSequences, 64);

        s_pShaderProg->setUniformValueArray("vecVertices", s_sliceBBoxVerts, 8, 3);

        //index of start vert for edge test
        //for even number points the last index is not used
        //so we just repeat the last two verts
        s_pShaderProg->setUniformValueArray("v1", s_vert1Indices, 24);

        //index of end vert for edge test
        s_pShaderProg->setUniformValueArray("v2", s_vert2Indices, 24);

        s_pShaderProg->setUniformValue("VoxelSampler", 0);
        //s_pShaderProg->setUniformValue("ColorLUTSampler", 1);

        s_pShaderProg->release();
    }

    vox::TextureIDArray& textureIDs = pVoxels->getTextureIDArray();

    textureIDs.reserve(1);
    textureIDs.resize(1);

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

        textureIDs[0] = voxOpenGL::GLUtils::Create3DTexture(internalFormat,
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
        }

		voxOpenGL::GLUtils::CheckOpenGLError();
    }
    else
    {
        vox::Vec4ub* pVoxelColors = pVoxels->getColorsUB();
        if(pVoxelColors == NULL)
        {
            qreal intensity = 128.0/255.0;
            qreal alpha = 128.0/255.0;
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
    }//textureIDs[1] = voxOpenGL::GLUtils::Create1DTexture(colorLUT, static_cast<int>(sizeof(colorLUT) / (sizeof(unsigned char) * 4.0f)));

    //create the slice plane geometry
    vox::FloatArray& vertexArray = pVoxels->getFloatVertexArray();
    for(size_t plane = 0; plane < m_numSlicePlanes; ++plane)
    {
        vertexArray.push_back(0.0f);
        vertexArray.push_back(plane);

        vertexArray.push_back(1.0f);
        vertexArray.push_back(plane);
        
        vertexArray.push_back(2.0f);
        vertexArray.push_back(plane);
        
        vertexArray.push_back(3.0f);
        vertexArray.push_back(plane);
        
        vertexArray.push_back(4.0f);
        vertexArray.push_back(plane);

        vertexArray.push_back(5.0f);
        vertexArray.push_back(plane);
    }
    
    glEnableClientState(GL_VERTEX_ARRAY);

	voxOpenGL::GLUtils::CheckOpenGLError();
}

static inline bool CheckVtxDist(const QVector3D& vtx, 
                                const QVector3D& eye, 
                                double& minDistSq)
{
    double distSq = (vtx - eye).lengthSquared();
    if(distSq < minDistSq)
    {
        minDistSq = distSq;

        return true;
    }

    return false;
}

static inline double ComputePlaneIncrement(const QVector3D& pt1,
                                          const QVector3D& pt2,
                                          const QVector3D& camLook,
                                          size_t numSlicePlanes)
{
    vox::Plane frontPlane(pt1, camLook);

    double dist = frontPlane.eval(pt2);

    return dist / (double)numSlicePlanes;
}

static void Print(const QVector3D& v)
{
    std::cout << v.x() << ", " << v.y() << ", " << v.z() << std::endl;
}

void VolumeSlicer3DRenderer::draw(vox::Camera& camera,
                                 vox::SceneObject& scene)
{
    vox::VolumeDataSet* pVoxels = dynamic_cast<vox::VolumeDataSet*>(&scene);

    if(pVoxels == NULL)
        return;

	voxOpenGL::GLUtils::CheckOpenGLError();

    const vox::BoundingBox& sliceBBox = pVoxels->getBoundingBox();
    //vox::BoundingBox sliceBBox = pVoxels->getBoundingBox();
    //vox::BoundingSphere bsphere = pVoxels->getBoundingSphere();
    //bsphere.setRadius(bsphere.getRadius() * 2.0f);
    //sliceBBox.expandBy(bsphere);

    glDisable(GL_CULL_FACE);

    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    //glColor4ub(255, 255, 255, 255);

    //glBegin(GL_QUADS);

    ////front
    //glVertex3d(sliceBBox.xMin(), sliceBBox.yMin(), sliceBBox.zMax());
    //glVertex3d(sliceBBox.xMax(), sliceBBox.yMin(), sliceBBox.zMax());
    //glVertex3d(sliceBBox.xMax(), sliceBBox.yMax(), sliceBBox.zMax());
    //glVertex3d(sliceBBox.xMin(), sliceBBox.yMax(), sliceBBox.zMax());

    ////back
    //glVertex3d(sliceBBox.xMin(), sliceBBox.yMin(), sliceBBox.zMin());
    //glVertex3d(sliceBBox.xMax(), sliceBBox.yMin(), sliceBBox.zMin());
    //glVertex3d(sliceBBox.xMax(), sliceBBox.yMax(), sliceBBox.zMin());
    //glVertex3d(sliceBBox.xMin(), sliceBBox.yMax(), sliceBBox.zMin());

    ////right
    //glVertex3d(sliceBBox.xMin(), sliceBBox.yMin(), sliceBBox.zMin());
    //glVertex3d(sliceBBox.xMin(), sliceBBox.yMax(), sliceBBox.zMin());
    //glVertex3d(sliceBBox.xMin(), sliceBBox.yMax(), sliceBBox.zMax());
    //glVertex3d(sliceBBox.xMin(), sliceBBox.yMin(), sliceBBox.zMax());

    ////left
    //glVertex3d(sliceBBox.xMax(), sliceBBox.yMin(), sliceBBox.zMin());
    //glVertex3d(sliceBBox.xMax(), sliceBBox.yMax(), sliceBBox.zMin());
    //glVertex3d(sliceBBox.xMax(), sliceBBox.yMax(), sliceBBox.zMax());
    //glVertex3d(sliceBBox.xMax(), sliceBBox.yMin(), sliceBBox.zMax());

    //glEnd();

    //glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    //glEnable(GL_CULL_FACE);

    if(!s_pShaderProg->bind())
        return;

	voxOpenGL::GLUtils::CheckOpenGLError();

    //translation
    QVector3D center = sliceBBox.center();
    s_pShaderProg->setUniformValue("vecTranslate", 
                                    center.x(), 
                                    center.y(), 
                                    center.z());
    //scale
    QVector3D scale = QVector3D(sliceBBox.xMax() - sliceBBox.xMin(), 
                                sliceBBox.yMax() - sliceBBox.yMin(), 
                                sliceBBox.zMax() - sliceBBox.zMin());

    s_pShaderProg->setUniformValue("vecScale", 
                                    scale.x(),
                                    scale.y(),
                                    scale.z());

    //const vox::BoundingBox& bbox = pVoxels->getBoundingBox();
    //QVector3D uvScale = QVector3D(bbox.xMax() - bbox.xMin(), 
    //                              bbox.yMax() - bbox.yMin(), 
    //                              bbox.zMax() - bbox.zMin());

    ////uvScale.setX(scale.x() / uvScale.x());
    ////uvScale.setY(scale.y() / uvScale.y());
    ////uvScale.setZ(scale.z() / uvScale.z());

    //s_pShaderProg->setUniformValue("vecTexCoordScale",
    //                               uvScale.x(),
    //                               uvScale.y(),
    //                               uvScale.z());

    QVector3D camPos = camera.getPosition();
    const QVector3D& camLook = camera.getLook();

    QVector3D verts[] = 
    {
        QVector3D(sliceBBox.xMin(), sliceBBox.yMin(), sliceBBox.zMax()),
        QVector3D(sliceBBox.xMax(), sliceBBox.yMin(), sliceBBox.zMax()),
        sliceBBox.minimum(),
        QVector3D(sliceBBox.xMin(), sliceBBox.yMax(), sliceBBox.zMax()),
        sliceBBox.maximum(),
        QVector3D(sliceBBox.xMax(), sliceBBox.yMin(), sliceBBox.zMin()),
        QVector3D(sliceBBox.xMin(), sliceBBox.yMax(), sliceBBox.zMin()),
        QVector3D(sliceBBox.xMax(), sliceBBox.yMax(), sliceBBox.zMin())
    };

    static bool recompute = true;
    
    static int curFrontIndex = -1;
    static float curPlaneIncr;
    static float curHalfPlaneIncr;
    static QVector3D curUp;
    static QVector3D curLeft;
    static QVector3D curLook;

    if(recompute)
    {
        curLook = camera.getLook();
        curUp = camera.getUp();
        curLeft = camera.getLeft();

        //find the closest point and the plane D (from plane equation)
        //for the first and last plane
        const QVector3D& v0 = verts[0];
        const QVector3D& v1 = verts[1];
        const QVector3D& v2 = verts[2];
        const QVector3D& v3 = verts[3];
        const QVector3D& v4 = verts[4];
        const QVector3D& v5 = verts[5];
        const QVector3D& v6 = verts[6];
        const QVector3D& v7 = verts[7];

        int frontIndex = 0;
        int backIndex = 7;

        double minDistSq = (v0 - camPos).lengthSquared();

        if(CheckVtxDist(v1, camPos, minDistSq))
        {
            frontIndex = 1;
            backIndex = 6;
        }

        if(CheckVtxDist(v2, camPos, minDistSq))
        {
            frontIndex = 2;
            backIndex = 4;
        }

        if(CheckVtxDist(v3, camPos, minDistSq))
        {
            frontIndex = 3;
            backIndex = 5;
        }

        if(CheckVtxDist(v4, camPos, minDistSq))
        {
            frontIndex = 4;
            backIndex = 2;
        }

        if(CheckVtxDist(v5, camPos, minDistSq))
        {
            frontIndex = 5;
            backIndex = 3;
        }

        if(CheckVtxDist(v6, camPos, minDistSq))
        {
            frontIndex = 6;
            backIndex = 1;
        }

        if(CheckVtxDist(v7, camPos, minDistSq))
        {
            frontIndex = 7;
            backIndex = 0;
        }

        //bool doTest = false;
        if(curFrontIndex != frontIndex)
        {
            //std::cout << "FrontIndex=" << frontIndex << std::endl;
            curFrontIndex = frontIndex;

            //doTest = true;
        }

        s_pShaderProg->setUniformValue("frontIdx", frontIndex);

        s_pShaderProg->setUniformValue("vecView", camLook);

        double planeIncr = ComputePlaneIncrement(verts[frontIndex], 
                                                verts[backIndex], 
                                                camLook,
                                                m_numSlicePlanes);
        double halfPlaneIncr = planeIncr * 0.5;

        curPlaneIncr = planeIncr;
        curHalfPlaneIncr = halfPlaneIncr;

        vox::FloatArray& vertexArray = pVoxels->getFloatVertexArray();
    
        for(size_t plane = 0; plane < m_numSlicePlanes; ++plane)
        {
            size_t index = plane * 12;
        
            QVector3D ptOnPlane = verts[frontIndex] + (camLook * (((double)plane * planeIncr) + halfPlaneIncr));

            vox::Plane curPlane(ptOnPlane, camLook);

            float curPlaneD = -curPlane.planeD();
            //plane D is stored in second float
            vertexArray[index+1] = curPlaneD;
            vertexArray[index+3] = curPlaneD;
            vertexArray[index+5] = curPlaneD;
            vertexArray[index+7] = curPlaneD;
            vertexArray[index+9] = curPlaneD;
            vertexArray[index+11] = curPlaneD;
        }
    }

    const QMatrix4x4& projMtx = camera.getProjectionMatrix();
    //glGetDoublev(GL_PROJECTION_MATRIX, projMtx.data());

    s_pShaderProg->setUniformValue("ProjectionMatrix", projMtx);

    const QMatrix4x4& modelViewMtx = camera.getViewMatrix();
    //glGetDoublev(GL_MODELVIEW_MATRIX, modelViewMtx.data());

    s_pShaderProg->setUniformValue("ModelViewMatrix", modelViewMtx);

    vox::TextureIDArray& textureIDs = pVoxels->getTextureIDArray();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, textureIDs[0]);
    
    //glActiveTexture(GL_TEXTURE1);
    //glBindTexture(GL_TEXTURE_1D, textureIDs[1]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);    
    //glEnable(GL_ALPHA_TEST);

    vox::FloatArray& vertexArray = pVoxels->getFloatVertexArray();
    GLfloat* pVertexPtr = &vertexArray.front();
    glVertexPointer(2, GL_FLOAT, 0, pVertexPtr);

    //static size_t plane = 3;

    for(size_t plane = m_numSlicePlanes; plane > 0; --plane)
    {
        size_t index = (plane-1) * 6;
        
        glDrawArrays(GL_POLYGON, index, 6);
    }

    s_pShaderProg->release();

	voxOpenGL::GLUtils::CheckOpenGLError();

    //glDisable(GL_BLEND);
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    //
    //GLfloat ambAndDiff[] = { 0.0f, 1.0, 0.0, 1.0 };
    //glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, 
    //             ambAndDiff);
    //for(size_t plane = m_numSlicePlanes; plane > 0; --plane)
    //{
    //    size_t index = (plane-1) * 12;
    //    
    //    QVector3D ptOnPlane = verts[curFrontIndex] + (curLook * (((double)(plane-1) * curPlaneIncr) + curHalfPlaneIncr));

    //    const QVector3D& up = curUp;

    //    const QVector3D& left = curLeft;

    //    QVector3D upperLeft = ptOnPlane + (up * 3.5) + (left * 3.5);
    //    QVector3D lowerLeft = ptOnPlane + (up * -3.5) + (left * 3.5);
    //    QVector3D upperRight = ptOnPlane + (up * 3.5) + (left * -3.5);
    //    QVector3D lowerRight = ptOnPlane + (up * -3.5) + (left * -3.5);

    //    glBegin(GL_QUADS);

    //    glVertex3d(upperLeft.x(), upperLeft.y(), upperLeft.z());
    //    glVertex3d(lowerLeft.x(), lowerLeft.y(), lowerLeft.z());
    //    glVertex3d(lowerRight.x(), lowerRight.y(), lowerRight.z());
    //    glVertex3d(upperRight.x(), upperRight.y(), upperRight.z());

    //    glEnd();
    //}

    //for(size_t plane = m_numSlicePlanes; 
    //    plane > 0; 
    //    --plane)
    //{
    //    glBegin(GL_POLYGON);

    //    size_t index = (plane-1) * 12;
    //    std::cout << "Plane " << plane << std::endl;

    //    QVector3D vtx = VertexShaderTest(QVector3D(vertexArray[index], vertexArray[index+1], 0),
    //                                     center, scale, curFrontIndex, curLook);
    //    glVertex3d(vtx.x(), vtx.y(), vtx.z());

    //    vtx = VertexShaderTest(QVector3D(vertexArray[index+2], vertexArray[index+3], 0),
    //                    center, scale, curFrontIndex, curLook);
    //    glVertex3d(vtx.x(), vtx.y(), vtx.z());

    //    vtx = VertexShaderTest(QVector3D(vertexArray[index+4], vertexArray[index+5], 0),
    //                    center, scale, curFrontIndex, curLook);
    //    glVertex3d(vtx.x(), vtx.y(), vtx.z());

    //    vtx = VertexShaderTest(QVector3D(vertexArray[index+6], vertexArray[index+7], 0),
    //                    center, scale, curFrontIndex, curLook);
    //    glVertex3d(vtx.x(), vtx.y(), vtx.z());

    //    vtx = VertexShaderTest(QVector3D(vertexArray[index+8], vertexArray[index+9], 0),
    //                    center, scale, curFrontIndex, curLook);
    //    glVertex3d(vtx.x(), vtx.y(), vtx.z());

    //    vtx = VertexShaderTest(QVector3D(vertexArray[index+10], vertexArray[index+11], 0),
    //                    center, scale, curFrontIndex, curLook);
    //    glVertex3d(vtx.x(), vtx.y(), vtx.z());

    //    glEnd();
    //}
}
