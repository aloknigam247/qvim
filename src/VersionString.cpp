#include "VersionString.h"

#include "Version.h"

namespace qvim {

QString versionString() { return QStringLiteral("qvim ") + QString::fromLatin1(QVIM_VERSION); }

} // namespace qvim
