#include "AppIcon.h"

#include <QGuiApplication>
#include <QIcon>
#include <QString>

// Q_INIT_RESOURCE expands to a free function in the global namespace whose
// name encodes the .qrc basename. Forward-declare it here so we can call it
// from the qvim namespace without macro-pasting issues.
extern int qInitResources_qvim();

namespace qvim {

void setupApplicationIcon(QGuiApplication & /*app*/) {
    // The .qrc is linked into qvim_lib (a static library). Static-lib resources
    // are not auto-initialised in every translation unit, so we must invoke the
    // generated init explicitly before constructing the QIcon.
    qInitResources_qvim();
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.ico")));
}

} // namespace qvim
