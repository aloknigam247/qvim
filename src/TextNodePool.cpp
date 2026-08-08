#include "TextNodePool.h"

#include <QQuickWindow>
#include <QSGNode>
#include <QSGTextNode>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>

namespace qvim {

TextNodePool::~TextNodePool() = default;

void TextNodePool::beginFrame(QSGNode* parent, QQuickWindow* window) {
    m_parent = parent;
    m_window = window;
    m_keyToIndex.clear();
    m_used = 0;
}

void TextNodePool::addText(int key, const QString& text, const QFont& font,
                           const QColor& color, QPointF baselinePos) {
    if (!m_parent || !m_window || text.isEmpty()) return;

    QSGTextNode* node = nullptr;
    auto it = m_keyToIndex.constFind(key);
    if (it != m_keyToIndex.constEnd()) {
        node = m_nodes[it.value()];
    } else {
        const int slot = m_used++;
        if (slot < m_nodes.size()) {
            node = m_nodes[slot];
            node->clear();
        } else {
            node = m_window->createTextNode();
            if (!node) return;
            m_nodes.append(node);
            m_parent->appendChildNode(node);
        }
        // NativeRendering is the whole point of the scene-graph port: it is the
        // only render type that reaches the platform's subpixel-antialiased
        // glyph rasteriser. QtRendering (distance fields) and the old
        // QQuickPaintedItem path both produce grayscale AA, which deposits
        // visibly more ink than VS Code for the same font (issue #15).
        node->setRenderType(QSGTextNode::NativeRendering);
        node->setColor(color);
        m_keyToIndex.insert(key, slot);
    }

    // One unwrapped line. The grid supplies every x position itself, so the
    // layout is only ever asked to shape a single run.
    QTextLayout layout(text, font);
    QTextOption opt;
    opt.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(opt);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (!line.isValid()) { layout.endLayout(); return; }
    line.setLineWidth(qreal(1 << 20));
    line.setPosition(QPointF(0, 0));
    layout.endLayout();

    // addTextLayout anchors on the layout's top-left, but callers think in
    // baselines because that is what cell metrics are expressed in.
    node->addTextLayout(QPointF(baselinePos.x(), baselinePos.y() - line.ascent()),
                        &layout);
}

void TextNodePool::endFrame() {
    for (int i = m_used; i < m_nodes.size(); ++i) {
        if (m_parent) m_parent->removeChildNode(m_nodes[i]);
        delete m_nodes[i];
    }
    m_nodes.resize(m_used);
}

void TextNodePool::forget() {
    m_nodes.clear();
    m_keyToIndex.clear();
    m_used = 0;
    m_parent = nullptr;
}

} // namespace qvim
