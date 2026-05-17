#include "NvimConnector.h"
#include "MsgpackRpc.h"
#include "GridModel.h"
#include "HighlightTable.h"
#include "ModeInfo.h"
#include "TablineModel.h"
#include "PopupMenuModel.h"
#include "CmdlineModel.h"

#include <QDebug>

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
    , m_mode(new ModeInfo(this))
    , m_tabline(new TablineModel(this))
    , m_popupmenu(new PopupMenuModel(this))
    , m_cmdline(new CmdlineModel(this))
{
    connect(m_rpc, &MsgpackRpc::notification, this, &NvimConnector::onNotification);
    connect(m_rpc, &MsgpackRpc::disconnected, this, &NvimConnector::onRpcDisconnected);
}

NvimConnector::~NvimConnector() = default;

bool NvimConnector::start(const QString& nvimExe) {
    return m_rpc->startEmbeddedNvim(nvimExe);
}

bool NvimConnector::attachUi(int cols, int rows) {
    m_rpc->request(QStringLiteral("nvim_ui_attach"),
        [cols, rows](msgpack::packer<msgpack::sbuffer>& pk) {
            pk.pack_array(3);
            pk.pack(static_cast<int64_t>(cols));
            pk.pack(static_cast<int64_t>(rows));
            pk.pack_map(5);
            pk.pack("rgb");            pk.pack(true);
            pk.pack("ext_linegrid");   pk.pack(true);
            pk.pack("ext_tabline");    pk.pack(true);
            pk.pack("ext_popupmenu");  pk.pack(true);
            pk.pack("ext_cmdline");    pk.pack(true);
        },
        [this](RpcResult res) {
            if (res) {
                m_attached = true;
                emit attachedChanged();
            } else {
                qWarning() << "nvim_ui_attach failed:" << res.error().message;
            }
        });
    return true;
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

void NvimConnector::onNotification(const QString& method, qvim::ObjectHandlePtr params) {
    if (method == QStringLiteral("redraw")) {
        handleRedraw(paramsView(params));
        return;
    }
    emit customNotification(method, std::move(params));
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
        if (a.size >= 3) m_grid->resize(asInt(a.ptr[1]), asInt(a.ptr[2]));
        return;
    }
    if (name == "grid_clear") {
        m_grid->clear();
        return;
    }
    if (name == "grid_cursor_goto") {
        if (a.size >= 3) m_grid->setCursor(asInt(a.ptr[1]), asInt(a.ptr[2]));
        return;
    }
    if (name == "grid_line") {
        if (a.size >= 4) m_grid->applyLine(asInt(a.ptr[1]), asInt(a.ptr[2]), a.ptr[3]);
        return;
    }
    if (name == "grid_scroll") {
        if (a.size >= 7) {
            m_grid->scroll(asInt(a.ptr[1]), asInt(a.ptr[2]),
                           asInt(a.ptr[3]), asInt(a.ptr[4]),
                           asInt(a.ptr[5]));
        }
        return;
    }
    if (name == "default_colors_set") {
        if (a.size >= 4) {
            m_hl->setDefaultColors(asInt(a.ptr[0], -1),
                                   asInt(a.ptr[1], -1),
                                   asInt(a.ptr[2], -1));
        }
        return;
    }
    if (name == "hl_attr_define") {
        if (a.size >= 2) m_hl->defineAttr(asInt(a.ptr[0]), a.ptr[1]);
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
            if (opt == QStringLiteral("guifont")) {
                m_guifont = asQString(a.ptr[1]);
                emit guifontChanged();
            }
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
    // mouse_on, mouse_off, busy_start, busy_stop, set_icon, update_menu,
    // hl_group_set, grid_destroy, win_*, msg_*, wildmenu_* — ignored in v0.
}

} // namespace qvim
