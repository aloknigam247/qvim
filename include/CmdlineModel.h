#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <qqmlregistration.h>
#include <msgpack.hpp>

namespace qvim {

class CmdlineModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NvimConnector")

    Q_PROPERTY(bool        visible    READ visible    NOTIFY visibilityChanged)
    Q_PROPERTY(QString     content    READ content    NOTIFY contentChanged)
    Q_PROPERTY(int         cursorPos  READ cursorPos  NOTIFY contentChanged)
    Q_PROPERTY(QString     firstChar  READ firstChar  NOTIFY contentChanged)
    Q_PROPERTY(QString     prompt     READ prompt     NOTIFY contentChanged)
    Q_PROPERTY(int         indent     READ indent     NOTIFY contentChanged)
    Q_PROPERTY(int         level      READ level      NOTIFY contentChanged)
    Q_PROPERTY(QStringList blockLines READ blockLines NOTIFY blockChanged)

public:
    explicit CmdlineModel(QObject* parent = nullptr);

    void show(const msgpack::object& contentArr, int pos, const QString& firstchar,
              const QString& prompt, int indent, int level);
    void setPos(int pos, int level);
    void setSpecialChar(const QString& c, bool shift, int level);
    void hide();

    void blockShow(const msgpack::object& lines);
    void blockAppend(const msgpack::object& line);
    void blockHide();

    bool        visible() const    { return m_visible; }
    QString     content() const    { return m_content; }
    int         cursorPos() const  { return m_cursorPos; }
    QString     firstChar() const  { return m_firstChar; }
    QString     prompt() const     { return m_prompt; }
    int         indent() const     { return m_indent; }
    int         level() const      { return m_level; }
    QStringList blockLines() const { return m_blockLines; }

signals:
    void visibilityChanged();
    void contentChanged();
    void blockChanged();

private:
    static QString joinContent(const msgpack::object& contentArr);

    bool        m_visible = false;
    QString     m_content;
    int         m_cursorPos = 0;
    QString     m_firstChar;
    QString     m_prompt;
    int         m_indent = 0;
    int         m_level = 0;
    QStringList m_blockLines;
};

} // namespace qvim
