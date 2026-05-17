#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <qqmlregistration.h>

namespace qvim {

// Example custom (non-nvim) feature: reads/writes a JSON file under
// %APPDATA%\qvim\recent.json. Demonstrates that QML side panels can
// plug into the model layer without going through NvimConnector.
class RecentProjectsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        NameRole,
        LastOpenedRole,
    };

    explicit RecentProjectsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void recordOpen(const QString& path);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void reload();

    QString storePath() const;

private:
    struct Entry { QString path; QDateTime lastOpened; };

    void load();
    void save() const;

    QVector<Entry> m_entries;
};

} // namespace qvim
