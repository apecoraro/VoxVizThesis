#ifndef VOX_IMAGE_H
#define VOX_IMAGE_H

#include "VoxVizCore/Referenced.h"

#include <QtGui/qimage.h>

namespace vox
{
    class Image : public Referenced
    {
    private:
        QImage m_image;
    public:
        Image(const QImage& image);
        
        bool save(const std::string& filename) const;
    };
}
#endif
