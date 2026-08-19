#include "SessionCache.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace qvim {

namespace {

QString cacheFilePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("session.json"));
}

} // namespace

SessionCache SessionCache::load() {
    SessionCache c;
    QFile f(cacheFilePath());
    if(!f.open(QIODevice::ReadOnly)) return c;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if(!doc.isObject()) return c;
    const QJsonObject obj = doc.object();
    c.guifont = obj.value(QStringLiteral("guifont")).toString();
    c.cols = obj.value(QStringLiteral("cols")).toInt(0);
    c.rows = obj.value(QStringLiteral("rows")).toInt(0);
    return c;
}

void SessionCache::save(const SessionCache &cache) {
    QJsonObject obj;
    obj[QStringLiteral("guifont")] = cache.guifont;
    obj[QStringLiteral("cols")] = cache.cols;
    obj[QStringLiteral("rows")] = cache.rows;
    QFile f(cacheFilePath());
    if(!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace qvim
