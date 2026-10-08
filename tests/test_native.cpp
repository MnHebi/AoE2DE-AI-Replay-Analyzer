#include "window.h"
#include "pythonruntime.h"
#include "helpdialog.h"
#include <QtTest>
#include <QApplication>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QStatusBar>
#include <QTextBrowser>

class NativeTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary{qEnvironmentVariable("AOE2_TEST_SOURCE")+"/build/native-tests-XXXXXX"};
    QString python,source,database,backendRoot;
    QList<QJsonObject> queries()const{
        QFile file(temporary.filePath("queries.jsonl"));file.open(QIODevice::ReadOnly);
        QList<QJsonObject> records;
        for(const auto &line:file.readAll().split('\n'))if(!line.isEmpty())records.append(QJsonDocument::fromJson(line).object());
        return records;
    }
    void open(Window &window){
        window.show();window.tabs->setCurrentWidget(window.events.widget);window.loadDatabase(database);
    }
private slots:
    void initTestCase(){
        QVERIFY(temporary.isValid());python=qEnvironmentVariable("AOE2_TEST_PYTHON");source=qEnvironmentVariable("AOE2_TEST_SOURCE");
        QVERIFY(QFile::exists(python));QVERIFY(QFile::exists(source+"/backend/adapter.py"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
        QCoreApplication::setOrganizationName("ReplayAnalyzerTests");QCoreApplication::setApplicationName("Native");
        QProcess fixture;fixture.start(python,{source+"/tests/make_native_fixture.py",temporary.path(),source});
        QVERIFY(fixture.waitForFinished(10000));QVERIFY2(fixture.exitCode()==0,fixture.readAllStandardError().constData());
        database=temporary.filePath("fixture.sqlite");backendRoot=temporary.path();
        qputenv("AOE2_BACKEND_ROOT",backendRoot.toUtf8());qputenv("AOE2_PYTHON",python.toUtf8());
    }
    void init(){QSettings().clear();QFile::remove(temporary.filePath("queries.jsonl"));}
    void helpGuideWorksOfflineAndPreservesReplay(){
        Window window;window.show();window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTest::keyClick(&window,Qt::Key_F1);
        QTRY_VERIFY(window.helpGuide&&window.helpGuide->isVisible());
        auto *guide=window.helpGuide.data();
        auto *search=guide->findChild<QLineEdit*>("helpSearch");
        auto *topics=guide->findChild<QListWidget*>("helpTopics");
        auto *description=guide->findChild<QTextBrowser*>("helpDescription");
        QVERIFY(search&&topics&&description);QVERIFY(queries().isEmpty());
        QTest::keyClicks(search,"EPISODE");
        QVERIFY(topics->currentItem()->text().startsWith("Episode /"));
        search->setText("  derived   cache  ");
        QVERIFY(topics->currentItem()->text().startsWith("Cache /"));
        search->setText("zz_no_such_glossary_term_zz");
        for(int i=0;i<topics->count();++i)QVERIFY(topics->item(i)->isHidden());
        QVERIFY(description->toPlainText().contains("No matching terms"));
        search->clear();QVERIFY(!topics->item(0)->isHidden());
        topics->setFocus();QTest::keyClick(topics,Qt::Key_Down);
        QCOMPARE(topics->currentRow(),1);QVERIFY(!description->toPlainText().isEmpty());
        QTest::keyClick(guide,Qt::Key_Escape);QVERIFY(!guide->isVisible());
        QCOMPARE(window.showHelp(),guide);QVERIFY(guide->isVisible());guide->close();
        // Opening help over loaded data must not refresh or reset the replay.
        open(window);QTRY_COMPARE_WITH_TIMEOUT(window.events.model->total,800,10000);
        QTest::mouseClick(window.events.next,Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(window.events.model->offset,250,5000);
        const auto before=queries().size();const auto request=window.queryRequest("events");
        window.showHelp();search->setText("unknown ownership");guide->close();
        QCOMPARE(window.queryRequest("events"),request);
        QCOMPARE(window.events.model->offset,250);QCOMPARE(queries().size(),before);
    }
    void pythonCandidatesSkipStoreAndRejectOldVersions(){
        PythonRuntime resolver;QString found,error;
        const auto self=QCoreApplication::applicationFilePath();
        resolver.resolveCandidates({{self,"--fake-python27"},{self,"--fake-python311"},{self,"--fake-store-python"},{python}},
            [&](QString value){found=value;},[&](QString value){error=value;});
        QTRY_VERIFY_WITH_TIMEOUT(!found.isEmpty()||!error.isEmpty(),10000);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(QFileInfo(found).absoluteFilePath(),QFileInfo(python).absoluteFilePath());
#ifdef Q_OS_WIN
        QVERIFY(PythonRuntime::isStoreAlias("C:/Users/example/AppData/Local/Microsoft/WindowsApps/python3.exe"));
        for(const auto &candidate:PythonRuntime::candidates("C:/Users/example/AppData/Local/Microsoft/WindowsApps/python3.exe"))
            QVERIFY(!PythonRuntime::isStoreAlias(candidate.first()));
#endif
    }
    void missingPythonHasActionableError(){
        PythonRuntime resolver;QString error;bool succeeded=false;
        resolver.resolveCandidates({{QCoreApplication::applicationFilePath(),"--fake-python311"}},
            [&](QString){succeeded=true;},[&](QString value){error=value;});
        QTRY_VERIFY_WITH_TIMEOUT(!error.isEmpty(),5000);QVERIFY(!succeeded);
        QVERIFY(error.contains("3.12"));QVERIFY(error.contains("Backend settings"));
    }
    void directExePathDiscoversPythonWithoutEnvironmentOverride(){
        qunsetenv("AOE2_PYTHON");
        // A normal installation later on PATH must survive an earlier Store alias.
        const auto oldPath=qgetenv("PATH");
        auto discoveryPath=QFileInfo(python).absolutePath()+QDir::listSeparator()+QString::fromUtf8(oldPath);
#ifdef Q_OS_WIN
        discoveryPath="C:/Users/example/AppData/Local/Microsoft/WindowsApps;"+discoveryPath;
#endif
        qputenv("PATH",discoveryPath.toUtf8());
        Window window;open(window);QTRY_COMPARE_WITH_TIMEOUT(window.events.model->total,800,15000);
        QCOMPARE(QFileInfo(window.backend.python).absoluteFilePath(),QFileInfo(python).absoluteFilePath());
        qputenv("PATH",oldPath);qputenv("AOE2_PYTHON",python.toUtf8());
    }
    void visibleQueriesAndTabsPreservePagination(){
        Window window;open(window);QTRY_COMPARE_WITH_TIMEOUT(window.events.model->total,800,10000);
        QCOMPARE(queries().size(),2);QCOMPARE(queries()[0].value("view").toString(),QString("overview"));
        QCOMPARE(queries()[1].value("view").toString(),QString("events"));
        QTest::mouseClick(window.events.next,Qt::LeftButton);QTRY_COMPARE_WITH_TIMEOUT(window.events.model->offset,250,5000);
        QTest::mouseClick(window.events.next,Qt::LeftButton);QTRY_COMPARE_WITH_TIMEOUT(window.events.model->offset,500,5000);
        window.tabs->setCurrentWidget(window.diagnostics.widget);
        QTRY_VERIFY_WITH_TIMEOUT(window.smokeDiagnostics,5000);
        const auto count=queries().size();window.tabs->setCurrentWidget(window.events.widget);
        QCOMPARE(window.events.model->offset,500);QCOMPARE(queries().size(),count);
        window.search->setText("ORDER");QTRY_VERIFY_WITH_TIMEOUT(queries().size()==count+1,5000);
        QTRY_COMPARE_WITH_TIMEOUT(window.events.model->offset,0,5000);
        QCOMPARE(queries().last().value("view").toString(),QString("events"));
        window.tabs->setCurrentWidget(window.analysis);QTRY_VERIFY_WITH_TIMEOUT(window.smokeStats,5000);
        const auto afterStats=queries().size();window.tabs->setCurrentWidget(window.comparison);
        QCOMPARE(queries().size(),afterStats);
    }
    void timelineClickAnchorsDenseEventPageAndRetainsContext(){
        Window window;open(window);QTRY_COMPARE_WITH_TIMEOUT(window.events.model->total,800,10000);
        window.tabs->setCurrentWidget(window.timeline->parentWidget());QTRY_VERIFY_WITH_TIMEOUT(window.smokeTimeline,5000);
        window.zoom->setValue(1000);const auto timelineQueries=queries().size();
        const auto area=window.timeline->rect().adjusted(12,35,-12,-28);
        QTest::mouseClick(window.timeline,Qt::LeftButton,Qt::NoModifier,QPoint(area.left()+int(area.width()*0.8),area.center().y()));
        QVERIFY(window.navigationTime.has_value());const auto clicked=*window.navigationTime;QVERIFY(clicked>350000);
        QTRY_VERIFY_WITH_TIMEOUT(window.events.model->offset>0&&window.events.page->text()!="Querying…",5000);
        const auto selection=window.events.table->selectionModel()->selectedRows();QVERIFY(!selection.isEmpty());
        const auto time=window.events.model->record(selection.first().row()).value("time_ms").toInteger();
        QVERIFY(std::abs(time-clicked)<=500);QVERIFY(time>350000);
        const auto offset=window.events.model->offset;
        window.tabs->setCurrentWidget(window.timeline->parentWidget());
        QCOMPARE(queries().size(),timelineQueries+1); // Only the anchored event query.
        window.tabs->setCurrentWidget(window.events.widget);QCOMPARE(window.events.model->offset,offset);
        const auto last=queries().last();QVERIFY(last.contains("anchor_ms"));
    }
    void numericFiltersShowInvalidInputAndBlockQueries(){
        Window window;open(window);QTRY_COMPARE_WITH_TIMEOUT(window.events.model->total,800,10000);
        const auto before=queries().size();window.actor->setText("abc");
        QVERIFY(!window.actor->hasAcceptableInput());QVERIFY(!window.actor->styleSheet().isEmpty());
        QCOMPARE(window.events.model->rowCount(),0);QVERIFY(window.statusBar()->currentMessage().contains("Actor ID"));
        window.refresh();QCOMPARE(queries().size(),before);
        window.actor->setText("9223372036854775808");QVERIFY(!window.actor->hasAcceptableInput());
        window.actor->setText("400");QTRY_COMPARE_WITH_TIMEOUT(window.events.model->total,800,5000);
        QVERIFY(window.actor->styleSheet().isEmpty());
    }
    void ownersAndSameFilenameLabelsStayDistinct(){
        Window window;const QJsonObject stats{{"counts",QJsonArray{
            QJsonObject{{"player_id",QJsonValue::Null},{"category","orders"},{"count",3}},
            QJsonObject{{"player_id",0},{"category","orders"},{"count",5}}}}};
        window.overview={{"replay",QJsonObject{{"filename","same.aoe2record"}}},
                         {"provenance",QJsonObject{{"replay_sha256","current"}}}};
        window.comparisonOverview={{"replay",QJsonObject{{"filename","same.aoe2record"}}},
                                   {"provenance",QJsonObject{{"replay_sha256","reference"}}},{"statistics",stats}};
        window.labels={{"0",QJsonObject{{"label","Current Gaia label"}}}};
        window.updateStats(stats);const auto text=window.analysis->toPlainText();
        QVERIFY(text.contains("Unknown ownership\n  orders: 3"));QVERIFY(text.contains("Player 0\n  orders: 5"));
        window.updateComparison(stats);QCOMPARE(window.comparison->rowCount(),5);
        QCOMPARE(window.comparison->item(2,1)->text(),QString("0"));
        QCOMPARE(window.comparison->item(2,2)->text(),QString("Current Gaia label"));
        QCOMPARE(window.comparison->item(4,1)->text(),QString("0"));QVERIFY(window.comparison->item(4,2)->text().isEmpty());
        const auto context=window.comparison->item(0,0)->text();
        QVERIFY(context.contains("Map ID: unavailable"));QVERIFY(context.contains("Game version: unavailable"));
        QVERIFY(context.contains("Game mode ID: unavailable"));QVERIFY(context.contains("Decoded settings: unavailable"));
        QVERIFY(context.contains("Duration: unavailable"));
        auto a=window.overview.value("replay").toObject();auto b=window.comparisonOverview.value("replay").toObject();
        a["settings"]=QJsonObject{{"rms_map_id",42},{"game_version","A"},{"game_type_id",1},{"population_limit",200}};
        b["settings"]=QJsonObject{{"rms_map_id",43},{"game_version","B"},{"game_type_id",2},{"population_limit",200}};
        a["duration_ms"]=1000;b["duration_ms"]=2000;window.overview["replay"]=a;window.comparisonOverview["replay"]=b;
        window.updateComparison(stats);const auto different=window.comparison->item(0,0)->text();
        QVERIFY(different.contains("Map ID: different"));QVERIFY(different.contains("Game version: different"));
        QVERIFY(different.contains("Game mode ID: different"));QVERIFY(different.contains("Duration: different"));
    }
};

int main(int argc,char **argv){
    // Controlled invalid candidates exercise the real asynchronous probe path.
    if(argc>1&&QString::fromLocal8Bit(argv[1]).startsWith("--fake-")){
        if(QString::fromLocal8Bit(argv[1])=="--fake-store-python"){fputs("Python was not found",stdout);return 1;}
        const bool python2=QString::fromLocal8Bit(argv[1])=="--fake-python27";
        const auto payload=QJsonDocument(QJsonObject{{"executable",QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath()},
            {"major",python2?2:3},{"minor",python2?7:11}}).toJson(QJsonDocument::Compact);
        fputs(payload.constData(),stdout);return 0;
    }
    QApplication app(argc,argv);NativeTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "test_native.moc"
