#ifndef RECTNODEPOOL_H
#define RECTNODEPOOL_H

#include <QColor>
#include <QRectF>
#include <QVector>

QT_BEGIN_NAMESPACE
class QQuickWindow;
class QSGNode;
class QSGRectangleNode;
QT_END_NAMESPACE

namespace qvim {

// Pool of solid-colour rectangle nodes parented under a caller-owned container.
// Backs every flat fill the renderer emits: the default-background clear, the
// per-run cell backgrounds, underlines, and the cursor block.
//
// These are QSGRectangleNodes rather than one batched QSGGeometryNode because
// the Qt Quick *software* adaptation does not render generic geometry nodes at
// all — it only knows a fixed set of built-in node types. A batched geometry
// node is faster on the RHI backends but silently draws nothing under software
// rendering, which is what users get on VMs, over RDP, and via
// QT_QUICK_BACKEND=software. Rectangle nodes render on both, and the RHI
// backend's batch renderer merges adjacent solid rects anyway.
//
// The cost is bounded in practice: runs whose background is the protocol
// default are not filled at all (GridRuns::fillBg), so a typical screen emits
// a handful of rects for the statusline, selection and cursorline rather than
// one per run.
//
// Nodes are reused frame to frame; a frame with the same rect count allocates
// nothing. Surplus nodes are removed from the parent AND deleted in endFrame(),
// because QSGNode::removeChildNode does not transfer ownership back.
//
// All methods must be called on the SceneGraph render thread.
class RectNodePool {
public:
    RectNodePool() = default;
    ~RectNodePool();
    Q_DISABLE_COPY_MOVE(RectNodePool)

    // parent and window must outlive the frame. Call once per updatePaintNode.
    void beginFrame(QSGNode *parent, QQuickWindow *window);

    // Rects are drawn in submission order; later calls paint over earlier ones.
    void add(const QRectF &rect, const QColor &color);

    void endFrame();

    // Drops every node after the scene graph has already destroyed them.
    // Deleting them here would be a double free.
    void forget();

private:
    QSGNode *m_parent = nullptr;
    QQuickWindow *m_window = nullptr;
    QVector<QSGRectangleNode *> m_nodes;
    int m_used = 0;
};

} // namespace qvim

#endif
