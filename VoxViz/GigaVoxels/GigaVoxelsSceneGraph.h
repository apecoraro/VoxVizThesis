#ifndef GIGA_VOXELS_SCENE_GRAPH_H
#define GIGA_VOXELS_SCENE_GRAPH_H

#include "GigaVoxels/GigaVoxelsOctTree.h"

#include "VoxVizCore/Referenced.h"
#include "VoxVizCore/SmartPtr.h"

#include <QtGui/QVector3D>

#include <vector>

namespace gv
{
    class NodeVisitor;

    class Node : public vox::Referenced
    {
    private:
        QVector3D m_center;
        float m_radius;
    public:
        //TODO figure out what is going on with frustum and node/group/etc
        Node();
        Node(const QVector3D& center, float radius);

        virtual void accept(NodeVisitor& vis);
        virtual void traverse(NodeVisitor& nv) {};

        const QVector3D& center() const;
        float radius() const;

        void setBoundingSphere(const QVector3D& center, float radius)
        {
            m_center = center;
            m_radius = radius;
        }
    protected:
        virtual ~Node();
    };

    class Group : public Node
    {
    private:
        typedef std::vector< vox::SmartPtr<gv::Node> > Children;
        Children m_children;
    public:
        Group();
        
        virtual void accept(NodeVisitor& vis) override;
        virtual void traverse(NodeVisitor& nv) override;

        void addChild(Node* pChild);
    protected:
        virtual ~Group();
    };

    class OctTreeNode : public Node
    {
    private:
        vox::SmartPtr<gv::GigaVoxelsOctTree> m_spOctTree;
    public:
        OctTreeNode(gv::GigaVoxelsOctTree* pOctTree=nullptr);
        OctTreeNode(const QVector3D& center, 
            float radius) : Node(center, radius) {}

        gv::GigaVoxelsOctTree* getOctTree();
        void setOctTree(gv::GigaVoxelsOctTree* pOctTree)
        {
            m_spOctTree = pOctTree;
        }

        virtual void accept(NodeVisitor& vis) override;
        
    protected:
        virtual ~OctTreeNode();
    };

    class PagedOctTreeNode : public OctTreeNode
    {
    private:
        bool m_octTreeIsCompressed;
        size_t m_octTreeBrickDimX;
        size_t m_octTreeBrickDimY;
        size_t m_octTreeBrickDimZ;
        QVector3D m_octTreeSizeMeters;
        std::string m_octTreeFile;
        vox::SmartPtr<gv::GigaVoxelsOctTree> m_spLoadedOctTree;
        size_t m_lastAccessFrameIndex;
        bool m_isOnLoadRequestList;
    public:
        PagedOctTreeNode(bool octTreeIsCompressed,
                         size_t octTreeBrickDimX,
                         size_t octTreeBrickDimY,
                         size_t octTreeBrickDimZ,
                         const QVector3D& center, 
                         float radius,
                         const QVector3D& octTreeSizeMeters,
                         const std::string& octTreeFile);

        void getOctTreeData(bool& octTreeIsCompressed,
                            size_t& octTreeBrickDimX,
                            size_t& octTreeBrickDimY,
                            size_t& octTreeBrickDimZ)
        {
            octTreeIsCompressed = m_octTreeIsCompressed;
            octTreeBrickDimX = m_octTreeBrickDimX;
            octTreeBrickDimY = m_octTreeBrickDimY;
            octTreeBrickDimZ = m_octTreeBrickDimZ;
        }

        const QVector3D& getOctTreeSizeMeters() { return m_octTreeSizeMeters; }
                            
        const std::string& getOctTreeFile() const { return m_octTreeFile; }
        void setLoadedOctTree(GigaVoxelsOctTree* pLoadedOctTree)
        {
            m_spLoadedOctTree = pLoadedOctTree;
        }

        bool octTreeLoaded() const { return m_spLoadedOctTree.get() != nullptr; }

        void mergeLoadedOctTree()
        {
            if(m_spLoadedOctTree.get() != nullptr)
            {
                setOctTree(m_spLoadedOctTree.get());
                m_spLoadedOctTree = nullptr;
            }
        }

        void setLastAccess(size_t frameIndex) { m_lastAccessFrameIndex = frameIndex; }
        size_t getLastAccess() { return m_lastAccessFrameIndex; }

        bool isOnLoadRequestList() { return m_isOnLoadRequestList; }
        void setIsOnLoadRequestList(bool flag) { m_isOnLoadRequestList = flag; }

        virtual void accept(NodeVisitor& vis) override;
        
    protected:
        virtual ~PagedOctTreeNode();
    };

    class NodeVisitor
    {
    public:
        NodeVisitor() {}
        virtual ~NodeVisitor() {}

        void traverse(Node& node);

        virtual void apply(Node& node) { traverse(node); }
        virtual void apply(Group& node) { apply(static_cast<Node&>(node)); }
        virtual void apply(OctTreeNode& node) { apply(static_cast<Node&>(node)); }
        virtual void apply(PagedOctTreeNode& node) { apply(static_cast<OctTreeNode&>(node)); }
    };
}
#endif