#ifndef TEXTNODEPOOL_H
#define TEXTNODEPOOL_H

#include <QColor>
#include <QFont>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QQuickWindow;
class QSGNode;
class QSGTextNode;
QT_END_NAMESPACE

namespace qvim {

// Pool of QSGTextNodes parented under a caller-owned container node.
//
// Text is submitted per run; runs sharing a bucket key are merged into a single
// QSGTextNode, so the node count tracks the number of distinct highlight styles
// on screen rather than the number of runs. QSGTextNode carries one colour for
// all its content, which is why the key must determine the colour.
//
// Nodes are reused frame to frame: a frame with the same set of keys allocates
// nothing. Surplus nodes are removed from the parent AND deleted in endFrame(),
// because QSGNode::removeChildNode does not transfer ownership back.
//
// All methods must be called on the SceneGraph render thread.
class TextNodePool {
public:
    ~TextNodePool();

    // parent and window must outlive the frame. Call once per updatePaintNode.
    void beginFrame(QSGNode *parent, QQuickWindow *window);

    // Draws text with its baseline-left corner at baselinePos.
    //
    // key buckets runs into nodes and must determine colour — two runs sharing
    // a key but not a colour would silently take the first one's colour.
    void addText(int key, const QString &text, const QFont &font, const QColor &color,
                 QPointF baselinePos);

    void endFrame();

    // Drops every node after the scene graph has already destroyed them.
    // Deleting them here would be a double free.
    void forget();

    // Node inspection for tests. NativeRendering is the entire point of the
    // scene-graph port (issue #15), and no pixel test can catch a silent flip
    // back to QtRendering because the suite runs on the software backend.
    int nodeCount() const { return int(m_nodes.size()); }
    QSGTextNode *nodeAt(int i) const { return m_nodes.value(i); }

private:
    QSGNode *m_parent = nullptr;
    QQuickWindow *m_window = nullptr;
    QVector<QSGTextNode *> m_nodes;
    QHash<int, int> m_keyToIndex;
    int m_used = 0;
};

} // namespace qvim

#endif
