#include "pythonruntime.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
const QString missingPython = QStringLiteral(
    "No working Python 3.12 or newer was found. Install Python 3.12+ and select its "
    "python.exe under File > Backend settings, or set AOE2_PYTHON to that executable. "
    "The Windows Store shortcut is not an installed interpreter.");
}

PythonRuntime::PythonRuntime(QObject *parent):QObject(parent){
    probeTimeout.setSingleShot(true);totalTimeout.setSingleShot(true);
    connect(&probeTimeout,&QTimer::timeout,this,[this]{if(probe)probe->kill();});
    connect(&totalTimeout,&QTimer::timeout,this,[this]{finish({});});
}

bool PythonRuntime::isStoreAlias(const QString &path){
#ifdef Q_OS_WIN
    const auto normalized=QDir::fromNativeSeparators(path).toLower();
    const auto name=QFileInfo(normalized).fileName();
    return normalized.contains("/microsoft/windowsapps/") &&
           (name=="python.exe"||name=="python3.exe"||name=="pythonw.exe");
#else
    Q_UNUSED(path);return false;
#endif
}

QList<QStringList> PythonRuntime::candidates(const QString &preferred){
    QList<QStringList> result;
    auto append=[&](QString executable,QStringList arguments=QStringList{}){
        if(executable.isEmpty())return;
        if(QFileInfo(executable).isRelative())executable=QStandardPaths::findExecutable(executable);
        if(executable.isEmpty()||isStoreAlias(executable)||!QFileInfo(executable).isFile())return;
        QStringList command{QFileInfo(executable).absoluteFilePath()};command+=arguments;
        if(!result.contains(command))result.append(command);
    };
    append(preferred);
#ifdef Q_OS_WIN
    // Enumerate PATH entries rather than accepting only the first python alias.
    for(const auto &directory:qEnvironmentVariable("PATH").split(QDir::listSeparator(),Qt::SkipEmptyParts)){
        append(QDir(directory).filePath("python3.exe"));append(QDir(directory).filePath("python.exe"));
    }
    const auto launcher=QStandardPaths::findExecutable("py.exe");
    append(launcher,{"-3"});append(launcher,{"-3.12"});
    const auto local=qEnvironmentVariable("LOCALAPPDATA");
    const auto userPython=QDir(local).filePath("Programs/Python");
    for(const auto &parent:QStringList{userPython,qEnvironmentVariable("ProgramFiles"),qEnvironmentVariable("ProgramFiles(x86)")}){
        if(parent.isEmpty())continue;
        const QDir directory(parent);
        for(const auto &entry:directory.entryList({"Python*"},QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name|QDir::Reversed))
            append(directory.filePath(entry+"/python.exe"));
    }
    append(QDir(QDir::homePath()).filePath(".cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe"));
#else
    append("python3");append("python");
#endif
    return result;
}

void PythonRuntime::resolve(const QString &preferred,Result ready,Result error){
    if(!validatedExecutable.isEmpty()&&(preferred==validatedExecutable||preferred==validatedPreference)){
        ready(validatedExecutable);return;
    }
    if(resolving){waiting.append({std::move(ready),std::move(error)});return;}
    requested=preferred;resolveCandidates(candidates(preferred),std::move(ready),std::move(error));
}

void PythonRuntime::resolveCandidates(const QList<QStringList> &options,Result ready,Result error){
    waiting.append({std::move(ready),std::move(error)});
    if(resolving)return;
    commands=options;resolving=true;totalTimeout.start(15000);next();
}

void PythonRuntime::next(){
    if(commands.isEmpty()){finish({});return;}
    const auto command=commands.takeFirst();
    if(command.isEmpty()||isStoreAlias(command.first())){next();return;}
    auto *process=new QProcess(this);probe=process;
    process->setProgram(command.first());
    auto arguments=command.mid(1);
    arguments+=QStringList{"-I","-c", "import sys,json,sqlite3; print(json.dumps({'executable':sys.executable,'major':sys.version_info[0],'minor':sys.version_info[1]}))"};
    process->setArguments(arguments);
    connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError cause){
        if(probe!=process||cause!=QProcess::FailedToStart)return;
        probeTimeout.stop();probe=nullptr;process->deleteLater();next();
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process](int code,QProcess::ExitStatus status){
        if(probe!=process)return;
        probeTimeout.stop();probe=nullptr;
        const auto value=QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        const auto executable=value.value("executable").toString();
        const bool good=code==0&&status==QProcess::NormalExit&&value.value("major").toInt()==3&&
            value.value("minor").toInt()>=12&&QFileInfo(executable).isAbsolute()&&QFileInfo(executable).isFile()&&!isStoreAlias(executable);
        process->deleteLater();
        if(good)finish(QFileInfo(executable).absoluteFilePath());else next();
    });
    probeTimeout.start(2500);process->start();
}

void PythonRuntime::finish(const QString &executable){
    probeTimeout.stop();totalTimeout.stop();
    if(probe){auto *process=probe;probe=nullptr;process->disconnect(this);process->kill();process->deleteLater();}
    resolving=false;
    if(!executable.isEmpty()){validatedPreference=requested;validatedExecutable=executable;}
    auto callbacks=std::move(waiting);waiting.clear();
    for(auto &callback:callbacks){if(executable.isEmpty())callback.error(missingPython);else callback.ready(executable);}
}
