#pragma once
#include "backend.h"
#include "eventmodel.h"
#include "timeline.h"
#include <QMainWindow>
#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QTableView>
#include <QTableWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QLabel>
#include <QTimer>

struct TablePane {
    QWidget *widget=nullptr;
    QTableView *table=nullptr;
    EventModel *model=nullptr;
    QLabel *page=nullptr;
    QPushButton *previous=nullptr, *next=nullptr;
};

class Window : public QMainWindow {
public:
    Window();
    void openReplay(const QString &path, bool comparison=false);
    void loadDatabase(const QString &path);
    void setCacheDirectory(const QString &path);
    // Integration harness runs the same asynchronous UI path as an interactive load.
    std::function<void(bool,QString)> smokeComplete;
    QString screenshot;
private:
    Backend backend;
    QString database, comparisonDatabase, replayPath;
    QJsonObject overview, comparisonOverview, profile, labels, evidenceFilter;
    QListWidget *players;
    QComboBox *action, *category, *status;
    QLineEdit *search, *actor, *target, *type;
    QSpinBox *from, *to, *zoom;
    QCheckBox *timeFilter, *unknown;
    QLabel *profileLabel, *timeLabel, *selectionLabel;
    QProgressBar *progress;
    QPushButton *cancel;
    QTabWidget *tabs;
    QPlainTextEdit *summary, *details, *analysis;
    QTableWidget *comparison;
    Timeline *timeline;
    TablePane events, episodes, diagnostics;
    QTimer debounce;
    QMenu *recentMenu;
    bool loading=false;
    bool smokeEvents=false, smokeEpisodes=false, smokeDiagnostics=false, smokeTimeline=false;
    bool smokeStats=false;
    int smokeStage=0;
    qint64 smokeExpected=0;
    void fail(const QString &message);
    void settings();
    void recentFiles();
    void applyOverview(const QJsonObject &data);
    void renderOverview();
    void refresh();
    void refreshPage(TablePane &pane, int offset=0);
    void pageReceived(TablePane &pane, const QJsonObject &data);
    QJsonObject filters(bool eventFields) const;
    QJsonObject request(const QString &view) const;
    TablePane makeTable(const QString &view);
    void inspect(TablePane &pane);
    void evidence(TablePane &pane);
    void exportView(const QString &view, const QString &format);
    void assignLabel();
    void loadProfile();
    void updateStats(const QJsonObject &data);
    void updateComparison(const QJsonObject &data);
    void checkSmoke();
};
