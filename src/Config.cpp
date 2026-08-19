#include "Config.h"

#include <QDebug>

namespace qvim {

Config::Config(QObject *parent) : QObject(parent) {}

void Config::registerOption(const QString &name, ConfigType type, QVariant defaultValue) {
    if(m_entries.contains(name)) {
        qWarning() << "Config::registerOption: option already registered:" << name;
        return;
    }
    Entry e;
    e.type = type;
    e.defaultValue = std::move(defaultValue);
    m_entries.insert(name, std::move(e));
}

bool Config::has(const QString &name) const { return m_entries.contains(name); }

QVariant Config::value(const QString &name) const {
    const auto it = m_entries.find(name);
    if(it == m_entries.end()) return {};
    return resolve(*it);
}

QStringList Config::registeredNames() const {
    QStringList names = m_entries.keys();
    names.sort();
    return names;
}

ConfigType Config::type(const QString &name) const {
    const auto it = m_entries.find(name);
    if(it == m_entries.end()) return ConfigType::String;
    return it->type;
}

QVariant Config::resolve(const Entry &e) const {
    if(e.cli.has_value()) return *e.cli;
    if(e.gGlobal.has_value()) return *e.gGlobal;
    return e.defaultValue;
}

void Config::applyAndNotify(const QString &name, Entry &e, QVariant before) {
    const QVariant after = resolve(e);
    if(after != before) { emit changed(name); }
    Q_UNUSED(e);
}

void Config::setFromDefault(const QString &name, QVariant value) {
    auto it = m_entries.find(name);
    if(it == m_entries.end()) return;
    const QVariant before = resolve(*it);
    it->defaultValue = std::move(value);
    applyAndNotify(name, *it, before);
}

void Config::setFromGGlobal(const QString &name, QVariant value) {
    auto it = m_entries.find(name);
    if(it == m_entries.end()) return;
    const QVariant before = resolve(*it);
    it->gGlobal = std::move(value);
    applyAndNotify(name, *it, before);
}

void Config::setFromCli(const QString &name, QVariant value) {
    auto it = m_entries.find(name);
    if(it == m_entries.end()) return;
    const QVariant before = resolve(*it);
    it->cli = std::move(value);
    applyAndNotify(name, *it, before);
}

} // namespace qvim
