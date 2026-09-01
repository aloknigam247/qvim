#ifndef NVIMCONNECTOR_H
#define NVIMCONNECTOR_H

#include <QColor>
#include <QObject>
#include <QPointer>
#include <qqmlregistration.h>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <optional>
#include <QVariant>

#include "CmdlineModel.h"
#include "GridModel.h"
#include "HighlightTable.h"
#include "MessagesModel.h"
#include "ModeInfo.h"
#include "MsgpackRpc.h"
#include "PopupMenuModel.h"
#include "ResizeCoalescer.h"
#include "TablineModel.h"
#include <msgpack.hpp>

namespace qvim {

class NvimConnector : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided via context property $connector")

    Q_PROPERTY(qvim::GridModel *grid READ grid CONSTANT)
    Q_PROPERTY(qvim::HighlightTable *highlights READ highlights CONSTANT)
    Q_PROPERTY(qvim::MessagesModel *messages READ messages CONSTANT)
    Q_PROPERTY(qvim::ModeInfo *modeInfo READ modeInfo CONSTANT)
    Q_PROPERTY(qvim::TablineModel *tabline READ tabline CONSTANT)
    Q_PROPERTY(qvim::PopupMenuModel *popupmenu READ popupmenu CONSTANT)
    Q_PROPERTY(qvim::CmdlineModel *cmdline READ cmdline CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString guifont READ guifont NOTIFY guifontChanged)
    // Parsed convenience accessors so any QML overlay can do
    //   font.family: $connector.guifontFamily
    //   font.pointSize: $connector.guifontSize
    // without re-implementing the `Family:hN` parser. Both notify on
    // guifontChanged so bindings re-evaluate when nvim's option_set fires.
    Q_PROPERTY(QString guifontFamily READ guifontFamily NOTIFY guifontChanged)
    Q_PROPERTY(qreal guifontSize READ guifontSize NOTIFY guifontChanged)
    Q_PROPERTY(bool attached READ attached NOTIFY attachedChanged)
    Q_PROPERTY(QColor defaultBackground READ defaultBackground NOTIFY defaultBackgroundChanged)
    // ui-options propagated live from nvim via option_set redraw events.
    Q_PROPERTY(bool arabicshape READ arabicshape NOTIFY arabicshapeChanged)
    Q_PROPERTY(QString ambiwidth READ ambiwidth NOTIFY ambiwidthChanged)
    Q_PROPERTY(bool emoji READ emoji NOTIFY emojiChanged)
    Q_PROPERTY(QString guifontwide READ guifontwide NOTIFY guifontwideChanged)
    Q_PROPERTY(int linespace READ linespace NOTIFY linespaceChanged)
    Q_PROPERTY(bool mousefocus READ mousefocus NOTIFY mousefocusChanged)
    Q_PROPERTY(bool mousehide READ mousehide NOTIFY mousehideChanged)
    Q_PROPERTY(bool mousemoveevent READ mousemoveevent NOTIFY mousemoveeventChanged)
    Q_PROPERTY(int pumblend READ pumblend NOTIFY pumblendChanged)
    Q_PROPERTY(int showtabline READ showtabline NOTIFY showtablineChanged)
    Q_PROPERTY(bool termguicolors READ termguicolors NOTIFY termguicolorsChanged)

public:
    explicit NvimConnector(QObject *parent = nullptr);
    ~NvimConnector() override;
    Q_DISABLE_COPY_MOVE(NvimConnector)

    bool start(const QString &nvimExe = QStringLiteral("nvim"),
               const QStringList &nvimForwardArgs = {});
    Q_INVOKABLE bool attachUi(int cols, int rows);

    // Issues `nvim_get_var(name)` and invokes `cb` with the unpacked value on
    // success, or with an empty optional if the variable is unset / errored.
    // Used by ConfigGGlobalReader to populate Config from g:qvim_* globals.
    using GetVarCallback = std::function<void(std::optional<QVariant>)>;
    void getVar(const QString &name, GetVarCallback cb);

    GridModel *grid() const { return m_grid; }
    HighlightTable *highlights() const { return m_hl; }
    MessagesModel *messages() const { return m_messages; }
    ModeInfo *modeInfo() const { return m_mode; }
    TablineModel *tabline() const { return m_tabline; }
    PopupMenuModel *popupmenu() const { return m_popupmenu; }
    CmdlineModel *cmdline() const { return m_cmdline; }
    QString title() const { return m_title; }
    QString guifont() const { return m_guifont; }
    QString guifontFamily() const;
    qreal guifontSize() const;
    bool attached() const { return m_attached; }
    QColor defaultBackground() const { return m_hl->defaultBg(); }

    bool arabicshape() const { return m_arabicshape; }
    QString ambiwidth() const { return m_ambiwidth; }
    bool emoji() const { return m_emoji; }
    QString guifontwide() const { return m_guifontwide; }
    int linespace() const { return m_linespace; }
    bool mousefocus() const { return m_mousefocus; }
    bool mousehide() const { return m_mousehide; }
    bool mousemoveevent() const { return m_mousemoveevent; }
    int pumblend() const { return m_pumblend; }
    int showtabline() const { return m_showtabline; }
    bool termguicolors() const { return m_termguicolors; }

    Q_INVOKABLE void input(const QString &keys);
    Q_INVOKABLE void inputMouse(const QString &button, const QString &action,
                                const QString &modifier, int grid, int row, int col);
    Q_INVOKABLE void tryResize(int cols, int rows);
    // Debounced variant — coalesces a burst of resize requests into a single
    // RPC. Use from drag handlers (geometryChange); call tryResize directly
    // only from tests / programmatic resize where the synchronous behaviour
    // is required.
    Q_INVOKABLE void requestResize(int cols, int rows);
    qvim::ResizeCoalescer *resizeCoalescer() const { return m_resizeCoalescer; }
    Q_INVOKABLE void paste(const QString &text);
    Q_INVOKABLE void command(const QString &cmd);
    Q_INVOKABLE void execLua(const QString &code);
    // Populates the current buffer with `bytes`, splitting on \n and trimming
    // trailing \r. Used to implement `qvim -` (read stdin into buffer 1).
    void loadStdinIntoBuffer(const QByteArray &bytes);
    // Direct option_set entry point. Used by the redraw dispatch (after
    // unpacking the msgpack value to QVariant) and by unit tests that don't
    // want to spin up an nvim process. O(1) per call.
    Q_INVOKABLE void onOptionSet(const QString &name, const QVariant &value);

signals:
    void titleChanged();
    void guifontChanged();
    void attachedChanged();
    void attachComplete();
    void defaultBackgroundChanged();
    void flush(); // emitted after each redraw batch — UI should repaint here
    void bell();
    void disconnected();
    void customNotification(const qvim::Notification &note);
    void arabicshapeChanged();
    void ambiwidthChanged();
    void emojiChanged();
    void guifontwideChanged();
    void linespaceChanged();
    void mousefocusChanged();
    void mousehideChanged();
    void mousemoveeventChanged();
    void pumblendChanged();
    void showtablineChanged();
    void termguicolorsChanged();

private slots:
    void onNotification(const qvim::Notification &note);
    void onRpcDisconnected();

private:
    void handleRedraw(const msgpack::object &events);
    void dispatchEvent(const std::string &name, const msgpack::object &evt);

    MsgpackRpc *m_rpc = nullptr;
    GridModel *m_grid = nullptr;
    HighlightTable *m_hl = nullptr;
    MessagesModel *m_messages = nullptr;
    ModeInfo *m_mode = nullptr;
    TablineModel *m_tabline = nullptr;
    PopupMenuModel *m_popupmenu = nullptr;
    CmdlineModel *m_cmdline = nullptr;
    ResizeCoalescer *m_resizeCoalescer = nullptr;

    QString m_title;
    QString m_guifont;
    bool m_attached = false;
    bool m_extMultigrid = false;

    // Defaults match the nvim ui-options defaults (see :h ui-options).
    bool m_arabicshape = true;
    QString m_ambiwidth = QStringLiteral("single");
    bool m_emoji = true;
    QString m_guifontwide;
    int m_linespace = 0;
    bool m_mousefocus = false;
    bool m_mousehide = true;
    bool m_mousemoveevent = false;
    int m_pumblend = 0;
    int m_showtabline = 1;
    bool m_termguicolors = false;
};

} // namespace qvim

#endif
