#include "backend.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSettings>
#include <QPointer>
#include <memory>

Backend::Backend(QObject *parent) : QObject(parent) {
    const auto base = qEnvironmentVariable("AOE2_BACKEND_ROOT",QCoreApplication::applicationDirPath());
    adapter = base + "/backend/adapter.py";
    QSettings s;
    python = qEnvironmentVariable("AOE2_PYTHON");
    if(python.isEmpty())python=s.value("python").toString();
    parserRoot = s.value("parserRoot", base + "/backend/vendor").toString();
    toolsRoot = s.value("toolsRoot", base + "/backend/vendor_helpers").toString();
    cacheDir = s.value("cacheDir",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).toString();
    QDir().mkpath(cacheDir);
}

QProcess *Backend::launch(const QStringList &args) {
    auto *p = new QProcess(this);
    p->setArguments(QStringList{adapter} + args);
    p->setProcessChannelMode(QProcess::SeparateChannels);
    return p;
}

void Backend::start(QProcess *process,std::function<bool()> active,std::function<void(QString)> error){
    if(!QFileInfo::exists(adapter)){
        error("The backend adapter is missing. Extract the complete release ZIP beside the executable. "
              "Development builds can set AOE2_BACKEND_ROOT to the source directory.");
        process->deleteLater();return;
    }
    const QPointer<QProcess> guarded(process);
    runtime.resolve(python,[this,guarded,active](QString executable){
        if(!guarded||!active())return;
        python=executable;guarded->setProgram(executable);guarded->start();
    },[guarded,active,error](QString message){
        if(!guarded||!active())return;
        error(message);guarded->deleteLater();
    });
}

void Backend::build(const QString &path, Reply ready, std::function<void(QString,int)> progress,
                    std::function<void(QString)> error) {
    cancelBuild();
    auto *p = launch({"build",path,"--parser-root",parserRoot,"--tools-root",toolsRoot,"--cache-dir",cacheDir});
    buildProcess = p;
    auto pending = std::make_shared<QByteArray>();
    auto stderrText = std::make_shared<QByteArray>();
    auto gotReady = std::make_shared<bool>(false);
    connect(p,&QProcess::readyReadStandardError,this,[p,stderrText]{
        stderrText->append(p->readAllStandardError()); *stderrText = stderrText->right(8192);
    });
    connect(p,&QProcess::readyReadStandardOutput,this,[=,this]{
        pending->append(p->readAllStandardOutput());
        while (pending->contains('\n')) {
            auto end = pending->indexOf('\n');
            auto line = pending->left(end); pending->remove(0,end+1);
            auto obj = QJsonDocument::fromJson(line).object();
            if (buildProcess != p) continue;
            const auto kind = obj.value("kind").toString();
            if (kind == "progress") progress(obj.value("phase").toString(),obj.value("percent").toInt());
            else if (kind == "ready") { *gotReady = true; ready(obj); }
            else if (kind == "error") error(obj.value("message").toString());
        }
    });
    connect(p,&QProcess::errorOccurred,this,[=,this](QProcess::ProcessError cause){
        if (buildProcess == p) {
            if(cause==QProcess::FailedToStart){buildProcess=nullptr;progress("Stopped",0);p->deleteLater();}
            error(p->errorString());
        }
    });
    connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[=,this](int code,QProcess::ExitStatus){
        if (buildProcess == p) {
            buildProcess = nullptr;
            if (code != 0 && !*gotReady && !stderrText->isEmpty()) error(QString::fromUtf8(*stderrText));
            progress(*gotReady ? "Ready" : "Stopped", *gotReady ? 100 : 0);
        }
        p->deleteLater();
    });
    start(p,[this,p]{return buildProcess==p;},[this,error,progress](QString message){
        buildProcess=nullptr;progress("Stopped",0);error(message);
    });
}

void Backend::query(const QString &database, const QJsonObject &request, const QString &channel,
                    Reply done, std::function<void(QString)> error) {
    cancelQuery(channel);
    auto *p = launch({"query",database,"--request",QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact))});
    queries[channel] = p;
    auto output = std::make_shared<QByteArray>();
    connect(p,&QProcess::readyReadStandardOutput,this,[p,output]{output->append(p->readAllStandardOutput());});
    connect(p,&QProcess::errorOccurred,this,[=,this](QProcess::ProcessError){
        if (queries.value(channel) == p) { queries.remove(channel); error(p->errorString()); } p->deleteLater();
    });
    connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[=,this](int code,QProcess::ExitStatus){
        if (queries.value(channel) != p) { p->deleteLater(); return; }
        queries.remove(channel);
        output->append(p->readAllStandardOutput());
        QJsonParseError parseError;
        auto document = QJsonDocument::fromJson(*output,&parseError);
        if (code == 0 && parseError.error == QJsonParseError::NoError) done(document.object());
        else error(document.object().value("message").toString(QString::fromUtf8(p->readAllStandardError())));
        p->deleteLater();
    });
    start(p,[this,p,channel]{return queries.value(channel)==p;},[this,error,channel](QString message){queries.remove(channel);error(message);});
}

void Backend::exportData(const QString &database, const QString &path, const QString &format,
                         const QJsonObject &request, Reply done, std::function<void(QString)> error) {
    auto *p = launch({"export",database,path,"--format",format,"--request",QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact))});
    connect(p,&QProcess::errorOccurred,this,[=](QProcess::ProcessError){error(p->errorString()); p->deleteLater();});
    connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[=](int code,QProcess::ExitStatus){
        auto obj = QJsonDocument::fromJson(p->readAllStandardOutput()).object();
        if (code == 0) done(obj); else error(obj.value("message").toString(QString::fromUtf8(p->readAllStandardError())));
        p->deleteLater();
    });
    start(p,[]{return true;},error);
}

void Backend::cancelBuild() {
    if (buildProcess) { auto *p=buildProcess; buildProcess=nullptr; p->kill();p->deleteLater(); }
}
bool Backend::building() const { return buildProcess != nullptr; }

void Backend::cancelQuery(const QString &channel){
    if(auto *process=queries.take(channel)){process->disconnect(this);process->kill();process->deleteLater();}
}
