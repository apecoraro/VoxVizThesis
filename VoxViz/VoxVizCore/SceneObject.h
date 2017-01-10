#ifndef VOX_SCENE_OBJECT_H
#define VOX_SCENE_OBJECT_H

#include "VoxVizCore/Referenced.h"
#include "VoxVizCore/SmartPtr.h"
//#include "VoxVizCore/Controller.h"
#include "VoxVizCore/Renderer.h"
#include "VoxVizCore/Array.h"
#include "VoxVizCore/BoundingVolumes.h"

#include <QtGui/qvector3d.h>
#include <QtGui/qquaternion.h>
#include <QtGui/qmatrix4x4.h>

namespace vox
{
    class Renderer;

    class SceneObject : public Referenced
    {
    private:
        FloatArray m_vertices;
        //IntArray m_intVerts;
        FloatArray m_normals;
        //FloatArray m_colors;
        //FloatArray m_primTexCoords;
        //FloatArray m_secTexCoords;
        TextureIDArray m_textures;
        size_t m_vertexCount;
        unsigned int m_primitiveType;

        unsigned int m_vaoID;
        unsigned int m_vboID;

        QVector3D m_position;
        QQuaternion m_orientation;
        QMatrix4x4 m_transform;

        mutable BoundingSphere m_boundingSphere;
        mutable BoundingBox m_boundingBox;

        mutable bool m_boundingSphereComputed;
        mutable bool m_boundingBoxComputed;

        SmartPtr<Renderer> m_spRenderer;
        SmartPtr<Referenced> m_spUserData;
        
    public:
        SceneObject(const QVector3D& pos,
                    const QQuaternion& orient);

        const QVector3D& getPosition() const;
        void setPosition(const QVector3D& pos) { m_position = pos; }
        const QQuaternion& getOrientation() const;
        void setOrientation(const QQuaternion& orient) { m_orientation; }
        const QMatrix4x4& getTransform() const;

        const BoundingSphere& getBoundingSphere() const;
        void  setBoundingSphere(const BoundingSphere& sphere) { m_boundingSphere = sphere; m_boundingSphereComputed = true; }
        const BoundingBox& getBoundingBox() const;
        void setBoundingBox(const BoundingBox& bbox) { m_boundingBox = bbox; m_boundingBoxComputed = true; }
        void dirtyBounds();

    protected:
        virtual BoundingSphere computeBoundingSphere() const { return m_boundingSphere; }
        virtual BoundingBox computeBoundingBox() const { return m_boundingBox; }

    public:
        void createVAO();
        void draw();

        /*Controller* getController() { return m_spController.get(); }
        const Controller* getController() const { return m_spController.get(); }
        void setController(Controller* pController) { m_spController = pControlller; }*/

        Renderer* getRenderer() { return m_spRenderer.get(); }
        const Renderer* getRenderer() const { return m_spRenderer.get(); }
        void setRenderer(Renderer* pRenderer) { m_spRenderer = pRenderer; }

        void setPrimitiveType(unsigned int ptype) { m_primitiveType = ptype; }
        FloatArray& getFloatVertexArray() { return m_vertices; }
        //IntArray& getIntVertexArray() { return m_intVerts; }
        FloatArray& getNormalArray() { return m_normals; }
        //FloatArray& getColorArray() { return m_colors; }
        //FloatArray& getPrimaryTexCoordArray() { return m_primTexCoords; }
        //FloatArray& getSecondaryTexCoordArray() { return m_secTexCoords; }
        TextureIDArray& getTextureIDArray() { return m_textures; }

        void setUserData(Referenced* pUserData) { m_spUserData = pUserData; }
        Referenced* getUserData() { return m_spUserData.get(); }
        const Referenced* getUserData() const { return m_spUserData.get(); }
    protected:
        virtual ~SceneObject();
    };
}

#endif
