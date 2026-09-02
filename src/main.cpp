#include <array>
#include <cstdio>
#include <future>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

#include "AppIcon.h"
#include "ArgvParser.h"
#include "ClipboardBridge.h"
#include "Config.h"
#include "ConfigCliReader.h"
#include "ConfigGGlobalReader.h"
#include "HighlightTable.h"
#include "MsgpackRpc.h"
#include "NvimConnector.h"
#include "SessionCache.h"
#include "WindowChrome.h"

static QString locateNvim() {
    QString fromPath = QStandardPaths::findExecutable(QStringLiteral("nvim"));
    if(!fromPath.isEmpty()) return fromPath;
    return QStringLiteral("nvim");
}

namespace {

struct BootProfile {
    bool enabled;
    QString outFile;
    QElapsedTimer timer;
    BootProfile() :
        enabled(qEnvironmentVariableIntValue("QVIM_BOOT_PROFILE") != 0),
        outFile(qEnvironmentVariable("QVIM_BOOT_PROFILE_FILE")) {
        if(enabled) timer.start();
    }
    void mark(const char *phase) const {
        if(!enabled) return;
        const qint64 ms = timer.elapsed();
        qDebug().noquote() << "[boot]" << phase << ms << "ms";
        if(!outFile.isEmpty()) {
            std::FILE *fp = nullptr;
#ifdef _WIN32
            (void)::fopen_s(&fp, outFile.toLocal8Bit().constData(), "a");
#else
            fp = std::fopen(outFile.toLocal8Bit().constData(), "a");
#endif
            if(fp) {
                const QByteArray line =
                    QByteArray("[boot] ") + phase + ' ' + QByteArray::number(ms) + " ms\n";
                std::fputs(line.constData(), fp);
                std::fclose(fp);
            }
        }
    }
};

} // namespace

// An exception escaping main() terminates the process anyway; the Qt entry point does not wrap its
// body in a catch-all.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char *argv[]) {
    BootProfile boot;
    const qvim::QvimArgs cli = qvim::parseArgv(argc, argv);
    boot.mark("argv parsed");
    if(cli.helpRequested) {
        std::fputs("Usage: qvim [qvim-options] [nvim-options] [file ...]\n"
                   "\n"
                   "qvim is a Neovim GUI client. All arguments not in qvim's own\n"
                   "namespace are forwarded to the embedded `nvim --embed` process.\n"
                   "\n"
                   "qvim options:\n"
                   "  -h, --help       Show this help and exit.\n"
                   "  -v, --version    Show qvim version and exit.\n"
                   "\n"
                   "Everything else (e.g. `foo.txt`, `-O a.txt b.txt`, `+10 foo.txt`,\n"
                   "`-c \"set number\" foo.txt`) is forwarded to nvim.\n"
                   "\n"
                   "Reading stdin (`qvim -`):\n"
                   "  PowerShell intercepts the literal `-` token for its own pipeline\n"
                   "  handling, which delays spawning qvim by several seconds. Use cmd\n"
                   "  or Start-Process to avoid the delay:\n"
                   "    cmd /c \"echo hello | qvim -\"\n"
                   "    Start-Process qvim '-' -RedirectStandardInput in.txt\n",
                   stdout);
        return 0;
    }
    if(cli.versionRequested) {
        std::fputs("qvim 0.1.0\n", stdout);
        return 0;
    }

    // `qvim -`: slurp stdin synchronously before Qt is up. Reading via the CRT
    // works even though qvim is /SUBSYSTEM:WINDOWS — when the parent shell
    // redirects stdin (e.g. `echo test | qvim -`), the inherited handle is a
    // valid pipe. QProcess gives the embedded nvim its own RPC stdin pipe, so
    // qvim's stdin stays ours to consume.
    // Stdin slurp happens on a background thread so the rest of qvim's boot
    // (Qt construction, nvim spawn, QML load) is not blocked by the pipe.
    // PowerShell's pipeline can hold the pipe writer open for seconds even
    // after the producer has exited; doing this synchronously stalled the
    // visible launch to multi-second times for `echo X | qvim -`. The future
    // is awaited later when loadStdinIntoBuffer needs the bytes.
    //
    // Only slurp when stdin is actually redirected (pipe or file). If stdin
    // is the inherited console (the user typed `qvim -` interactively without
    // piping anything in), fread would block forever waiting for keystrokes
    // that never come. The background thread would leak.
    std::future<QByteArray> stdinFuture;
    bool stdinIsRedirected = false;
    if(cli.stdinAsBuffer) {
#ifdef _WIN32
        const DWORD t = GetFileType(GetStdHandle(STD_INPUT_HANDLE));
        stdinIsRedirected = (t == FILE_TYPE_PIPE || t == FILE_TYPE_DISK);
        if(stdinIsRedirected) _setmode(_fileno(stdin), _O_BINARY);
#else
        stdinIsRedirected = !isatty(fileno(stdin));
#endif
    }
    if(cli.stdinAsBuffer && stdinIsRedirected) {
        stdinFuture = std::async(std::launch::async, []() {
            QByteArray buf;
            std::array<char, 4096> chunk{};
            while(std::size_t n = std::fread(chunk.data(), 1, chunk.size(), stdin)) {
                buf.append(chunk.data(), static_cast<qsizetype>(n));
            }
            return buf;
        });
    }

#ifdef _WIN32
    // Detach from the parent shell's console so the shell prompt returns
    // immediately. Under /SUBSYSTEM:WINDOWS Windows doesn't allocate its own
    // console, but PowerShell still tracks the inherited std handles and
    // waits for the child to release them before showing the next prompt.
    // FreeConsole drops our reference; any subsequent stdio is no-op (we
    // don't write to stdout/stderr after this point).
    FreeConsole();
#endif

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("qvim"));
    QGuiApplication::setOrganizationName(QStringLiteral("qvim"));
    qvim::setupApplicationIcon(app);
    qRegisterMetaType<qvim::Notification>("qvim::Notification");
    qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    boot.mark("QGuiApplication ctor done");

    qvim::Config cfg;
    cfg.registerOption(QStringLiteral("chat_backend"), qvim::ConfigType::String,
                       QStringLiteral("bridge"));
    cfg.registerOption(QStringLiteral("rounded_highlights"), qvim::ConfigType::StringList,
                       QStringList{});

    QStringList forwardArgs = cli.nvimForwardArgs;
    qvim::ConfigCliReader::extract(forwardArgs, cfg);
    boot.mark("Config registered + CLI read");

    qvim::NvimConnector connector;
    qvim::ClipboardBridge clipboard;
    qvim::WindowChrome windowChrome;
    boot.mark("NvimConnector ctor done");

    if(!connector.start(locateNvim(), forwardArgs)) {
        qFatal("Failed to start nvim. Ensure it is on PATH.");
        return 1;
    }
    boot.mark("nvim --embed spawn returned");
    clipboard.attachTo(&connector);

    // Fire nvim_ui_attach NOW with the best-known grid size so the round-trip
    // (nvim sources init, builds initial highlights + grid, responds)
    // overlaps with QQmlApplicationEngine compilation/instantiation below
    // instead of stacking serially after it. Main.qml's Component.onCompleted
    // issues an nvim_ui_try_resize once GridItem has measured the real cell
    // size against the window — nvim handles the resize cheaply. This is
    // the single biggest cold-launch win: the ~400-700ms attach handshake
    // and the ~300-600ms QML cold load now run concurrently instead of
    // serially. Main.qml guards against double-attach via $connector.attached.
    //
    // If a session cache exists with a valid (cols, rows) from the last run,
    // use those dimensions — they'll match the real geometry when the font
    // hasn't changed, eliminating the tryResize round-trip entirely.
    const qvim::SessionCache sessionCache = qvim::SessionCache::load();
    const int attachCols = sessionCache.isValid() ? sessionCache.cols : 80;
    const int attachRows = sessionCache.isValid() ? sessionCache.rows : 24;
    connector.attachUi(attachCols, attachRows);
    boot.mark("nvim_ui_attach sent (early)");

    // Push the resolved rounded_highlights list into HighlightTable on
    // startup and whenever Config changes — must be wired BEFORE the
    // attachComplete handler runs ConfigGGlobalReader::read, because that
    // will fire Config::changed once the user's g:qvim_rounded_highlights
    // resolves.
    auto applyRounded = [&]() {
        if(auto *h = connector.highlights()) {
            h->setRoundedHighlights(cfg.value(QStringLiteral("rounded_highlights")).toStringList());
        }
    };
    applyRounded();
    QObject::connect(&cfg, &qvim::Config::changed, &connector, [applyRounded](const QString &name) {
        if(name == QStringLiteral("rounded_highlights")) applyRounded();
    });

    QObject::connect(&connector, &qvim::NvimConnector::disconnected, &app, &QCoreApplication::quit);
    QObject::connect(&connector, &qvim::NvimConnector::attachComplete, &cfg,
                     [&connector, &cfg, &boot]() {
        boot.mark("attachComplete signal");
        qvim::ConfigGGlobalReader::read(connector, cfg);
    });
    QObject::connect(&connector, &qvim::NvimConnector::flush, &connector, [&boot]() {
        static bool firstFlushSeen = false;
        if(firstFlushSeen) return;
        firstFlushSeen = true;
        boot.mark("first redraw flush received");
    });

    if(cli.stdinAsBuffer && stdinIsRedirected) {
        QObject::connect(&connector, &qvim::NvimConnector::attachComplete, &connector,
                         [&connector, fut = std::make_shared<std::future<QByteArray>>(
                                          std::move(stdinFuture))]() {
            connector.loadStdinIntoBuffer(fut->get());
        });
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("$config"), &cfg);
    engine.rootContext()->setContextProperty(QStringLiteral("$connector"), &connector);
    engine.rootContext()->setContextProperty(QStringLiteral("$clipboard"), &clipboard);
    engine.rootContext()->setContextProperty(QStringLiteral("$windowChrome"), &windowChrome);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
    boot.mark("engine.loadFromModule done");

    if(!engine.rootObjects().isEmpty()) {
        if(auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            QObject::connect(w, &QQuickWindow::frameSwapped, &app, [&boot]() {
                static bool firstFrameSeen = false;
                if(firstFrameSeen) return;
                firstFrameSeen = true;
                boot.mark("first frame swapped");
            }, Qt::SingleShotConnection);
            // Main.qml keeps the window invisible until the first redraw flush
            // arrives at the post-attach resized geometry — that's the moment
            // the user actually sees the editor. Track it as a distinct boot
            // phase so we can tell time-to-first-frame from time-to-visible.
            QObject::connect(w, &QQuickWindow::visibleChanged, &app, [w, &boot, &connector]() {
                static bool firstShowSeen = false;
                if(firstShowSeen) return;
                if(!w->isVisible()) return;
                firstShowSeen = true;
                boot.mark("window shown at real size");
                // Persist the final grid dimensions so the next launch can
                // skip the placeholder→resize round-trip.
                if(auto *g = connector.grid()) {
                    qvim::SessionCache cache;
                    cache.guifont = connector.guifont();
                    cache.cols = g->gridCols(1);
                    cache.rows = g->gridRows(1);
                    if(cache.isValid()) qvim::SessionCache::save(cache);
                }
            });
        }
    }

    return QGuiApplication::exec();
}
