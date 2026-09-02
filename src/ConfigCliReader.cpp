#include "ConfigCliReader.h"

#include "Config.h"

#include <QDebug>
#include <QStringList>
#include <QVariant>

namespace qvim {

constexpr auto kPrefix = "--qvim-";
constexpr qsizetype kPrefixLen = 7;

static std::optional<bool> parseBool(const QString &s) {
    const QString v = s.trimmed().toLower();
    if(v == QStringLiteral("1") || v == QStringLiteral("true") || v == QStringLiteral("yes") ||
       v == QStringLiteral("on"))
        return true;
    if(v == QStringLiteral("0") || v == QStringLiteral("false") || v == QStringLiteral("no") ||
       v == QStringLiteral("off"))
        return false;
    return std::nullopt;
}

static std::optional<QVariant> coerce(ConfigType type, const QString &raw) {
    switch(type) {
        case ConfigType::Bool: {
            auto b = parseBool(raw);
            if(!b.has_value()) return std::nullopt;
            return QVariant(*b);
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
        case ConfigType::String:
            return QVariant(raw);
        case ConfigType::StringList: {
            const QStringList parts = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
            return QVariant(parts);
        }
    }
    return std::nullopt;
}

QStringList ConfigCliReader::extract(QStringList &args, Config &cfg) {
    QStringList kept;
    kept.reserve(args.size());
    for(const QString &arg: args) {
        if(!arg.startsWith(QLatin1String(kPrefix))) {
            kept.push_back(arg);
            continue;
        }
        const QString body = arg.mid(kPrefixLen);
        const qsizetype eq = body.indexOf(QLatin1Char('='));
        QString name;
        std::optional<QString> rawValue;
        if(eq < 0) {
            name = body;
        } else {
            name = body.left(eq);
            rawValue = body.mid(eq + 1);
        }
        if(!cfg.has(name)) {
            qDebug() << "ConfigCliReader: unknown --qvim option:" << arg;
            continue;
        }
        const ConfigType t = cfg.type(name);
        QString effective;
        if(!rawValue.has_value()) {
            if(t != ConfigType::Bool) {
                qDebug() << "ConfigCliReader: option requires a value:" << arg;
                continue;
            }
            effective = QStringLiteral("true");
        } else {
            effective = *rawValue;
        }
        const auto coerced = coerce(t, effective);
        if(!coerced.has_value()) {
            qDebug() << "ConfigCliReader: malformed value for" << name << ":" << effective;
            continue;
        }
        cfg.setFromCli(name, *coerced);
    }
    args = kept;
    return args;
}

} // namespace qvim
