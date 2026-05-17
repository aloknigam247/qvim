#include "RecentProjectsModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace qvim {

namespace {
constexpr int kMaxEntries = 50;
} // namespace

RecentProjectsModel::RecentProjectsModel(QObject* parent) : QAbstractListModel(parent) {
    load();
}

QString RecentProjectsModel::storePath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("recent.json"));
}

void RecentProjectsModel::load() {
    beginResetModel();
    m_entries.clear();
    QFile f(storePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) {
            for (const QJsonValue v : doc.array()) {
                if (!v.isObject()) continue;
                const QJsonObject o = v.toObject();
                Entry e;
                e.path = o.value(QStringLiteral("path")).toString();
                e.lastOpened = QDateTime::fromString(
                    o.value(QStringLiteral("lastOpened")).toString(), Qt::ISODate);
                if (!e.path.isEmpty()) m_entries.push_back(e);
            }
        }
    }
    endResetModel();
}

void RecentProjectsModel::save() const {
    QJsonArray arr;
    for (const Entry& e : m_entries) {
        QJsonObject o;
        o.insert(QStringLiteral("path"), e.path);
        o.insert(QStringLiteral("lastOpened"), e.lastOpened.toString(Qt::ISODate));
        arr.append(o);
    }
    QFile f(storePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }
}

int RecentProjectsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant RecentProjectsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size()) return {};
    const Entry& e = m_entries[index.row()];
    switch (role) {
        case PathRole:       return e.path;
        case NameRole:       return QFileInfo(e.path).fileName().isEmpty()
                                    ? QFileInfo(e.path).absoluteFilePath()
                                    : QFileInfo(e.path).fileName();
        case LastOpenedRole: return e.lastOpened;
        default:             return {};
    }
}

QHash<int, QByteArray> RecentProjectsModel::roleNames() const {
    return {
        {PathRole,       "path"},
        {NameRole,       "name"},
        {LastOpenedRole, "lastOpened"},
    };
}

void RecentProjectsModel::recordOpen(const QString& path) {
    if (path.isEmpty()) return;
    const QString canon = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_entries.size(); ++i) {
        if (QFileInfo(m_entries[i].path).absoluteFilePath() == canon) {
            beginRemoveRows({}, i, i);
            m_entries.remove(i);
            endRemoveRows();
            break;
        }
    }
    beginInsertRows({}, 0, 0);
    m_entries.prepend(Entry{canon, QDateTime::currentDateTime()});
    endInsertRows();
    if (m_entries.size() > kMaxEntries) {
        beginRemoveRows({}, kMaxEntries, m_entries.size() - 1);
        m_entries.resize(kMaxEntries);
        endRemoveRows();
    }
    save();
}

void RecentProjectsModel::remove(int index) {
    if (index < 0 || index >= m_entries.size()) return;
    beginRemoveRows({}, index, index);
    m_entries.remove(index);
    endRemoveRows();
    save();
}

void RecentProjectsModel::reload() {
    load();
}

} // namespace qvim
