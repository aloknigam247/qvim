#include "RectNodePool.h"

#include <QQuickWindow>
#include <QSGNode>
#include <QSGRectangleNode>

namespace qvim {

RectNodePool::~RectNodePool() = default;

void RectNodePool::beginFrame(QSGNode* parent, QQuickWindow* window) {
    m_parent = parent;
    m_window = window;
    m_used = 0;
}

void RectNodePool::add(const QRectF& rect, const QColor& color) {
    if (!m_parent || !m_window || rect.isEmpty() || !color.isValid()) return;

    QSGRectangleNode* node = nullptr;
    if (m_used < m_nodes.size()) {
        node = m_nodes[m_used];
    } else {
        node = m_window->createRectangleNode();
        if (!node) return;
        m_nodes.append(node);
        m_parent->appendChildNode(node);
    }
    ++m_used;
    node->setRect(rect);
    node->setColor(color);
}

void RectNodePool::endFrame() {
    for (int i = m_used; i < m_nodes.size(); ++i) {
        if (m_parent) m_parent->removeChildNode(m_nodes[i]);
        delete m_nodes[i];
    }
    m_nodes.resize(m_used);
}

void RectNodePool::forget() {
    m_nodes.clear();
    m_used = 0;
    m_parent = nullptr;
}

} // namespace qvim
