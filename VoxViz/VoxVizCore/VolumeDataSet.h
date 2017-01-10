#ifndef VOX_VOLUME_DATASET_H
#define VOX_VOLUME_DATASET_H

#include <VoxVizCore/Referenced.h>
#include <VoxVizCore/SceneObject.h>

#include <vector>
#include <algorithm>

namespace vox
{
    struct Vec4ub
    {
        unsigned char r;
        unsigned char g;
        unsigned char b;
        unsigned char a;
        Vec4ub() : r(0), g(0), b(0), a(0) {}
        Vec4ub(unsigned char _r,
               unsigned char _g,
               unsigned char _b,
               unsigned char _a) : r(_r), g(_g), b(_b), a(_a) {}
    };

    struct Vec4f
    {
        float x;
        float y;
        float z;
        float w;
        Vec4f() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
        Vec4f(const QVector4D& copy) : 
        x((float)copy.x()), y((float)copy.y()), z((float)copy.z()), w((float)copy.w()) {}
        Vec4f(float i, float j, float k, float l) : x(i), y(j), z(k), w(l) {}
    };

    struct Vec3f
    {
        float x;
        float y;
        float z;
        Vec3f() : x(0.0f), y(0.0f), z(0.0f) {}
        Vec3f(const QVector3D& copy) : 
        x((float)copy.x()), y((float)copy.y()), z((float)copy.z()) {}
        Vec3f(float i, float j, float k) : x(i), y(j), z(k) {}
    };

    class VolumeDataSet : public SceneObject
    {
    public:
        typedef unsigned char Voxels;
        class SubVolume
        {
        public:
            enum Format
            {
                FORMAT_UBYTE_RGBA,
                FORMAT_FLOAT_RGBA,
                FORMAT_UBYTE_SCALARS,
                FORMAT_UNDEFINED
            };
        private:
            unsigned int m_rangeStartX, m_rangeStartY, m_rangeStartZ;
            unsigned int m_rangeEndX, m_rangeEndY, m_rangeEndZ;
            Format m_format;
            size_t m_dataSize;
            void* m_pData;
        public:
            SubVolume(unsigned int rangeStartX=0, 
                      unsigned int rangeStartY=0, 
                      unsigned int rangeStartZ=0,
                      unsigned int rangeEndX=0,
                      unsigned int rangeEndY=0,
                      unsigned int rangeEndZ=0,
                      Format format=FORMAT_UNDEFINED):
                  m_rangeStartX(rangeStartX), m_rangeStartY(rangeStartY), m_rangeStartZ(rangeStartZ), 
                  m_rangeEndX(rangeEndX), m_rangeEndY(rangeEndY), m_rangeEndZ(rangeEndZ),
                  m_format(format),
                  m_dataSize(0),
                  m_pData(NULL)
            {
                unsigned int rangeXSize = rangeEndX - rangeStartX;
                unsigned int rangeYSize = rangeEndY - rangeStartY;
                unsigned int rangeZSize = rangeEndZ - rangeStartZ;

                size_t subImageVoxelCount = rangeXSize * rangeYSize * rangeZSize;
                switch(format)
                {
                case FORMAT_UBYTE_RGBA:
                    m_dataSize = subImageVoxelCount * sizeof(unsigned char) * 4;
                    break;
                case FORMAT_FLOAT_RGBA:
                    m_dataSize = subImageVoxelCount * sizeof(float) * 4;
                    break;
                case FORMAT_UBYTE_SCALARS:
                    m_dataSize = subImageVoxelCount * sizeof(unsigned char);
                    break;
                default:
                    m_dataSize = 0;
                }

                if(m_dataSize > 0)
                    m_pData = malloc(m_dataSize);
            }

            SubVolume(const SubVolume& copy):
                  m_rangeStartX(copy.m_rangeStartX), m_rangeStartY(copy.m_rangeStartY), m_rangeStartZ(copy.m_rangeStartZ), 
                  m_rangeEndX(copy.m_rangeEndX), m_rangeEndY(copy.m_rangeEndY), m_rangeEndZ(copy.m_rangeEndZ),
                  m_format(copy.m_format),
                  m_dataSize(copy.m_dataSize),
                  m_pData(copy.m_pData)
            {
                const_cast<SubVolume&>(copy).m_dataSize = 0;
                const_cast<SubVolume&>(copy).m_pData = NULL;
            }

            ~SubVolume()
            {
                if(m_pData != NULL)
                    free(m_pData);
            }

            SubVolume& operator=(const SubVolume& rhs)
            {
                if(this == &rhs)
                    return *this;
                
                m_rangeStartX = rhs.m_rangeStartX;
                m_rangeStartY = rhs.m_rangeStartY;
                m_rangeStartZ = rhs.m_rangeStartZ;
                m_rangeEndX = rhs.m_rangeEndX;
                m_rangeEndY = rhs.m_rangeEndY;
                m_rangeEndZ = rhs.m_rangeEndZ;
                m_format = rhs.m_format;
                m_dataSize = rhs.m_dataSize;
                m_pData = rhs.m_pData;
                const_cast<SubVolume&>(rhs).m_dataSize = 0;
                const_cast<SubVolume&>(rhs).m_pData = NULL;
                return *this;
            }

            unsigned int rangeStartX() const { return m_rangeStartX; }
            unsigned int rangeStartY() const { return m_rangeStartY; }
            unsigned int rangeStartZ() const { return m_rangeStartZ; }
            unsigned int rangeEndX() const { return m_rangeEndX; }
            unsigned int rangeEndY() const { return m_rangeEndY; }
            unsigned int rangeEndZ() const { return m_rangeEndZ; }
            Format getFormat() const { return m_format; }
            void* data() { return m_pData; }
            size_t dataSize() const { return m_dataSize; }
        };

    private:
        std::string m_inputFile;
        double m_scaleX;
        double m_scaleY;
        double m_scaleZ;
        size_t m_dimX;
        size_t m_dimY;
        size_t m_dimZ;
        size_t m_numSamples;
        Voxels* m_pVoxels;
        Vec4ub* m_pVoxelColorsUB;
        Vec4f* m_pVoxelColorsF;
        std::vector<SubVolume> m_voxelSubVolumes;
    public:
        VolumeDataSet(const std::string& filename,
                      const QVector3D& pos,
                      const QQuaternion& quat,
                      double scaleX,
                      double scaleY,
                      double scaleZ,
                      size_t dimX, 
                      size_t dimY, 
                      size_t dimZ) : 
            SceneObject(pos, quat),
            m_inputFile(filename),
            m_scaleX(scaleX),
            m_scaleY(scaleY),
            m_scaleZ(scaleZ),
            m_dimX(dimX),
            m_dimY(dimY),
            m_dimZ(dimZ),
            m_numSamples(0),
            m_pVoxels(NULL),
            m_pVoxelColorsUB(NULL),
            m_pVoxelColorsF(NULL)
        {
        }

        void initVoxels()
        {
            m_pVoxels = new Voxels[dimX() * dimY() * dimZ()];
            memset(m_pVoxels, 0, sizeof(Voxels) * dimX() * dimY() * dimZ());
        }

        void addSubVolume(const SubVolume& subVolume)
        {
            m_voxelSubVolumes.push_back(subVolume);
        }

        size_t subVolumeCount() const
        {
            return m_voxelSubVolumes.size();
        }

        SubVolume::Format subVolumeFormat() const
        {
            return m_voxelSubVolumes.front().getFormat();
        }

        bool popSubVolume(SubVolume& subVol)
        {
            if(m_voxelSubVolumes.size() == 0)
                return false;
            subVol = m_voxelSubVolumes.back();
            m_voxelSubVolumes.pop_back();
            return true;
        }

        void generateFromSubVolumes();

        VolumeDataSet(const std::string& filename) :
            SceneObject(QVector3D(), QQuaternion()),
            m_inputFile(filename),
            m_scaleX(1.0),
            m_scaleY(1.0),
            m_scaleZ(1.0),
            m_dimX(0),
            m_dimY(0),
            m_dimZ(0),
            m_numSamples(0),
            m_pVoxels(NULL),
            m_pVoxelColorsUB(NULL),
            m_pVoxelColorsF(NULL)
        {
        }

        const std::string& getInputFile() const
        {
            return m_inputFile;
        }

        std::string getInputFileExtension() const;
       
        void setNumSamples(size_t numSamples)
        {
            m_numSamples = numSamples;
        }

        size_t getNumSamples() const
        {
            return m_numSamples;
        }

        void freeVoxels();

        size_t dimX() const { return m_dimX; }
        size_t dimY() const { return m_dimY; }
        size_t dimZ() const { return m_dimZ; }

        virtual BoundingSphere computeBoundingSphere() const;

        virtual BoundingBox computeBoundingBox() const;

        unsigned char value(size_t x, size_t y, size_t z) const
        {
            return (*this)(x, y, z);
        }

        unsigned char& value(size_t x, size_t y, size_t z)
        {
            return (*this)(x, y, z);
        }

        float valueAsFloat(size_t x, size_t y, size_t z) const
        {
            return static_cast<float>((*this)(x, y, z)) / 255.0f;
        }

        unsigned char operator()(size_t x, size_t y, size_t z) const
        {
            return m_pVoxels[(z * dimY() * dimX()) + (y * dimX()) + x];
        }

        unsigned char& operator()(size_t x, size_t y, size_t z)
        {
            return m_pVoxels[(z * dimY() * dimX()) + (y * dimX()) + x];
        }

        QVector3D xyz(size_t x, size_t y, size_t z) const;

        void setData(Voxels* pData)
        {
            m_pVoxels = pData;
        }

        const Voxels* getData() const { return m_pVoxels; }

        void setColors(Vec4ub* pColors)
        {
            m_pVoxelColorsUB = pColors;
        }
        void setColors(Vec4f* pColors)
        {
            m_pVoxelColorsF = pColors;
        }
        const Vec4ub* getColorsUB() const { return m_pVoxelColorsUB; }
        Vec4ub* getColorsUB() { return m_pVoxelColorsUB; }
        const Vec4f* getColorsF() const { return m_pVoxelColorsF; }
        Vec4f* getColorsF() { return m_pVoxelColorsF; }
        void setColor(size_t x, size_t y, size_t z,
                      float rf, float gf, float bf, float af)
        {
            Vec4ub color;
            color.r = static_cast<unsigned char>(rf * 255.0);
            color.g = static_cast<unsigned char>(gf * 255.0);
            color.b = static_cast<unsigned char>(bf * 255.0);
            color.a = static_cast<unsigned char>(af * 255.0);
            size_t index = (z * dimY() * dimX()) + (y * dimX()) + x;
            m_pVoxelColorsUB[index] = color;
        }

        typedef std::vector<QVector4D> ColorLUT;
        void convert(const VolumeDataSet::ColorLUT& colorLUT,
                     Vec4ub* pVoxelColors,
                     Vec3f* pVoxelGrads=NULL) const;

    private:
        ~VolumeDataSet();
    };
}

#endif
