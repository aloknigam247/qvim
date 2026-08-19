#include "ConfigGGlobalReader.h"

#include "Config.h"
#include "NvimConnector.h"

#include <QDebug>
#include <QMetaType>
#include <QStringList>
#include <QVariant>

namespace qvim {

namespace {

constexpr auto kGGlobalPrefix = "qvim_";

std::optional<QVariant> coerce(ConfigType type, const QVariant &raw) {
    switch(type) {
        case ConfigType::Bool: {
            if(raw.typeId() == QMetaType::Bool) return raw;
            bool ok = false;
            const qlonglong v = raw.toLongLong(&ok);
            if(ok) return QVariant(v != 0);
            return std::nullopt;
        }
        case ConfigType::Int: {
            bool ok = false;
            const qlonglong v = raw.toLongLong(&ok);
            if(!ok) return std::nullopt;
            return QVariant(v);
        }
        case ConfigType::Float: {
            bool ok = false;
            const double v = raw.toDouble(&ok);
            if(!ok) return std::nullopt;
            return QVariant(v);
        }
        case ConfigType::String: {
            if(raw.canConvert<QString>()) return QVariant(raw.toString());
            return std::nullopt;
        }
        case ConfigType::StringList: {
            if(raw.typeId() == QMetaType::QVariantList || raw.typeId() == QMetaType::QStringList) {
                const QVariantList list = raw.toList();
                QStringList out;
                out.reserve(list.size());
                for(const QVariant &v: list) out.push_back(v.toString());
                return QVariant(out);
            }
            if(raw.canConvert<QString>()) { return QVariant(QStringList{ raw.toString() }); }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace

void ConfigGGlobalReader::read(NvimConnector &connector, Config &cfg) {
    const QStringList names = cfg.registeredNames();
    for(const QString &name: names) {
        const ConfigType t = cfg.type(name);
        const QString gName = QString::fromLatin1(kGGlobalPrefix) + name;
        connector.getVar(gName, [name, t, &cfg](std::optional<QVariant> v) {
            if(!v.has_value() || !v->isValid()) return;
            const auto coerced = coerce(t, *v);
            if(!coerced.has_value()) {
                qDebug() << "ConfigGGlobalReader: g:" << QString::fromLatin1(kGGlobalPrefix) + name
                         << "has unexpected type — ignoring";
                return;
            }
            cfg.setFromGGlobal(name, *coerced);
        });
    }
}

void ConfigGGlobalReader::readFromMap(const QHash<QString, QVariant> &vars, Config &cfg) {
    const QStringList names = cfg.registeredNames();
    for(const QString &name: names) {
        const QString gName = QString::fromLatin1(kGGlobalPrefix) + name;
        const auto it = vars.find(gName);
        if(it == vars.end()) continue;
        const auto coerced = coerce(cfg.type(name), *it);
        if(!coerced.has_value()) {
            qDebug() << "ConfigGGlobalReader: g:" << gName << "has unexpected type — ignoring";
            continue;
        }
        cfg.setFromGGlobal(name, *coerced);
    }
}

} // namespace qvim
