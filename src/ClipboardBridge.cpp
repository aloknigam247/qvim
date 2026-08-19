#include "ClipboardBridge.h"
#include "NvimConnector.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QStringList>

namespace qvim {

ClipboardBridge::ClipboardBridge(QObject *parent) : QObject(parent) {}

void ClipboardBridge::attachTo(NvimConnector *conn) {
    m_conn = conn;
    if(!conn) return;
    connect(conn, &NvimConnector::customNotification, this, &ClipboardBridge::onCustomNotification);
    installYankAutocmd();
}

void ClipboardBridge::installYankAutocmd() {
    static constexpr auto kLua = R"LUA(
        local ch = vim.api.nvim_get_chans()
        vim.api.nvim_create_autocmd('TextYankPost', {
          group = vim.api.nvim_create_augroup('qvim_clipboard', { clear = true }),
          callback = function()
            local ev = vim.v.event
            if ev.regname == '+' or ev.regname == '*' or ev.regname == '' then
              vim.rpcnotify(0, 'qvim_yank', ev.regcontents, ev.regtype)
            end
          end,
        })
    )LUA";
    m_conn->execLua(QString::fromUtf8(kLua));
}

void ClipboardBridge::onCustomNotification(const qvim::Notification &note) {
    if(note.method != QStringLiteral("qvim_yank")) return;
    const msgpack::object &obj = note.params();
    if(obj.type != msgpack::type::ARRAY || obj.via.array.size < 1) return;

    const msgpack::object &linesObj = obj.via.array.ptr[0];
    if(linesObj.type != msgpack::type::ARRAY) return;

    QStringList lines;
    lines.reserve(linesObj.via.array.size);
    for(uint32_t i = 0; i < linesObj.via.array.size; ++i) {
        const auto &l = linesObj.via.array.ptr[i];
        if(l.type == msgpack::type::STR) {
            lines.push_back(QString::fromUtf8(l.via.str.ptr, l.via.str.size));
        }
    }
    QGuiApplication::clipboard()->setText(lines.join(QChar('\n')));
}

void ClipboardBridge::pasteFromClipboard() {
    if(!m_conn) return;
    const QString text = QGuiApplication::clipboard()->text();
    if(!text.isEmpty()) m_conn->paste(text);
}

} // namespace qvim
