#pragma once

#include <msgpack.hpp>
#include <unordered_map>
#include <QColor>
#include <QObject>
#include <qqmlregistration.h>
#include <QSet>
#include <QString>
#include <QStringList>

namespace qvim {

struct HlAttr {
    QColor fg;
    QColor bg;
    QColor sp;
    bool bold = false;
    bool italic = false;
    bool isRounded = false;
    QStringList names;
    bool underline = false;
    bool undercurl = false;
    bool strikethrough = false;
    bool reverse = false;
    int blend = 0;
};

class HighlightTable : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NvimConnector")

public:
    explicit HighlightTable(QObject *parent = nullptr);

    void setDefaultColors(int rgbFg, int rgbBg, int rgbSp);
    void defineAttr(int id, const msgpack::object &rgbAttr, const msgpack::object *info = nullptr);
    void clear();

    void setRoundedHighlights(const QStringList &names);

    HlAttr attr(int id) const;
    bool isRounded(int id) const;
    QColor defaultFg() const { return m_defaultFg; }
    QColor defaultBg() const { return m_defaultBg; }
    QColor defaultSp() const { return m_defaultSp; }

    HlAttr resolved(int id) const;

signals:
    void changed();

private:
    QColor m_defaultFg = Qt::white;
    QColor m_defaultBg = Qt::black;
    QColor m_defaultSp = Qt::red;
    QSet<QString> m_roundedHighlights;
    std::unordered_map<int, HlAttr> m_attrs;
};

} // namespace qvim
