#pragma once

#include <msgpack.hpp>
#include <QObject>
#include <qqmlregistration.h>
#include <QString>
#include <QVector>

namespace qvim {

enum class CursorShape {
    Block,
    Horizontal,
    Vertical
};

struct ModeDescriptor {
    QString name;
    QString shortName;
    CursorShape shape = CursorShape::Block;
    int cellPercentage = 100;
    int blinkWait = 0;
    int blinkOn = 0;
    int blinkOff = 0;
    int attrId = 0;
};

class ModeInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NvimConnector")
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(int cursorShape READ cursorShapeInt NOTIFY currentChanged)
    Q_PROPERTY(int cellPercentage READ cellPercentage NOTIFY currentChanged)
    Q_PROPERTY(int attrId READ attrId NOTIFY currentChanged)
    Q_PROPERTY(bool cursorStyleEnabled READ cursorStyleEnabled NOTIFY currentChanged)

public:
    explicit ModeInfo(QObject *parent = nullptr);

    void setModes(const msgpack::object &info, bool cursorStyleEnabled);
    void setCurrentMode(const QString &name, int idx);
    // Clears all mode descriptors and the current mode. Used on :restart so no
    // stale cursor shape survives before the new server sends mode_info_set.
    void reset();

    QString currentName() const { return m_current.name; }
    int cursorShapeInt() const { return static_cast<int>(m_current.shape); }
    int cellPercentage() const { return m_current.cellPercentage; }
    int attrId() const { return m_current.attrId; }
    int blinkWait() const { return m_current.blinkWait; }
    int blinkOn() const { return m_current.blinkOn; }
    int blinkOff() const { return m_current.blinkOff; }
    bool cursorStyleEnabled() const { return m_cursorStyleEnabled; }

signals:
    void currentChanged();

private:
    QVector<ModeDescriptor> m_modes;
    ModeDescriptor m_current;
    bool m_cursorStyleEnabled = false;
};

} // namespace qvim
