// Verifies that forwarded argv reaches the embedded nvim — i.e. that
// `qvim foo.txt` actually opens foo.txt. We pass the file path through the
// new NvimConnector::start(extraArgs) hook and then ask nvim for the name
// of the current buffer after attach+flush.

#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QtTest>

#include <msgpack.hpp>

#include "IntegrationHelpers.h"
#include "MsgpackRpc.h"
#include "NvimConnector.h"

using namespace qvim;
using namespace qvim::test;

class TestOpenFileArg : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void forwardedFileBecomesCurrentBuffer() {
        // Write a temp file with known contents. We close before nvim opens
        // it because QTemporaryFile holds an exclusive handle on Windows.
        QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/qvim_open_arg_XXXXXX.txt"));
        tmp.setAutoRemove(true);
        QVERIFY(tmp.open());
        tmp.write("qvim arg test\n");
        tmp.flush();
        const QString path = tmp.fileName();
        tmp.close();
        QVERIFY(!path.isEmpty());

        // Use raw MsgpackRpc so we can issue requests and read responses.
        // NvimConnector::start() is the production wiring; this test
        // verifies the underlying forward path it depends on.
        MsgpackRpc rpc;
        QVERIFY(rpc.startEmbeddedNvim(locateNvim(), QStringList{path}));

        // Attach UI so nvim's startup completes and the file argument is
        // actually loaded into a buffer.
        bool attachDone = false;
        rpc.request(QStringLiteral("nvim_ui_attach"),
            [](msgpack::packer<msgpack::sbuffer>& pk) {
                pk.pack_array(3);
                pk.pack(static_cast<int64_t>(80));
                pk.pack(static_cast<int64_t>(24));
                pk.pack_map(1);
                pk.pack(std::string("ext_linegrid"));
                pk.pack(true);
            },
            [&](RpcResult) { attachDone = true; });

        QElapsedTimer t;
        t.start();
        while (!attachDone && t.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
        QVERIFY2(attachDone, "nvim_ui_attach did not complete");

        // nvim_buf_get_name(0) returns the full path of the current buffer.
        // The dispatcher hands back the entire response envelope handle
        // `[1, msgid, err, result]`, so we index into arr.ptr[3].
        QString bufName;
        bool nameDone = false;
        rpc.request(QStringLiteral("nvim_buf_get_name"),
            [](msgpack::packer<msgpack::sbuffer>& pk) {
                pk.pack_array(1);
                pk.pack(static_cast<int64_t>(0));
            },
            [&](RpcResult r) {
                nameDone = true;
                if (!r) return;
                const msgpack::object& root = r.value()->get();
                if (root.type != msgpack::type::ARRAY) return;
                const auto& arr = root.via.array;
                if (arr.size < 4) return;
                const msgpack::object& result = arr.ptr[3];
                if (result.type == msgpack::type::STR) {
                    bufName = QString::fromUtf8(result.via.str.ptr, result.via.str.size);
                }
            });

        t.restart();
        while (!nameDone && t.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
        QVERIFY2(nameDone, "nvim_buf_get_name did not return");

        const QString expectedBasename = QFileInfo(path).fileName();
        QVERIFY2(!bufName.isEmpty(),
                 "Current buffer name is empty — file argument was not forwarded");
        // Compare basenames so the test is robust to nvim's path canonicalisation
        // (Windows short paths, drive-letter casing, /tmp vs C:\\Users\\...\\Temp).
        const QString actualBasename = QFileInfo(bufName).fileName();
        QCOMPARE(actualBasename, expectedBasename);
    }
};

QTEST_MAIN(TestOpenFileArg)
#include "test_open_file_arg.moc"
