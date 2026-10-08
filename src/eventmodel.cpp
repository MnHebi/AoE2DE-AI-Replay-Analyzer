#include "eventmodel.h"
#include <QBrush>
#include <QJsonDocument>

EventModel::EventModel(QString view, QObject *parent) : QAbstractTableModel(parent),kind(view) {
    if (kind=="events") {
        keys={"id","time_ms","player_id","action","category","actors","target_id","type_id","status","offset"};
        headers={"Event","Time","Player","Action","Category","Actors","Target","Type ID","Evidence","Byte offset"};
    } else if (kind=="episodes") {
        keys={"id","start_ms","end_ms","player_id","actor_id","action","target_id","event_count","status","outcome"};
        headers={"Episode","Start","End","Player","Actor","Action","Target","Commands","Evidence","Execution outcome"};
    } else {
        keys={"id","start_ms","end_ms","player_id","kind","status","description","uncertainty"};
        headers={"Finding","Start","End","Player","Kind","Evidence","Evidence description","Uncertainty"};
    }
}
int EventModel::rowCount(const QModelIndex &parent) const {return parent.isValid()?0:rows.size();}
int EventModel::columnCount(const QModelIndex &parent) const {return parent.isValid()?0:keys.size();}
QString EventModel::timestamp(qint64 ms) {
    return QString("%1:%2:%3.%4").arg(ms/3600000,2,10,QChar('0')).arg((ms/60000)%60,2,10,QChar('0'))
        .arg((ms/1000)%60,2,10,QChar('0')).arg(ms%1000,3,10,QChar('0'));
}
QVariant EventModel::data(const QModelIndex &index,int role) const {
    if (!index.isValid()||index.row()>=rows.size()||index.column()>=keys.size()) return {};
    const auto row=record(index.row()); const auto key=keys[index.column()]; const auto value=row.value(key);
    if (role==Qt::ToolTipRole) return row.value("uncertainty").toString("Decoded packet; command execution outcome is unavailable.");
    if (role!=Qt::DisplayRole) return {};
    if (value.isNull()||value.isUndefined()) return QStringLiteral("Unavailable");
    if (key.endsWith("_ms")) return timestamp(value.toInteger());
    if (value.isArray()) {
        QStringList items; for (const auto item:value.toArray()) items<<QString::number(item.toInteger());
        return items.isEmpty()?QStringLiteral("Unavailable"):items.join(", ");
    }
    if (key=="type_id") {
        const auto raw=QJsonDocument::fromJson(row.value("raw_json").toString().toUtf8()).object();
        auto names=typeLabels.value(raw.contains("unit_id")?"units":raw.contains("building_id")?"buildings":"technologies").toObject();
        const auto label=names.value(QString::number(value.toInteger())).toString();
        if(!label.isEmpty())return QString::number(value.toInteger())+" ("+label+", profile label)";
    }
    return value.toVariant();
}
QVariant EventModel::headerData(int section,Qt::Orientation orientation,int role) const {
    if (role==Qt::DisplayRole&&orientation==Qt::Horizontal&&section<headers.size()) return headers[section];
    return {};
}
void EventModel::replace(const QJsonObject &page) {
    beginResetModel(); rows=page.value("rows").toArray(); total=page.value("total").toInteger(); offset=page.value("offset").toInt(); endResetModel();
}
QJsonObject EventModel::record(int row) const {return row>=0&&row<rows.size()?rows.at(row).toObject():QJsonObject{};}
