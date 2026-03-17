#include "bindstorage.hpp"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace BindStorage {

static QString config_path()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
           + "/binds.json";
}

void save(const QList<BindRecord>& binds)
{
    const QString path = config_path();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray arr;
    for (const auto& b : binds) {
        QJsonObject obj;
        obj["enabled"] = b.enabled;
        obj["trigger"] = b.trigger;
        QJsonArray acts;
        for (const auto& a : b.actions) acts.append(a);
        obj["actions"] = acts;
        arr.append(obj);
    }

    QJsonObject root;
    root["binds"] = arr;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson());
}

QList<BindRecord> load()
{
    QFile f(config_path());
    if (!f.open(QIODevice::ReadOnly)) return {};

    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return {};

    QList<BindRecord> result;
    for (const auto& v : doc.object()["binds"].toArray()) {
        const QJsonObject obj = v.toObject();
        BindRecord r;
        r.enabled = obj["enabled"].toBool(true);
        r.trigger = obj["trigger"].toString();
        for (const auto& a : obj["actions"].toArray())
            r.actions.append(a.toString());
        if (!r.trigger.isEmpty() && !r.actions.isEmpty())
            result.append(r);
    }
    return result;
}

static const struct { const char* name; ds5::ButtonBit btn; } NAME_MAP[] = {
    {"Triangle",  ds5::BTN_TRIANGLE},
    {"Circle",    ds5::BTN_CIRCLE},
    {"Cross",     ds5::BTN_CROSS},
    {"Square",    ds5::BTN_SQUARE},
    {"L1",        ds5::BTN_L1},
    {"R1",        ds5::BTN_R1},
    {"L2",        ds5::BTN_L2},
    {"R2",        ds5::BTN_R2},
    {"L3",        ds5::BTN_L3},
    {"R3",        ds5::BTN_R3},
    {"Options",   ds5::BTN_OPTIONS},
    {"Create",    ds5::BTN_CREATE},
    {"PS",        ds5::BTN_PS},
    {"Touchpad",  ds5::BTN_TOUCHPAD},
    {"DPadUp",    ds5::BTN_DPAD_UP},
    {"DPadRight", ds5::BTN_DPAD_RIGHT},
    {"DPadDown",  ds5::BTN_DPAD_DOWN},
    {"DPadLeft",  ds5::BTN_DPAD_LEFT},
    {"LFN",       ds5::BTN_LFN},
    {"RFN",       ds5::BTN_RFN},
    {"LB",        ds5::BTN_LB},
    {"RB",        ds5::BTN_RB},
    {"L4",        ds5::BTN_L4},
    {"R4",        ds5::BTN_R4},
};

ds5::ButtonBit nameToBtn(const QString& name)
{
    for (const auto& e : NAME_MAP) {
        if (name == QLatin1String(e.name)) return e.btn;
    }
    return {0, 0};
}

QString btnToName(ds5::ButtonBit btn)
{
    for (const auto& e : NAME_MAP) {
        if (e.btn.byte_offset == btn.byte_offset && e.btn.mask == btn.mask)
            return QString::fromLatin1(e.name);
    }
    return QStringLiteral("Unknown");
}

} // namespace BindStorage
