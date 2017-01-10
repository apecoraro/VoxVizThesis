#ifndef VOX_REFERENCED_H
#define VOX_REFERENCED_H

namespace vox
{
    //based on osg::Referenced class
    class Referenced
    {
    public:
        Referenced() : _refCount(0) {}
        Referenced(const Referenced&);
        inline Referenced& operator = (const Referenced&) { return *this; }

        inline void ref() const;        
        inline void unref() const;
        void unref_nodelete() const;
        inline int referenceCount() const { return _refCount; }
       
    protected:    
        virtual ~Referenced();
        mutable int _refCount;
    };

    inline void Referenced::ref() const
    {
        ++_refCount;
    }

    inline void Referenced::unref() const
    {
        bool needDelete = (--_refCount == 0);

        if (needDelete)
        {
            delete this;
        }
    }
};

#endif
