#include "Referenced.h"

#include <iostream>

using namespace vox;

Referenced::Referenced(const Referenced&) :
    _refCount(0)
{
}

Referenced::~Referenced()
{
    if(_refCount > 0)
    {
        std::cerr << "WARNING: deleting object with _refCount == " 
                  << _refCount 
                  << ", this could be memory corruption." 
                  << std::endl;
    }
}

void Referenced::unref_nodelete() const
{
    --_refCount;
}
