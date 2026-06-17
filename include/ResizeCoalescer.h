#pragma once

#include <QObject>
#include <QTimer>

namespace qvim {

// Coalesces a burst of window-resize requests into a single
// nvim_ui_try_resize RPC. While the user is dragging the window edge,
// geometryChange fires per pixel; without coalescing, each edge step
// triggers grid_resize + default_colors_set + redraw + (with multigrid)
// win_pos for every grid. The single-shot timer ensures we only RPC after
// the drag has settled. Suppresses re-fires that would re-emit the same
// (cols, rows) (e.g. cmdline_show reflow paths that recompute geometry).
class ResizeCoalescer : public QObject {
    Q_OBJECT
public:
    explicit ResizeCoalescer(QObject* parent = nullptr);

    void requestResize(int cols, int rows);
    void setIntervalMs(int ms);
    Q_INVOKABLE void flushNow();

    // Sync the coalescer after a direct tryResize bypasses it. Marks the
    // given size as already-emitted and stops any pending timer so a stale
    // coalesced request doesn't overwrite the direct resize.
    void syncAfterDirectResize(int cols, int rows);

signals:
    void resizeRequested(int cols, int rows);

private:
    void onTimeout();

    QTimer m_timer;
    int m_pendingCols = -1;
    int m_pendingRows = -1;
    int m_lastEmittedCols = -1;
    int m_lastEmittedRows = -1;
    // 0 ms = next event-loop tick. Resize feels instant but Qt still
    // collapses bursts within a single event-loop iteration into one timer
    // fire, so we avoid the per-pixel grid_resize storm.
    static constexpr int kDefaultIntervalMs = 0;
};

} // namespace qvim
