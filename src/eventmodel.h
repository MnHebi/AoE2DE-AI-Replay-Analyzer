#pragma once
#include <QAbstractTableModel>
#include <QJsonArray>
#include <QJsonObject>

class EventModel : public QAbstractTableModel {
public:
    explicit EventModel(QString kind, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void replace(const QJsonObject &page);
    QJsonObject record(int row) const;
    QString kind;
    qint64 total = 0;
    int offset = 0;
    QJsonObject typeLabels;
    static QString timestamp(qint64 milliseconds);
private:
    QStringList keys, headers;
    QJsonArray rows;
};
