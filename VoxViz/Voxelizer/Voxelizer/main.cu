#include "Voxelizer.h"

#include "VoxelBrickWriter.h"

#include <cmath>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>

#include <osg/ArgumentParser>
#include <osg/Geometry>
#include <osg/PrimitiveSet>
#include <osg/Node>
#include <osg/Matrix>
#include <osg/MatrixTransform>
#include <osg/TriangleFunctor>
#include <osg/ValueObject>
#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>
#include <osgDB/ReadFile>
#include <osg/NodeVisitor>
#include <osg/Geode>
#include <osg/Texture2D>
#include <osg/PagedLOD>
#include <osg/ProxyNode>
#include <osg/ref_ptr>
#include <osgDB/Registry>
#include <osgDB/ReaderWriter>
#include <osgUtil/SmoothingVisitor>

#define GLM_FORCE_CUDA
#include <glm/glm.hpp>

class Primitive
{
public:
    
    Primitive() : m_worldTransformIsIdentity(true),  m_faceNormalDirty(true){}
    
    ~Primitive() {}
        
    osg::Geometry* getGeometry() { return m_spGeometry.get(); }
    const osg::Geometry* getGeometry() const { return m_spGeometry.get(); }
        
    osg::PrimitiveSet* getPrimitiveSet()
    {
        return m_spPrimSet.get();
    }
        
    void setGeometry(osg::Geometry* newGeometry)
    {
        m_spGeometry = newGeometry;
    }
        
    void setPrimitiveSet(osg::PrimitiveSet* primSet)
    {
        m_spPrimSet = primSet;
    }
    
    void setIndices(unsigned int index1,
                    unsigned int index2,
                    unsigned int index3)
    {
        m_indices.clear();
        m_indices.push_back(index1);
        m_indices.push_back(index2);
        m_indices.push_back(index3);
    }

    unsigned int getVertexIndex(unsigned int pos) const
    {
        return m_indices[pos];
    }
    
    void setIndices(unsigned int index1,
                    unsigned int index2,
                    unsigned int index3,
                    unsigned int index4)
    {
        m_indices.clear();
        m_indices.push_back(index1);
        m_indices.push_back(index2);
        m_indices.push_back(index3);
        m_indices.push_back(index4);
    }

    void setWorldTransform(osg::RefMatrix* pWorldTransform)
    {
        if(!(m_worldTransformIsIdentity = pWorldTransform->isIdentity()))
        {
            m_spWorldTransform = pWorldTransform;
            m_faceNormalDirty = true;
        }
    }

    void setWorldTransform(const osg::Matrix& worldTransform)
    {
        if(!(m_worldTransformIsIdentity = worldTransform.isIdentity()))
        {
            m_spWorldTransform = new osg::RefMatrix(worldTransform);
            m_faceNormalDirty = true;
        }
    }

    const osg::RefMatrix* getWorldTransform() const { return m_spWorldTransform.get(); }
    
    void setPrimSetIndex(int primSetIndex) { m_primSetIndex = primSetIndex; }
        
    unsigned int getPrimSetIndex() const { return m_primSetIndex; }
    
    bool getVertex(unsigned int index, osg::Vec3& vtx) const;
    osg::Vec3 getVertex(unsigned int index) const;
    
    unsigned int getVtxCount() const { return m_indices.size(); }
    
    bool hasColorData() const
    {
        if(!m_spGeometry.valid())
            return false;
    
        return m_spGeometry->getColorArray() != NULL &&
                m_spGeometry->getColorArray()->getNumElements() > 0;
    }
    
    osg::Geometry::AttributeBinding getColorBinding() const
    {
        if(!m_spGeometry.valid())
            return osg::Geometry::BIND_OFF;
    
        return m_spGeometry->getColorBinding();
    }
    
    bool getColor(unsigned int index, osg::Vec4& color) const;
    osg::Vec4ub getColor(unsigned int index) const
    {
        osg::Vec4ub ret(UCHAR_MAX, 
                        UCHAR_MAX, 
                        UCHAR_MAX, 
                        UCHAR_MAX);
        osg::Vec4 rgba;
        if(getColor(index, rgba))
        {
            ret.r() = static_cast<unsigned char>(rgba.r() * 255.0f);
            ret.g() = static_cast<unsigned char>(rgba.g() * 255.0f);
            ret.b() = static_cast<unsigned char>(rgba.b() * 255.0f);
            ret.a() = static_cast<unsigned char>(rgba.a() * 255.0f);               
        }

        return ret;
    }

    bool hasNormalData() const
    {
        if(!m_spGeometry.valid())
            return false;
    
        return m_spGeometry->getNormalArray() != NULL &&
                m_spGeometry->getNormalArray()->getNumElements() > 0;
    }
    
    osg::Geometry::AttributeBinding getNormalBinding() const
    {
        if(!m_spGeometry.valid())
            return osg::Geometry::BIND_OFF;
    
        return m_spGeometry->getNormalBinding();
    }
    
    bool getNormal(unsigned int index, osg::Vec3& normal) const;
    osg::Vec3 getNormal(unsigned int index) const
    {
        osg::Vec3 ret(0.0f, 0.0f, 1.0f);
        getNormal(index, ret);

        return ret;
    }
    
    bool hasTextureData() const
    {
        if(!m_spGeometry.valid())
            return false;
    
        return m_spGeometry->getNumTexCoordArrays() > 0 &&
                m_spGeometry->getTexCoordArray(0) != NULL && 
                m_spGeometry->getTexCoordArray(0)->getNumElements() > 0;
    }

    unsigned int getNumTextureCoordArrays() const
    {
        return m_spGeometry->getNumTexCoordArrays();
    }
    
    bool getTexCoord(unsigned int texCoordArrayIndex, unsigned int index, osg::Vec2& texCoord) const;
    osg::Vec2 getTexCoord(unsigned int texCoordArrayIndex, unsigned int index) const
    {
        osg::Vec2 ret;
        getTexCoord(texCoordArrayIndex, index, ret);

        return ret;
    }
    
    const osg::Vec3& getFaceNormal() const
    {
        if(m_faceNormalDirty)
        {
            calcFaceNormal();
            m_faceNormalDirty = false;
        }
        return m_faceNormal;
    }
    
    void deleteMe();
     
private:
    
    void calcFaceNormal() const
    {
        osg::Vec3 vtx0;
        getVertex(0, vtx0);
            
        osg::Vec3 vtx1;
        getVertex(1, vtx1);
            
        osg::Vec3 vtx2;
        getVertex(2, vtx2);

        m_faceNormal = calcNormal(vtx0, vtx1, vtx2);
    }
    
    osg::Vec3 calcNormal(const osg::Vec3& v0, const osg::Vec3& v1, const osg::Vec3& v2) const
    {
        float ux, uy, uz, vx, vy, vz;
    
        ux = v1.x() - v0.x();
        uy = v1.y() - v0.y();
        uz = v1.z() - v0.z();
        vx = v2.x() - v0.x();
        vy = v2.y() - v0.y();
        vz = v2.z() - v0.z();
    
        osg::Vec3 norm(uy*vz - uz*vy,
                        uz*vx - ux*vz,
                        ux*vy - uy*vx);
            
        float oneOverLen = 1.0f/norm.length();
    
        norm.x() *= oneOverLen;           
        norm.y() *= oneOverLen;           
        norm.z() *= oneOverLen;           
    
        return norm;
    }
    
    osg::ref_ptr<osg::Geometry> m_spGeometry;
    osg::ref_ptr<osg::PrimitiveSet> m_spPrimSet;
    osg::ref_ptr<osg::RefMatrix> m_spWorldTransform;
    
    std::vector<unsigned int> m_indices;
        
    unsigned int m_primSetIndex;           //index of polygon in primitive set
    
    bool m_worldTransformIsIdentity;
    mutable bool m_faceNormalDirty;
    mutable osg::Vec3 m_faceNormal;
};
    
class PrimitiveSetIterator : public osg::Referenced
{
public:
    
    PrimitiveSetIterator();
    
    virtual ~PrimitiveSetIterator();
    
    virtual void init(osg::Geometry* searchGeometry, osg::PrimitiveSet* searchPrimSet)=0;
    void reset()
    {
        m_spGeometry = NULL;
        m_spPrimSet = NULL;
        m_numberOfPolys = 0;
    }
    /* virtual method implemented by individual instances that inherit 
        this class.Returns the last created RequestedPoly data structure */
    virtual bool getPoly(unsigned int index, Primitive& poly) = 0;
    
    unsigned int getNumberOfPolys() const
    {
        return m_numberOfPolys;
    }

    unsigned int getNumberOfVerticesPerPoly() const
    {
        Primitive poly;
        const_cast<PrimitiveSetIterator*>(this)->getPoly(0, poly);

        return poly.getVtxCount();
    }

    static PrimitiveSetIterator* getIterator(osg::PrimitiveSet* primitiveSet, osg::Geometry* geometry);
    static unsigned int getNumberOfSupportedModes() { return 5; }
    static unsigned int getNumberOfSupportedTypes() { return 5; }
    
    const osg::Geometry* getGeometry() const { return m_spGeometry.get(); }
    const osg::PrimitiveSet* getPrimitiveSet() const { return m_spPrimSet.get(); }
protected:
    
    //osg::Geometry* geometry;
    osg::ref_ptr<osg::Geometry> m_spGeometry;
    osg::ref_ptr<osg::PrimitiveSet> m_spPrimSet;
    unsigned int m_numberOfPolys;
};

class TriangleDrawArraysIterator : public PrimitiveSetIterator
{
  public:

    TriangleDrawArraysIterator() {};
    
    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static TriangleDrawArraysIterator* TriangleDrawArraysIteratorInit(osg::Geometry* searchGeometry, osg::PrimitiveSet* primSet)
{
    TriangleDrawArraysIterator* triDrawArraysItr = new TriangleDrawArraysIterator();
    triDrawArraysItr->init(searchGeometry, primSet);
    return triDrawArraysItr;
}


class TriangleDrawArrayLengthsIterator : public PrimitiveSetIterator
{
  public:

    TriangleDrawArrayLengthsIterator() {};
    
    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);

};

static TriangleDrawArrayLengthsIterator* TriangleDrawArrayLengthsIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    TriangleDrawArrayLengthsIterator* triDrawArrayLengthsItr = new TriangleDrawArrayLengthsIterator();
    triDrawArrayLengthsItr->init(geom, primSet);
    return triDrawArrayLengthsItr;
}

class TriangleDrawElementsIterator : public PrimitiveSetIterator
{
  public:

    TriangleDrawElementsIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};


static TriangleDrawElementsIterator* TriangleDrawElementsIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    TriangleDrawElementsIterator* triDrawElemsItr = new TriangleDrawElementsIterator();
    triDrawElemsItr->init(geom, primSet);
    return triDrawElemsItr;
}

class TriangleStripDrawArraysIterator : public PrimitiveSetIterator
{
  public:

    TriangleStripDrawArraysIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static TriangleStripDrawArraysIterator* TriangleStripDrawArraysIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    TriangleStripDrawArraysIterator* triStripDrawArraysItr = new TriangleStripDrawArraysIterator();
    triStripDrawArraysItr->init(geom, primSet);
    return triStripDrawArraysItr;
}

class TriangleStripDrawArrayLengthsIterator : public PrimitiveSetIterator
{
  public:

    TriangleStripDrawArrayLengthsIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static TriangleStripDrawArrayLengthsIterator* TriangleStripDrawArrayLengthsIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    TriangleStripDrawArrayLengthsIterator* triStripDrawArrayLengthsItr = new TriangleStripDrawArrayLengthsIterator();
    triStripDrawArrayLengthsItr->init(geom, primSet);
    return triStripDrawArrayLengthsItr;
}

class TriangleStripDrawElementsIterator : public PrimitiveSetIterator
{
  public:

    TriangleStripDrawElementsIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};


static TriangleStripDrawElementsIterator* TriangleStripDrawElementsIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    TriangleStripDrawElementsIterator* triStripDrawElemsItr = new TriangleStripDrawElementsIterator();
    triStripDrawElemsItr->init(geom, primSet);
    return triStripDrawElemsItr;
}

class TriangleFanDrawArraysIterator : public PrimitiveSetIterator
{
  public:

    TriangleFanDrawArraysIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);

};

static TriangleFanDrawArraysIterator* TriangleFanDrawArraysIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    TriangleFanDrawArraysIterator* triFanDrawArraysItr = new TriangleFanDrawArraysIterator();
    triFanDrawArraysItr->init(geom, primSet);
    return triFanDrawArraysItr;
}

class TriangleFanDrawArrayLengthsIterator : public PrimitiveSetIterator
{
  public:

    TriangleFanDrawArrayLengthsIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static TriangleFanDrawArrayLengthsIterator* TriangleFanDrawArrayLengthsIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    TriangleFanDrawArrayLengthsIterator* triFanDrawArrayLengthsItr = new TriangleFanDrawArrayLengthsIterator();
    triFanDrawArrayLengthsItr->init(geom, primSet);
    return triFanDrawArrayLengthsItr;
}

class TriangleFanDrawElementsIterator : public PrimitiveSetIterator
{
  public:

    TriangleFanDrawElementsIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static TriangleFanDrawElementsIterator* TriangleFanDrawElementsIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    TriangleFanDrawElementsIterator* triFanDrawElemsItr = new TriangleFanDrawElementsIterator();
    triFanDrawElemsItr->init(geom, primSet);
    return triFanDrawElemsItr;
}

class QuadDrawArraysIterator : public PrimitiveSetIterator
{
  public:

    QuadDrawArraysIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static QuadDrawArraysIterator* QuadDrawArraysIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    QuadDrawArraysIterator* quadDrawArraysItr = new QuadDrawArraysIterator();
    quadDrawArraysItr->init(geom, primSet);
    return quadDrawArraysItr;
}

class QuadDrawArrayLengthsIterator : public PrimitiveSetIterator
{
  public:

    QuadDrawArrayLengthsIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static QuadDrawArrayLengthsIterator* QuadDrawArrayLengthsIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    QuadDrawArrayLengthsIterator* quadDrawArrayLengthsItr = new QuadDrawArrayLengthsIterator();
    quadDrawArrayLengthsItr->init(geom, primSet);
    return quadDrawArrayLengthsItr;
}

class QuadDrawElementsIterator : public PrimitiveSetIterator
{
  public:

    QuadDrawElementsIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static QuadDrawElementsIterator* QuadDrawElementsIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    QuadDrawElementsIterator* quadDrawElemsItr = new QuadDrawElementsIterator();
    quadDrawElemsItr->init(geom, primSet);
    return quadDrawElemsItr;
}

class QuadStripDrawArraysIterator : public PrimitiveSetIterator
{
  public:

    QuadStripDrawArraysIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static QuadStripDrawArraysIterator* QuadStripDrawArraysIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    QuadStripDrawArraysIterator* quadStripDrawArraysItr = new QuadStripDrawArraysIterator();
    quadStripDrawArraysItr->init(geom, primSet);
    return quadStripDrawArraysItr;
}

class QuadStripDrawArrayLengthsIterator : public PrimitiveSetIterator
{
  public:

    QuadStripDrawArrayLengthsIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static QuadStripDrawArrayLengthsIterator* QuadStripDrawArrayLengthsIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    QuadStripDrawArrayLengthsIterator* quadStripDrawArrayLengthsItr = new QuadStripDrawArrayLengthsIterator();
    quadStripDrawArrayLengthsItr->init(geom, primSet);
    return quadStripDrawArrayLengthsItr;
}

class QuadStripDrawElementsIterator : public PrimitiveSetIterator
{
  public:

    QuadStripDrawElementsIterator() {};

    virtual void init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet);

    virtual bool getPoly(unsigned int index, Primitive& poly);
};

static QuadStripDrawElementsIterator* QuadStripDrawElementsIteratorInit(osg::Geometry* geom, osg::PrimitiveSet* primSet)
{
    QuadStripDrawElementsIterator* quadStripDrawElemsItr = new QuadStripDrawElementsIterator();
    quadStripDrawElemsItr->init(geom, primSet);
    return quadStripDrawElemsItr;
}

/*
  Method which returns the right primitiveSetIterator class instance
*/
PrimitiveSetIterator* PrimitiveSetIterator::getIterator(osg::PrimitiveSet* primitiveSet, osg::Geometry* geometry)
{
    if (primitiveSet->getMode() == osg::PrimitiveSet::QUADS)
    {
        switch(primitiveSet->getType())
        {
        case osg::PrimitiveSet::DrawArraysPrimitiveType:
            return QuadDrawArraysIteratorInit(geometry, primitiveSet);

        case osg::PrimitiveSet::DrawArrayLengthsPrimitiveType:
            return QuadDrawArrayLengthsIteratorInit(geometry, primitiveSet);

        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            return QuadDrawElementsIteratorInit(geometry, primitiveSet);

        default:
            return NULL;
        }
    }
    else if (primitiveSet->getMode() == osg::PrimitiveSet::QUAD_STRIP)
    {
        switch(primitiveSet->getType())
        {
        case osg::PrimitiveSet::DrawArraysPrimitiveType:
            return QuadStripDrawArraysIteratorInit(geometry, primitiveSet);

        case osg::PrimitiveSet::DrawArrayLengthsPrimitiveType:
            return QuadStripDrawArrayLengthsIteratorInit(geometry, primitiveSet);

        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            return QuadStripDrawElementsIteratorInit(geometry, primitiveSet);

        default:
            return NULL;
        }
    }
    else if (primitiveSet->getMode() == osg::PrimitiveSet::TRIANGLES)
    {
        switch(primitiveSet->getType())
        {
        case osg::PrimitiveSet::DrawArraysPrimitiveType:
            return TriangleDrawArraysIteratorInit(geometry, primitiveSet);

        case osg::PrimitiveSet::DrawArrayLengthsPrimitiveType:
            return TriangleDrawArrayLengthsIteratorInit(geometry, primitiveSet);

        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            return TriangleDrawElementsIteratorInit(geometry, primitiveSet);

        default:
            return NULL;
        }
    }
    else if (primitiveSet->getMode() == osg::PrimitiveSet::TRIANGLE_STRIP)
    {
        switch(primitiveSet->getType())
        {
        case osg::PrimitiveSet::DrawArraysPrimitiveType:
            return TriangleStripDrawArraysIteratorInit(geometry, primitiveSet);

        case osg::PrimitiveSet::DrawArrayLengthsPrimitiveType:
            return TriangleStripDrawArrayLengthsIteratorInit(geometry, primitiveSet);

        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            return TriangleStripDrawElementsIteratorInit(geometry, primitiveSet);

        default:
            return NULL;
        }
    }
    else if (primitiveSet->getMode() == osg::PrimitiveSet::TRIANGLE_FAN)
    {
        switch(primitiveSet->getType())
        {
        case osg::PrimitiveSet::DrawArraysPrimitiveType:
            return TriangleFanDrawArraysIteratorInit(geometry, primitiveSet);

        case osg::PrimitiveSet::DrawArrayLengthsPrimitiveType:
            return TriangleFanDrawArrayLengthsIteratorInit(geometry, primitiveSet);
        
        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            return TriangleFanDrawElementsIteratorInit(geometry, primitiveSet);

        default:
            return NULL;
        }
    }
    
    return NULL;
}

class DrawElementsAnyType
{
  public:
    DrawElementsAnyType(osg::PrimitiveSet* primSet) :
        m_spPrimSet(primSet)
    {
        
    }
    
    DrawElementsAnyType(const DrawElementsAnyType& copy) :
        m_spPrimSet(NULL)
    {
        const osg::PrimitiveSet* primSet = copy.getPrimitiveSet();
        switch(primSet->getType())
        {
        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            {
                m_spPrimSet = new osg::DrawElementsUByte(static_cast<const osg::DrawElementsUByte&>(*primSet), 0); //0 for shallow copy
                break;
            }
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
            {
                m_spPrimSet = new osg::DrawElementsUShort(static_cast<const osg::DrawElementsUShort&>(*primSet), 0);
                break;
            }
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            {
                m_spPrimSet = new osg::DrawElementsUInt(static_cast<const osg::DrawElementsUInt&>(*primSet), 0);
                break;
            }
        default:
            {
                throw std::string("DrawElementsAnyType(): invalid type");
            }
        }
    }

    const osg::PrimitiveSet* getPrimitiveSet() const { return m_spPrimSet.get(); }
    osg::PrimitiveSet* getPrimitiveSet() { return m_spPrimSet.get(); }

    unsigned int at(unsigned int index) const
    {
        switch(m_spPrimSet->getType())
        {
        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            return static_cast<osg::DrawElementsUByte*>(m_spPrimSet.get())->at(index);
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
            return static_cast<osg::DrawElementsUShort*>(m_spPrimSet.get())->at(index);
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            return static_cast<osg::DrawElementsUInt*>(m_spPrimSet.get())->at(index);
        default:
            {
                throw std::string("DrawElementsAnyType(): invalid type");
            }
        }
    }

    unsigned int size() const
    {
        switch(m_spPrimSet->getType())
        {
        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            return static_cast<osg::DrawElementsUByte*>(m_spPrimSet.get())->size();
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
            return static_cast<osg::DrawElementsUShort*>(m_spPrimSet.get())->size();
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            return static_cast<osg::DrawElementsUInt*>(m_spPrimSet.get())->size();
        default:
            {
                throw std::string("DrawElementsAnyType(): invalid type");
            }
        }
    }
    
    unsigned int begin() const
    {
        return 0;
    }

    unsigned int end()
    {
        return size();
    }
    
    void erase(unsigned int index)
    {
        switch(m_spPrimSet->getType())
        {
        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            {
                osg::DrawElementsUByte* primSet = static_cast<osg::DrawElementsUByte*>(m_spPrimSet.get());
                primSet->erase(primSet->begin() + index);
                break;
            }
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
            {
                osg::DrawElementsUShort* primSet = static_cast<osg::DrawElementsUShort*>(m_spPrimSet.get());
                primSet->erase(primSet->begin() + index);
                break;
            }
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            {
                osg::DrawElementsUInt* primSet = static_cast<osg::DrawElementsUInt*>(m_spPrimSet.get());
                primSet->erase(primSet->begin() + index);
                break;
            }
        default:
            {
                throw std::string("DrawElementsAnyType(): invalid type");
            }
        }
    }

    void erase(unsigned int start, unsigned int end)
    {
        switch(m_spPrimSet->getType())
        {
        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            {
                osg::DrawElementsUByte* primSet = static_cast<osg::DrawElementsUByte*>(m_spPrimSet.get());
                primSet->erase(primSet->begin() + start, primSet->begin() + end);
                break;
            }
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
            {
                osg::DrawElementsUShort* primSet = static_cast<osg::DrawElementsUShort*>(m_spPrimSet.get());
                primSet->erase(primSet->begin() + start, primSet->begin() + end);
                break;
            }
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            {
                osg::DrawElementsUInt* primSet = static_cast<osg::DrawElementsUInt*>(m_spPrimSet.get());
                primSet->erase(primSet->begin() + start, primSet->begin() + end);
                break;
            }
        default:
            {
                throw std::string("DrawElementsAnyType(): invalid type");
            }
        }
    }
    
    void insert(unsigned int index, unsigned int value)
    {
        switch(m_spPrimSet->getType())
        {
        case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            {
                osg::DrawElementsUByte* primSet = static_cast<osg::DrawElementsUByte*>(m_spPrimSet.get());
                primSet->insert(primSet->begin() + index, (osg::DrawElementsUByte::value_type)value);
                break;
            }
        case osg::PrimitiveSet::DrawElementsUShortPrimitiveType:
            {
                osg::DrawElementsUShort* primSet = static_cast<osg::DrawElementsUShort*>(m_spPrimSet.get());
                primSet->insert(primSet->begin() + index, (osg::DrawElementsUShort::value_type)value);
                break;
            }
        case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
            {
                osg::DrawElementsUInt* primSet = static_cast<osg::DrawElementsUInt*>(m_spPrimSet.get());
                primSet->insert(primSet->begin() + index, (osg::DrawElementsUInt::value_type)value);
                break;
            }
        default:
            {
                throw std::string("DrawElementsAnyType(): invalid type");
            }
        }
    }

  private:
    osg::ref_ptr<osg::PrimitiveSet> m_spPrimSet;
};


class PrimitiveSetEraser
{
  public:
    PrimitiveSetEraser() {}
    virtual ~PrimitiveSetEraser() {}

    virtual void deletePoly(Primitive& poly)=0;
    static PrimitiveSetEraser* getPrimitiveSetEraser(osg::PrimitiveSet& primSet);
  protected:
    void updateColorsAndNormals(Primitive& poly, int primSetsAdded);
};

class TriangleDrawArraysEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static TriangleDrawArraysEraser g_triangleDrawArraysEraser;

class TriangleDrawArrayLengthsEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static TriangleDrawArrayLengthsEraser g_triangleDrawArrayLengthsEraser;

class TriangleDrawElementsEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static TriangleDrawElementsEraser g_triangleDrawElementsEraser;

class TriangleStripDrawArraysEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static TriangleStripDrawArraysEraser g_triangleStripDrawArraysEraser;

class TriangleStripDrawArrayLengthsEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static TriangleStripDrawArrayLengthsEraser g_triangleStripDrawArrayLengthsEraser;

class TriangleStripDrawElementsEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static TriangleStripDrawElementsEraser g_triangleStripDrawElementsEraser;

class TriangleFanDrawArraysEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static TriangleFanDrawArraysEraser g_triangleFanDrawArraysEraser;

class TriangleFanDrawArrayLengthsEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static TriangleFanDrawArrayLengthsEraser g_triangleFanDrawArrayLengthsEraser;

class TriangleFanDrawElementsEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static TriangleFanDrawElementsEraser g_triangleFanDrawElementsEraser;

class QuadDrawArraysEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static QuadDrawArraysEraser g_quadDrawArraysEraser;

class QuadDrawArrayLengthsEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static QuadDrawArrayLengthsEraser g_quadDrawArrayLengthsEraser;

class QuadDrawElementsEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static QuadDrawElementsEraser g_quadDrawElementsEraser;

class QuadStripDrawArraysEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static QuadStripDrawArraysEraser g_quadStripDrawArraysEraser;

class QuadStripDrawArrayLengthsEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static QuadStripDrawArrayLengthsEraser g_quadStripDrawArrayLengthsEraser;

class QuadStripDrawElementsEraser : public PrimitiveSetEraser
{
  public:
    virtual void deletePoly(Primitive& poly);
};

static QuadStripDrawElementsEraser g_quadStripDrawElementsEraser;

PrimitiveSetEraser* PrimitiveSetEraser::getPrimitiveSetEraser(osg::PrimitiveSet& primSet)
{
    switch(primSet.getMode())
    {
    case osg::PrimitiveSet::TRIANGLES:
        {
            switch(primSet.getType())
            {
            case osg::PrimitiveSet::DrawArraysPrimitiveType:
                return &g_triangleDrawArraysEraser;

            case osg::PrimitiveSet::DrawArrayLengthsPrimitiveType: 
                return &g_triangleDrawArrayLengthsEraser;

            case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            case osg::PrimitiveSet::DrawElementsUShortPrimitiveType: 
            case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
                return &g_triangleDrawElementsEraser;
            
            default:
                return NULL;
            }
            break;
        }
    case osg::PrimitiveSet::TRIANGLE_STRIP:
        {
            switch(primSet.getType())
            {
            case osg::PrimitiveSet::DrawArraysPrimitiveType:
                return &g_triangleStripDrawArraysEraser;

            case osg::PrimitiveSet::DrawArrayLengthsPrimitiveType: 
                return &g_triangleStripDrawArrayLengthsEraser;

            case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            case osg::PrimitiveSet::DrawElementsUShortPrimitiveType: 
            case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
                return &g_triangleStripDrawElementsEraser;
            
            default:
                return NULL;

            }
            break;
        }
    case osg::PrimitiveSet::TRIANGLE_FAN:
        {
            switch(primSet.getType())
            {
            case osg::PrimitiveSet::DrawArraysPrimitiveType:
                return &g_triangleFanDrawArraysEraser;

            case osg::PrimitiveSet::DrawArrayLengthsPrimitiveType: 
                return &g_triangleFanDrawArrayLengthsEraser;

            case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            case osg::PrimitiveSet::DrawElementsUShortPrimitiveType: 
            case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
                return &g_triangleFanDrawElementsEraser;
            
            default:
                return NULL;

            }
            break;
        }
    case osg::PrimitiveSet::QUADS:
        {
            switch(primSet.getType())
            {
            case osg::PrimitiveSet::DrawArraysPrimitiveType:
                return &g_quadDrawArraysEraser;

            case osg::PrimitiveSet::DrawArrayLengthsPrimitiveType: 
                return &g_quadDrawArrayLengthsEraser;

            case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            case osg::PrimitiveSet::DrawElementsUShortPrimitiveType: 
            case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
                return &g_quadDrawElementsEraser;
            
            default:
                return NULL;

            }
            break;
        }
    case osg::PrimitiveSet::QUAD_STRIP:
        {
            switch(primSet.getType())
            {
            case osg::PrimitiveSet::DrawArraysPrimitiveType:
                return &g_quadStripDrawArraysEraser;

            case osg::PrimitiveSet::DrawArrayLengthsPrimitiveType: 
                return &g_quadStripDrawArrayLengthsEraser;

            case osg::PrimitiveSet::DrawElementsUBytePrimitiveType:
            case osg::PrimitiveSet::DrawElementsUShortPrimitiveType: 
            case osg::PrimitiveSet::DrawElementsUIntPrimitiveType:
                return &g_quadStripDrawElementsEraser;
            
            default:
                return NULL;

            }
            break;
        }
    default:
        {
            //do nothing
            break;
        }
    }
    
    return NULL;
}

/*
  ----------------------
  Primitive CLASS:
  ----------------------
*/

void Primitive::deleteMe()
{
    if(!m_spGeometry.valid() || !m_spPrimSet.valid())
        return;

    PrimitiveSetEraser* eraser = PrimitiveSetEraser::getPrimitiveSetEraser(*m_spPrimSet.get());
    if(eraser == NULL)
        return;

    eraser->deletePoly(*this);

    //set these to null so now you can't try to delete this poly again
    m_spGeometry = NULL;
    m_spPrimSet = NULL;
}

inline unsigned int getIndex(unsigned int pos, const osg::IndexArray* indices)
{
    switch(indices->getType())
    {
        case(osg::Array::ByteArrayType): return (*static_cast<const osg::ByteArray*>(indices))[pos];
        case(osg::Array::ShortArrayType): return (*static_cast<const osg::ShortArray*>(indices))[pos];
        case(osg::Array::IntArrayType): return (*static_cast<const osg::IntArray*>(indices))[pos];
        case(osg::Array::UByteArrayType): return (*static_cast<const osg::UByteArray*>(indices))[pos];
        case(osg::Array::UShortArrayType): return (*static_cast<const osg::UShortArray*>(indices))[pos];
        case(osg::Array::UIntArrayType): return (*static_cast<const osg::UIntArray*>(indices))[pos];
        default: return 0;
    }
}

inline void pushBackIndex(unsigned int value, osg::IndexArray* indices)
{
    switch(indices->getType())
    {
        case(osg::Array::ByteArrayType): 
        {
            osg::ByteArray& array = (*static_cast<osg::ByteArray*>(indices));
            array.push_back(value);
            break;
        }
        case(osg::Array::ShortArrayType):
        {
            osg::ShortArray& array = (*static_cast<osg::ShortArray*>(indices));
            array.push_back(value);
            break;
        }
        case(osg::Array::IntArrayType):
        {
            osg::IntArray& array = (*static_cast<osg::IntArray*>(indices));
            array.push_back(value);
            break;
        }
        case(osg::Array::UByteArrayType):
        {
            osg::UByteArray& array = (*static_cast<osg::UByteArray*>(indices));
            array.push_back(value);
            break;
        }
        case(osg::Array::UShortArrayType):
        {
            osg::UShortArray& array = (*static_cast<osg::UShortArray*>(indices));
            array.push_back(value);
            break;
        }
        case(osg::Array::UIntArrayType):
        {
            osg::UIntArray& array = (*static_cast<osg::UIntArray*>(indices));
            array.push_back(value);
            break;
        }
        default:
        {
            //do nothing
            break;
        }
    }
}

inline void eraseIndex(unsigned int pos, osg::IndexArray* indices)
{
    switch(indices->getType())
    {
        case(osg::Array::ByteArrayType): 
        {
            osg::ByteArray& array = (*static_cast<osg::ByteArray*>(indices));
            array.erase(array.begin()+pos);
            break;
        }
        case(osg::Array::ShortArrayType):
        {
            osg::ShortArray& array = (*static_cast<osg::ShortArray*>(indices));
            array.erase(array.begin()+pos);
            break;
        }
        case(osg::Array::IntArrayType):
        {
            osg::IntArray& array = (*static_cast<osg::IntArray*>(indices));
            array.erase(array.begin()+pos);
            break;
        }
        case(osg::Array::UByteArrayType):
        {
            osg::UByteArray& array = (*static_cast<osg::UByteArray*>(indices));
            array.erase(array.begin()+pos);
            break;
        }
        case(osg::Array::UShortArrayType):
        {
            osg::UShortArray& array = (*static_cast<osg::UShortArray*>(indices));
            array.erase(array.begin()+pos);
            break;
        }
        case(osg::Array::UIntArrayType):
        {
            osg::UIntArray& array = (*static_cast<osg::UIntArray*>(indices));
            array.erase(array.begin()+pos);
            break;
        }
        default:
        {
            //do nothing
            break;
        }
    }
}

osg::Vec3 Primitive::getVertex(unsigned int index) const
{
    osg::Vec3 vtx;
    getVertex(index, vtx);
    return vtx;
}

bool Primitive::getVertex(unsigned int index, osg::Vec3& vtx) const
{
    class translator : public osg::ConstValueVisitor
    {
    public:
        osg::Vec3 translate;
        bool translated;
        translator() : translated(false) {}
        virtual void apply(const osg::Vec2& v) { translate.set(v.x(), v.y(), 0.0f);  translated = true; }
        virtual void apply(const osg::Vec3& v) { translate.set(v.x(), v.y(), v.z()); translated = true; }
        virtual void apply(const osg::Vec4& v) { translate.set(v.x(), v.y(), v.z()); translated = true; }
    };

    const osg::IndexArray* vtxIndices = m_spGeometry->getVertexIndices();
    const osg::Array* polyVertices = m_spGeometry->getVertexArray();

    translator trans;

    unsigned int pos = m_indices[index];
    if(vtxIndices)
        pos = getIndex(m_indices[index], vtxIndices);

    if(pos >= polyVertices->getNumElements())
        return false;

    polyVertices->accept(pos, trans);

    if(trans.translated)
    {
        if(!m_worldTransformIsIdentity)
            vtx = m_spWorldTransform->preMult(trans.translate);
        else
            vtx = trans.translate;

        return true;
    }
    else
        return false;
}

bool Primitive::getColor(unsigned int index, osg::Vec4& color) const
{
    if(m_spGeometry->getColorArray() == NULL)
        return false;

    const osg::IndexArray* colorIndices = m_spGeometry->getColorIndices();

    unsigned int vindex = index;
    if(m_spGeometry->getColorBinding() == osg::Geometry::BIND_PER_VERTEX)
    {
        if(index >= m_indices.size())
            return false;

        vindex = m_indices[index];
    }

    unsigned int pos = vindex;
    if(colorIndices != NULL)
        pos = getIndex(vindex, colorIndices);

    const osg::Array* polyColors = m_spGeometry->getColorArray();

    if(pos >= polyColors->getNumElements())
        return false;
    
    switch(polyColors->getType())
    {
    case(osg::Array::Vec4ArrayType):
        {
            color = (*static_cast<const osg::Vec4Array*>(polyColors))[pos];
            break;
        }
    case(osg::Array::Vec4ubArrayType):
        {
            const osg::Vec4ub& colorUB =(*static_cast<const osg::Vec4ubArray*>(polyColors))[pos];
            color.set((float)colorUB.r() / 255.0f,
                      (float)colorUB.g() / 255.0f,
                      (float)colorUB.b() / 255.0f,
                      (float)colorUB.a() / 255.0f);
            break;
        }
    case(osg::Array::Vec3ArrayType):
        {   
            const osg::Vec3& color3f = (*static_cast<const osg::Vec3Array*>(polyColors))[pos];
            color.set((float)color3f.x() / 255.0f,
                      (float)color3f.y() / 255.0f,
                      (float)color3f.z() / 255.0f,
                      1.0f);
            break;
        }
    default:
    //OSG by default only supports the above three array types for normals
        return false;
    }

    return true;
}

bool Primitive::getNormal(unsigned int index, osg::Vec3& normal) const
{
    if(m_spGeometry->getNormalArray() == NULL)
        return false;

    const osg::IndexArray* normalIndices = m_spGeometry->getNormalIndices();

    unsigned int vindex = index;
    if(m_spGeometry->getNormalBinding() == osg::Geometry::BIND_PER_VERTEX)
    {
        if(index >= m_indices.size())
            return false;

        vindex = m_indices[index];
    }

    unsigned int pos = vindex;
    if(normalIndices != NULL)
        pos = getIndex(vindex, normalIndices);

    const osg::Array* polyNormals = m_spGeometry->getNormalArray();

    if(pos >= polyNormals->getNumElements())
        return false;
    
    switch(polyNormals->getType())
    {
    case(osg::Array::Vec3ArrayType):
        {
            normal = (*static_cast<const osg::Vec3Array*>(polyNormals))[pos];
            break;
        }
    case(osg::Array::Vec3sArrayType):
        {
            const osg::Vec3s& normal3s =(*static_cast<const osg::Vec3sArray*>(polyNormals))[pos];
            normal.set((float)normal3s.x(),
                       (float)normal3s.y(),
                       (float)normal3s.z());
            break;
        }
    case(osg::Array::Vec3bArrayType):
        {   
            const osg::Vec3b& normal3b = (*static_cast<const osg::Vec3bArray*>(polyNormals))[pos];
            normal.set((float)normal3b.x(),
                      (float)normal3b.y(),
                      (float)normal3b.z());
            break;
        }
    case(osg::Array::Vec4sArrayType):
        {
            const osg::Vec4s& normal4s =(*static_cast<const osg::Vec4sArray*>(polyNormals))[pos];
            normal.set((float)normal4s.x(),
                       (float)normal4s.y(),
                       (float)normal4s.z());
            break;
        }
    case(osg::Array::Vec4bArrayType):
        {   
            const osg::Vec4b& normal4b = (*static_cast<const osg::Vec4bArray*>(polyNormals))[pos];
            normal.set((float)normal4b.x(),
                      (float)normal4b.y(),
                      (float)normal4b.z());
            break;
        }
    default:
    //OSG by default only supports the types above for normals
        return false;
    }

    return true;
}

bool Primitive::getTexCoord(unsigned int texCoordArrayIndex, unsigned int index, osg::Vec2& texCoord) const
{
    if(texCoordArrayIndex >= m_spGeometry->getNumTexCoordArrays())
        texCoordArrayIndex = m_spGeometry->getNumTexCoordArrays()-1;

    if(index >= m_indices.size())
        index = m_indices.size()-1;

    const osg::IndexArray* texIndices = m_spGeometry->getTexCoordIndices(texCoordArrayIndex);

    unsigned int pos = m_indices[index];
    if(texIndices)
        pos = getIndex(m_indices[index], texIndices);

    //we only support 2 component texture coords so need to translate to Vec2
    class translator : public osg::ConstValueVisitor
    {
    public:
        osg::Vec2& translate;
        bool translated;
        translator(osg::Vec2& ref) : translate(ref), translated(false) {}
        virtual void apply(const GLfloat& v){ translate.set(v, v); translated = true; }
        virtual void apply(const osg::Vec2& v)   { translate.set(v.x(), v.y()); translated = true; }
        virtual void apply(const osg::Vec3& v)   { translate.set(v.x(), v.y()); translated = true; }
        virtual void apply(const osg::Vec4& v)   { translate.set(v.x(), v.y()); translated = true; }
    };
    
    osg::Array* texCoords = m_spGeometry->getTexCoordArray(texCoordArrayIndex);
    translator trans(texCoord);

    if(pos < texCoords->getNumElements())
    {
        texCoords->accept(pos, trans);

        return trans.translated;
    }
    
    texCoord = osg::Vec2(0.0f, 0.0f);
    return false;
}

/*
  --------------------------
  PRIMITIVESETITERATOR CLASS:
  --------------------------
*/

PrimitiveSetIterator::PrimitiveSetIterator()
{
    m_spGeometry = NULL;
    m_spPrimSet = NULL;
    m_numberOfPolys = 0;
}

PrimitiveSetIterator::~PrimitiveSetIterator()
{
}

void copyNormalToEndOfNormalArray(osg::Geometry* pGeometry, unsigned int indexToCopy)
{
    osg::Array* normalArray = pGeometry->getNormalArray();
    osg::IndexArray* indexArray = pGeometry->getNormalIndices();
    switch(normalArray->getType())
    {
        case (osg::Array::Vec3ArrayType):
        {
            osg::Vec3Array& normals = *static_cast<osg::Vec3Array*>(normalArray);
            if(indexArray != NULL)
            {
                indexToCopy = getIndex(indexToCopy, indexArray);
                pushBackIndex(normals.size(), indexArray);
            }
            const osg::Vec3& copyNormal = normals[indexToCopy];
            normals.push_back(copyNormal);
            break;
        }
        case (osg::Array::Vec3sArrayType):
        {
            osg::Vec3sArray& normals = *static_cast<osg::Vec3sArray*>(normalArray);
            if(indexArray != NULL)
            {
                indexToCopy = getIndex(indexToCopy, indexArray);
                pushBackIndex(normals.size(), indexArray);
            }
            const osg::Vec3s& copyNormal = normals[indexToCopy];
            normals.push_back(copyNormal);
            break;
        }
        case (osg::Array::Vec4sArrayType):
        {
            osg::Vec4sArray& normals = *static_cast<osg::Vec4sArray*>(normalArray);
            if(indexArray != NULL)
            {
                indexToCopy = getIndex(indexToCopy, indexArray);
                pushBackIndex(normals.size(), indexArray);
            }
            const osg::Vec4s& copyNormal = normals[indexToCopy];
            normals.push_back(copyNormal);
            break;
        }
        case (osg::Array::Vec3bArrayType):
        {
            osg::Vec3bArray& normals = *static_cast<osg::Vec3bArray*>(normalArray);
            if(indexArray != NULL)
            {
                indexToCopy = getIndex(indexToCopy, indexArray);
                pushBackIndex(normals.size(), indexArray);
            }
            const osg::Vec3b& copyNormal = normals[indexToCopy];
            normals.push_back(copyNormal);
            break;
        }
        case (osg::Array::Vec4bArrayType):
        {
            osg::Vec4bArray& normals = *static_cast<osg::Vec4bArray*>(normalArray);
            if(indexArray != NULL)
            {
                indexToCopy = getIndex(indexToCopy, indexArray);
                pushBackIndex(normals.size(), indexArray);
            }
            const osg::Vec4b& copyNormal = normals[indexToCopy];
            normals.push_back(copyNormal);
            break;
        }
        default:
            break;
    }
}

void eraseNormal(osg::Geometry* pGeometry, unsigned int indexToErase)
{
    osg::Array* normalArray = pGeometry->getNormalArray();
    osg::IndexArray* indexArray = pGeometry->getNormalIndices();
    switch(normalArray->getType())
    {
        case (osg::Array::Vec3ArrayType):
        {
            osg::Vec3Array& normals = *static_cast<osg::Vec3Array*>(normalArray);
            if(indexArray != NULL)
            {
                unsigned int saveIndex = indexToErase;
                indexToErase = getIndex(indexToErase, indexArray);
                eraseIndex(saveIndex, indexArray);
            }
            normals.erase(normals.begin()+indexToErase);
            break;
        }
        case (osg::Array::Vec3sArrayType):
        {
            osg::Vec3sArray& normals = *static_cast<osg::Vec3sArray*>(normalArray);
            if(indexArray != NULL)
            {
                unsigned int saveIndex = indexToErase;
                indexToErase = getIndex(indexToErase, indexArray);
                eraseIndex(saveIndex, indexArray);
            }
            normals.erase(normals.begin()+indexToErase);
            break;
        }
        case (osg::Array::Vec4sArrayType):
        {
            osg::Vec4sArray& normals = *static_cast<osg::Vec4sArray*>(normalArray);
            if(indexArray != NULL)
            {
                unsigned int saveIndex = indexToErase;
                indexToErase = getIndex(indexToErase, indexArray);
                eraseIndex(saveIndex, indexArray);
            }
            normals.erase(normals.begin()+indexToErase);
            break;
        }
        case (osg::Array::Vec3bArrayType):
        {
            osg::Vec3bArray& normals = *static_cast<osg::Vec3bArray*>(normalArray);
            if(indexArray != NULL)
            {
                unsigned int saveIndex = indexToErase;
                indexToErase = getIndex(indexToErase, indexArray);
                eraseIndex(saveIndex, indexArray);
            }
            normals.erase(normals.begin()+indexToErase);
            break;
        }
        case (osg::Array::Vec4bArrayType):
        {
            osg::Vec4bArray& normals = *static_cast<osg::Vec4bArray*>(normalArray);
            if(indexArray != NULL)
            {
                unsigned int saveIndex = indexToErase;
                indexToErase = getIndex(indexToErase, indexArray);
                eraseIndex(saveIndex, indexArray);
            }
            normals.erase(normals.begin()+indexToErase);
        }
        default:
        {
            break;
        }
    }
}

void copyColorToEndOfColorArray(osg::Geometry* pGeometry, unsigned int indexToCopy)
{
    osg::Array* colorArray = pGeometry->getNormalArray();
    osg::IndexArray* indexArray = pGeometry->getColorIndices();
    switch(colorArray->getType())
    {
        case (osg::Array::Vec3ArrayType):
        {
            osg::Vec3Array& colors = *static_cast<osg::Vec3Array*>(colorArray);
            if(indexArray != NULL)
            {
                indexToCopy = getIndex(indexToCopy, indexArray);
                pushBackIndex(colors.size(), indexArray);
            }
            const osg::Vec3& copyColor = colors[indexToCopy];
            colors.push_back(copyColor);
            break;
        }
        case (osg::Array::Vec4ArrayType):
        {
            osg::Vec4Array& colors = *static_cast<osg::Vec4Array*>(colorArray);
            if(indexArray != NULL)
            {
                indexToCopy = getIndex(indexToCopy, indexArray);
                pushBackIndex(colors.size(), indexArray);
            }
            const osg::Vec4& copyColor = colors[indexToCopy];
            colors.push_back(copyColor);
            break;
        }
        case (osg::Array::Vec4ubArrayType):
        {
            osg::Vec4ubArray& colors = *static_cast<osg::Vec4ubArray*>(colorArray);
            if(indexArray != NULL)
            {
                indexToCopy = getIndex(indexToCopy, indexArray);
                pushBackIndex(colors.size(), indexArray);
            }
            const osg::Vec4ub& copyColor = colors[indexToCopy];
            colors.push_back(copyColor);
            break;
        }
        default:
            break;
    }
}

void eraseColor(osg::Geometry* pGeometry, unsigned int indexToErase)
{
    osg::Array* colorArray = pGeometry->getColorArray();
    osg::IndexArray* indexArray = pGeometry->getColorIndices();
    switch(colorArray->getType())
    {
        case (osg::Array::Vec3ArrayType):
        {
            osg::Vec3Array& colors = *static_cast<osg::Vec3Array*>(colorArray);
            if(indexArray != NULL)
            {
                unsigned int saveIndex = indexToErase;
                indexToErase = getIndex(indexToErase, indexArray);
                eraseIndex(saveIndex, indexArray);
            }
            colors.erase(colors.begin()+indexToErase);
            break;
        }
        case (osg::Array::Vec4ArrayType):
        {
            osg::Vec4Array& colors = *static_cast<osg::Vec4Array*>(colorArray);
            if(indexArray != NULL)
            {
                unsigned int saveIndex = indexToErase;
                indexToErase = getIndex(indexToErase, indexArray);
                eraseIndex(saveIndex, indexArray);
            }
            colors.erase(colors.begin()+indexToErase);
            break;
        }
        case (osg::Array::Vec4ubArrayType):
        {
            osg::Vec4ubArray& colors = *static_cast<osg::Vec4ubArray*>(colorArray);
            if(indexArray != NULL)
            {
                unsigned int saveIndex = indexToErase;
                indexToErase = getIndex(indexToErase, indexArray);
                eraseIndex(saveIndex, indexArray);
            }
            colors.erase(colors.begin()+indexToErase);
            break;
        }
        default:
        {
            break;
        }
    }
}

void PrimitiveSetEraser::updateColorsAndNormals(Primitive& poly, int primSetsAdded)
{
    osg::Geometry* pGeometry = poly.getGeometry();
    //normal array 
    if (pGeometry->getNormalBinding() == osg::Geometry::BIND_PER_PRIMITIVE_SET)
    {
        osg::PrimitiveSet* pPrimSet = poly.getPrimitiveSet();
        osg::Array* normals = pGeometry->getNormalArray();
        if(normals->referenceCount() > 1)//if this normal array is shared then copy it
        {
            osg::CopyOp copyOp(osg::CopyOp::DEEP_COPY_DRAWABLES);
            normals = copyOp(normals);
            pGeometry->setNormalArray(normals);

            osg::Array* indexArray = pGeometry->getNormalIndices();
            if(indexArray != NULL && indexArray->referenceCount() > 1)
            {
                //copy the index array too
                pGeometry->setNormalIndices(static_cast<osg::IndexArray*>(copyOp(indexArray)));
            }
        }
            
        unsigned int primSetIndex = pGeometry->getPrimitiveSetIndex(pPrimSet);
        for (int i = 0; i < primSetsAdded; ++i)
        {
            //copy the normal at index 'primSetIndex' to the end of this
            //geom's normal array because the added primitive sets should 
            //use the same normal vector as the poly's primitive set
            copyNormalToEndOfNormalArray(pGeometry, primSetIndex);
        }

    } 
    else if (pGeometry->getNormalBinding() == osg::Geometry::BIND_PER_PRIMITIVE)
    {
        osg::Array* normals = pGeometry->getNormalArray();
        if(normals->referenceCount() > 1)
        {
            osg::CopyOp copyOp(osg::CopyOp::DEEP_COPY_DRAWABLES);
            normals = copyOp(normals);
            pGeometry->setNormalArray(normals);
            
            osg::Array* indexArray = pGeometry->getNormalIndices();
            if(indexArray != NULL && indexArray->referenceCount() > 1)
            {
                //copy the index array too
                pGeometry->setNormalIndices(static_cast<osg::IndexArray*>(copyOp(indexArray)));
            }
        }
        
        //this poly was deleted so need to delete the normal
        //that goes with it
        int polyIndex = poly.getPrimSetIndex();
        eraseNormal(pGeometry, polyIndex);
    }

    //color array
    if (pGeometry->getColorBinding() == osg::Geometry::BIND_PER_PRIMITIVE_SET)
    {
        osg::PrimitiveSet* pPrimSet = poly.getPrimitiveSet();
        osg::Array* colors = pGeometry->getColorArray();
        if(colors->referenceCount() > 1)//if this normal array is shared then copy it
        {
            osg::CopyOp copyOp(osg::CopyOp::DEEP_COPY_DRAWABLES);
            colors = copyOp(colors);
            pGeometry->setColorArray(colors);

            osg::Array* indexArray = pGeometry->getColorIndices();
            if(indexArray != NULL && indexArray->referenceCount() > 1)
            {
                //copy the index array too
                pGeometry->setColorIndices(static_cast<osg::IndexArray*>(copyOp(indexArray)));
            }
        }
            
        unsigned int primSetIndex = pGeometry->getPrimitiveSetIndex(pPrimSet);
        for (int i = 0; i < primSetsAdded; ++i)
        {
            //copy the color at index 'primSetIndex' to the end of this
            //geom's color array because the added primitive sets should 
            //use the same color as the poly's primitive set
            copyColorToEndOfColorArray(pGeometry, primSetIndex);
        }
    }
    else if (pGeometry->getColorBinding() == osg::Geometry::BIND_PER_PRIMITIVE)
    {
        osg::Array* colors = pGeometry->getColorArray();
        
        if(colors->referenceCount() > 1)//if this normal array is shared then copy it
        {
            osg::CopyOp copyOp(osg::CopyOp::DEEP_COPY_DRAWABLES);
            colors = copyOp(colors);
            pGeometry->setColorArray(colors);

            osg::Array* indexArray = pGeometry->getColorIndices();
            if(indexArray != NULL && indexArray->referenceCount() > 1)
            {
                //copy the index array too
                pGeometry->setColorIndices(static_cast<osg::IndexArray*>(copyOp(indexArray)));
            }
        }
        
        //this poly was deleted so need to delete the color
        //that goes with it
        int polyIndex = poly.getPrimSetIndex();
        eraseColor(pGeometry, polyIndex);
    }
}


/*
  ----------------------------------------------------
  DIFFERENT TYPES OF POLYGON CLASSES:
  essentially how they work is that they are used 
  to iterate through a primitive and in each iteration 
  the properties of the current polygon overwrites that 
  of the previous then if the current polygon is the 
  right one its properties are saved within the 
  instance of the class.
  ---------------------------------------------------
*/

void TriangleDrawArraysIterator::init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet = primitiveSet;
    m_numberOfPolys = dynamic_cast<osg::DrawArrays*>(primitiveSet)->getCount() / 3;
}

bool TriangleDrawArraysIterator::getPoly(unsigned int index, Primitive& poly)
{

    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);

        osg::DrawArrays * primSet = (osg::DrawArrays*)m_spPrimSet.get();
        unsigned int vtxArrayIndex = primSet->getFirst() + (index*3);
        poly.setIndices(vtxArrayIndex, vtxArrayIndex + 1, vtxArrayIndex + 2);

        return true;
    }
}

void TriangleDrawArraysEraser::deletePoly(Primitive& poly)
{

    //next part will find if it is middle,end or begining triangle and thus how to delete
    int index = poly.getPrimSetIndex()*3; //this is then the last point of the triangle
    osg::DrawArrays * primSet = (osg::DrawArrays*)poly.getPrimitiveSet();

    unsigned int primitiveSetsAdded = 0;
    if (index == 0)
    {
        //if the triangle is the first one in this primitive set
        //then change count and first
        primSet->setCount(primSet->getCount() - 3);
        primSet->setFirst(primSet->getFirst() + 3);
    }
    else if (index < primSet->getCount() - 3)
    {
        //if the primitive is not the first and not the last then
        //break up into two primitives 
        //break up primitive into two at the triangle to be deleted and thus exclude it

        osg::DrawArrays * secondPrimSet = new osg::DrawArrays(*primSet, 0); //0 for shallow copy
        secondPrimSet->setFirst(primSet->getFirst() + index + 3);
        secondPrimSet->setCount(primSet->getCount() - (index + 3));
        poly.getGeometry()->addPrimitiveSet(secondPrimSet);
        
        primSet->setCount(index); 

        primitiveSetsAdded = 1;
    }
    else
    {
        //if the triangle is the last in this primitive set
        //then change the count
        primSet->setCount(primSet->getCount() - 3);
    }

    updateColorsAndNormals(poly, primitiveSetsAdded);

    //must be called or deletion will not occur
    poly.getGeometry()->dirtyBound();
    poly.getGeometry()->releaseGLObjects();
}

void TriangleDrawArrayLengthsIterator::init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    m_numberOfPolys = primitiveSet->getNumIndices() / 3;
}

bool TriangleDrawArrayLengthsIterator::getPoly(unsigned int index, Primitive& poly)
{

    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);

        osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)m_spPrimSet.get();
        unsigned int vtxArrayIndex = primSet->getFirst() + (index*3);
        poly.setIndices(vtxArrayIndex, vtxArrayIndex + 1, vtxArrayIndex + 2);

        return true;
    }
}

void TriangleDrawArrayLengthsEraser::deletePoly(Primitive& poly)
{

    //next part will find if it is middle,end or begining triangle and thus how to delete
    osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)poly.getPrimitiveSet();

    //this is the index of the first vertex in the triangle
    unsigned int polyIndex = primSet->getFirst() + (poly.getPrimSetIndex()*3);
    //if this is the first triangle then we can skip all the complicated crap below

    if (polyIndex == static_cast<unsigned int>(primSet->getFirst()))
    {
        //just change the first and the count of the first element
        primSet->setFirst(polyIndex + 3);
        //we could potentially be changing the count here to zero,
        //but its not a huge deal if we do it will just start at the 
        //next length
        (*primSet)[0] = primSet->at(0)-3;
        
        updateColorsAndNormals(poly, 0/*primitiveSetsAdded*/);
        
        //must be called or deletion will not occur
        poly.getGeometry()->dirtyBound();
        poly.getGeometry()->releaseGLObjects();
        return;
    }

    unsigned int currentVtxStart = primSet->getFirst();

    osg::DrawArrayLengths::vector_type::iterator itr = primSet->begin();
    unsigned int itrIndex = 0;
    for (; itr != primSet->end(); ++itr, ++itrIndex)
    {
        unsigned int currentVtxEnd = currentVtxStart + *itr;

        //figure out if the current triangle is part of the current
        //draw array length
        if (polyIndex >= currentVtxStart && polyIndex < currentVtxEnd)
        {
            //just break into two primitive sets and exclude the triangle to be deleted
            osg::DrawArrayLengths * secondPrimSet = new osg::DrawArrayLengths(*primSet, 0); //0 for shallow copy
            //delete from the beginning up to, but not including, the current itr
            secondPrimSet->erase(secondPrimSet->begin(), secondPrimSet->begin() + itrIndex);
            //change the starting point of second prim set to be just beyond the end
            //of the current triangle
            secondPrimSet->setFirst(polyIndex+3);
            (*secondPrimSet)[0] = secondPrimSet->at(0)-3;

            poly.getGeometry()->addPrimitiveSet(secondPrimSet);
            //delete everything after the current itr
            primSet->erase(itr+1, primSet->end());
            //then modify the value (i.e. the length) of the current itr
            (*primSet)[itrIndex] = polyIndex - currentVtxStart;
            
            updateColorsAndNormals(poly, 1/*primitiveSetsAdded*/);
            break;
        }

        currentVtxStart = currentVtxEnd;
    }

    //must be called or deletion will not occur
    poly.getGeometry()->dirtyBound();
    poly.getGeometry()->releaseGLObjects();
}

void TriangleDrawElementsIterator::init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    m_numberOfPolys = primitiveSet->getNumIndices() / 3;

}

bool TriangleDrawElementsIterator::getPoly(unsigned int index, Primitive& poly)
{

    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);

        DrawElementsAnyType primSet(m_spPrimSet.get());
        unsigned int vtxIndex = index*3;
        poly.setIndices(primSet.at(vtxIndex), primSet.at(vtxIndex + 1), primSet.at(vtxIndex + 2));

        return true;
    }
}


void TriangleDrawElementsEraser::deletePoly(Primitive& poly)
{
    //next part will find if it is middle,end or begining triangle and thus how to delete
    unsigned int index = poly.getPrimSetIndex()*3; //this is then the last point of the triangle
    DrawElementsAnyType primSet(poly.getPrimitiveSet());

    unsigned int primitiveSetsAdded = 0;
    if (index == 0)
    {   
        //if this is the first poly then
        //delete first three elements
        primSet.erase(0, 3);

    }
    else if (index < primSet.size() - 3)
    {
        //if this poly is not the first and not the last then
        //break up into two primitives,assuming sequential and no repeats 
        //break up primitive into two at the triangle to be deleted and thus exclude it  
        DrawElementsAnyType secondPrimSet(primSet);//new osg::DrawElementsUByte(*primSet, 0);   //0 for shallow copy
        secondPrimSet.erase(secondPrimSet.begin(), secondPrimSet.begin() + index + 3);
        poly.getGeometry()->addPrimitiveSet(secondPrimSet.getPrimitiveSet());

        primSet.erase(primSet.begin() + index, primSet.end());

        primitiveSetsAdded = 1;
    }
    else
    {
        //if this poly is the last one then
        //delete last three elements
        primSet.erase(index, primSet.end());
    }

    updateColorsAndNormals(poly, primitiveSetsAdded);

    //must be called or deletion will not occur
    poly.getGeometry()->dirtyBound();
    poly.getGeometry()->releaseGLObjects();
}


void TriangleStripDrawArraysIterator::init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    m_numberOfPolys = (primitiveSet->getNumIndices() - 2);
}

bool TriangleStripDrawArraysIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {

        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);
        /*
           for a triangleStripDrawArrays the second one triangle has its 
           first point at vertex array index 1
         */
        osg::DrawArrays* primSet = (osg::DrawArrays*)m_spPrimSet.get();
        index += primSet->getFirst();
        //order is important
        if(index & 1)//if the number is odd
            poly.setIndices(index, index + 2, index + 1);
        else
            poly.setIndices(index, index + 1, index + 2);

        return true;
    }
}



void TriangleStripDrawArraysEraser::deletePoly(Primitive& poly)
{

    //next part will find if it is middle,end or begining triangle and thus how to delete
    unsigned int index = poly.getPrimSetIndex(); //this is then the last point of the triangle
    osg::DrawArrays * primSet = (osg::DrawArrays*)poly.getPrimitiveSet();

    unsigned int primitiveSetsAdded = 0;

    if (index == 0)
    {
        //if poly is the first one then move the first to the next vertex
        //and shorten the count by one
        //NOTE: sort of works but second and third points do not print out in the same order after deletion
        primSet->setCount(primSet->getCount() - 1);
        primSet->setFirst(primSet->getFirst() + 1);
    }

    else if (index < static_cast<unsigned int>(primSet->getCount()) - 3)
    {
        //NOTE:same problem with secondPrimSet here
        //if the poly is a middle polygon then 
        //break up primitive into two at the triangle to be deleted and thus exclude it
        osg::DrawArrays * secondPrimSet = new osg::DrawArrays(*primSet, 0); //0 for shallow copy
        secondPrimSet->setFirst(primSet->getFirst() + index + 1); //exclude first point of triangle to be deleted
        secondPrimSet->setCount(primSet->getCount() - (index + 1));
        poly.getGeometry()->addPrimitiveSet(secondPrimSet);
        
        primSet->setCount(index+2);   //end right before triangle to be deleted

        primitiveSetsAdded = 1;
    }
    else
    {
        //if the poly is the last one then shorten the count
        primSet->setCount(index+2);
    }

    updateColorsAndNormals(poly, primitiveSetsAdded);

    //must be called or deletion will not occur
    poly.getGeometry()->dirtyBound();
    poly.getGeometry()->releaseGLObjects();
}

void TriangleStripDrawArrayLengthsIterator::init(osg::Geometry * searchGeometry,
                                                 osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;

    m_numberOfPolys = 0;
    //find the numberOfPrimitives in this primitive set
    osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)m_spPrimSet.get();
    for (osg::DrawArrayLengths::vector_type::const_iterator itr = primSet->begin(); 
         itr != primSet->end(); 
         ++itr)
    {
        m_numberOfPolys += (*itr - 2);
    }

}

bool TriangleStripDrawArrayLengthsIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setPrimSetIndex(index);
        /*
           basic algorithm here is that for each element value
           there are (element value - 2) triangles.
         */
        osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)m_spPrimSet.get();
        osg::DrawArrayLengths::vector_type::const_iterator itr = primSet->begin();
        unsigned int polysTraversed = 0;
        unsigned int verticesTraversed = 0;
        for (polysTraversed = 0, verticesTraversed = 0; itr != primSet->end(); ++itr)
        {
            //if the polygon is in the current strip
            if (index <= (polysTraversed + (*itr - 2)))
            {
                poly.setGeometry(m_spGeometry.get());
                poly.setPrimitiveSet(m_spPrimSet.get());

                index += primSet->getFirst();
                //+2 becasue dont count the first 2 points
                //look at the remaining number of polys that are in this strip
                if(index & 1)//if this index is odd
                    poly.setIndices((index - polysTraversed) + verticesTraversed,
                                ((index - polysTraversed) + verticesTraversed) + 2,
                                ((index - polysTraversed) + verticesTraversed) + 1);
                else
                    poly.setIndices((index - polysTraversed) + verticesTraversed,
                                ((index - polysTraversed) + verticesTraversed) + 1,
                                ((index - polysTraversed) + verticesTraversed) + 2);


                return true;
            }

            polysTraversed += (*itr - 2);
            verticesTraversed += *itr;
        }

        return false;
    }
}


void TriangleStripDrawArrayLengthsEraser::deletePoly(Primitive& poly)
{

    //next part will find if it is middle,end or begining triangle and thus how to delete
    osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)poly.getPrimitiveSet();
    unsigned int polyIndex = poly.getPrimSetIndex();

    //if this is the first triangle in the strip
    //then can skip the complicated crap below
    if(polyIndex == 0)
    {
        //change the first
        primSet->setFirst(primSet->getFirst()+1);
        (*primSet)[0] = primSet->at(0)-1;
        
        updateColorsAndNormals(poly, 0/*primitiveSetsAdded*/);

        //must be called or deletion will not occur
        poly.getGeometry()->dirtyBound();
        poly.getGeometry()->releaseGLObjects();
        return;
    }

    osg::DrawArrayLengths::vector_type::iterator itr = primSet->begin();
    unsigned int itrIndex = 0;
    unsigned int curPolyStartIndex = 0;
    unsigned int curFirst = primSet->getFirst();
    
    for (; itr != primSet->end(); ++itr, ++itrIndex)
    {
        unsigned int curVtxCount = *itr;
        unsigned int curPolyCount = curVtxCount - 2;
        
        if(polyIndex >= curPolyStartIndex && polyIndex < (curPolyStartIndex + curPolyCount))
        {
            osg::DrawArrayLengths * secondPrimSet = new osg::DrawArrayLengths(*primSet, 0); //0 for shallow copy
            //erase everything up to, but not including, the current itr
            secondPrimSet->erase(secondPrimSet->begin(), secondPrimSet->begin() + itrIndex);
            secondPrimSet->setFirst(curFirst + (polyIndex - curPolyStartIndex + 1));
            //update the count
            (*secondPrimSet)[0] = secondPrimSet->at(0) - (secondPrimSet->getFirst() - curFirst);
            poly.getGeometry()->addPrimitiveSet(secondPrimSet);

            //now erase from itr+1 to end
            primSet->erase(itr+1, primSet->end());
            //change the count for itr
            (*primSet)[itrIndex] = primSet->at(itrIndex) - secondPrimSet->at(0) + 1;

            updateColorsAndNormals(poly, 1/*primitiveSetsAdded*/);
            break;
        }

        curPolyStartIndex += curPolyCount;
        curFirst += curVtxCount;

    }

    //must be called or deletion will not occur
    poly.getGeometry()->dirtyBound();
    poly.getGeometry()->releaseGLObjects();

}

void TriangleStripDrawElementsIterator::init(osg::Geometry * searchGeometry,
                                             osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    m_numberOfPolys = (primitiveSet->getNumIndices() - 2);
}

bool TriangleStripDrawElementsIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);

        DrawElementsAnyType primSet(m_spPrimSet.get());
        if(index + 2 >= m_spPrimSet->getNumIndices())
            index = index - 2;
        if(index & 1)//if odd
            poly.setIndices(primSet.at(index), primSet.at(index + 2), primSet.at(index + 1));
        else
            poly.setIndices(primSet.at(index), primSet.at(index + 1), primSet.at(index + 2));

        return true;
    }
}



void TriangleStripDrawElementsEraser::deletePoly(Primitive& poly)
{

    //next part will find if it is middle,end or begining triangle and thus how to delete
    unsigned int index = poly.getPrimSetIndex(); //this is then the last point of the triangle
    DrawElementsAnyType primSet(poly.getPrimitiveSet());

    //in this case I assume that you cannot have Triangle_strips of DrawElements where two of the elements are the same.

    unsigned int primitiveSetsAdded = 0;
    if (index == 0)
    {
        //if this poly is the first one then
        //erase this index so that the first 
        //index becomes the index of the second poly
        primSet.erase(primSet.begin());
    }
    else if (index < primSet.size()-2)
    {
        DrawElementsAnyType secondPrimSet(primSet);
        //break up primitive into two at the triangle to be deleted and thus exclude it
        secondPrimSet.erase(secondPrimSet.begin(), secondPrimSet.begin() + index + 1);
        poly.getGeometry()->addPrimitiveSet(secondPrimSet.getPrimitiveSet());

        primSet.erase(index+1, primSet.end());

        primitiveSetsAdded = 1;
    }
    else
    {
        primSet.erase(primSet.end()-1);
    }

    updateColorsAndNormals(poly, primitiveSetsAdded);

    //must be called or deletion will not occur
    poly.getGeometry()->dirtyBound();
    poly.getGeometry()->releaseGLObjects();

}

void TriangleFanDrawArraysIterator::init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    osg::DrawArrays* primSet = (osg::DrawArrays*)m_spPrimSet.get();
    m_numberOfPolys = (primSet->getCount() - 1);
}

bool TriangleFanDrawArraysIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);

        osg::DrawArrays* primSet = (osg::DrawArrays*)m_spPrimSet.get();
        int primSetStart = primSet->getFirst();
        index += primSetStart;
        poly.setIndices(primSetStart, index + 1, index + 2);

        return true;
    }
}

void TriangleFanDrawArraysEraser::deletePoly(Primitive& poly)
{

    osg::DrawArrays* primSet = (osg::DrawArrays*)poly.getPrimitiveSet();
    //next part will find if it is middle,end or begining triangle and thus how to delete
    unsigned int index = poly.getPrimSetIndex(); //this is then the last point of the triangle

    unsigned int primitiveSetsAdded = 0;
    if (index == 0)
    {
        //in this case you have to convert the entire primitive to a DrawElements
        osg::DrawElementsUShort * deus = new osg::DrawElementsUShort(osg::PrimitiveSet::TRIANGLE_FAN, 0);   //(mode,vector size)
        deus->push_back(primSet->getFirst());

        //deusElement used to skip over the first two points which are of the deleted triangle
        for (GLsizei deusElement = 2; deusElement < primSet->getCount(); ++deusElement)
        {
            deus->push_back(primSet->getFirst() + deusElement);
        }

        poly.getGeometry()->removePrimitiveSet(poly.getGeometry()->getPrimitiveSetIndex(primSet));
        poly.getGeometry()->addPrimitiveSet(deus);
    }
    else if (index < static_cast<unsigned int>(primSet->getCount()) - 2)
    {
        //only second half has to be a DrawElements, first half can be made into triangle_strips
        osg::DrawElementsUShort * deus = new osg::DrawElementsUShort(osg::PrimitiveSet::TRIANGLE_FAN, 0);
        deus->push_back(primSet->getFirst());

        //deusElement used to skip over the first two points which are of the deleted triangle
        for (GLsizei deusElement = index+2; deusElement < primSet->getCount(); ++deusElement)
        {
            deus->push_back(primSet->getFirst() + deusElement);
        }

        poly.getGeometry()->addPrimitiveSet(deus);
        
        primSet->setCount(index+2);

        primitiveSetsAdded = 1;
    }
    else
    {
        primSet->setCount(primSet->getCount() - 1);
    }

    updateColorsAndNormals(poly, primitiveSetsAdded);

    //must be called or deletion will not occur
    poly.getGeometry()->dirtyBound();
    poly.getGeometry()->releaseGLObjects();

}

void TriangleFanDrawArrayLengthsIterator::init(osg::Geometry * searchGeometry,
                                               osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;

    m_numberOfPolys = 0;
    //find the numberOfPrimitives in this primitive set
    osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)m_spPrimSet.get();
    for (osg::DrawArrayLengths::vector_type::const_iterator itr = primSet->begin(); itr != primSet->end(); ++itr)
    {
        m_numberOfPolys += (*itr - 2);
    }
}

bool TriangleFanDrawArrayLengthsIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setPrimSetIndex(index);

        osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)m_spPrimSet.get();
        osg::DrawArrayLengths::vector_type::const_iterator itr = primSet->begin();
        unsigned int polysTraversed;
        unsigned int verticesTraversed;
        unsigned int primSetStart = primSet->getFirst();
        for (polysTraversed = 0, verticesTraversed = primSetStart; itr != primSet->end(); ++itr)
        {
            unsigned int curPolyCount = (*itr - 2);
            //if in this fan
            if (index <= (polysTraversed + curPolyCount))
            {
                poly.setGeometry(m_spGeometry.get());
                poly.setPrimitiveSet(m_spPrimSet.get());

                index -= polysTraversed;
                poly.setIndices(verticesTraversed, index + verticesTraversed + 1, index + verticesTraversed + 2);

                return true;

            }

            verticesTraversed += *itr;
            polysTraversed += curPolyCount;

        }

    }

    return false;
}



void TriangleFanDrawArrayLengthsEraser::deletePoly(Primitive& poly)
{
    //next part will find if it is middle,end or begining triangle and thus how to delete
    osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)poly.getPrimitiveSet();
    unsigned int polyIndex = poly.getPrimSetIndex();

    unsigned int curFirstVtx = primSet->getFirst();
    unsigned int curFirstPoly = 0;
    osg::DrawArrayLengths::vector_type::iterator itr = primSet->begin();
    unsigned int itrIndex = 0;
    for (; itr != primSet->end(); ++itr, ++itrIndex)
    {
        unsigned int curVtxCount = *itr;
        unsigned int curPolyCount = curVtxCount - 2;
        
        if(polyIndex >= curFirstPoly && polyIndex < (curFirstPoly + curPolyCount))
        {
            osg::DrawElementsUInt* secondPrimSet = new osg::DrawElementsUInt();
            secondPrimSet->push_back(curFirstVtx);

            unsigned int startVtxIndex = curFirstVtx + ((polyIndex - curFirstPoly) + 2);
            for(unsigned int index = startVtxIndex; index < (curFirstVtx + curVtxCount); ++index)
            {
                secondPrimSet->push_back(index);
            }

            poly.getGeometry()->addPrimitiveSet(secondPrimSet);

            osg::DrawArrayLengths* thirdPrimSet = new osg::DrawArrayLengths(*primSet, 0);
            //erase from beginning up to and including the itr
            thirdPrimSet->erase(thirdPrimSet->begin(), thirdPrimSet->begin() + (itrIndex + 1));
            thirdPrimSet->setFirst(curFirstVtx + curVtxCount);

            poly.getGeometry()->addPrimitiveSet(thirdPrimSet);
            
            //erase everything from the itr to the end
            primSet->erase(itr, primSet->end());

            updateColorsAndNormals(poly, 2/*primitiveSetsAdded*/);
    
            //must be called or deletion will not occur
            poly.getGeometry()->dirtyBound();
            poly.getGeometry()->releaseGLObjects();
            break;
        }

        curFirstVtx += curVtxCount;
        curFirstPoly += curPolyCount;
    }
}


void TriangleFanDrawElementsIterator::init(osg::Geometry * searchGeometry,
                                           osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    m_numberOfPolys = (primitiveSet->getNumIndices() - 2);    //starts at 0
}

bool TriangleFanDrawElementsIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);

        DrawElementsAnyType primSet(m_spPrimSet.get());

        poly.setIndices(primSet.at(0), primSet.at(index + 1), primSet.at(index + 2));

        return true;
    }
}


void TriangleFanDrawElementsEraser::deletePoly(Primitive& poly)
{
    //next part will find if it is middle,end or begining triangle and thus how to delete
    unsigned int index = poly.getPrimSetIndex(); //this is then the last point of the triangle
    DrawElementsAnyType primSet(poly.getPrimitiveSet());

    unsigned int primitiveSetsAdded = 0;
    //begining one
    if (index == 0)
    { 
        //erase the first vertice's index, so we jump right to the second one
        primSet.erase(1);
        //primSet.insert(1, primSet.at(0));
        //primSet.erase(0);
    }
    //middle one
    //there is one vertex to specify each triangle except the first, last, and the fan vertex
    else if (index < primSet.size()-3) 
    {   //break up into two primitive sets, the second one starts at index 
        primSet.erase(index+2, primSet.end());
        
        DrawElementsAnyType secondPrimSet(primSet);
        //don't erase the fan vertex
        secondPrimSet.erase(secondPrimSet.begin()+1, index+2);
        poly.getGeometry()->addPrimitiveSet(secondPrimSet.getPrimitiveSet());

        primitiveSetsAdded = 1;
    }
    //end one
    else
    {   //delete last point
        primSet.erase(primSet.end() - 1, primSet.end());
    }

    updateColorsAndNormals(poly, primitiveSetsAdded);

    //must be called or deletion will not occur
    poly.getGeometry()->dirtyBound();
    poly.getGeometry()->releaseGLObjects();

}

void QuadDrawArraysIterator::init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    osg::DrawArrays * primSet = (osg::DrawArrays*)m_spPrimSet.get();
    m_numberOfPolys = primSet->getCount() / 4;
}

bool QuadDrawArraysIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);

        osg::DrawArrays * primSet = (osg::DrawArrays*)m_spPrimSet.get();
        unsigned int vtxArrayIndex = primSet->getFirst() + (index*4);
        poly.setIndices(vtxArrayIndex, vtxArrayIndex + 1, vtxArrayIndex + 2, vtxArrayIndex + 3);

        return true;
    }
}


void QuadDrawArraysEraser::deletePoly(Primitive& poly)
{

    //next part will find if it is middle,end or begining triangle and thus how to delete
    unsigned int index = poly.getPrimSetIndex()*4;
    osg::DrawArrays * primSet = (osg::DrawArrays*)poly.getPrimitiveSet();
    osg::Geometry* geometry = poly.getGeometry();

    unsigned int primitiveSetsAdded = 0;

    if (index == 0)
    {                           //change count and first
        primSet->setCount(primSet->getCount() - 4);
        primSet->setFirst(primSet->getFirst() + 4);
    }
    else if (index < static_cast<unsigned int>(primSet->getCount()) - 4)
    {                           //break up primitive into two at the triangle to be deleted and thus exclude it
        osg::DrawArrays * secondPrimSet = new osg::DrawArrays(*primSet, 0); //0 for shallow copy
        secondPrimSet->setFirst(primSet->getFirst() + index + 4);
        secondPrimSet->setCount(primSet->getCount() - (index + 4));
        poly.getGeometry()->addPrimitiveSet(secondPrimSet);
        
        primSet->setCount(index); 

        primitiveSetsAdded = 1;
    }
    else
    {
        primSet->setCount(primSet->getCount() - 4);
    }

    updateColorsAndNormals(poly, primitiveSetsAdded);

    //must be called or deletion will not occur
    geometry->dirtyBound();
    geometry->releaseGLObjects();
}



void QuadDrawArrayLengthsIterator::init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    m_numberOfPolys = primitiveSet->getNumIndices() / 4;
}

bool QuadDrawArrayLengthsIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);

        osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)m_spPrimSet.get();
        unsigned int vtxArrayIndex = primSet->getFirst() + (index*4);
        poly.setIndices(vtxArrayIndex, vtxArrayIndex + 1, vtxArrayIndex + 2, vtxArrayIndex + 3);

        return true;

    }
}

void QuadDrawArrayLengthsEraser::deletePoly(Primitive& poly)
{
    //next part will find if it is middle,end or begining triangle and thus how to delete
    osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)poly.getPrimitiveSet();

    //this is the index of the first vertex in the triangle
    unsigned int polyIndex = static_cast<unsigned int>(primSet->getFirst()) + (poly.getPrimSetIndex()*4);
    //if this is the first triangle then we can skip all the complicated crap below
    if (polyIndex == static_cast<unsigned int>(primSet->getFirst()))
    {
        //just change the first and the count of the first element
        primSet->setFirst(polyIndex + 4);
        //we could potentially be changing the count here to zero,
        //but its not a huge deal if we do it will just start at the 
        //next length
        (*primSet)[0] = primSet->at(0)-4;

        updateColorsAndNormals(poly, 0/*primitiveSetsAdded*/);

        //must be called or deletion will not occur
        poly.getGeometry()->dirtyBound();
        poly.getGeometry()->releaseGLObjects();
        return;
    }

    unsigned int currentVtxStart = static_cast<unsigned int>(primSet->getFirst());

    osg::DrawArrayLengths::vector_type::iterator itr = primSet->begin();
    unsigned int itrIndex = 0;
    for (; itr != primSet->end(); ++itr, ++itrIndex)
    {
        unsigned int currentVtxEnd = currentVtxStart + *itr;

        //figure out if the current triangle is part of the current
        //draw array length
        if (polyIndex >= currentVtxStart && polyIndex < currentVtxEnd)
        {
            //just break into two primitive sets and exclude the triangle to be deleted
            osg::DrawArrayLengths * secondPrimSet = new osg::DrawArrayLengths(*primSet, 0); //0 for shallow copy
            //delete from the beginning up to, but not including, the current itr
            secondPrimSet->erase(secondPrimSet->begin(), secondPrimSet->begin() + itrIndex);
            //change the starting point of second prim set to be just beyond the end
            //of the current triangle
            secondPrimSet->setFirst(polyIndex+4);
            (*secondPrimSet)[0] = secondPrimSet->at(0)-4;

            poly.getGeometry()->addPrimitiveSet(secondPrimSet);
            //delete everything after the current itr
            primSet->erase(itr+1, primSet->end());
            //then modify the value (i.e. the length) of the current itr
            (*primSet)[itrIndex] = polyIndex - currentVtxStart;

            updateColorsAndNormals(poly, 1/*primitiveSetsAdded*/);

            //must be called or deletion will not occur
            poly.getGeometry()->dirtyBound();
            poly.getGeometry()->releaseGLObjects();
            break;
        }

        currentVtxStart = currentVtxEnd;
    }

}

void QuadDrawElementsIterator::init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    DrawElementsAnyType primSet(m_spPrimSet.get());
    m_numberOfPolys = primSet.size() / 4;
}

bool QuadDrawElementsIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);
        DrawElementsAnyType primSet(m_spPrimSet.get());
        unsigned int vtxIndex = index*4;
        poly.setIndices(primSet.at(vtxIndex), primSet.at(vtxIndex + 1), primSet.at(vtxIndex + 2), primSet.at(vtxIndex + 3));

        return true;
    }
}



void QuadDrawElementsEraser::deletePoly(Primitive& poly)
{

    //next part will find if it is middle,end or begining triangle and thus how to delete
    unsigned int index = poly.getPrimSetIndex()*4; //this is then the last point of the triangle
    DrawElementsAnyType primSet(poly.getPrimitiveSet());

    unsigned int primitiveSetsAdded = 0;

    if (index == 0)
    {                           //delete first three elements
        primSet.erase(primSet.begin(), primSet.begin() + 4);
    }
    else if (index < primSet.size() - 4)
    {                           //break up into two primitives,assuming sequential and no repeats 
        //break up primitive into two at the triangle to be deleted and thus exclude it  
        DrawElementsAnyType secondPrimSet(primSet);
        secondPrimSet.erase(secondPrimSet.begin(), secondPrimSet.begin() + index + 4);
        poly.getGeometry()->addPrimitiveSet(secondPrimSet.getPrimitiveSet());
        
        primSet.erase(primSet.begin() + index, primSet.end());

        primitiveSetsAdded = 1;
    }
    else
    {                           //delete last four elements
        primSet.erase(index, primSet.end());
    }

    updateColorsAndNormals(poly, primitiveSetsAdded);

    //must be called or deletion will not occur
    poly.getGeometry()->dirtyBound();
    poly.getGeometry()->releaseGLObjects();
}

void QuadStripDrawArraysIterator::init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    osg::DrawArrays * primSet = (osg::DrawArrays*)m_spPrimSet.get();
    m_numberOfPolys = (primSet->getCount() - 2) / 2;
}

bool QuadStripDrawArraysIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);
        /*
           for a QuadStripDrawArrays the second quad has its 
           first point at vertex array index 2
         */
        osg::DrawArrays * primSet = (osg::DrawArrays*)m_spPrimSet.get();
        index *= 2;
        index += primSet->getFirst();
        poly.setIndices(index, index + 1, index + 2, index + 3);

        return true;
    }
}

void QuadStripDrawArraysEraser::deletePoly(Primitive& poly)
{
    //next part will find if it is middle,end or begining triangle and thus how to delete
    //this is the index of the first vertex in the poly
    unsigned int index = (poly.getPrimSetIndex() << 1);
    osg::DrawArrays * primSet = (osg::DrawArrays*)poly.getPrimitiveSet();
    osg::Geometry* geometry = poly.getGeometry();

    unsigned int primitiveSetsAdded = 0;

    //first one
    if (index == 0)
    {                           //change first and count
        primSet->setCount(primSet->getCount() - 2);
        primSet->setFirst(primSet->getFirst() + 2);
    }
    //middle one
    else if (index > ((unsigned int)primSet->getFirst() +3) && index < (unsigned int)(primSet->getFirst() + primSet->getCount() - 1))
    {                           //break up into two primitive sets
        //break up primitive into two at the triangle to be deleted and thus exclude it
        osg::DrawArrays * secondPrimSet = new osg::DrawArrays(*primSet, 0); //0 for shallow copy
        secondPrimSet->setFirst(primSet->getFirst() + (index + 2));
        secondPrimSet->setCount(primSet->getCount() - (index + 2));
        
        geometry->addPrimitiveSet(secondPrimSet);
        
        primSet->setCount(index + 2);

        primitiveSetsAdded = 1;
    }
    //end one
    else
    {                           //change count
        primSet->setCount(primSet->getCount() - 2);
    }

    updateColorsAndNormals(poly, primitiveSetsAdded);
    //must be called or deletion will not occur
    geometry->dirtyBound();
    geometry->releaseGLObjects();

}

void QuadStripDrawArrayLengthsIterator::init(osg::Geometry * searchGeometry, 
                                             osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet = primitiveSet;

    m_numberOfPolys = 0;
    //find the numberOfPrimitives in this primitive set
    osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)m_spPrimSet.get();
    for (osg::DrawArrayLengths::vector_type::const_iterator itr = primSet->begin(); 
         itr != primSet->end(); 
         ++itr)
    {
        m_numberOfPolys += (*itr - 2) / 2;
    }

}

bool QuadStripDrawArrayLengthsIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        /*
           basic algorithm here is that for each element value
           there are (element value - 2)/2 quads.
         */
        osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)m_spPrimSet.get();
        osg::DrawArrayLengths::vector_type::const_iterator itr = primSet->begin();
        unsigned int polysTraversed;
        for (polysTraversed = 0; itr != primSet->end(); ++itr)
        {
            unsigned int curPolyCount = (unsigned int)((*itr-2)*0.5f);
            //if the polygon is in the current strip
            if (index <= (polysTraversed + curPolyCount))
            {
                poly.setGeometry(m_spGeometry.get());
                poly.setPrimitiveSet(m_spPrimSet.get());

                poly.setPrimSetIndex(index);

                index *= 2;
                index += primSet->getFirst();
                poly.setIndices(index, index + 1, index + 2, index + 3);

                return true;
            }

            polysTraversed += curPolyCount;
        }

        return false;
    }
}

void QuadStripDrawArrayLengthsEraser::deletePoly(Primitive& poly)
{
    //next part will find if it is middle,end or begining triangle and thus how to delete
    osg::DrawArrayLengths* primSet = (osg::DrawArrayLengths*)poly.getPrimitiveSet();
    unsigned int polyIndex = poly.getPrimSetIndex();

    //if this is the first triangle in the strip
    //then can skip the complicated crap below
    if(polyIndex == 0)
    {
        //change the first
        primSet->setFirst(primSet->getFirst()+2);
        (*primSet)[0] = primSet->at(0)-2;

        updateColorsAndNormals(poly, 1/*primSetsAdded*/);

        //must be called or deletion will not occur
        poly.getGeometry()->dirtyBound();
        poly.getGeometry()->releaseGLObjects();
        return;
    }

    osg::DrawArrayLengths::vector_type::iterator itr = primSet->begin();
    unsigned int itrIndex = 0;
    unsigned int curPolyStartIndex = 0;
    unsigned int curFirst = primSet->getFirst();
    
    for (; itr != primSet->end(); ++itr, ++itrIndex)
    {
        unsigned int curVtxCount = *itr;
        //vertex count minus two then divided by 2
        unsigned int curPolyCount = ((curVtxCount - 2) >> 1);
        
        if(polyIndex >= curPolyStartIndex && polyIndex < (curPolyStartIndex + curPolyCount))
        {
            osg::DrawArrayLengths * secondPrimSet = new osg::DrawArrayLengths(*primSet, 0); //0 for shallow copy
            //erase everything up to, but not including, the current itr
            secondPrimSet->erase(secondPrimSet->begin(), secondPrimSet->begin() + itrIndex);

            unsigned int polyFirstIndex = curFirst + (polyIndex << 1);
            secondPrimSet->setFirst(curFirst + (polyFirstIndex + 2));
            //update the count
            (*secondPrimSet)[0] = secondPrimSet->at(0) - (secondPrimSet->getFirst() - curFirst);
            poly.getGeometry()->addPrimitiveSet(secondPrimSet);

            //now erase from itr+1 to end
            primSet->erase(itr+1, primSet->end());
            //change the count for itr
            (*primSet)[itrIndex] = primSet->at(itrIndex) - secondPrimSet->at(0);
            
            updateColorsAndNormals(poly, 1/*primSetsAdded*/);
    
            //must be called or deletion will not occur
            poly.getGeometry()->dirtyBound();
            poly.getGeometry()->releaseGLObjects();
            break;
        }

        curPolyStartIndex += curPolyCount;
        curFirst += curVtxCount;
    }
}

void QuadStripDrawElementsIterator::init(osg::Geometry * searchGeometry, osg::PrimitiveSet * primitiveSet)
{
    m_spGeometry = searchGeometry;
    m_spPrimSet =  primitiveSet;
    m_numberOfPolys = (primitiveSet->getNumIndices() - 2) / 2;
}

bool QuadStripDrawElementsIterator::getPoly(unsigned int index, Primitive& poly)
{
    if (index >= m_numberOfPolys)
        return false;
    else
    {
        poly.setGeometry(m_spGeometry.get());
        poly.setPrimitiveSet(m_spPrimSet.get());

        poly.setPrimSetIndex(index);

        DrawElementsAnyType primSet(m_spPrimSet.get());
        index *= 2;
        poly.setIndices(primSet.at(index), primSet.at(index + 1), primSet.at(index + 2), primSet.at(index + 3));

        return true;
    }
}

void QuadStripDrawElementsEraser::deletePoly(Primitive& poly)
{


    //next part will find if it is middle,end or begining triangle and thus how to delete
    unsigned int index = (poly.getPrimSetIndex() << 1);
    DrawElementsAnyType primSet(poly.getPrimitiveSet());

    unsigned int primSetsAdded = 0;
    //begining one
    if (index == 0)
    {
        primSet.erase(primSet.begin(), primSet.begin() + 2);
    }
    //middle one
    else if (index < primSet.size() - 4)
    {
        //break up primitive into two at the triangle to be deleted and thus exclude it
        DrawElementsAnyType secondPrimSet(primSet);
        secondPrimSet.erase(secondPrimSet.begin(), secondPrimSet.begin() + index + 2);
        poly.getGeometry()->addPrimitiveSet(secondPrimSet.getPrimitiveSet());

        primSet.erase(primSet.begin() + index + 2, primSet.end());

        primSetsAdded = 1;
    }
    //end one
    else
    {
        primSet.erase(primSet.end() - 2, primSet.end());
    }

    updateColorsAndNormals(poly, primSetsAdded);

    //must be called or deletion will not occur
    poly.getGeometry()->dirtyBound();
    poly.getGeometry()->releaseGLObjects();
}

class OsgFileReader : public osgDB::ReadFileCallback
{
public:
    bool loadImageData;
    std::map<std::string, osg::ref_ptr<osg::Image> > cachedImages;
    osg::ref_ptr<osgDB::Options> spImageLoadOptions;
    OsgFileReader() : loadImageData(false), spImageLoadOptions(new osgDB::Options())
    {
        spImageLoadOptions->setObjectCacheHint(osgDB::Options::CACHE_NONE);
    }

    virtual osgDB::ReaderWriter::ReadResult readImage(const std::string& filename, const osgDB::Options* options)
    {
        if(this->loadImageData)
        {
            std::string newFilename = filename;
            if(osgDB::getFileExtension(newFilename) != "tga")
            {
                newFilename = osgDB::getNameLessExtension(newFilename);
                newFilename += ".tga";
                std::string findFilename = osgDB::findDataFile(newFilename);
                if(findFilename.length() == 0)
                {
                    newFilename = osgDB::getNameLessExtension(newFilename);
                    newFilename += ".bmp";
                }
            }
            
            newFilename = osgDB::findDataFile(newFilename);
            newFilename = osgDB::getRealPath(newFilename);
                
            std::map<std::string, osg::ref_ptr<osg::Image> >::iterator findIt = cachedImages.find(newFilename);
            if(findIt == cachedImages.end())
            {
                osgDB::ReaderWriter::ReadResult result = osgDB::ReadFileCallback::readImage(newFilename,
                                                                                            spImageLoadOptions.get());
                if(result.getImage() != nullptr)
                {
                    if(cachedImages.size() == 5)
                        cachedImages.clear();
                    /*if(result.getImage()->getPixelFormat() != GL_RGBA ||
                       result.getImage()->getDataType() != GL_UNSIGNED_BYTE)
                    {
                        if(!cuda::VoxelBrickWriter::ConvertImageToRGBA8(result.getImage()))
                            return nullptr;
                    }*/
                    if(result.getImage()->getPixelFormat() == GL_RGB)
                    {
                        int imageWidth = result.getImage()->s();
                        int imageHeight = result.getImage()->t();
                        osg::Vec4ub* pRGBA = new osg::Vec4ub[imageWidth * imageHeight];
                        for(int row = 0; row < imageHeight; ++row)
                        {
                            for(int col = 0; col < imageWidth; ++col)
                            {
                                const unsigned char* color = result.getImage()->data(col, row);
                                pRGBA[(row*imageWidth) + col] = osg::Vec4ub(color[0], color[1], color[2], 255);
                            }
                        }
                        result.getImage()->setImage(imageWidth,
                                                    imageHeight, 
                                                    1,
                                                    result.getImage()->getInternalTextureFormat(),
                                                    GL_RGBA,
                                                    result.getImage()->getDataType(),
                                                    (unsigned char*)pRGBA, 
                                                    osg::Image::USE_NEW_DELETE);
                    }
                    cachedImages[newFilename] = result.getImage();
                }
                return result;
            }
            return findIt->second.get();
        }
        else
        {
            osg::Image* pImage = new osg::Image();
            pImage->setFileName(filename);
            return pImage;
        }
    }
};

static OsgFileReader* s_pOsgFileReader = nullptr;

typedef std::vector<glm::vec3> TriangleVerts;
typedef std::vector<glm::vec3> VertexNormals;
typedef std::vector<glm::vec2> TriangleTexCoords;
struct TriData
{
    TriangleVerts triangles;
    VertexNormals normals;
    TriangleTexCoords triangleTexCoords;
    osg::BoundingBox triBBox;
};
typedef std::map< osg::ref_ptr<osg::Texture2D>, TriData> TriangleData;
typedef std::set<std::string> TerrainTextureSet;

static cudaTextureAddressMode OpenGLModeToCudaMode(osg::Texture::WrapMode wrapMode)
{
    switch(wrapMode)
    {
    case osg::Texture::CLAMP_TO_BORDER:
        return cudaAddressModeBorder;
    case osg::Texture::REPEAT:
        return cudaAddressModeWrap;
    case osg::Texture::MIRROR:
        return cudaAddressModeMirror;
    case osg::Texture::CLAMP:
    case osg::Texture::CLAMP_TO_EDGE:
    default:
        return cudaAddressModeClamp;
    
    }
}

int Voxelize(cuda::Voxelizer& voxelizer,
             const glm::vec3& p,
             const glm::vec3& deltaP,
             TriangleData& triData,
             const std::string& terrainTexturePrefix,
             const TerrainTextureSet& terrainTextures,
             bool hasNormals,
             const std::string& outputDir,
             bool outputBinary,
             bool outputCompressed)
{
    voxelizer.setVoxelizationParams(p, deltaP);

    //load images and triangle data into voxelizer
    std::vector< osg::ref_ptr<osg::Image> > images;
    //load images now
    s_pOsgFileReader->loadImageData = true;

    for(TriangleData::iterator itr = triData.begin();
        itr != triData.end();
        ++itr)
    {
        osg::Texture2D* pTexture = itr->first;
        unsigned char* pImageData = nullptr;
        int imageWidth = 0;
        int imageHeight = 0;
        bool isTerrain = pTexture != nullptr;
        if(pTexture != nullptr)
        {
            osg::ref_ptr<osg::Image> spImage = pTexture->getImage();
            if(spImage->data() == nullptr)
            {
                std::string filePath;
                if(pTexture->getUserValue("TexImagePath", filePath))
                {
                    filePath += "/";
                    filePath += spImage->getFileName();
                }
                else
                    filePath = spImage->getFileName();
                spImage = osgDB::readImageFile(filePath);
            }
            //float u = 0.006932;
            //float v = 0.006932;
            //osg::Vec4 color = spImage->getColor(u, v);
            if(spImage.get() != nullptr)
            {
                pImageData = spImage->data();
                images.push_back(spImage.get());
                imageWidth = spImage->s();
                imageHeight = spImage->t();
                
                std::string filename = osgDB::getSimpleFileName(spImage->getFileName());
                if(filename.find(terrainTexturePrefix) != std::string::npos ||
                    terrainTextures.find(filename) != terrainTextures.end())
                {
                    isTerrain = true;
                }
                else
                    isTerrain = false;
            }
            else
            {
                std::cerr << "Failed to load image " << spImage->getFileName() << std::endl;
            }
        }

        cudaTextureAddressMode texAddressMode0 = OpenGLModeToCudaMode(pTexture->getWrap(osg::Texture::WRAP_S));
        cudaTextureAddressMode texAddressMode1 = OpenGLModeToCudaMode(pTexture->getWrap(osg::Texture::WRAP_T));

        voxelizer.addTriangleGroup(&itr->second.triangles, 
                                   hasNormals ? &itr->second.normals : nullptr,
                                   &itr->second.triangleTexCoords,
                                   glm::vec3(itr->second.triBBox.xMin(),
                                             itr->second.triBBox.yMin(),
                                             itr->second.triBBox.zMin()),
                                   glm::vec3(itr->second.triBBox.xMax(),
                                             itr->second.triBBox.yMax(),
                                             itr->second.triBBox.zMax()),
                                   isTerrain,
                                   pImageData,
                                   imageWidth, imageHeight,
                                   texAddressMode0,
                                   texAddressMode1);
    }

    s_pOsgFileReader->loadImageData = false;

    if(!voxelizer.allocateTriangleMemory())
    {
        std::cerr << "allocateTriangleMemory() failed." << std::endl;
        std::cerr << voxelizer.getErrorMessage() << std::endl;
        return 1;
    }

    if(!voxelizer.computeEdgesFaceNormalsAndBounds())
    {
        std::cerr << "computeEdgesFaceNormalsAndBounds() failed." << std::endl;
        std::cerr << voxelizer.getErrorMessage() << std::endl;
        return 1;
    }

    int retStatus = voxelizer.generateVoxelsAndOctTree(outputDir, outputBinary, outputCompressed);
    if(retStatus == 0)
    {
        std::cerr << "generateVoxelsAndOctTree failed." << std::endl;
        std::cerr << voxelizer.getErrorMessage() << std::endl;
        return 1;
    }
    else if(retStatus == -1)
    {
        std::cerr << "No triangles overlapped with voxel grid." << std::endl;
        return -1;
    }

    return 0;
}


void cpuTest(const glm::vec3* verts, 
             int numVerts,
             const glm::vec3& p,
             const glm::vec3& deltaP,
             int *voxels,
             const glm::uvec3& voxDim);

class PagedGigaVoxelOctTreeGenerator
{
private:
    glm::vec3 _voxelSizeMeters;
    glm::uvec3 _brickDimensions;
    glm::uvec3 _maxVoxelMipMapDimensions;
    glm::vec3 _maxVoxelMipMapSizeMeters;

    osg::ref_ptr<osg::Node> _spInputNode;
    std::string _inputDir;
    std::string _outputDir;
    std::string _progressFile;

public:
    PagedGigaVoxelOctTreeGenerator() {}
    ~PagedGigaVoxelOctTreeGenerator() {}

    void setVoxelizationParams(const glm::vec3& voxelSizeMeters, 
                               const glm::uvec3& brickDimensions, 
                               const glm::uvec3& maxVoxelMipMapDimension)
    {
        _voxelSizeMeters = voxelSizeMeters;
        _brickDimensions = brickDimensions;
        _maxVoxelMipMapDimensions = maxVoxelMipMapDimension;

        _maxVoxelMipMapSizeMeters = static_cast<glm::vec3>(_maxVoxelMipMapDimensions) * _voxelSizeMeters;
    }

    void setInput(const std::string& inputFileName, osg::Node* pNode)
    {
        _inputDir = osgDB::getFilePath(inputFileName);
        osgDB::Registry::instance()->getDataFilePathList().push_back(_inputDir);
        _spInputNode = pNode;
        _spInputNode->getBound();//initialize bounds
    }

    void setOutputDirectory(const std::string& outputDir)
    {
        _outputDir = outputDir;
        _progressFile = _outputDir;
        _progressFile += "/";
        _progressFile += "pgvot_progress.txt";
    }

    struct Triangle
    {
        osg::Vec3f v0;
        osg::Vec3f v1;
        osg::Vec3f v2;
        osg::Vec3f n0;
        osg::Vec3f n1;
        osg::Vec3f n2;
        osg::Vec2f uv0;
        osg::Vec2f uv1;
        osg::Vec2f uv2;
        bool hasNormals;
        Triangle() {}
        Triangle(const osg::Vec3& _v0,const osg::Vec3& _v1,const osg::Vec3& _v2) :
        v0(_v0), v1(_v1), v2(_v2), hasNormals(false) {}
        void setUVs(const osg::Vec2& _uv0, const osg::Vec2& _uv1, const osg::Vec2& _uv2)
        {
            uv0 = _uv0;
            uv1 = _uv1;
            uv2 = _uv2;
        }
        void setNormals(const osg::Vec3& _n0,const osg::Vec3& _n1,const osg::Vec3& _n2)
        {
            n0 = _n0;
            n1 = _n1;
            n2 = _n2;
            hasNormals = true;
        }
    };

    typedef std::vector<Triangle> Triangles;
    typedef std::map< osg::ref_ptr<osg::Texture2D> , Triangles> TrianglesPerTexture;
    typedef std::string PagedFileName;
    typedef std::pair< osg::ref_ptr<osg::MatrixTransform>, PagedFileName> PagedFilePair;
    typedef std::vector< PagedFilePair > PagedFiles;
        
    class TriangleCollector : public osg::NodeVisitor
    {
   public:
        TriangleCollector() : 
            osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN) {}

        osg::ref_ptr<osg::RefMatrixd> curWorldTransform;
        osg::BoundingBox filterBBox;
        TrianglesPerTexture trianglesPerTexture;
        //PagedFiles pagedFiles;
        typedef std::list<std::string> PagedFileUsageQueue;
        PagedFileUsageQueue pagedFileUsageQueue;
        typedef std::map<std::string, std::pair<osg::ref_ptr<osg::Node>, PagedFileUsageQueue::iterator> > PagedFileCache;
        PagedFileCache pagedFileCache;
        std::string curFilePath;

        class TriangleAttributeFunctor : public osg::Drawable::AttributeFunctor
        {
        private:
            osg::MatrixList worldMats;
            osg::MatrixList worldRotMats;
        public:
            osg::BoundingBox filterBBox;
            TrianglesPerTexture& trianglesPerTexture;
            void setWorldMats(const osg::MatrixList& mlist)
            {
                worldMats = mlist;
                for(size_t matIndex = 0; 
                        matIndex < worldMats.size(); 
                        ++matIndex)
                {
                    const osg::Matrix& worldMat = worldMats.at(matIndex);
                    worldRotMats.resize(worldRotMats.size()+1);
                    worldRotMats.back() = osg::Matrix(worldMat.getRotate());
                }
            }
            TriangleAttributeFunctor(TrianglesPerTexture& tpt) : 
                trianglesPerTexture(tpt) {}

            void assignTrianglesToTexture(osg::Texture2D* pTexture, PrimitiveSetIterator* pItr)
            {
                TrianglesPerTexture::iterator findIt = trianglesPerTexture.find(pTexture);

                if(findIt == trianglesPerTexture.end())
                    findIt = trianglesPerTexture.insert(std::make_pair(pTexture, Triangles())).first;

                for(size_t i = 0; i < pItr->getNumberOfPolys(); ++i)
                {
                    Primitive poly;
                    pItr->getPoly(i, poly);
                    if(poly.getVtxCount() != 3)
                        continue;
                    this->insert(poly,
                                 findIt->second);
                }
            }

            void insert(Primitive& poly, 
                        Triangles& triangles)
            {
                osg::Vec3d v1 = poly.getVertex(0);
                osg::Vec3d v2 = poly.getVertex(1);
                osg::Vec3d v3 = poly.getVertex(2);

                if(worldMats.size() == 0)
                {
                    osg::BoundingBox triBBox;
                    triBBox.expandBy(v1);
                    triBBox.expandBy(v2);
                    triBBox.expandBy(v3);
                    if(!filterBBox.intersects(triBBox))
                    {
                        //if triangle completely outside then throw out
                        return;
                    }

                    Triangle tri(v1, v2, v3);
                    if(poly.hasTextureData())
                        tri.setUVs(poly.getTexCoord(0, 0),
                                   poly.getTexCoord(0, 1),
                                   poly.getTexCoord(0, 2));
                    if(poly.hasNormalData() &&
                       poly.getNormalBinding() == osg::Geometry::AttributeBinding::BIND_PER_VERTEX)
                    {
                        tri.setNormals(poly.getNormal(0),
                                       poly.getNormal(1),
                                       poly.getNormal(2));
                    }
                    triangles.push_back(tri);
                }
                else
                {
                    //transform to world space
                    for(size_t matIndex = 0; 
                        matIndex < worldMats.size(); 
                        ++matIndex)
                    {
                        const osg::Matrix& worldMat = worldMats.at(matIndex);
                        const osg::Matrix& rotMat = worldRotMats.at(matIndex);
                        osg::Vec3d worldVtx1 = worldMat.preMult(v1);
                        osg::Vec3d worldVtx2 = worldMat.preMult(v2);
                        osg::Vec3d worldVtx3 = worldMat.preMult(v3);
                        osg::BoundingBox triBBox;
                        triBBox.expandBy(worldVtx1);
                        triBBox.expandBy(worldVtx2);
                        triBBox.expandBy(worldVtx3);
                        if(!filterBBox.intersects(triBBox))
                        {
                            return;
                        }
                        
                        triangles.resize(triangles.size()+1);
                        Triangle& tri = triangles.at(triangles.size()-1);
                        tri.v0 = worldVtx1;
                        tri.v1 = worldVtx2;
                        tri.v2 = worldVtx3;
                        if(poly.hasTextureData())
                            tri.setUVs(poly.getTexCoord(0, 0),
                                       poly.getTexCoord(0, 1),
                                       poly.getTexCoord(0, 2));

                        if(poly.hasNormalData() &&
                           poly.getNormalBinding() == osg::Geometry::AttributeBinding::BIND_PER_VERTEX)
                        {
                            osg::Vec3d n0 = poly.getNormal(0);
                            //osg::Vec3d t = v1 + n0;
                            n0 = rotMat.preMult(n0);
                            //t = worldMat.preMult(t);
                            //osg::Vec3d t2 = tri.v0 + n0;
                            
                            osg::Vec3d n1 = poly.getNormal(1);
                            n1 = rotMat.preMult(n1);
                            
                            osg::Vec3d n2 = poly.getNormal(2);
                            n2 = rotMat.preMult(n2);

                            tri.setNormals(n0, n1, n2);
                        }
                    }
                }
            }
        };

        osg::BoundingBox transformBBox(const osg::BoundingBox& bbox) const
        {
            if(this->curWorldTransform.get() != nullptr)
            {
                osg::BoundingBox xformedBBox;
                for(size_t i = 0; i < 8; ++i)
                {
                    osg::Vec3d corner = bbox.corner(i);
                    osg::Vec3d xformed = this->curWorldTransform->preMult(corner);
                    xformedBBox.expandBy(xformed);
                }
                return xformedBBox;
            }
            return bbox;
        }

        osg::BoundingSphere transformBSphere(const osg::BoundingSphere& bsphere) const
        {
            if(this->curWorldTransform.get() != nullptr)
            {
                osg::Vec3d center = bsphere.center();
                //going to assume that the transform has no scaling factor
                center = this->curWorldTransform->preMult(center);
                osg::BoundingSphere newBSphere(center, bsphere.radius());
                return newBSphere;
            }

            return bsphere;
        }

        virtual void apply(osg::MatrixTransform& node) override
        {
            const osg::Matrixd& matrix = node.getMatrix();
            osg::ref_ptr<osg::RefMatrixd> saveCur = this->curWorldTransform;
            if(this->curWorldTransform.get() != nullptr)
            {
                osg::Matrixd combined = *this->curWorldTransform.get();
                combined *= matrix;
                this->curWorldTransform = new osg::RefMatrixd(combined);
            }
            else
                this->curWorldTransform = new osg::RefMatrixd(matrix);

            traverse(node);

            this->curWorldTransform = saveCur;
        }

        virtual void apply(osg::Geode& geode) override
        {
            if(this->filterBBox.intersects(transformBBox(geode.getBoundingBox())) == false)
            {
                traverse(geode);
                return;
            }

            TriangleAttributeFunctor tf(this->trianglesPerTexture);
            tf.filterBBox = this->filterBBox;
            tf.setWorldMats(geode.getWorldMatrices());
            //TODO test normal transformation
            for(size_t i = 0; i < geode.getNumDrawables(); ++i)
            {
                osg::Drawable* pDrawable = geode.getDrawable(i);
                osg::Geometry* pGeometry = dynamic_cast<osg::Geometry*>(pDrawable);
                if(pGeometry == nullptr)
                    continue;

                osg::Geometry::AttributeBinding normalBinding = 
                                            pGeometry->getNormalBinding();
                if(normalBinding != osg::Geometry::BIND_PER_VERTEX &&
                   normalBinding != osg::Geometry::BIND_PER_PRIMITIVE)
                {
                    osgUtil::SmoothingVisitor::smooth(*pGeometry);
                }

                for(size_t j = 0; j < pGeometry->getNumPrimitiveSets(); ++j)
                {
                    osg::PrimitiveSet* pPrimSet = pGeometry->getPrimitiveSet(j);
                    osg::ref_ptr<PrimitiveSetIterator> spItr = PrimitiveSetIterator::getIterator(pPrimSet, pGeometry);
                    osg::StateSet* pStateSet = pDrawable->getStateSet();
                    if(pStateSet == nullptr)
                        pStateSet = geode.getStateSet();
                    osg::Texture2D* pTexture = nullptr;
                    if(pStateSet != nullptr)
                    {
                        pTexture = dynamic_cast<osg::Texture2D*>(pStateSet->getTextureAttribute(0, osg::StateAttribute::TEXTURE));
                        if(pTexture)
                            pTexture->setUserValue("TexImagePath", this->curFilePath);
                    }
                    
                    tf.assignTrianglesToTexture(pTexture, spItr.get());
                }
            }

            traverse(geode);
        }

        virtual void apply(osg::ProxyNode& node) override
        {
            traverse(node);
        }

        virtual void apply(osg::PagedLOD& pagedLOD) override
        {
            osg::BoundingSphere bsphere = transformBSphere(pagedLOD.getBound());
            if(this->filterBBox.contains(bsphere.center()) == false)
            {
                osg::Vec2 bsphereXYCenter = osg::Vec2(bsphere.center().x(), bsphere.center().y());
                osg::Vec2 bboxXYCenter = osg::Vec2(this->filterBBox.center().x(), this->filterBBox.center().y());
                osg::Vec2 diffVector = bsphereXYCenter - bboxXYCenter;
                float dist2 = diffVector.length2();
                float bboxHalfWidth = (this->filterBBox.xMax() - this->filterBBox.xMin()) * 0.5f;
                float bboxHalfHeight = (this->filterBBox.yMax() - this->filterBBox.yMin()) * 0.5f;
                float bboxXYRadius2 = (bboxHalfWidth * bboxHalfWidth) + (bboxHalfHeight * bboxHalfHeight);
                if(dist2 > bsphere.radius2() + bboxXYRadius2)
                {
                    //traverse(pagedLOD);
                    return;
                }
            }

            if(pagedLOD.getNumFileNames() == 0)
            {
                traverse(pagedLOD);
                return;
            }

            //we only want the highest resolution
            for(size_t fileIndex = 0; fileIndex < pagedLOD.getNumFileNames(); ++fileIndex)
            {
                float minRange = pagedLOD.getMinRange(fileIndex);
                if(minRange > 0.0f)
                    continue;
                const std::string& pagedFile = pagedLOD.getFileName(fileIndex);

                std::string relativeFilePath = pagedLOD.getDatabasePath() + "/" + pagedFile;
                relativeFilePath = osgDB::findDataFile(relativeFilePath);
                std::string pagedFilePath = osgDB::getRealPath(relativeFilePath);

                PagedFileCache::iterator findIt = pagedFileCache.find(pagedFilePath);
                osg::ref_ptr<osg::Node> spNode;
                if(findIt == pagedFileCache.end())
                {
                    spNode = osgDB::readNodeFile(pagedFilePath);
                    this->pagedFileUsageQueue.push_front(pagedFilePath);
                    this->pagedFileCache[pagedFilePath] = std::make_pair(spNode, this->pagedFileUsageQueue.begin());
                    if(this->pagedFileCache.size() > 10)
                    {
                        const std::string& lruPagedFilePath = this->pagedFileUsageQueue.back();
                        this->pagedFileCache.erase(lruPagedFilePath);
                        this->pagedFileUsageQueue.pop_back();
                    }
                }
                else
                {
                    spNode = findIt->second.first;

                    PagedFileUsageQueue::iterator itr = findIt->second.second;
                    this->pagedFileUsageQueue.splice(this->pagedFileUsageQueue.begin(), 
                                                     this->pagedFileUsageQueue, 
                                                     itr);
                    findIt->second.second = this->pagedFileUsageQueue.begin();
                }

                if(spNode.get() == nullptr)
                    std::cerr << "ERROR: failed to load external file " << pagedFilePath << std::endl;
                else
                {
                    osg::MatrixList worldMats = pagedLOD.getWorldMatrices();
                    for(size_t matIndex = 0; 
                        matIndex < worldMats.size(); 
                        ++matIndex)
                    {
                        const osg::Matrix& worldMat = worldMats.at(matIndex);
                        
                        osg::ref_ptr<osg::MatrixTransform> spMatrixTransform = 
                                                                new osg::MatrixTransform();

                        spMatrixTransform->setMatrix(worldMat);
                        
                        spMatrixTransform->addChild(spNode.get());

                        //this->pagedFiles.push_back(std::make_pair(spMatrixTransform.get(), 
                                                                  //pagedFilePath));

                        //spMatrixTransform->removeChild(spNode.get());
                        spMatrixTransform->getBound();//initialize bounds
                        osg::ref_ptr<osg::RefMatrixd> saveCur = this->curWorldTransform;
                        std::string saveCurFilePath = this->curFilePath;
                        this->curWorldTransform = nullptr;
                        this->curFilePath = osgDB::getFilePath(pagedFilePath);
                        spMatrixTransform->accept(*this);
                        this->curWorldTransform = saveCur.get();
                        this->curFilePath = saveCurFilePath;
                    }
                }
            }
        }

        virtual void apply(osg::LOD& lod) override
        {
            osg::BoundingSphere bsphere = transformBSphere(lod.getBound());
            if(this->filterBBox.contains(bsphere.center()) == false)
            {
                osg::Vec2 bsphereXYCenter = osg::Vec2(bsphere.center().x(), bsphere.center().y());
                osg::Vec2 bboxXYCenter = osg::Vec2(this->filterBBox.center().x(), this->filterBBox.center().y());
                osg::Vec2 diffVector = bsphereXYCenter - bboxXYCenter;
                float dist2 = diffVector.length2();
                float bboxHalfWidth = (this->filterBBox.xMax() - this->filterBBox.xMin()) * 0.5f;
                float bboxHalfHeight = (this->filterBBox.yMax() - this->filterBBox.yMin()) * 0.5f;
                float bboxXYRadius2 = (bboxHalfWidth * bboxHalfWidth) + (bboxHalfHeight * bboxHalfHeight);
                if(dist2 > bsphere.radius2() + bboxXYRadius2)
                {
                    //traverse(lod);
                    return;
                }
            }

            if(lod.getNumChildren() == 0)
            {
                //traverse(lod);
                return;
            }

            //we only want the highest resolution
            for(size_t cIndex = 0; cIndex < lod.getNumChildren(); ++cIndex)
            {
                float minRange = lod.getMinRange(cIndex);
                if(minRange > 0.0f)
                    continue;
                osg::Node* pChild = lod.getChild(cIndex);

                pChild->accept(*this);
            }
        }
    };

    class TriangleOctTreeGrid
    {
    public:
        struct GridTree
        {
            glm::vec3 origin;
            osg::BoundingBox bbox;
            TriangleData triangleData;
            bool hasNormals;
            GridTree* pNeighbor;
            GridTree() : hasNormals(false), pNeighbor(NULL) {}
            ~GridTree() { if(pNeighbor != NULL) delete pNeighbor; }
            GridTree* addNeighborTree()
            {
                pNeighbor = new GridTree();
                pNeighbor->hasNormals = this->hasNormals;
                pNeighbor->origin = this->origin;
                pNeighbor->bbox = this->bbox;
                return pNeighbor;
            }

            bool floatsEqual(float a, float b) const
            {
                float diff = a - b;
                static float epsilon = 0.001f;
                return diff <= epsilon && diff >= -epsilon;
            }

            bool vertsEqual(const glm::vec3& lhs, const glm::vec3& rhs) const
            {
                if(floatsEqual(lhs.x, rhs.x) == false)
                    return false;
                if(floatsEqual(lhs.y, rhs.y) == false)
                    return false;
                return floatsEqual(lhs.z, rhs.z);
            }

            void add(osg::Texture2D* pTexture, const Triangle& tri)
            {
                TriangleData::iterator findIt = triangleData.find(pTexture);
                if(findIt == triangleData.end())
                    findIt = triangleData.insert(std::make_pair(pTexture, TriData())).first;
                TriData& triData = findIt->second;
                /*for(size_t itr = 0; itr < triData.triangles.size(); itr+=3)
                {
                    glm::vec3& v0 = triData.triangles.at(itr);
                    if(vertsEqual(v0, (glm::vec3&)tri.v0) == false)
                        break;
                    glm::vec3& v1 = triData.triangles.at(itr+1);
                    if(vertsEqual(v1, (glm::vec3&)tri.v1) == false)
                        break;
                    glm::vec3& v2 = triData.triangles.at(itr+2);
                    if(vertsEqual(v2, (glm::vec3&)tri.v2) == false)
                        break;
                    return;
                }*/

                triData.triBBox.expandBy(tri.v0);
                triData.triBBox.expandBy(tri.v1);
                triData.triBBox.expandBy(tri.v2);
                triData.triangles.push_back((glm::vec3&)tri.v0);
                triData.triangles.push_back((glm::vec3&)tri.v1);
                triData.triangles.push_back((glm::vec3&)tri.v2);
                triData.triangleTexCoords.push_back((glm::vec2&)tri.uv0);
                triData.triangleTexCoords.push_back((glm::vec2&)tri.uv1);
                triData.triangleTexCoords.push_back((glm::vec2&)tri.uv2);
                if(tri.hasNormals)
                {
                    triData.normals.push_back((glm::vec3&)tri.n0);
                    triData.normals.push_back((glm::vec3&)tri.n1);
                    triData.normals.push_back((glm::vec3&)tri.n2);
                    hasNormals = true;
                }
                else if(hasNormals)
                {
                    std::cerr << "Triangle has no per vertex normals, but it is grouped with other triangles that have per vertex normals!" << std::endl;
                    glm::vec3 up(0.0f, 0.0f, 1.0f);
                    triData.normals.push_back(up);
                    triData.normals.push_back(up);
                    triData.normals.push_back(up);
                }
                
                if(bbox.zMin() > tri.v0.z())
                    bbox.zMin() = tri.v0.z();
                if(bbox.zMax() < tri.v0.z())
                    bbox.zMax() = tri.v0.z();
                
                if(bbox.zMin() > tri.v1.z())
                    bbox.zMin() = tri.v1.z();
                if(bbox.zMax() < tri.v1.z())
                    bbox.zMax() = tri.v1.z();

                if(bbox.zMin() > tri.v2.z())
                    bbox.zMin() = tri.v2.z();
                if(bbox.zMax() < tri.v2.z())
                    bbox.zMax() = tri.v2.z();
            }
            //PagedFiles pagedFiles;
        };
        typedef std::vector<GridTree> GridTrees;
    private:
        GridTrees _grid;

        glm::uvec2 _gridDim;
        glm::vec3 _voxelMipMapSizeInMeters;
        glm::vec3 _voxelBorderSizeInMeters;
        glm::vec3 _triOrigin;
    public:
        TriangleOctTreeGrid(const glm::uvec2& gridDim, 
                            const osg::BoundingBox& inputBBox,
                            const glm::vec3& voxelMipMapSizeInMeters,
                            const glm::vec3& voxelBorderSizeInMeters) :
            _gridDim(gridDim),
            _voxelMipMapSizeInMeters(voxelMipMapSizeInMeters),
            _voxelBorderSizeInMeters(voxelBorderSizeInMeters),
            _triOrigin(inputBBox.xMin(), inputBBox.yMin(), inputBBox.zMin())
        {
            glm::vec3 origin;
            origin.z = inputBBox.zMin();
            //for(size_t z = 0; z < gridDim.z; ++z, origin.z += _voxelMipMapSizeInMeters.z)
            {
                origin.y = inputBBox.yMin();
                for(size_t y = 0; y < gridDim.y; ++y, origin.y += _voxelMipMapSizeInMeters.y)
                {
                    origin.x = inputBBox.xMin();
                    for(size_t x = 0; x < gridDim.x; ++x, origin.x += _voxelMipMapSizeInMeters.x)
                    {
                        GridTree gridTree;
                        gridTree.origin = origin;
                        gridTree.bbox.set(origin.x, origin.y, FLT_MAX,
                                          origin.x + _voxelMipMapSizeInMeters.x,
                                          origin.y + _voxelMipMapSizeInMeters.y,
                                          -FLT_MAX);

                        _grid.push_back(gridTree);
                    }
                }
            }
        }

    public:
        const GridTrees& getGrid() const { return _grid; }
        const glm::uvec2& getGridDim() const { return _gridDim; }

        void assignTriangles(TrianglesPerTexture& trianglesPerTexture)
        {
            //TODO make this CUDA
            for(TrianglesPerTexture::iterator itr = trianglesPerTexture.begin();
                itr != trianglesPerTexture.end();
                ++itr)
            {
                osg::ref_ptr<osg::Texture2D> spTexture = itr->first;
                Triangles& triangles = itr->second;
                for(size_t i = 0; i < triangles.size(); ++i)
                {                
                    glm::uvec2 minXY;
                    glm::uvec2 maxXY;
                    glm::uvec2 min(_gridDim);
                    glm::uvec2 max(0u);

                    const Triangle& triangle = triangles.at(i);
                    getVertGridTreeXY(triangle.v0, 
                                      minXY.x, maxXY.x,
                                      minXY.y, maxXY.y);
                    min = minXY;
                    max = maxXY;

                    getVertGridTreeXY(triangle.v1, 
                                      minXY.x, maxXY.x,
                                      minXY.y, maxXY.y);
                    min = glm::min(min, minXY);
                    max = glm::max(max, maxXY);

                    getVertGridTreeXY(triangle.v2, 
                                      minXY.x, maxXY.x,
                                      minXY.y, maxXY.y);
                    min = glm::min(min, minXY);
                    max = glm::max(max, maxXY);

                    //insert into each tree that this triangle is potentially
                    //contained inside
                    //for(glm::uint z = min.z; z <= max.z; ++z)
                    {
                        for(glm::uint y = min.y; y <= max.y; ++y)
                        {
                            for(glm::uint x = min.x; x <= max.x; ++x)
                            {
                                GridTree& gridTree = getGridTree(x, y);
                            
                                gridTree.add(spTexture.get(), triangle);
                            }
                        }
                    }
                }
            }
        }

        const TriangleData& getTriangles(size_t x, size_t y) const
        {
            const GridTree& gridTree = _grid.at((y * _gridDim.x) + x);
            const TriangleData& triData = gridTree.triangleData;

            return triData;
        }

        TriangleData& getTriangles(size_t x, size_t y)
        {
            GridTree& gridTree = _grid.at((y * _gridDim.x) + x);
            TriangleData& triData = gridTree.triangleData;
            
            return triData;
        }

        const glm::vec3& getOrigin(size_t x, size_t y) const
        {
            return _grid.at((y * _gridDim.x) + x).origin;
        }

        GridTree& getGridTree(size_t x, size_t y)
        {
             return _grid.at((y * _gridDim.x) + x);
        }

        const GridTree& getGridTree(size_t x, size_t y) const
        {
             return _grid.at((y * _gridDim.x) + x);
        }

    private:

        void computeMinMaxGridCell(float vert, 
                                   float triOrigin, 
                                   float voxelMipMapSizeInMeters, 
                                   float voxelBorderSizeInMeters, 
                                   unsigned int gridDim,
                                   glm::uint& min, glm::uint& max)
        {
            float distFromOrigin = vert - triOrigin;
            min = max = static_cast<glm::uint>(distFromOrigin / voxelMipMapSizeInMeters);

            //if the x is within the distance of the voxel border from the edge 
            //then add it to both grids so that we can compute voxelization overlaps for 
            //interpolation
            float maxBorder = (min+1) * voxelMipMapSizeInMeters;

            float dist = maxBorder - distFromOrigin;
            if(dist <= voxelBorderSizeInMeters)
                max = min + 1;
            else if(min != 0u)
            {
                float minBorder = maxBorder - voxelMipMapSizeInMeters;
                dist = distFromOrigin - minBorder;
                if(dist <= voxelBorderSizeInMeters)
                {
                    max = min;
                    min = min - 1;
                }
            }
                
            if(min >= gridDim)
                min = gridDim - 1;

            if(max >= gridDim)
                max = gridDim - 1;
        }

        void getVertGridTreeXY(const osg::Vec3& vert,
                                glm::uint& minX,
                                glm::uint& maxX,
                                glm::uint& minY,
                                glm::uint& maxY)
        {
            if(vert.x() < _triOrigin.x)
                minX = maxX = 0u;
            else
            {
                computeMinMaxGridCell(vert.x(), 
                                      _triOrigin.x, 
                                      _voxelMipMapSizeInMeters.x, 
                                      _voxelBorderSizeInMeters.x, 
                                      _gridDim.x, 
                                      minX, maxX);
            }

            if(vert.y() < _triOrigin.y)
                minY = maxY = 0u;
            else
            {
                computeMinMaxGridCell(vert.y(), 
                                      _triOrigin.y, 
                                      _voxelMipMapSizeInMeters.y, 
                                      _voxelBorderSizeInMeters.y, 
                                      _gridDim.y, 
                                      minY, maxY);
            }
        }
    };

    void removeProgressFile()
    {
        int error = remove(_progressFile.c_str());
        if(error != 0)
        {
            const char* errorString = strerror(errno);
            std::cerr << "Failed to remove " 
                      << _progressFile 
                      << ", error: " 
                      << errorString 
                      << std::endl;
        }
    }

    void updateProgressFile(size_t x, size_t y, size_t z)
    {
        std::fstream fileOut;
        fileOut.open(_progressFile, std::fstream::out | std::fstream::trunc);
        if(fileOut.is_open())
            fileOut << x << " " << y << " " << z << std::endl;
        else
        {
            std::cerr << "Failed to open " << _progressFile << "." << std::endl;
        }
    }

    bool generate(size_t startGridX, size_t startGridY, size_t startGridZ,
                  bool generateRootFileOnly,
                  const osg::BoundingBox& filterBBox,
                  const std::string& terrainTexturePrefix,
                  const TerrainTextureSet& terrainTextures,
                  bool binaryOutput, bool compressedOutput,
                  int numOctTrees)
    {
        if(_spInputNode.get() == nullptr ||
           osgDB::fileExists(_outputDir) == false)
        {
            return false;
        }

        if(compressedOutput)
            binaryOutput = true;

        //compute number of separate gigavoxel trees we'll need
        osg::BoundingBox inputBBox;
        if(filterBBox.valid())
            inputBBox = filterBBox;
        else
            inputBBox.expandBy(_spInputNode->getBound());

        float inputWidth = (inputBBox.xMax() - inputBBox.xMin());
        float inputDepth = (inputBBox.yMax() - inputBBox.yMin());
        //float inputHeight = (inputBBox.zMax() - inputBBox.zMin());
        glm::vec2 octTreeGridFDim;
        octTreeGridFDim.x = inputWidth / _maxVoxelMipMapSizeMeters.x;
        octTreeGridFDim.y = inputDepth / _maxVoxelMipMapSizeMeters.y;
        //octTreeGridFDim.z = inputHeight / _maxVoxelMipMapSizeMeters.z;

        glm::uvec2 octTreeGridUDim = static_cast<glm::uvec2>(glm::ceil(octTreeGridFDim));
        
        cuda::Voxelizer voxelizer(_maxVoxelMipMapDimensions, 
                                  _brickDimensions);
        //compute actual voxel size after adding a border to each edge
        glm::vec3 voxelBorderSizeInMeters = _voxelSizeMeters *
            static_cast<glm::vec3>(voxelizer.getExtraVoxChunk());//16.0f;// * static_cast<float>((glm::max(glm::max(_maxVoxelMipMapDimensions.x,
                                                             //                    _maxVoxelMipMapDimensions.y),
                                                             //                    _maxVoxelMipMapDimensions.z) >> 1u));

        TriangleOctTreeGrid octTreeGrid(octTreeGridUDim, 
                                        inputBBox, 
                                        _maxVoxelMipMapSizeMeters, 
                                        voxelBorderSizeInMeters);

        

        std::cout << "Generating grid "
                  << octTreeGridUDim.x 
                  << "x" 
                  << octTreeGridUDim.y 
                  << std::endl;

        if(!voxelizer.initCuda()
           || !voxelizer.allocateVoxelMemory())
        {
            std::cerr << voxelizer.getErrorMessage() << std::endl;
            return false;
        }

        std::cout << "Starting at "
                  << startGridX
                  << " "
                  << startGridY
                  << " "
                  << startGridZ
                  << std::endl;

        TriangleCollector bboxTriVis;
        //octTreeGrid.assignPagedFiles(bboxTriVis.pagedFiles);
        //build one oct-tree per grid cell
        //for(size_t gridZ = 0;
        //    gridZ < octTreeGridUDim.z;
        //    ++gridZ)
        {
            size_t gridY = startGridY;
            size_t gridX = startGridX;
            size_t gridZ = startGridZ;
            for( ;
                gridY < octTreeGridUDim.y;
                ++gridY)
            {
                for( ;
                    gridX < octTreeGridUDim.x;
                    ++gridX)
                {
                    osg::BoundingBox gridTreeBBox = octTreeGrid.getGridTree(gridX, gridY).bbox;
                    
                    osg::Vec3 newMin(gridTreeBBox.xMin() - voxelBorderSizeInMeters.x,
                                     gridTreeBBox.yMin() - voxelBorderSizeInMeters.y,
                                     -FLT_MAX);// - voxelBorderSizeInMeters.z);
                    osg::Vec3 newMax(gridTreeBBox.xMax() + voxelBorderSizeInMeters.x,
                                     gridTreeBBox.yMax() + voxelBorderSizeInMeters.y,
                                     FLT_MAX);// + voxelBorderSizeInMeters.z);
                    gridTreeBBox.expandBy(newMin);
                    gridTreeBBox.expandBy(newMax);

                    bboxTriVis.filterBBox = gridTreeBBox;

                    _spInputNode->accept(bboxTriVis);

                    TriangleOctTreeGrid::GridTree& gridTree = octTreeGrid.getGridTree(gridX, gridY);
                    TriangleData& triData = gridTree.triangleData;
                    triData.clear();

                    octTreeGrid.assignTriangles(bboxTriVis.trianglesPerTexture);

                    //reset the visitor
                    bboxTriVis.trianglesPerTexture.clear();
    
                    if(triData.size() == 0)
                        continue;

                    float inputHeight = gridTree.bbox.zMax() - gridTree.bbox.zMin();
                    int numZGrids = static_cast<int>(std::ceilf(inputHeight / _maxVoxelMipMapSizeMeters.z));
                    float gridHeight = numZGrids * _maxVoxelMipMapSizeMeters.z;
                    if(gridHeight > inputHeight)
                    {
                        float diff = (gridHeight - inputHeight) * 0.5f;
                        gridTree.bbox.zMin() -= diff;
                        gridTree.bbox.zMax() = gridTree.bbox.zMin() + _maxVoxelMipMapSizeMeters.z;
                    }
                    gridTree.origin.z = std::floorf(gridTree.bbox.zMin());
                    TriangleOctTreeGrid::GridTree* pCurGridTree = &gridTree;
                    for( ;
                        gridZ < numZGrids;
                        ++gridZ)
                    {
                        std::stringstream outputDirStr;
                        outputDirStr << _outputDir 
                                     << "/grid_" 
                                     << gridX << "_"
                                     << gridY << "_"
                                     << gridZ;

                        std::string outputDir = osgDB::getRealPath(outputDirStr.str());
                        if(!osgDB::makeDirectory(outputDir))
                        {
                            std::cerr << "ERROR: failed to create output directory " << outputDir << std::endl;
                            return false;
                        }

                        updateProgressFile(gridX, gridY, gridZ);

                        if(numOctTrees == 0)
                        {
                            std::cout << "Generated max number of oct-trees. Quiting now." << std::endl;
                            return false;
                        }

                        std::cout << "Generating GigaVoxel Octree: " 
                                  << osgDB::getSimpleFileName(outputDir)
                                  << std::endl;

                        const glm::vec3& octTreeOrigin = pCurGridTree->origin;

                        bool hasNormals = pCurGridTree->hasNormals;

                        bool trisVoxelized = true;
                        bool doVoxelization = generateRootFileOnly == false;//(gridX == 2);
                        int status = 0;
                        if(doVoxelization)
                        {
                            for(size_t tries = 0; tries < 3; ++tries)
                            {
                                status = Voxelize(voxelizer,
                                                octTreeOrigin,
                                                _voxelSizeMeters,
                                                triData,
                                                terrainTexturePrefix,
                                                terrainTextures,
                                                hasNormals,
                                                outputDir, 
                                                binaryOutput,
                                                compressedOutput);

                                voxelizer.deallocateTriangleMemory();

                                if(status == 0 || status == -1)
                                {
                                    trisVoxelized = (status == 0);
                                    break;
                                }
                                else
                                    voxelizer.resetCuda();
                            }

                            if(status == 0 || status == -1)
                                --numOctTrees;
                        }

                        std::string emptyFile = outputDir;
                        emptyFile += "/";
                        emptyFile += "GigaVoxelOctreeIsEmpty.txt";
                        bool emptyMarkerExists = osgDB::fileExists(emptyFile);
                        if(trisVoxelized == false)
                        {
                            //mark this grid as empty
                            if(emptyMarkerExists == false)
                            {
                                std::fstream emptyMarker(emptyFile, std::fstream::out);
                                emptyMarker << "This octree voxelized no triangles." << std::endl;
                            }
                        }
                        else
                        {
                            //make sure this grid is not marked as empty
                            if(emptyMarkerExists)
                            {
                                status = remove(emptyFile.c_str());
                                if(status != 0)
                                {
                                    std::cerr << "Failed to remove empty tree marker on non-empty octree: " 
                                              << osgDB::getSimpleFileName(outputDir)
                                              << std::endl;
                                }
                            }
                        }
                        
                        if(status == 0 || status == -1)
                            std::cout << "Generation done" << (trisVoxelized ? "." : " - no triangles voxelized.") << std::endl;
                        else//error ocurred, so quit
                        {
                            if(gridX == startGridX && gridY == startGridY && gridZ == startGridZ)
                            {
                                std::cerr << "Error ocurred on starting grid cell, skipping it for now." << std::endl;
                                //if this is the grid we started on then skip it and go to next one
                                //otherwise we'll try again with this grid
                                gridZ += 1;
                                if(gridZ == numZGrids)
                                {
                                    gridZ = startGridZ;
                                    gridX += 1;
                                    if(gridX == octTreeGridUDim.x)
                                    {
                                        gridX = startGridX;
                                        gridY += 1;
                                        if(gridY == octTreeGridUDim.y)
                                        {
                                            std::cerr << "Starting grid cell is last grid cell... Finished." << std::endl;
                                            removeProgressFile();//remove this so script doesn't loop
                                            return false;//this is the last grid so just return
                                        }
                                    }
                                }
                            }
                            updateProgressFile(gridX, gridY, gridZ);
                            return false;
                        }

                        if(gridZ+1 != numZGrids)
                        {
                            pCurGridTree = pCurGridTree->addNeighborTree();
                            pCurGridTree->origin.z += _maxVoxelMipMapSizeMeters.z;
                            pCurGridTree->bbox.zMin() = pCurGridTree->bbox.zMax();
                            pCurGridTree->bbox.zMax() = pCurGridTree->bbox.zMin() + _maxVoxelMipMapSizeMeters.z;
                        }
                    }
                    gridZ = 0;

                    triData.clear();
                }
                gridX = 0;
            }
        }

        voxelizer.deallocateVoxelMemory();

        removeProgressFile();//don't leave behind a progress file
        
        if(generateRootFileOnly || (startGridX == 0 && startGridY == 0 && startGridZ == 0))
            return writeRootFile(octTreeGrid,
                                 octTreeGridUDim, 
                                 compressedOutput);
        else
            return true;
    }

private:
    bool writeRootFile(const TriangleOctTreeGrid& grid,
                       const glm::uvec2& gridDim,
                       bool compressed)
    {
        std::string rootFile = _outputDir + "/";
        rootFile += "root.gvp";

        rootFile = osgDB::getRealPath(rootFile);
        std::fstream fileOut(rootFile, std::fstream::out);
        if(!fileOut.is_open())
        {
            std::cerr << "ERROR: failed to open root file for writing: "
                      << rootFile
                      << std::endl;
            return false;
        }

        std::cout << "Writing root file: " << rootFile << std::endl;

        fileOut << "<OctTrees "
                << "Compressed='" << (compressed ? "YES" : "NO") << "' "
                << "BrickXSize='" << _brickDimensions.x << "' "
                << "BrickYSize='" << _brickDimensions.y << "' "
                << "BrickZSize='" << _brickDimensions.z << "' "
                << ">" << std::endl;

        for(size_t x = 0; x < gridDim.x; ++x)
        {
            for(size_t y = 0; y < gridDim.y; ++y)
            {
                const TriangleOctTreeGrid::GridTree& rootGridTree = grid.getGridTree(x, y);
                size_t z = 0;
                for(const TriangleOctTreeGrid::GridTree* pCurGridTree = &rootGridTree;
                    pCurGridTree != NULL;
                    pCurGridTree = pCurGridTree->pNeighbor,
                    ++z)
                {
                    const TriangleOctTreeGrid::GridTree& gridTree = *pCurGridTree;
                    
                    std::stringstream outputDir;
                    outputDir << "grid_" 
                             << x << "_"
                             << y << "_"
                             << z;
                    std::string emptyFile = _outputDir + "/" + outputDir.str();
                    emptyFile += "/";
                    emptyFile += "GigaVoxelOctreeIsEmpty.txt";
                    if(osgDB::fileExists(emptyFile))
                    {
                        std::cout << "Skipping empty GigaVoxelOctree at " << emptyFile << std::endl;
                        continue;//this gvo was generated, but no tris were voxelized
                    }
                        
                    std::string treeFileRelPath = outputDir.str() + "/tree.gvx";
                    std::string fullPath = _outputDir + "/" + treeFileRelPath;
                    if(osgDB::fileExists(fullPath) == false)
                    {
                        std::cout << "Skipping non-existent GigaVoxelOctree at " << fullPath << std::endl;
                        continue;
                    }

                    std::cout << "Adding GigaVoxelOctree at " << treeFileRelPath << std::endl;

                    fileOut << "    "
                            << "<OctTree "
                            << "CenterX=\""
                            << gridTree.bbox.center().x() << "\" "
                            << "CenterY=\""
                            << gridTree.bbox.center().y() << "\" "
                            << "CenterZ=\""
                            << gridTree.bbox.center().z() << "\" "
                            << "Radius=\""
                            << gridTree.bbox.radius() << "\" "
                            << "SizeXMeters=\"" 
                            << _maxVoxelMipMapSizeMeters.x << "\" "
                            << "SizeYMeters=\""
                            << _maxVoxelMipMapSizeMeters.y << "\" "
                            << "SizeZMeters=\"" 
                            << _maxVoxelMipMapSizeMeters.z << "\" "
                            << "TreeFile=\""
                            << treeFileRelPath
                            << "\" />"
                            << std::endl;
                }
            }
        }
        fileOut << "</OctTrees>" << std::endl;

        std::cout << "Done writing root file." << std::endl;

        return true;
    }
};

static void OnExit()
{
    char* debuggerEnv = getenv("VOXELIZER_DEBUGGER_ENV");
    if(debuggerEnv != NULL)
    {
        std::cout << "Hit Enter to exit..." << std::endl;
        char key;
        std::cin.get(key);
        std::cin.clear();
    }
}
     
int main(int argc, char* argv[])
{
    //std::cout << "Sleeping" << std::endl;
    //_sleep(10000);
    //std::cout << "Done Sleeping" << std::endl;

    osg::ArgumentParser arguments(&argc,argv);

    arguments.getApplicationUsage()->setApplicationName(arguments.getApplicationName());
    arguments.getApplicationUsage()->setDescription(arguments.getApplicationName() +
                                                    " is a application for generating a GigaVoxel Oct-Tree "
                                                    "from polygonal data.");
    arguments.getApplicationUsage()->setCommandLineUsage(arguments.getApplicationName() +
                                                         " --input <filename> "
                                                         "--output <directory> [ "
                                                         "--voxel-size <width centimeters> <height centimeters> <depth centimeters> " 
                                                         "--brick-size <width> <height> <depth> "
                                                         "--filter-bbox <min-x> <min-y> <min-z> <max-x> <max-y> <max-z>] "
                                                         "--grid-start-x <start-x> --grid-start-y <start-y> --grid-start-z <start-z> "
                                                         "--output-count <number of oct-trees to output before quiting> "
                                                         "--geocentric]");
    arguments.getApplicationUsage()->addCommandLineOption("-h or --help","Display command line parameters");

    if(arguments.read("-h") || arguments.read("--help"))
    {
        arguments.getApplicationUsage()->write(std::cerr);
        OnExit();
        return 1;
    }

    std::string outputDir;
    if(!arguments.read("--output", outputDir))
    {
        std::cerr << "ERROR: Please specify an output directory with --output <directory>." << std::endl;
        OnExit();
        return 1;
    }
    else
    {
        outputDir = osgDB::getRealPath(outputDir);

        osgDB::FileType fileType = osgDB::fileType(outputDir);
        if(fileType == osgDB::FILE_NOT_FOUND)
        {
            if(osgDB::makeDirectory(outputDir) == false)
            {
                std::cerr << "ERROR: Unable to create directory for output at " << outputDir << std::endl;
                OnExit();
                return 1;
            }
        }
        else if(fileType != osgDB::DIRECTORY)
        {
            std::cerr << "ERROR: The specified output directory is not a directory." << std::endl;
            OnExit();
            return 1;
        }
    }

    std::string inputFileName;
    if(!arguments.read("--input", inputFileName))
    {
        std::cerr << "ERROR: Please specify an input file with --input <filename>." << std::endl;
        OnExit();
        return 1;
    }
    else if(!osgDB::fileExists(inputFileName))
    {
        std::cerr << "ERROR: The specified input file " << inputFileName << " does not exist." << std::endl;
        OnExit();
        return 1;
    }
    
    inputFileName = osgDB::getRealPath(inputFileName);

    osgDB::Options* pOptions = osgDB::Registry::instance()->getOptions();
    if(pOptions == nullptr)
        pOptions = new osgDB::Options();
    pOptions->setObjectCacheHint(osgDB::Options::CACHE_ALL);
    osgDB::Registry::instance()->setOptions(pOptions);
    s_pOsgFileReader = new OsgFileReader();
    osgDB::Registry::instance()->setReadFileCallback(s_pOsgFileReader);

    //initially don't load externally referenced files
    pOptions->setOptionString("noLoadExternalReferenceFiles");
    osg::ref_ptr<osg::Node> spRootNode = osgDB::readNodeFile(inputFileName, pOptions);
    if(spRootNode.get() == nullptr)
    {
        std::cerr << "ERROR: Failed to read " << inputFileName << std::endl;
        OnExit();
        return 1;
    }
    
    bool isGeocentric = arguments.read("--geocentric");

    if(isGeocentric)
    {
        osg::Matrixd gccInvXForm;
        osg::Vec3d center = spRootNode->getBound().center();
        osg::Matrixd inverseTranslate;
        inverseTranslate.makeTranslate(-center);
        osg::Matrixd inverseRotation;
        osg::Vec3d normal = center;
        normal.normalize(); 
        inverseRotation.makeRotate(normal, osg::Vec3d(0.0, 0.0, 1.0));
        //{This is wrong?
        //gccInvXForm *= inverseRotation;
        //gccInvXForm *= inverseTranslate;
        //osg::Vec3d test1 = gccInvXForm * (center + normal);
        //osg::Vec3d test2 = (center + normal) * gccInvXForm;
        //osg::Vec3d normal2 = osg::Matrixd::transform3x3(gccInvXForm, normal);
        //osg::Vec3d normal3 = inverseRotation * normal;
        //osg::Vec3d normal4 = normal * inverseRotation;
        //osg::Vec3d normal5;
        //}
        //{This is right!
        //gccInvXForm.makeIdentity();
        gccInvXForm *= inverseTranslate;
        gccInvXForm *= inverseRotation;
        //osg::Vec3d test1 = gccInvXForm * (center + normal);
        //osg::Vec3d test2 = (center + normal) * gccInvXForm;
        //osg::Vec3d normal2 = osg::Matrixd::transform3x3(gccInvXForm, normal);
        //osg::Matrixd rot(gccInvXForm.getRotate());
        //osg::Vec3d normal3 = rot * normal;
        //osg::Vec3d normal4 = normal * rot;
        //osg::Vec3d normal5;
        //}
        osg::ref_ptr<osg::MatrixTransform> spGccInvXForm = new osg::MatrixTransform(gccInvXForm);
        spGccInvXForm->addChild(spRootNode.get());
        spRootNode = spGccInvXForm.get();
        spRootNode->getBound();
    }

    pOptions->setOptionString("");//reset this now so that paged terrain loads external files (if any)

    glm::vec3 voxelSize(10.0f, 10.0f, 10.0f);//width, height, depth of a single voxel in centimeters
    arguments.read("--voxel-size", 
                   voxelSize.x,
                   voxelSize.y,
                   voxelSize.z);

    //convert to meters
    voxelSize /= 100.0f;

    glm::uvec3 brickDimensions(30u, 30u, 30u);//width, height, depth of a voxel brick
    arguments.read("--brick-size",
                   brickDimensions.x,
                   brickDimensions.y,
                   brickDimensions.z);

    //glm::uvec3 maxVoxelMipMapDimension(480, 480, 480);//max resolution of highest detail voxel mip map
    //glm::uvec3 maxVoxelMipMapDimension(960, 960, 960);//max resolution of highest detail voxel mip map
    glm::uvec3 maxVoxelMipMapDimension(1920, 1920, 1920);
    
    arguments.read("--max-voxels-size", 
                    maxVoxelMipMapDimension.x,
                    maxVoxelMipMapDimension.y,
                    maxVoxelMipMapDimension.z);
    
    osg::BoundingBox filterBBox;
    
    if(arguments.read("--filter-bbox",
                   filterBBox.xMin(),
                   filterBBox.yMin(),
                   filterBBox.xMax(),
                   filterBBox.yMax()))
    {
        filterBBox.zMin() = -FLT_MAX;
        filterBBox.zMax() = FLT_MAX;

        std::cout << "Filter BBox: "
                  << filterBBox.xMin()
                  << " "
                  << filterBBox.yMin()
                  << " "
                  << filterBBox.xMax()
                  << " "
                  << filterBBox.yMax()
                  << std::endl;
    }

    std::string terrainTexturePrefix = "";
    arguments.read("--terrain-texture-prefix", terrainTexturePrefix);

    TerrainTextureSet terrainTextures;
    std::string terrainTexture;
    while(arguments.read("--terrain-texture", terrainTexture))
        terrainTextures.insert(terrainTexture);

    unsigned int gridStartX = 0;
    arguments.read("--grid-start-x", gridStartX);

    unsigned int gridStartY = 0;
    arguments.read("--grid-start-y", gridStartY);
    
    unsigned int gridStartZ = 0;
    arguments.read("--grid-start-z", gridStartZ);

    int numOctTrees = -1;
    arguments.read("--output-count", numOctTrees);

    bool generateRootFileOnly = arguments.read("--generate-root-file-only");

    PagedGigaVoxelOctTreeGenerator generator;
    
    generator.setVoxelizationParams(voxelSize, brickDimensions, maxVoxelMipMapDimension);

    generator.setInput(inputFileName, spRootNode.get());

    generator.setOutputDirectory(outputDir);

    bool outputCompressed = true;
    bool outputBinary = true;
    generator.generate(gridStartX, gridStartY, gridStartZ,
                       generateRootFileOnly,
                       filterBBox,
                       terrainTexturePrefix,
                       terrainTextures,
                       outputBinary, 
                       outputCompressed,
                       numOctTrees);
    OnExit();
#if 0
    int numVerts = 6;    
    glm::vec3 vertArray[] = { 
        //front face
        //glm::vec3(0.5f, 15.5f, 7.5f),
        //glm::vec3(7.5f, 15.5f, 7.5f),
        //glm::vec3(7.5f, 15.5f, 0.5f),
        glm::vec3(0.5f, 15.5f, 7.5f),
        glm::vec3(7.5f, 15.5f, 7.5f),
        glm::vec3(7.5f, 23.5f, 7.5f),
        //top face 1
        /*glm::vec3(0.5f, 0.5f, 2.5f),
        glm::vec3(2.5f, 0.5f, 2.5f),
        glm::vec3(2.5f, 2.5f, 2.5f),
        //top face 2
        glm::vec3(2.5f, 2.5f, 2.5f),
        glm::vec3(0.5f, 2.5f, 2.5f),
        glm::vec3(0.5f, 0.5f, 2.5f),
        //left face 1
        glm::vec3(0.5f, 0.5f, 0.5f),
        glm::vec3(0.5f, 2.5f, 2.5f),
        glm::vec3(0.5f, 2.5f, 0.5f),
        //left face 2
        glm::vec3(0.5f, 0.5f, 0.5f),
        glm::vec3(0.5f, 0.5f, 2.5f),
        glm::vec3(0.5f, 2.5f, 2.5f),
        //right face 1
        glm::vec3(0.5f, 0.5f, 0.5f),
        glm::vec3(0.5f, 2.5, 0.5f),
        glm::vec3(2.5f, 0.5f, 2.5f),
        //right face 2
        glm::vec3(2.5f, 0.5f, 2.5f),
        glm::vec3(0.5f, 2.5f, 0.5f),
        glm::vec3(2.5f, 2.5f, 2.5f),*/
        //back face
        glm::vec3(2.5f, 0.5f, 7.5f),
        glm::vec3(2.5f, 2.5f, 7.5f),
        glm::vec3(0.5f, 2.5f, 7.5f)
    };

    std::vector<glm::vec3> verts(&vertArray[0], &vertArray[numVerts]);

    glm::vec3 p(0.0f, 0.0f, 0.0f);
    glm::vec3 deltaP(1.0f, 1.0f, 1.0f);
    glm::uvec3 voxDim(64u, 64u, 64u);
    glm::uvec3 brickDim(8u, 8u, 8u);
    std::string outputDir = ".";

    int *voxels = new int[voxDim.x * voxDim.y * voxDim.z];
    memset(voxels, 0, sizeof(int) * voxDim.x * voxDim.y * voxDim.z);
    //cpuTest(&verts.front(), verts.size(), p, deltaP, voxels, voxDim);

    Voxelize(verts, voxDim, brickDim, p, deltaP, outputDir);
    
    std::cout << "Hit Enter to exit..." << std::endl;
    char key;
    std::cin.get(key);
    std::cin.clear();
#endif

    return 0;
}