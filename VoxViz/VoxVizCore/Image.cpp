#include "VoxVizCore/Image.h"

#include <QtCore/qstring.h>

using namespace vox;

Image::Image(const QImage& image) :
    m_image(image)
{
}
        
bool Image::save(const std::string& filename) const
{
    return m_image.save(QString(filename.c_str()));
}
