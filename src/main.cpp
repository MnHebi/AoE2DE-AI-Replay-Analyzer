#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTextStream>
#include <QFontDatabase>
#include <QDir>

int main(int argc,char **argv){
    QApplication app(argc,argv);QCoreApplication::setOrganizationName("AoE2ReplayTools");QCoreApplication::setApplicationName("ReplayAnalysis");
#ifdef Q_OS_WIN
    // Qt's offscreen Windows plugin has no native font database. Supply a system
    // font for readable integration screenshots; interactive Windows uses its own.
    if(qEnvironmentVariable("QT_QPA_PLATFORM")=="offscreen"){
        const auto font=QDir(qEnvironmentVariable("WINDIR","C:/Windows")).filePath("Fonts/segoeui.ttf");
        const auto id=QFontDatabase::addApplicationFont(font);
        if(id>=0&&!QFontDatabase::applicationFontFamilies(id).isEmpty())app.setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(),9));
    }
#endif
    QCommandLineParser parser;parser.setApplicationDescription("Native AoE2 DE replay analysis using the established mgz decoder");parser.addHelpOption();
    parser.addOption({"smoke-test","Construct and render the native GUI, then exit."});
    parser.addOption({"database","Open a versioned replay index.","path"});
    parser.addOption({"screenshot","Save the smoke-test window to a PNG.","path"});
    parser.addOption({"cache-dir","Use this directory for derived replay indices.","path"});
    parser.addPositionalArgument("replay","Optional .aoe2record file");parser.process(app);
    Window window;window.show();window.screenshot=parser.value("screenshot");
    if(parser.isSet("cache-dir"))window.setCacheDirectory(parser.value("cache-dir"));
    if(parser.isSet("smoke-test")){
        QTimer::singleShot(30000,&app,[&app]{app.exit(2);});
        if(parser.isSet("database")||!parser.positionalArguments().isEmpty()){
            window.smokeComplete=[&app](bool ok,QString result){QTextStream(stdout)<<result<<Qt::endl;QTimer::singleShot(0,&app,[&app,ok]{app.exit(ok?0:1);});};
            if(parser.isSet("database"))window.loadDatabase(parser.value("database"));
            else window.openReplay(parser.positionalArguments().first());
        }else QTimer::singleShot(100,&app,[&]{bool ok=window.isVisible();if(!window.screenshot.isEmpty())ok=window.grab().save(window.screenshot)&&ok;app.exit(ok?0:1);});
    }else if(parser.isSet("database"))window.loadDatabase(parser.value("database"));
    else if(!parser.positionalArguments().isEmpty())window.openReplay(parser.positionalArguments().first());
    return app.exec();
}
