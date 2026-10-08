#pragma once
#include <QWidget>
#include <QJsonObject>
#include <functional>
#include <optional>

class Timeline : public QWidget {
public:
    explicit Timeline(QWidget *parent=nullptr);
    void setBins(const QJsonObject &data);
    void setSelection(bool enabled, qint64 from, qint64 to, std::optional<qint64> at);
    std::function<void(qint64)> navigate;
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
private:
    QJsonObject bins;
    bool selectionEnabled=false;
    qint64 selectionFrom=0, selectionTo=0;
    std::optional<qint64> selectionTime;
};
