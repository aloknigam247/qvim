#include "ResizeCoalescer.h"

namespace qvim {

ResizeCoalescer::ResizeCoalescer(QObject* parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    m_timer.setInterval(kDefaultIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ResizeCoalescer::onTimeout);
}

void ResizeCoalescer::requestResize(int cols, int rows) {
    m_pendingCols = cols;
    m_pendingRows = rows;
    m_timer.start();
}

void ResizeCoalescer::setIntervalMs(int ms) {
    m_timer.setInterval(ms);
}

void ResizeCoalescer::flushNow() {
    m_timer.stop();
    onTimeout();
}

void ResizeCoalescer::syncAfterDirectResize(int cols, int rows) {
    m_pendingCols = cols;
    m_pendingRows = rows;
    m_lastEmittedCols = cols;
    m_lastEmittedRows = rows;
    m_timer.stop();
}

void ResizeCoalescer::onTimeout() {
    if (m_pendingCols < 0 || m_pendingRows < 0) return;
    if (m_pendingCols == m_lastEmittedCols && m_pendingRows == m_lastEmittedRows) {
        return;
    }
    m_lastEmittedCols = m_pendingCols;
    m_lastEmittedRows = m_pendingRows;
    emit resizeRequested(m_pendingCols, m_pendingRows);
}

} // namespace qvim
