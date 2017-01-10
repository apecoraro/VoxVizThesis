#include "GigaVoxels/GigaVoxelsSceneGraph.h"

#include <QtGui/QVector3D>

using namespace gv;

void NodeVisitor::traverse(Node& node)
{
    node.traverse(*this);
}

Node::Node() {}

Node::Node(const QVector3D& center, float radius) :
    m_center(center), m_radius(radius)
{
}

void Node::accept(NodeVisitor& vis)
{
    vis.apply(*this);
}

const QVector3D& Node::center() const
{
    return m_center;
}

float Node::radius() const
{
    return m_radius;
}

Node::~Node() {}

Group::Group() {}

void Group::accept(NodeVisitor& vis)
{
    vis.apply(*this);
}

void Group::traverse(NodeVisitor& nv)
{
    for(Children::iterator itr = m_children.begin();
        itr != m_children.end();
        ++itr)
    {
        (*itr)->accept(nv);
    }
}

void Group::addChild(Node* pChild)
{
    m_children.push_back(pChild);
}
      
Group::~Group()
{
}

OctTreeNode::OctTreeNode(gv::GigaVoxelsOctTree* pOctTree) :
    m_spOctTree(pOctTree)
{
}

void OctTreeNode::accept(NodeVisitor& vis)
{
    vis.apply(*this);
}

gv::GigaVoxelsOctTree* OctTreeNode::getOctTree()
{
    return m_spOctTree.get();
}

OctTreeNode::~OctTreeNode() 
{
}

gv::PagedOctTreeNode::PagedOctTreeNode(bool octTreeIsCompressed,
                                       size_t octTreeBrickDimX,
                                       size_t octTreeBrickDimY,
                                       size_t octTreeBrickDimZ,
                                       const QVector3D& center,
                                       float radius, 
                                       const QVector3D& octTreeSizeMeters,
                                       const std::string& octTreeFile) : 
    gv::OctTreeNode(center, radius),
    m_octTreeIsCompressed(octTreeIsCompressed),
    m_octTreeBrickDimX(octTreeBrickDimX),
    m_octTreeBrickDimY(octTreeBrickDimY),
    m_octTreeBrickDimZ(octTreeBrickDimZ),
    m_octTreeSizeMeters(octTreeSizeMeters),
    m_octTreeFile(octTreeFile),
    m_lastAccessFrameIndex(0),
    m_isOnLoadRequestList(false)
{
}

PagedOctTreeNode::~PagedOctTreeNode()
{
}

void PagedOctTreeNode::accept(NodeVisitor& vis)
{
    vis.apply(*this);
}