#pragma once
#include <QWidget>
#include <QJsonObject>
#include <functional>

class Timeline : public QWidget {
public:
    explicit Timeline(QWidget *parent=nullptr);
    void setBins(const QJsonObject &data);
    std::function<void(qint64)> navigate;
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
private:
    QJsonObject bins;
};
