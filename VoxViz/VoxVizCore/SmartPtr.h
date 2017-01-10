#ifndef VOX_SMARTPTR_H
#define VOX_SMARTPTR_H

namespace vox
{
    //based on osg::ref_ptr class
    template<class T>
    class SmartPtr
    {
    public:
        typedef T element_type;

        SmartPtr() : _ptr(0) {}
        SmartPtr(T* ptr) : _ptr(ptr) { if (_ptr) _ptr->ref(); }
        SmartPtr(const SmartPtr& rp) : _ptr(rp._ptr) { if (_ptr) _ptr->ref(); }
        template<class Other> SmartPtr(const SmartPtr<Other>& rp) : _ptr(rp._ptr) { if (_ptr) _ptr->ref(); }

        ~SmartPtr() { if (_ptr) _ptr->unref();  _ptr = 0; }

        SmartPtr& operator = (const SmartPtr& rp)
        {
            assign(rp);
            return *this;
        }

        template<class Other> SmartPtr& operator = (const SmartPtr<Other>& rp)
        {
            assign(rp);
            return *this;
        }

        inline SmartPtr& operator = (T* ptr)
        {
            if (_ptr==ptr) return *this;
            T* tmp_ptr = _ptr;
            _ptr = ptr;
            if (_ptr) _ptr->ref();
            // unref second to prevent any deletion of any object which might
            // be referenced by the other object. i.e rp is child of the
            // original _ptr.
            if (tmp_ptr) tmp_ptr->unref();
            return *this;
        }

        operator T*() const { return _ptr; }

        // comparison operators for SmartPtr.
        bool operator == (const SmartPtr& rp) const { return (_ptr==rp._ptr); }
        bool operator == (const T* ptr) const { return (_ptr==ptr); }
        friend bool operator == (const T* ptr, const SmartPtr& rp) { return (ptr==rp._ptr); }

        bool operator != (const SmartPtr& rp) const { return (_ptr!=rp._ptr); }
        bool operator != (const T* ptr) const { return (_ptr!=ptr); }
        friend bool operator != (const T* ptr, const SmartPtr& rp) { return (ptr!=rp._ptr); }
 
        T& operator*() const { return *_ptr; }
        T* operator->() const { return _ptr; }
        T* get() const { return _ptr; }

        bool valid() const       { return _ptr!=0; }

        T* release() { T* tmp=_ptr; if (_ptr) _ptr->unref_nodelete(); _ptr=0; return tmp; }

        void swap(SmartPtr& rp) { T* tmp=_ptr; _ptr=rp._ptr; rp._ptr=tmp; }

    private:

        template<class Other> void assign(const SmartPtr<Other>& rp)
        {
            if (_ptr==rp._ptr) return;
            T* tmp_ptr = _ptr;
            _ptr = rp._ptr;
            if (_ptr) _ptr->ref();
            // unref second to prevent any deletion of any object which might
            // be referenced by the other object. i.e rp is child of the
            // original _ptr.
            if (tmp_ptr) tmp_ptr->unref();
        }

        template<class Other> friend class SmartPtr;

        T* _ptr;
    };


    template<class T> inline
    void swap(SmartPtr<T>& rp1, SmartPtr<T>& rp2) { rp1.swap(rp2); }

    template<class T> inline
    T* get_pointer(const SmartPtr<T>& rp) { return rp.get(); }

    template<class T, class Y> inline
    SmartPtr<T> static_pointer_cast(const SmartPtr<Y>& rp) { return static_cast<T*>(rp.get()); }

    template<class T, class Y> inline
    SmartPtr<T> dynamic_pointer_cast(const SmartPtr<Y>& rp) { return dynamic_cast<T*>(rp.get()); }

    template<class T, class Y> inline
    SmartPtr<T> const_pointer_cast(const SmartPtr<Y>& rp) { return const_cast<T*>(rp.get()); }
};

#endif
