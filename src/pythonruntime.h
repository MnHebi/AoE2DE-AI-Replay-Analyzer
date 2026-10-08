#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QStringList>
#include <functional>

// Each candidate contains an executable followed by optional launcher arguments.
class PythonRuntime : public QObject {
public:
    using Result = std::function<void(QString)>;
    explicit PythonRuntime(QObject *parent=nullptr);
    void resolve(const QString &preferred, Result ready, Result error);
    static QList<QStringList> candidates(const QString &preferred);
    static bool isStoreAlias(const QString &path);
    // The same probes can be run against controlled candidates in native tests.
    void resolveCandidates(const QList<QStringList> &commands, Result ready, Result error);
private:
    struct Waiting { Result ready, error; };
    QList<Waiting> waiting;
    QList<QStringList> commands;
    QString requested, validatedPreference, validatedExecutable;
    QProcess *probe=nullptr;
    QTimer probeTimeout, totalTimeout;
    bool resolving=false;
    void next();
    void finish(const QString &executable);
};
