#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QHash>
#include <functional>

class Backend : public QObject {
public:
    using Reply = std::function<void(QJsonObject)>;
    explicit Backend(QObject *parent = nullptr);
    QString python, adapter, parserRoot, toolsRoot, cacheDir;
    void build(const QString &path, Reply ready, std::function<void(QString,int)> progress,
               std::function<void(QString)> error);
    void query(const QString &database, const QJsonObject &request, const QString &channel, Reply done,
               std::function<void(QString)> error);
    void exportData(const QString &database, const QString &path, const QString &format,
                    const QJsonObject &request, Reply done, std::function<void(QString)> error);
    void cancelBuild();
    bool building() const;
private:
    QProcess *buildProcess = nullptr;
    QHash<QString,QProcess*> queries;
    QProcess *launch(const QStringList &args);
};
