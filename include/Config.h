#ifndef CONFIG_H
#define CONFIG_H

#include <optional>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace qvim {

enum class ConfigType {
    Bool,
    Int,
    Float,
    String,
    StringList
};

// Central registry for qvim configuration options.
//
// Lifetime: a single Config is created at startup before any option
// reader runs. Each option is registered once with a name, type, and
// default. Values can subsequently be set from three sources:
//
//   * default        — set at register time
//   * g: global var  — read from nvim after attach (ConfigGGlobalReader)
//   * CLI arg        — extracted from argv before nvim starts (ConfigCliReader)
//
// Precedence on read is CLI > g: > default. The changed() signal fires
// only when the RESOLVED value actually changes — setters that do not
// move the resolved value (e.g. setting a g: value while a CLI value
// shadows it) are silent.
class Config : public QObject {
    Q_OBJECT
public:
    explicit Config(QObject *parent = nullptr);

    void registerOption(const QString &name, ConfigType type, QVariant defaultValue);

    Q_INVOKABLE bool has(const QString &name) const;
    Q_INVOKABLE QVariant value(const QString &name) const;
    Q_INVOKABLE QStringList registeredNames() const;

    ConfigType type(const QString &name) const;

    void setFromDefault(const QString &name, QVariant value);
    void setFromGGlobal(const QString &name, QVariant value);
    void setFromCli(const QString &name, QVariant value);

signals:
    void changed(QString name);

private:
    struct Entry {
        ConfigType type{};
        QVariant defaultValue;
        std::optional<QVariant> gGlobal;
        std::optional<QVariant> cli;
    };

    static QVariant resolve(const Entry &e);
    void applyAndNotify(const QString &name, Entry &e, const QVariant &before);

    QHash<QString, Entry> m_entries;
};

} // namespace qvim

#endif
