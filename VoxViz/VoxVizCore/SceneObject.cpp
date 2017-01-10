#include "VoxVizCore/SceneObject.h"

#include "VoxVizOpenGL/GLExtensions.h"

using namespace vox;

SceneObject::SceneObject(const QVector3D& pos,
                         const QQuaternion& orient) : 
    m_vertexCount(0),
    m_primitiveType(GL_TRIANGLES),
    m_vaoID(0),
    m_vboID(0),
    m_position(pos), 
    m_orientation(orient),
    m_boundingSphereComputed(false),
    m_boundingBoxComputed(false)
{
    m_transform.translate(pos);
    m_transform.rotate(orient);
}

const QVector3D& SceneObject::getPosition() const
{
    return m_position;
}

const QQuaternion& SceneObject::getOrientation() const
{
    return m_orientation;
}

const QMatrix4x4& SceneObject::getTransform() const
{
    return m_transform;
}

const BoundingSphere& SceneObject::getBoundingSphere() const
{
    if(!m_boundingSphereComputed)
    {
        m_boundingSphere = computeBoundingSphere();

        m_boundingSphere.setCenter(m_boundingSphere.getCenter() + m_position);
                    
        m_boundingSphereComputed = true;
    }

    return m_boundingSphere;
}

const BoundingBox& SceneObject::getBoundingBox() const
{
    if(!m_boundingBoxComputed)
    {
        BoundingBox boundingBox = computeBoundingBox();

        QVector3D newMin = m_transform * boundingBox.minimum();
        QVector3D newMax = m_transform * boundingBox.maximum();

        m_boundingBox.init();
        m_boundingBox.expandBy(newMin);
        m_boundingBox.expandBy(newMax);
                    
        m_boundingBoxComputed = true;
    }

    return m_boundingBox;
}

void SceneObject::dirtyBounds()
{
    m_boundingSphereComputed = false;
    m_boundingBoxComputed = false;
}

void SceneObject::createVAO()
{
    if(m_vertices.size() == 0)
        return;

    glGenVertexArrays(1, &m_vaoID);
    
    glBindVertexArray(m_vaoID);

    glGenBuffers(1, &m_vboID);
    glBindBuffer(GL_ARRAY_BUFFER, m_vboID);

    float* pVtxPtr = &m_vertices.front();
    size_t vtxAttributesSize = sizeof(float) * 3;

    float* pVtxAttrs = pVtxPtr;

    float* pNormalsPtr = NULL;
    if(m_normals.size() > 0)
    {
        vtxAttributesSize += sizeof(float) * 3;
        pNormalsPtr = &m_normals.front();

        pVtxAttrs = new float[(m_vertices.size() + m_normals.size()) * 3];
        size_t attrIndex = 0;
        //interleave vertices and normals
        for(size_t i = 0; 
            i < m_vertices.size(); 
            i+=3)
        {
            memcpy(&pVtxAttrs[attrIndex], 
                   pVtxPtr, 
                   sizeof(float) * 3);

            pVtxPtr += 3;
            attrIndex += 3;
            if(pNormalsPtr != NULL)
            {
                memcpy(&pVtxAttrs[attrIndex], 
                       pNormalsPtr, 
                       sizeof(float) * 3);

                pNormalsPtr += 3;
                attrIndex += 3;
            }
        }
    }
    
    m_vertexCount = m_vertices.size() / 3;

    glBufferData(GL_ARRAY_BUFFER,
                 m_vertexCount * vtxAttributesSize, 
                 pVtxAttrs, 
                 GL_STATIC_DRAW);

    if(pNormalsPtr)
        delete [] pVtxAttrs;
    //{ FloatArray empty; empty.swap(m_vertices); }//release m_vertices memory
    //{ FloatArray empty; empty.swap(m_normals); }//release m_normals memory

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 
                          3, GL_FLOAT, GL_FALSE, 
                          vtxAttributesSize, 0);

    if(pNormalsPtr != NULL)
    {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 
                              3, GL_FLOAT, GL_FALSE, 
                              vtxAttributesSize, reinterpret_cast<void*>(sizeof(float) * 3));
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void SceneObject::draw()
{
    glBindVertexArray(m_vaoID);

    glDrawArrays(m_primitiveType, 0, m_vertexCount);

    glBindVertexArray(0);
}

SceneObject::~SceneObject()
{
    glDeleteBuffers(1, &m_vboID);
    glDeleteVertexArrays(1, &m_vaoID);
}
