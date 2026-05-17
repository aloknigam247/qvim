#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <qqmlregistration.h>
#include <msgpack.hpp>

namespace qvim {

struct Cell {
    QString text;
    int     hlId        = 0;
    bool    doubleWidth = false;
};

class GridModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NvimConnector")
    Q_PROPERTY(int  cols       READ cols       NOTIFY sizeChanged)
    Q_PROPERTY(int  rows       READ rows       NOTIFY sizeChanged)
    Q_PROPERTY(int  cursorRow  READ cursorRow  NOTIFY cursorChanged)
    Q_PROPERTY(int  cursorCol  READ cursorCol  NOTIFY cursorChanged)

public:
    explicit GridModel(QObject* parent = nullptr);

    void resize(int cols, int rows);
    void clear();
    void applyLine(int row, int colStart, const msgpack::object& cellsArr);
    void scroll(int top, int bot, int left, int right, int rows);
    void setCursor(int row, int col);

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    int cursorRow() const { return m_cursorRow; }
    int cursorCol() const { return m_cursorCol; }

    const Cell& cell(int row, int col) const;

    QString dumpAscii() const;   // for tests + debug overlay

signals:
    void sizeChanged();
    void cursorChanged();
    void contentChanged();       // emitted on flush by NvimConnector

private:
    int           m_cols = 0;
    int           m_rows = 0;
    int           m_cursorRow = 0;
    int           m_cursorCol = 0;
    QVector<Cell> m_cells;       // row-major, size = rows*cols
};

} // namespace qvim
