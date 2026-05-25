#include "NvimConnector.h"
#include "MsgpackRpc.h"
#include "GridModel.h"
#include "HighlightTable.h"
#include "MessagesModel.h"
#include "ModeInfo.h"
#include "TablineModel.h"
#include "PopupMenuModel.h"
#include "CmdlineModel.h"

#include <QDebug>
#include <QFontDatabase>
#include <QVariant>

namespace qvim {

namespace {
QString asQString(const msgpack::object& o) {
    if (o.type != msgpack::type::STR) return {};
    return QString::fromUtf8(o.via.str.ptr, o.via.str.size);
}

bool asBool(const msgpack::object& o, bool def = false) {
    if (o.type == msgpack::type::BOOLEAN) return o.via.boolean;
    return def;
}

int64_t asInt(const msgpack::object& o, int64_t def = 0) {
    if (o.type == msgpack::type::POSITIVE_INTEGER) return static_cast<int64_t>(o.via.u64);
    if (o.type == msgpack::type::NEGATIVE_INTEGER) return o.via.i64;
    return def;
}
} // namespace

NvimConnector::NvimConnector(QObject* parent)
    : QObject(parent)
    , m_rpc(new MsgpackRpc(this))
    , m_grid(new GridModel(this))
    , m_hl(new HighlightTable(this))
    , m_messages(new MessagesModel(this))
    , m_mode(new ModeInfo(this))
    , m_tabline(new TablineModel(this))
    , m_popupmenu(new PopupMenuModel(this))
    , m_cmdline(new CmdlineModel(this))
    , m_resizeCoalescer(new ResizeCoalescer(this))
{
    connect(m_rpc, &MsgpackRpc::notification, this, &NvimConnector::onNotification);
    connect(m_rpc, &MsgpackRpc::disconnected, this, &NvimConnector::onRpcDisconnected);
    connect(m_resizeCoalescer, &ResizeCoalescer::resizeRequested,
            this, &NvimConnector::tryResize);
}

NvimConnector::~NvimConnector() = default;

bool NvimConnector::start(const QString& nvimExe, const QStringList& nvimForwardArgs) {
    return m_rpc->startEmbeddedNvim(nvimExe, nvimForwardArgs);
}

namespace {
// Same parsing the GridItem uses for its own font selection. Centralised here
// so QML overlays can bind via Q_PROPERTY without duplicating the regex.
// Before nvim's first option_set arrives we defer to the OS-supplied fixed
// font (Consolas on Windows, Menlo on macOS, system monospace on Linux) so
// qvim doesn't impose its own font choice.
constexpr qreal kDefaultGuifontSize = 14.0;

QString systemFixedFontFamily() {
    // Cached on first call so QFontDatabase isn't queried on every property
    // read. Safe because the platform's fixed font doesn't change at runtime.
    static const QString cached = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    return cached;
}

void parseGuifontImpl(const QString& guifont, QString& family, qreal& size) {
    if (guifont.isEmpty()) return;
    const auto parts = guifont.split(QLatin1Char(':'));
    if (parts.isEmpty()) return;
    family = parts.first();
    family.replace(QLatin1Char('_'), QLatin1Char(' '));
    for (int i = 1; i < parts.size(); ++i) {
        const QString p = parts.at(i);
        if (p.startsWith(QLatin1Char('h')) && p.size() > 1) {
            bool ok = false;
            const qreal v = p.mid(1).toDouble(&ok);
            if (ok) size = v;
        }
    }
}
} // namespace

QString NvimConnector::guifontFamily() const {
    QString family = systemFixedFontFamily();
    qreal size = kDefaultGuifontSize;
    parseGuifontImpl(m_guifont, family, size);
    return family;
}

qreal NvimConnector::guifontSize() const {
    QString family = systemFixedFontFamily();
    qreal size = kDefaultGuifontSize;
    parseGuifontImpl(m_guifont, family, size);
    return size;
}

bool NvimConnector::attachUi(int cols, int rows) {
    m_rpc->request(QStringLiteral("nvim_ui_attach"),
        [cols, rows](msgpack::packer<msgpack::sbuffer>& pk) {
            pk.pack_array(3);
            pk.pack(static_cast<int64_t>(cols));
            pk.pack(static_cast<int64_t>(rows));
            // Diagnostic mode: every non-core extension disabled to bisect the
            // residual focus-loss-after-':' bug. Re-enable by flipping the
            // matching bool back to true once the offending feature is found.
            // The C++ dispatch handlers, QML overlays, and models all remain
            // wired in — nvim simply won't emit the events that drive them.
            pk.pack_map(8);
            pk.pack("rgb");            pk.pack(true);
            pk.pack("ext_linegrid");   pk.pack(true);
            pk.pack("ext_hlstate");    pk.pack(true);
            pk.pack("ext_multigrid");  pk.pack(false);
            pk.pack("ext_tabline");    pk.pack(false);
            pk.pack("ext_popupmenu");  pk.pack(false);
            pk.pack("ext_cmdline");    pk.pack(false);
            pk.pack("ext_messages");   pk.pack(false);
        },
        [this](RpcResult res) {
            if (res) {
                m_attached = true;
                emit attachedChanged();
                // Ensure nvim emits set_title and that the format shows just
                // the file name (+ modified marker) unless the user has
                // configured their own 'titlestring'. We probe the option
                // first and only install our default when it's empty.
                m_rpc->request(QStringLiteral("nvim_get_option_value"),
                    [](msgpack::packer<msgpack::sbuffer>& pk) {
                        pk.pack_array(2);
                        pk.pack(std::string("titlestring"));
                        pk.pack_map(0);
                    },
                    [this](RpcResult getRes) {
                        bool empty = true;
                        if (getRes) {
                            const msgpack::object& o = (*getRes)->get();
                            if (o.type == msgpack::type::STR && o.via.str.size > 0) {
                                empty = false;
                            }
                        }
                        if (empty) {
                            m_rpc->notify(QStringLiteral("nvim_set_option_value"),
                                [](msgpack::packer<msgpack::sbuffer>& pk) {
                                    pk.pack_array(3);
                                    pk.pack(std::string("titlestring"));
                                    pk.pack(std::string("%t%( %M%)"));
                                    pk.pack_map(0);
                                });
                        }
                        m_rpc->notify(QStringLiteral("nvim_set_option_value"),
                            [](msgpack::packer<msgpack::sbuffer>& pk) {
                                pk.pack_array(3);
                                pk.pack(std::string("title"));
                                pk.pack(true);
                                pk.pack_map(0);
                            });
                    });
                emit attachComplete();
            } else {
                qWarning() << "nvim_ui_attach failed:" << res.error().message;
            }
        });
    return true;
}

namespace {
QVariant msgpackToVariant(const msgpack::object& o) {
    switch (o.type) {
        case msgpack::type::NIL:
            return {};
        case msgpack::type::BOOLEAN:
            return QVariant(o.via.boolean);
        case msgpack::type::POSITIVE_INTEGER:
            return QVariant(static_cast<qulonglong>(o.via.u64));
        case msgpack::type::NEGATIVE_INTEGER:
            return QVariant(static_cast<qlonglong>(o.via.i64));
        case msgpack::type::FLOAT32:
        case msgpack::type::FLOAT64:
            return QVariant(o.via.f64);
        case msgpack::type::STR:
            return QVariant(QString::fromUtf8(o.via.str.ptr, o.via.str.size));
        case msgpack::type::ARRAY: {
            QVariantList list;
            list.reserve(static_cast<int>(o.via.array.size));
            for (uint32_t i = 0; i < o.via.array.size; ++i) {
                list.push_back(msgpackToVariant(o.via.array.ptr[i]));
            }
            return list;
        }
        case msgpack::type::BIN:
            return QVariant(QByteArray(o.via.bin.ptr, static_cast<int>(o.via.bin.size)));
        default:
            return {};
    }
}
} // namespace

void NvimConnector::getVar(const QString& name, GetVarCallback cb) {
    m_rpc->request(QStringLiteral("nvim_get_var"),
        [&name](msgpack::packer<msgpack::sbuffer>& pk) {
            pk.pack_array(1);
            pk.pack(name.toStdString());
        },
        [cb = std::move(cb)](RpcResult res) {
            if (!cb) return;
            if (!res) {
                cb(std::nullopt);
                return;
            }
            // Callback receives the whole [1, msgid, err, result] envelope.
            const msgpack::object& root = res.value()->get();
            if (root.type != msgpack::type::ARRAY || root.via.array.size < 4) {
                cb(std::nullopt);
                return;
            }
            cb(msgpackToVariant(root.via.array.ptr[3]));
        });
}

void NvimConnector::input(const QString& keys) {
    m_rpc->notify(QStringLiteral("nvim_input"),
        [&keys](msgpack::packer<msgpack::sbuffer>& pk) {
            pk.pack_array(1);
            pk.pack(keys.toStdString());
        });
}

void NvimConnector::inputMouse(const QString& button, const QString& action,
                               const QString& modifier, int grid, int row, int col) {
    m_rpc->notify(QStringLiteral("nvim_input_mouse"),
        [&](msgpack::packer<msgpack::sbuffer>& pk) {
            pk.pack_array(6);
            pk.pack(button.toStdString());
            pk.pack(action.toStdString());
            pk.pack(modifier.toStdString());
            pk.pack(static_cast<int64_t>(grid));
            pk.pack(static_cast<int64_t>(row));
            pk.pack(static_cast<int64_t>(col));
        });
}

void NvimConnector::tryResize(int cols, int rows) {
    m_rpc->notify(QStringLiteral("nvim_ui_try_resize"),
        [cols, rows](msgpack::packer<msgpack::sbuffer>& pk) {
            pk.pack_array(2);
            pk.pack(static_cast<int64_t>(cols));
            pk.pack(static_cast<int64_t>(rows));
        });
}

void NvimConnector::requestResize(int cols, int rows) {
    m_resizeCoalescer->requestResize(cols, rows);
}

void NvimConnector::paste(const QString& text) {
    m_rpc->request(QStringLiteral("nvim_paste"),
        [&text](msgpack::packer<msgpack::sbuffer>& pk) {
            pk.pack_array(3);
            pk.pack(text.toStdString());
            pk.pack(true);
            pk.pack(static_cast<int64_t>(-1));
        }, nullptr);
}

void NvimConnector::command(const QString& cmd) {
    m_rpc->request(QStringLiteral("nvim_command"),
        [&cmd](msgpack::packer<msgpack::sbuffer>& pk) {
            pk.pack_array(1);
            pk.pack(cmd.toStdString());
        }, nullptr);
}

void NvimConnector::execLua(const QString& code) {
    m_rpc->request(QStringLiteral("nvim_exec_lua"),
        [&code](msgpack::packer<msgpack::sbuffer>& pk) {
            pk.pack_array(2);
            pk.pack(code.toStdString());
            pk.pack_array(0);
        }, nullptr);
}

void NvimConnector::onOptionSet(const QString& name, const QVariant& value) {
    // O(1) string-table dispatch. Each branch reads the value typed, compares
    // against the cached field, and emits NOTIFY only on real change so QML
    // bindings don't re-evaluate spuriously (relevant for guifont, where a
    // notify triggers font-cache rebuilds in GridItem).
    if (name == QStringLiteral("guifont")) {
        const QString s = value.toString();
        if (s == m_guifont) return;
        m_guifont = s;
        emit guifontChanged();
        return;
    }
    if (name == QStringLiteral("guifontwide")) {
        const QString s = value.toString();
        if (s == m_guifontwide) return;
        m_guifontwide = s;
        emit guifontwideChanged();
        return;
    }
    if (name == QStringLiteral("linespace")) {
        const int v = value.toInt();
        if (v == m_linespace) return;
        m_linespace = v;
        emit linespaceChanged();
        return;
    }
    if (name == QStringLiteral("arabicshape")) {
        const bool v = value.toBool();
        if (v == m_arabicshape) return;
        m_arabicshape = v;
        emit arabicshapeChanged();
        return;
    }
    if (name == QStringLiteral("ambiwidth")) {
        const QString s = value.toString();
        if (s == m_ambiwidth) return;
        m_ambiwidth = s;
        emit ambiwidthChanged();
        return;
    }
    if (name == QStringLiteral("emoji")) {
        const bool v = value.toBool();
        if (v == m_emoji) return;
        m_emoji = v;
        emit emojiChanged();
        return;
    }
    if (name == QStringLiteral("mousefocus")) {
        const bool v = value.toBool();
        if (v == m_mousefocus) return;
        m_mousefocus = v;
        emit mousefocusChanged();
        return;
    }
    if (name == QStringLiteral("mousehide")) {
        const bool v = value.toBool();
        if (v == m_mousehide) return;
        m_mousehide = v;
        emit mousehideChanged();
        return;
    }
    if (name == QStringLiteral("mousemoveevent")) {
        const bool v = value.toBool();
        if (v == m_mousemoveevent) return;
        m_mousemoveevent = v;
        emit mousemoveeventChanged();
        return;
    }
    if (name == QStringLiteral("pumblend")) {
        const int v = value.toInt();
        if (v == m_pumblend) return;
        m_pumblend = v;
        emit pumblendChanged();
        return;
    }
    if (name == QStringLiteral("showtabline")) {
        const int v = value.toInt();
        if (v == m_showtabline) return;
        m_showtabline = v;
        emit showtablineChanged();
        return;
    }
    if (name == QStringLiteral("termguicolors")) {
        const bool v = value.toBool();
        if (v == m_termguicolors) return;
        m_termguicolors = v;
        emit termguicolorsChanged();
        return;
    }
    // ext_* flags are GUI-controlled (we set them in nvim_ui_attach); nvim
    // echoes them back through option_set but we don't react. Log and drop.
    if (name.startsWith(QStringLiteral("ext_"))) {
        return;
    }
    qDebug() << "option_set: unhandled" << name << value;
}

void NvimConnector::onNotification(const qvim::Notification& note) {
    if (note.method == QStringLiteral("redraw")) {
        handleRedraw(note.params());
        return;
    }
    emit customNotification(note);
}

void NvimConnector::onRpcDisconnected() {
    m_attached = false;
    emit attachedChanged();
    emit disconnected();
}

void NvimConnector::handleRedraw(const msgpack::object& events) {
    if (events.type != msgpack::type::ARRAY) return;
    const auto& arr = events.via.array;
    for (uint32_t i = 0; i < arr.size; ++i) {
        const msgpack::object& evt = arr.ptr[i];
        if (evt.type != msgpack::type::ARRAY || evt.via.array.size < 1) continue;
        const msgpack::object& nameObj = evt.via.array.ptr[0];
        if (nameObj.type != msgpack::type::STR) continue;
        const std::string name(nameObj.via.str.ptr, nameObj.via.str.size);
        for (uint32_t j = 1; j < evt.via.array.size; ++j) {
            dispatchEvent(name, evt.via.array.ptr[j]);
        }
    }
}

void NvimConnector::dispatchEvent(const std::string& name, const msgpack::object& evt) {
    if (evt.type != msgpack::type::ARRAY) return;
    const auto& a = evt.via.array;

    if (name == "grid_resize") {
        // [grid, width, height]
        if (a.size >= 3) m_grid->resize(static_cast<int>(asInt(a.ptr[0])),
                                        static_cast<int>(asInt(a.ptr[1])),
                                        static_cast<int>(asInt(a.ptr[2])));
        return;
    }
    if (name == "grid_clear") {
        // [grid]
        if (a.size >= 1) m_grid->clear(static_cast<int>(asInt(a.ptr[0])));
        return;
    }
    if (name == "grid_cursor_goto") {
        // [grid, row, col]
        if (a.size >= 3) m_grid->setCursor(static_cast<int>(asInt(a.ptr[0])),
                                           static_cast<int>(asInt(a.ptr[1])),
                                           static_cast<int>(asInt(a.ptr[2])));
        return;
    }
    if (name == "grid_line") {
        // [grid, row, col_start, cells, wrap?]
        if (a.size >= 4) m_grid->applyLine(static_cast<int>(asInt(a.ptr[0])),
                                           static_cast<int>(asInt(a.ptr[1])),
                                           static_cast<int>(asInt(a.ptr[2])),
                                           a.ptr[3]);
        return;
    }
    if (name == "grid_scroll") {
        // [grid, top, bot, left, right, rows, cols]
        if (a.size >= 7) {
            m_grid->scroll(static_cast<int>(asInt(a.ptr[0])),
                           static_cast<int>(asInt(a.ptr[1])),
                           static_cast<int>(asInt(a.ptr[2])),
                           static_cast<int>(asInt(a.ptr[3])),
                           static_cast<int>(asInt(a.ptr[4])),
                           static_cast<int>(asInt(a.ptr[5])));
        }
        return;
    }
    if (name == "grid_destroy") {
        // [grid]
        if (a.size >= 1) m_grid->destroyGrid(static_cast<int>(asInt(a.ptr[0])));
        return;
    }
    if (name == "win_pos") {
        // [grid, win, start_row, start_col, width, height]
        if (a.size >= 6) {
            m_grid->setPos(static_cast<int>(asInt(a.ptr[0])),
                           static_cast<int>(asInt(a.ptr[3])),  // x = start_col
                           static_cast<int>(asInt(a.ptr[2])),  // y = start_row
                           static_cast<int>(asInt(a.ptr[4])),  // w
                           static_cast<int>(asInt(a.ptr[5]))); // h
        }
        return;
    }
    if (name == "win_float_pos") {
        // [grid, win, anchor, anchor_grid, anchor_row, anchor_col, focusable, zindex]
        if (a.size >= 8) {
            m_grid->setFloatPos(static_cast<int>(asInt(a.ptr[0])),
                                static_cast<int>(asInt(a.ptr[3])),
                                static_cast<int>(asInt(a.ptr[4])),
                                static_cast<int>(asInt(a.ptr[5])),
                                asBool(a.ptr[6]),
                                static_cast<int>(asInt(a.ptr[7])));
        }
        return;
    }
    if (name == "win_external_pos") {
        // [grid, win]
        if (a.size >= 1) m_grid->setExternalPos(static_cast<int>(asInt(a.ptr[0])));
        return;
    }
    if (name == "win_hide") {
        // [grid]
        if (a.size >= 1) m_grid->setHidden(static_cast<int>(asInt(a.ptr[0])));
        return;
    }
    if (name == "win_close") {
        // [grid] — like destroy, but issued when the window is closed
        if (a.size >= 1) m_grid->destroyGrid(static_cast<int>(asInt(a.ptr[0])));
        return;
    }
    if (name == "win_viewport") {
        // [grid, win, topline, botline, curline, curcol, line_count?, scroll_delta?]
        if (a.size >= 6) {
            m_grid->setViewport(static_cast<int>(asInt(a.ptr[0])),
                                static_cast<int>(asInt(a.ptr[2])),
                                static_cast<int>(asInt(a.ptr[3])),
                                static_cast<int>(asInt(a.ptr[4])),
                                static_cast<int>(asInt(a.ptr[5])));
        }
        return;
    }
    if (name == "default_colors_set") {
        if (a.size >= 4) {
            m_hl->setDefaultColors(asInt(a.ptr[0], -1),
                                   asInt(a.ptr[1], -1),
                                   asInt(a.ptr[2], -1));
            emit defaultBackgroundChanged();
        }
        return;
    }
    if (name == "hl_attr_define") {
        if (a.size >= 4) {
            m_hl->defineAttr(asInt(a.ptr[0]), a.ptr[1], &a.ptr[3]);
        } else if (a.size >= 2) {
            m_hl->defineAttr(asInt(a.ptr[0]), a.ptr[1]);
        }
        return;
    }
    if (name == "mode_info_set") {
        if (a.size >= 2) m_mode->setModes(a.ptr[1], asBool(a.ptr[0]));
        return;
    }
    if (name == "mode_change") {
        if (a.size >= 2) m_mode->setCurrentMode(asQString(a.ptr[0]), asInt(a.ptr[1]));
        return;
    }
    if (name == "tabline_update") {
        if (a.size >= 2) m_tabline->update(a.ptr[0], a.ptr[1]);
        return;
    }
    if (name == "popupmenu_show") {
        if (a.size >= 4) {
            m_popupmenu->show(a.ptr[0], asInt(a.ptr[1]),
                              asInt(a.ptr[2]), asInt(a.ptr[3]));
        }
        return;
    }
    if (name == "popupmenu_select") {
        if (a.size >= 1) m_popupmenu->select(asInt(a.ptr[0]));
        return;
    }
    if (name == "popupmenu_hide") {
        m_popupmenu->hide();
        return;
    }
    if (name == "cmdline_show") {
        if (a.size >= 6) {
            m_cmdline->show(a.ptr[0], asInt(a.ptr[1]),
                            asQString(a.ptr[2]), asQString(a.ptr[3]),
                            asInt(a.ptr[4]), asInt(a.ptr[5]));
        }
        return;
    }
    if (name == "cmdline_pos") {
        if (a.size >= 2) m_cmdline->setPos(asInt(a.ptr[0]), asInt(a.ptr[1]));
        return;
    }
    if (name == "cmdline_special_char") {
        if (a.size >= 3) {
            m_cmdline->setSpecialChar(asQString(a.ptr[0]), asBool(a.ptr[1]), asInt(a.ptr[2]));
        }
        return;
    }
    if (name == "cmdline_hide") {
        m_cmdline->hide();
        return;
    }
    if (name == "cmdline_block_show") {
        if (a.size >= 1) m_cmdline->blockShow(a.ptr[0]);
        return;
    }
    if (name == "cmdline_block_append") {
        if (a.size >= 1) m_cmdline->blockAppend(a.ptr[0]);
        return;
    }
    if (name == "cmdline_block_hide") {
        m_cmdline->blockHide();
        return;
    }
    if (name == "option_set") {
        if (a.size >= 2) {
            const QString opt = asQString(a.ptr[0]);
            const msgpack::object& v = a.ptr[1];
            QVariant qv;
            switch (v.type) {
            case msgpack::type::BOOLEAN:
                qv = v.via.boolean; break;
            case msgpack::type::POSITIVE_INTEGER:
                qv = static_cast<qlonglong>(v.via.u64); break;
            case msgpack::type::NEGATIVE_INTEGER:
                qv = static_cast<qlonglong>(v.via.i64); break;
            case msgpack::type::STR:
                qv = QString::fromUtf8(v.via.str.ptr, v.via.str.size); break;
            default:
                break;
            }
            onOptionSet(opt, qv);
        }
        return;
    }
    if (name == "set_title") {
        if (a.size >= 1) {
            m_title = asQString(a.ptr[0]);
            emit titleChanged();
        }
        return;
    }
    if (name == "bell" || name == "visual_bell") {
        emit bell();
        return;
    }
    if (name == "flush") {
        emit flush();
        return;
    }
    if (name == "msg_show") {
        // [kind, content, replace_last] (history flag in newer nvim, ignored)
        if (a.size >= 3) {
            m_messages->msgShow(a.ptr[0], a.ptr[1], asBool(a.ptr[2]));
        }
        return;
    }
    if (name == "msg_clear") {
        m_messages->msgClear();
        return;
    }
    if (name == "msg_history_show") {
        if (a.size >= 1) m_messages->msgHistoryShow(a.ptr[0]);
        return;
    }
    if (name == "msg_showmode") {
        if (a.size >= 1) m_messages->msgShowMode(a.ptr[0]);
        return;
    }
    if (name == "msg_showcmd") {
        if (a.size >= 1) m_messages->msgShowCmd(a.ptr[0]);
        return;
    }
    if (name == "msg_ruler") {
        if (a.size >= 1) m_messages->msgRuler(a.ptr[0]);
        return;
    }
    if (name == "msg_history_clear") {
        // history cleared on nvim side — no UI state to update for v1.
        return;
    }
    // mouse_on, mouse_off, busy_start, busy_stop, set_icon, update_menu,
    // hl_group_set, msg_set_pos, wildmenu_* — ignored in v0.
}

} // namespace qvim
