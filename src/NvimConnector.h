#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <qqmlregistration.h>

#include <msgpack.hpp>
#include "MsgpackRpc.h"
#include "GridModel.h"
#include "HighlightTable.h"
#include "MessagesModel.h"
#include "ModeInfo.h"
#include "TablineModel.h"
#include "PopupMenuModel.h"
#include "CmdlineModel.h"

namespace qvim {

class NvimConnector : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided via context property $connector")

    Q_PROPERTY(qvim::GridModel*       grid       READ grid       CONSTANT)
    Q_PROPERTY(qvim::HighlightTable*  highlights READ highlights CONSTANT)
    Q_PROPERTY(qvim::MessagesModel*   messages   READ messages   CONSTANT)
    Q_PROPERTY(qvim::ModeInfo*        modeInfo   READ modeInfo   CONSTANT)
    Q_PROPERTY(qvim::TablineModel*    tabline    READ tabline    CONSTANT)
    Q_PROPERTY(qvim::PopupMenuModel*  popupmenu  READ popupmenu  CONSTANT)
    Q_PROPERTY(qvim::CmdlineModel*    cmdline    READ cmdline    CONSTANT)
    Q_PROPERTY(QString                title      READ title      NOTIFY titleChanged)
    Q_PROPERTY(QString                guifont       READ guifont       NOTIFY guifontChanged)
    // Parsed convenience accessors so any QML overlay can do
    //   font.family: $connector.guifontFamily
    //   font.pointSize: $connector.guifontSize
    // without re-implementing the `Family:hN` parser. Both notify on
    // guifontChanged so bindings re-evaluate when nvim's option_set fires.
    Q_PROPERTY(QString                guifontFamily READ guifontFamily NOTIFY guifontChanged)
    Q_PROPERTY(qreal                  guifontSize   READ guifontSize   NOTIFY guifontChanged)
    Q_PROPERTY(bool                   attached      READ attached      NOTIFY attachedChanged)
    Q_PROPERTY(QColor                 defaultBackground READ defaultBackground NOTIFY defaultBackgroundChanged)

public:
    explicit NvimConnector(QObject* parent = nullptr);
    ~NvimConnector() override;

    bool start(const QString& nvimExe = QStringLiteral("nvim"),
               const QStringList& nvimForwardArgs = {});
    Q_INVOKABLE bool attachUi(int cols, int rows);

    GridModel*      grid()       const { return m_grid; }
    HighlightTable* highlights() const { return m_hl; }
    MessagesModel*  messages()   const { return m_messages; }
    ModeInfo*       modeInfo()   const { return m_mode; }
    TablineModel*   tabline()    const { return m_tabline; }
    PopupMenuModel* popupmenu()  const { return m_popupmenu; }
    CmdlineModel*   cmdline()    const { return m_cmdline; }
    QString         title()         const { return m_title; }
    QString         guifont()       const { return m_guifont; }
    QString         guifontFamily() const;
    qreal           guifontSize()   const;
    bool            attached()      const { return m_attached; }
    QColor          defaultBackground() const { return m_hl->defaultBg(); }

    Q_INVOKABLE void input(const QString& keys);
    Q_INVOKABLE void inputMouse(const QString& button, const QString& action,
                                const QString& modifier, int grid, int row, int col);
    Q_INVOKABLE void tryResize(int cols, int rows);
    Q_INVOKABLE void paste(const QString& text);
    Q_INVOKABLE void command(const QString& cmd);
    Q_INVOKABLE void execLua(const QString& code);

signals:
    void titleChanged();
    void guifontChanged();
    void attachedChanged();
    void defaultBackgroundChanged();
    void flush();   // emitted after each redraw batch — UI should repaint here
    void bell();
    void disconnected();
    void customNotification(const qvim::Notification& note);

private slots:
    void onNotification(const qvim::Notification& note);
    void onRpcDisconnected();

private:
    void handleRedraw(const msgpack::object& events);
    void dispatchEvent(const std::string& name, const msgpack::object& evt);

    MsgpackRpc*     m_rpc       = nullptr;
    GridModel*      m_grid      = nullptr;
    HighlightTable* m_hl        = nullptr;
    MessagesModel*  m_messages  = nullptr;
    ModeInfo*       m_mode      = nullptr;
    TablineModel*   m_tabline   = nullptr;
    PopupMenuModel* m_popupmenu = nullptr;
    CmdlineModel*   m_cmdline   = nullptr;

    QString m_title;
    QString m_guifont;
    bool    m_attached = false;
};

} // namespace qvim
