#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include "ds5_report.hpp"

namespace BindStorage {

struct BindRecord {
    bool        enabled = true;
    QString     trigger;
    QStringList actions;
};

void           save(const QList<BindRecord>& binds);
QList<BindRecord> load();

ds5::ButtonBit nameToBtn(const QString& name);
QString        btnToName(ds5::ButtonBit btn);

} // namespace BindStorage
