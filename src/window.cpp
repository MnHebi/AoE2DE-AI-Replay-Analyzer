#include "window.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QValidator>
#include <algorithm>
#include <map>
#include <limits>

namespace {
QString jsonText(const QJsonObject &obj){return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));}
QString display(const QJsonValue &v){return v.isNull()||v.isUndefined()?"Unavailable":v.toVariant().toString();}
QPushButton *button(const QString &text,QLayout *layout){auto *b=new QPushButton(text);layout->addWidget(b);return b;}
class IdValidator : public QValidator {
public:
    using QValidator::QValidator;
    State validate(QString &input,int &)const override{
        if(input.isEmpty())return Acceptable;
        bool valid=false;input.toLongLong(&valid);
        // Keep incomplete/invalid input visible so its error can be explained.
        return valid?Acceptable:Intermediate;
    }
};
using Owner=std::optional<qint64>;
Owner owner(const QJsonValue &value){return value.isNull()||value.isUndefined()?Owner{}:Owner{value.toInteger()};}
QString ownerText(const Owner &id){return id?"Player "+QString::number(*id):"Unknown ownership";}
QString comparisonState(const QJsonValue &a,const QJsonValue &b){
    if(a.isNull()||a.isUndefined()||b.isNull()||b.isUndefined()||
       (a.isString()&&a.toString().isEmpty())||(b.isString()&&b.toString().isEmpty()))return "unavailable";
    if((a.isObject()&&a.toObject().isEmpty())||(b.isObject()&&b.toObject().isEmpty())||
       (a.isArray()&&a.toArray().isEmpty())||(b.isArray()&&b.toArray().isEmpty()))return "unavailable";
    return a==b?"match":"different";
}
}

Window::Window() {
    setWindowTitle("AoE2 Replay Analysis");resize(1450,900);
    auto *file=menuBar()->addMenu("&File");
    auto *open=file->addAction("&Open Replay…",QKeySequence::Open);
    connect(open,&QAction::triggered,this,[this]{auto path=QFileDialog::getOpenFileName(this,"Open replay",{},"DE replay (*.aoe2record)");if(!path.isEmpty())openReplay(path);});
    recentMenu=file->addMenu("Recent replays");recentFiles();
    connect(file->addAction("Open comparison replay…"),&QAction::triggered,this,[this]{auto path=QFileDialog::getOpenFileName(this,"Comparison replay",{},"DE replay (*.aoe2record)");if(!path.isEmpty())openReplay(path,true);});
    connect(file->addAction("Open indexed replay…"),&QAction::triggered,this,[this]{auto p=QFileDialog::getOpenFileName(this,"Open indexed replay",{},"Replay index (*.sqlite)");if(!p.isEmpty())loadDatabase(p);});
    file->addSeparator();
    connect(file->addAction("Export summary…"),&QAction::triggered,this,[this]{exportView("events","summary");});
    connect(file->addAction("Backend settings…"),&QAction::triggered,this,&Window::settings);
    connect(file->addAction("Exit",QKeySequence::Quit),&QAction::triggered,this,&QWidget::close);
    auto *sources=menuBar()->addMenu("&Sources");
    connect(sources->addAction("Load AI profile / name metadata…"),&QAction::triggered,this,&Window::loadProfile);
    connect(sources->addAction("Use Generic AoE2"),&QAction::triggered,this,[this]{profile={};profileLabel->setText("Profile: Generic AoE2");events.model->typeLabels={};renderOverview();refresh();});
    connect(sources->addAction("Assign player label / identity…"),&QAction::triggered,this,&Window::assignLabel);
    connect(sources->addAction("Comparison replay metadata…"),&QAction::triggered,this,[this]{
        if(comparisonOverview.isEmpty())return;QDialog d(this);d.setWindowTitle("Reference replay metadata");d.resize(760,600);auto *l=new QVBoxLayout(&d);auto *text=new QPlainTextEdit;text->setReadOnly(true);text->setPlainText(jsonText(comparisonOverview));l->addWidget(text);auto *b=new QDialogButtonBox(QDialogButtonBox::Close);l->addWidget(b);connect(b,&QDialogButtonBox::rejected,&d,&QDialog::reject);d.exec();
    });
    auto *help=menuBar()->addMenu("&Help");
    connect(help->addAction("Evidence and limitations"),&QAction::triggered,this,[this]{QMessageBox::information(this,"Evidence levels",
        "Observed: decoded replay data.\nInferred: grouping or interpretation of observations.\nUnresolved: evidence does not establish an outcome.\nUnavailable: information is not exposed.\n\nRequests do not prove execution. Command gaps do not prove idle units. Profiles supply optional labels; raw observations remain unchanged.");});

    auto *split=new QSplitter;setCentralWidget(split);
    auto *side=new QWidget;side->setMaximumWidth(330);auto *sideLayout=new QVBoxLayout(side);split->addWidget(side);
    sideLayout->addWidget(new QLabel("Players to analyze (header identity)"));
    players=new QListWidget;players->setMinimumWidth(255);sideLayout->addWidget(players);
    unknown=new QCheckBox("Include unknown ownership");unknown->setChecked(true);sideLayout->addWidget(unknown);
    auto *labelButton=button("Assign label / identity…",sideLayout);connect(labelButton,&QPushButton::clicked,this,&Window::assignLabel);
    profileLabel=new QLabel("Profile: Generic AoE2");profileLabel->setWordWrap(true);sideLayout->addWidget(profileLabel);
    selectionLabel=new QLabel("Open a replay to begin.");selectionLabel->setWordWrap(true);sideLayout->addWidget(selectionLabel);

    auto *right=new QWidget;auto *rightLayout=new QVBoxLayout(right);split->addWidget(right);split->setStretchFactor(1,1);
    auto *filterRow=new QHBoxLayout;rightLayout->addLayout(filterRow);
    search=new QLineEdit;search->setPlaceholderText("Search decoded event fields");filterRow->addWidget(search,2);
    action=new QComboBox;action->setMaximumWidth(180);action->addItem("All actions","");filterRow->addWidget(action);
    category=new QComboBox;category->setMaximumWidth(180);category->addItem("All categories","");filterRow->addWidget(category);
    status=new QComboBox;status->setMaximumWidth(140);for(const auto &v:QStringList{"All evidence","observed","inferred","unresolved"})status->addItem(v,v=="All evidence"?"":v);filterRow->addWidget(status);
    auto *idRow=new QHBoxLayout;rightLayout->addLayout(idRow);
    actor=new QLineEdit;actor->setPlaceholderText("Actor ID");idRow->addWidget(actor);
    target=new QLineEdit;target->setPlaceholderText("Target ID");idRow->addWidget(target);
    type=new QLineEdit;type->setPlaceholderText("Unit/building/tech type ID");idRow->addWidget(type);
    for(auto *edit:{actor,target,type}){
        edit->setMaximumWidth(150);edit->setValidator(new IdValidator(edit));
        edit->setToolTip("Enter a numeric ID, or leave empty to include all IDs.");
    }
    auto *clear=button("Reset filters",idRow);
    timeFilter=new QCheckBox("Time range (seconds)");idRow->addWidget(timeFilter);
    from=new QSpinBox;to=new QSpinBox;from->setRange(0,100000000);to->setRange(0,100000000);from->setMaximumWidth(100);to->setMaximumWidth(100);idRow->addWidget(from);idRow->addWidget(to);
    connect(clear,&QPushButton::clicked,this,[this]{loading=true;search->clear();actor->clear();target->clear();type->clear();action->setCurrentIndex(0);category->setCurrentIndex(0);status->setCurrentIndex(0);timeFilter->setChecked(false);evidenceFilter={};navigationTime.reset();loading=false;refresh();});

    tabs=new QTabWidget;rightLayout->addWidget(tabs,3);
    summary=new QPlainTextEdit;summary->setReadOnly(true);summary->setPlainText("Open a Definitive Edition replay.\n\nThe native interface uses the existing Python decoder and a versioned SQLite index.\nOnly decoded fields are shown; missing simulation state remains unavailable.");tabs->addTab(summary,"Overview");
    auto *timePage=new QWidget;auto *timeLayout=new QVBoxLayout(timePage);
    auto *timeControls=new QHBoxLayout;timeLayout->addLayout(timeControls);
    timeLabel=new QLabel("Click the chart to inspect events");timeControls->addWidget(timeLabel,1);
    timeControls->addWidget(new QLabel("Window length (s)"));zoom=new QSpinBox;zoom->setRange(1,36000);zoom->setValue(60);timeControls->addWidget(zoom);
    timeline=new Timeline;timeLayout->addWidget(timeline,1);
    timeLayout->addWidget(new QLabel("Counts describe recorded activity. Zoom with the time-range controls; findings link to their supporting events."));
    timeline->navigate=[this](qint64 at){navigateTimeline(at);};
    tabs->addTab(timePage,"Timeline");
    events=makeTable("events");tabs->addTab(events.widget,"Event explorer");
    analysis=new QPlainTextEdit;analysis->setReadOnly(true);tabs->addTab(analysis,"Player analysis");
    episodes=makeTable("episodes");tabs->addTab(episodes.widget,"Episodes");
    diagnostics=makeTable("diagnostics");tabs->addTab(diagnostics.widget,"Diagnostics");
    comparison=new QTableWidget;comparison->setEditTriggers(QAbstractItemView::NoEditTriggers);tabs->addTab(comparison,"Compare");
    details=new QPlainTextEdit;details->setReadOnly(true);details->setMaximumHeight(180);details->setPlaceholderText("Select an event or finding to inspect exact evidence. Double-click a finding to open supporting events.");rightLayout->addWidget(details);
    auto *progressRow=new QHBoxLayout;rightLayout->addLayout(progressRow);progress=new QProgressBar;progress->setRange(0,100);progressRow->addWidget(progress,1);cancel=button("Cancel loading",progressRow);cancel->setEnabled(false);
    connect(cancel,&QPushButton::clicked,this,[this]{backend.cancelBuild();cancel->setEnabled(false);progress->setValue(0);statusBar()->showMessage("Loading cancelled. The previously loaded replay remains available.");});
    debounce.setSingleShot(true);debounce.setInterval(220);connect(&debounce,&QTimer::timeout,this,&Window::refresh);
    auto changed=[this]{if(!loading){evidenceFilter={};navigationTime.reset();validateFilters();debounce.start();}};
    for(auto *edit:{search,actor,target,type})connect(edit,&QLineEdit::textChanged,this,changed);
    for(auto *combo:{action,category,status})connect(combo,&QComboBox::currentIndexChanged,this,changed);
    for(auto *spin:{from,to})connect(spin,&QSpinBox::valueChanged,this,changed);
    connect(timeFilter,&QCheckBox::toggled,this,changed);connect(unknown,&QCheckBox::toggled,this,changed);connect(players,&QListWidget::itemChanged,this,changed);
    connect(tabs,&QTabWidget::currentChanged,this,[this]{if(!loading)refresh();});
}

TablePane Window::makeTable(const QString &view){
    TablePane p;p.widget=new QWidget;auto *layout=new QVBoxLayout(p.widget);p.model=new EventModel(view,this);p.table=new QTableView;
    p.table->setModel(p.model);p.table->setSelectionBehavior(QAbstractItemView::SelectRows);p.table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    p.table->setAlternatingRowColors(true);p.table->setWordWrap(false);p.table->horizontalHeader()->setStretchLastSection(true);p.table->verticalHeader()->setDefaultSectionSize(26);layout->addWidget(p.table,1);
    auto *row=new QHBoxLayout;layout->addLayout(row);p.previous=button("Previous 250",row);p.next=button("Next 250",row);p.page=new QLabel;row->addWidget(p.page,1);
    auto *copy=button("Copy selected",row);auto *json=button("Export JSON…",row);auto *csv=button("Export CSV…",row);
    connect(p.previous,&QPushButton::clicked,this,[this,view]{auto &p=view=="events"?events:view=="episodes"?episodes:diagnostics;refreshPage(p,std::max(0,p.model->offset-250));});
    connect(p.next,&QPushButton::clicked,this,[this,view]{auto &p=view=="events"?events:view=="episodes"?episodes:diagnostics;refreshPage(p,p.model->offset+250);});
    connect(p.table->selectionModel(),&QItemSelectionModel::selectionChanged,this,[this,view]{auto &p=view=="events"?events:view=="episodes"?episodes:diagnostics;inspect(p);});
    connect(p.table,&QTableView::doubleClicked,this,[this,view]{auto &p=view=="events"?events:view=="episodes"?episodes:diagnostics;evidence(p);});
    auto copier=[this,view]{auto &p=view=="events"?events:view=="episodes"?episodes:diagnostics;QJsonArray data;for(const auto &index:p.table->selectionModel()->selectedRows())data.append(p.model->record(index.row()));QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented)));};
    connect(copy,&QPushButton::clicked,this,copier);auto *shortcut=new QShortcut(QKeySequence::Copy,p.table);shortcut->setContext(Qt::WidgetWithChildrenShortcut);connect(shortcut,&QShortcut::activated,this,copier);
    connect(json,&QPushButton::clicked,this,[this,view]{exportView(view,"json");});connect(csv,&QPushButton::clicked,this,[this,view]{exportView(view,"csv");});
    return p;
}

void Window::fail(const QString &message){
    cancel->setEnabled(false);statusBar()->showMessage("Error: "+message);details->setPlainText(message);
    if(smokeComplete){auto done=smokeComplete;smokeComplete={};done(false,message);}
    else QMessageBox::warning(this,"Replay analysis",message.isEmpty()?"Backend did not return a valid result.":message);
}

void Window::settings(){
    QDialog dialog(this);dialog.setWindowTitle("Backend settings");auto *layout=new QFormLayout(&dialog);
    QLineEdit interpreter(backend.python),parser(backend.parserRoot),tools(backend.toolsRoot),cache(backend.cacheDir);
    layout->addRow("Python 3.12+ executable",&interpreter);layout->addRow("Parser package root",&parser);layout->addRow("Existing helper tools",&tools);
    layout->addRow("Derived cache directory",&cache);
    auto *choose=new QPushButton("Browse Python…");layout->addRow(choose);connect(choose,&QPushButton::clicked,&dialog,[&]{auto p=QFileDialog::getOpenFileName(&dialog,"Python 3 executable");if(!p.isEmpty())interpreter.setText(p);});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){backend.python=interpreter.text();backend.parserRoot=parser.text();backend.toolsRoot=tools.text();backend.cacheDir=cache.text();QSettings s;s.setValue("python",backend.python);s.setValue("parserRoot",backend.parserRoot);s.setValue("toolsRoot",backend.toolsRoot);s.setValue("cacheDir",backend.cacheDir);}
}

void Window::recentFiles(){
    recentMenu->clear();QSettings s;for(const auto &path:s.value("recent").toStringList()){auto *a=recentMenu->addAction(path);connect(a,&QAction::triggered,this,[this,path]{openReplay(path);});}
}

void Window::openReplay(const QString &path,bool isComparison){
    if(backend.building()){statusBar()->showMessage("Finish or cancel the current load first.");return;}
    cancel->setEnabled(true);progress->setValue(0);statusBar()->showMessage("Loading "+QFileInfo(path).fileName());
    backend.build(path,[this,path,isComparison](QJsonObject ready){
        auto db=ready.value("database").toString();
        if(isComparison){comparisonDatabase=db;backend.query(db,{{"view","overview"}},"comparison-overview",[this](QJsonObject data){comparisonOverview=data;refresh();tabs->setCurrentWidget(comparison);},[this](QString e){fail(e);});}
        else {loadDatabase(db);replayPath=path;QSettings s;auto recent=s.value("recent").toStringList();recent.removeAll(path);recent.prepend(path);while(recent.size()>10)recent.removeLast();s.setValue("recent",recent);recentFiles();}
    },[this](QString phase,int percent){progress->setValue(percent);statusBar()->showMessage(phase);if(phase=="Ready"||phase=="Stopped")cancel->setEnabled(false);},[this](QString e){fail(e);});
}

void Window::loadDatabase(const QString &path){
    replayPath.clear();
    backend.query(path,{{"view","overview"}},"overview",[this,path](QJsonObject data){database=path;applyOverview(data);},[this](QString e){fail(e);});
}
void Window::setCacheDirectory(const QString &path){backend.cacheDir=path;}

void Window::applyOverview(const QJsonObject &data){
    loading=true;overview=data;players->clear();details->clear();evidenceFilter={};lastRequests.clear();navigationTime.reset();
    QSettings s;labels=QJsonDocument::fromJson(s.value("labels/"+data.value("provenance").toObject().value("replay_sha256").toString()).toByteArray()).object();
    for(const auto v:data.value("players").toArray()){
        const auto p=v.toObject();auto *item=new QListWidgetItem(players);item->setData(Qt::UserRole,p.value("number").toInt());item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Checked);
    }
    action->clear();action->addItem("All actions","");for(const auto v:data.value("actions").toArray())action->addItem(v.toString(),v.toString());
    category->clear();category->addItem("All categories","");for(const auto v:data.value("categories").toArray())category->addItem(v.toString(),v.toString());
    search->clear();actor->clear();target->clear();type->clear();status->setCurrentIndex(0);timeFilter->setChecked(false);from->setValue(0);to->setValue(int(data.value("replay").toObject().value("duration_ms").toInteger()/1000));
    renderOverview();if(smokeComplete)tabs->setCurrentWidget(events.widget);loading=false;refresh();
}

void Window::renderOverview(){
    if(overview.isEmpty())return;
    const auto replay=overview.value("replay").toObject();QString text=replay.value("filename").toString()+"\n";
    if(!replayPath.isEmpty())text+="Opened source: "+replayPath+"\n";
    text+="Duration: "+EventModel::timestamp(replay.value("duration_ms").toInteger())+" (last decoded synchronization)\n";
    text+="Outcome: Unavailable\nProfile: "+profile.value("name").toString("Generic AoE2")+"\n\nPlayers (observed header):\n";
    int i=0;for(const auto v:overview.value("players").toArray()){
        const auto p=v.toObject();const auto id=QString::number(p.value("number").toInt());const auto label=labels.value(id).toObject();
        const auto civId=QString::number(p.value("civilization_id").toInt());const auto civLabel=profile.value("civilizations").toObject().value(civId).toString();
        auto line="P"+id+" "+p.value("name").toString()+" | civ "+display(p.value("civilization_id"))+(civLabel.isEmpty()?"":" ("+civLabel+", profile label)")+
            " | team "+display(p.value("resolved_team_id"))+" | selected color ID "+display(p.value("selected_color_id"))+" | "+p.value("designation").toString("unavailable");
        if(!p.value("ai_name").toString().isEmpty())line+=" | AI name: "+p.value("ai_name").toString();
        if(!label.isEmpty())line+=" | "+label.value("label").toString()+" / "+label.value("designation").toString()+" (user-provided)";
        text+=line+"\n";if(i<players->count()){
            auto compact="P"+id+" "+p.value("name").toString(p.value("ai_name").toString());
            if(p.value("name").toString().isEmpty())compact="P"+id+" "+p.value("ai_name").toString();
            compact+="\nCiv "+display(p.value("civilization_id"))+" · Team "+display(p.value("resolved_team_id"))+" · Color "+display(p.value("selected_color_id"));
            compact+="\n"+p.value("designation").toString("unavailable");if(!label.isEmpty())compact+=" · "+label.value("label").toString()+" (user)";
            players->item(i)->setText(compact);players->item(i)->setToolTip(line);
        }++i;
    }
    text+="\nGame settings (IDs remain numeric without optional metadata):\n"+jsonText(replay.value("settings").toObject());
    text+="\nCoverage:\n"+jsonText(overview.value("coverage").toObject());
    text+="\nParser warnings:\n";const auto warnings=overview.value("warnings").toArray();if(warnings.isEmpty())text+="None reported\n";else for(const auto w:warnings)text+=w.toString()+"\n";
    text+="\nLimitations:\n";for(const auto l:overview.value("limitations").toArray())text+="• "+l.toString()+"\n";
    if(profile.contains("notes"))text+="\nOptional profile notes (user supplied): "+profile.value("notes").toString()+"\n";
    text+="\nProvenance:\n"+jsonText(overview.value("provenance").toObject());summary->setPlainText(text);
    setWindowTitle(replay.value("filename").toString()+" — AoE2 Replay Analysis");
}

QJsonObject Window::filters(bool eventFields)const{
    QJsonArray selected;for(int i=0;i<players->count();++i)if(players->item(i)->checkState()==Qt::Checked)selected.append(players->item(i)->data(Qt::UserRole).toInt());
    QJsonObject f{{"players",selected},{"include_unknown",unknown->isChecked()}};
    if(timeFilter->isChecked()){f["from_ms"]=qint64(from->value())*1000;f["to_ms"]=qint64(to->value())*1000+999;}
    if(!status->currentData().toString().isEmpty())f["status"]=status->currentData().toString();
    if(eventFields){
        if(!search->text().isEmpty())f["search"]=search->text();
        if(!action->currentData().toString().isEmpty())f["action"]=action->currentData().toString();
        if(!category->currentData().toString().isEmpty())f["category"]=category->currentData().toString();
        for(const auto &pair:QList<QPair<QString,QLineEdit*>>{{"actor_id",actor},{"target_id",target},{"type_id",type}}){bool ok=false;auto id=pair.second->text().toLongLong(&ok);if(ok)f[pair.first]=id;}
        for(auto i=evidenceFilter.begin();i!=evidenceFilter.end();++i)f[i.key()]=i.value();
    }
    return f;
}
QJsonObject Window::request(const QString &view)const{
    return {{"view",view},{"filters",filters(view=="events"||view=="timeline"||view=="statistics")},{"profile",profile.isEmpty()?QJsonObject{{"name","Generic AoE2"}}:profile},{"user_labels",labels},{"opened_replay_path",replayPath}};
}

QJsonObject Window::queryRequest(const QString &view)const{
    auto f=filters(view=="events"||view=="timeline"||view=="statistics");
    // Keep the activity chart as context while selecting an event time window.
    if(view=="timeline"){f.remove("from_ms");f.remove("to_ms");}
    return {{"view",view},{"filters",f}};
}

bool Window::validateFilters(){
    QStringList invalid;
    for(const auto &entry:QList<QPair<QString,QLineEdit*>>{{"Actor ID",actor},{"Target ID",target},{"Type ID",type}}){
        const bool valid=entry.second->hasAcceptableInput();
        entry.second->setStyleSheet(valid?QString{}:"QLineEdit { border: 2px solid #c0392b; }");
        if(!valid)invalid.append(entry.first);
    }
    if(invalid.isEmpty()){
        if(statusBar()->currentMessage().startsWith("Enter a numeric value or clear:"))statusBar()->clearMessage();
        return true;
    }
    const auto message="Enter a numeric value or clear: "+invalid.join(", ");
    statusBar()->showMessage(message);
    for(auto *pane:{&events,&episodes,&diagnostics}){
        backend.cancelQuery(pane->model->kind);pane->model->replace({});pane->page->setText(message);
        pane->previous->setEnabled(false);pane->next->setEnabled(false);
    }
    backend.cancelQuery("timeline");backend.cancelQuery("statistics");lastRequests.clear();
    timeline->setBins({});analysis->setPlainText(message);comparison->clearContents();details->clear();
    return false;
}

void Window::navigateTimeline(qint64 at){
    debounce.stop();loading=true;
    const qint64 half=qint64(zoom->value())*500;
    from->setValue(int(std::max(qint64(0),at-half)/1000));to->setValue(int((at+half)/1000));
    timeFilter->setChecked(true);evidenceFilter={};navigationTime=at;
    timeLabel->setText("Selected "+EventModel::timestamp(at));tabs->setCurrentWidget(events.widget);
    loading=false;
    timeline->setSelection(true,qint64(from->value())*1000,qint64(to->value())*1000+999,navigationTime);
    refreshPage(events,-1,at);
}

void Window::refresh(){
    if(database.isEmpty()||loading)return;
    if(!validateFilters())return;
    const auto selected=filters(false).value("players").toArray();selectionLabel->setText(QString("%1 players selected%2").arg(selected.size()).arg(unknown->isChecked()?" + unknown ownership":""));
    timeline->setSelection(timeFilter->isChecked(),qint64(from->value())*1000,qint64(to->value())*1000+999,navigationTime);
    const auto *visible=tabs->currentWidget();
    for(auto *pane:{&events,&episodes,&diagnostics})if(visible==pane->widget){refreshPage(*pane);return;}
    const auto view=visible==timeline->parentWidget()?QString("timeline"):
        (visible==analysis||visible==comparison)?QString("statistics"):QString{};
    if(view.isEmpty())return;
    const auto req=queryRequest(view);
    if(lastRequests.value(view)==req){if(view=="statistics")updateComparison(overview.value("filtered_statistics").toObject());return;}
    lastRequests[view]=req;const auto queriedDatabase=database;
    backend.query(database,req,view,[this,view,req,queriedDatabase](QJsonObject result){
        if(database!=queriedDatabase||queryRequest(view)!=req){if(lastRequests.value(view)==req)lastRequests.remove(view);return;}
        if(view=="timeline"){timeline->setBins(result);smokeTimeline=true;}
        else {overview["filtered_statistics"]=result;updateStats(result);updateComparison(result);smokeStats=true;}
        checkSmoke();
    },[this,view](QString e){lastRequests.remove(view);fail(e);});
}
void Window::refreshPage(TablePane &pane,int offset,std::optional<qint64> anchor){
    if(database.isEmpty()||!validateFilters())return;
    const auto view=pane.model->kind;auto req=queryRequest(view);
    const auto previous=lastRequests.value(view);
    const bool sameFilters=previous.value("filters")==req.value("filters");
    if(offset<0){
        offset=sameFilters?previous.value("offset").toInt():0;
        if(!anchor&&sameFilters&&previous.contains("anchor_ms"))anchor=previous.value("anchor_ms").toInteger();
    }
    req["offset"]=offset;req["limit"]=250;
    if(anchor)req["anchor_ms"]=*anchor;
    if(previous==req)return;
    lastRequests[view]=req;const auto queriedDatabase=database;
    pane.page->setText("Querying…");pane.previous->setEnabled(false);pane.next->setEnabled(false);
    auto *targetPane=&pane;
    backend.query(database,req,view,[this,targetPane,req,view,queriedDatabase](QJsonObject result){
        if(database!=queriedDatabase||queryRequest(view).value("filters")!=req.value("filters")){
            if(lastRequests.value(view)==req)lastRequests.remove(view);return;
        }
        auto completed=req;completed.remove("anchor_ms");completed["offset"]=result.value("offset");
        lastRequests[view]=completed;pageReceived(*targetPane,result);
    },[this,view](QString e){lastRequests.remove(view);fail(e);});
}
void Window::pageReceived(TablePane &pane,const QJsonObject &data){
    pane.model->replace(data);const auto count=pane.model->rowCount();pane.page->setText(QString("%1–%2 of %3").arg(count?pane.model->offset+1:0).arg(pane.model->offset+count).arg(pane.model->total));
    pane.previous->setEnabled(pane.model->offset>0);pane.next->setEnabled(pane.model->offset+count<pane.model->total);
    if(pane.model->kind=="events"&&navigationTime){
        int nearest=-1;qint64 distance=std::numeric_limits<qint64>::max();
        for(int row=0;row<count;++row){const auto delta=std::abs(pane.model->record(row).value("time_ms").toInteger()-*navigationTime);if(delta<distance){distance=delta;nearest=row;}}
        if(nearest>=0){pane.table->selectRow(nearest);pane.table->scrollTo(pane.model->index(nearest,0),QAbstractItemView::PositionAtCenter);}
        statusBar()->showMessage("Events around "+EventModel::timestamp(*navigationTime)+"; nearest event on this page selected.");
    }
    if(pane.model->kind=="events")smokeEvents=true;else if(pane.model->kind=="episodes")smokeEpisodes=true;else smokeDiagnostics=true;checkSmoke();
}
void Window::inspect(TablePane &pane){
    const auto rows=pane.table->selectionModel()->selectedRows();if(rows.isEmpty())return;auto record=pane.model->record(rows.first().row());
    if(record.contains("raw_json")){const auto raw=QJsonDocument::fromJson(record.value("raw_json").toString().toUtf8()).object();record["decoded_packet"]=raw;record.remove("raw_json");}
    details->setPlainText(jsonText(record));
}
void Window::evidence(TablePane &pane){
    if(pane.model->kind=="events")return;const auto rows=pane.table->selectionModel()->selectedRows();if(rows.isEmpty())return;
    const auto r=pane.model->record(rows.first().row());debounce.stop();navigationTime.reset();loading=true;
    search->clear();actor->clear();target->clear();type->clear();action->setCurrentIndex(0);category->setCurrentIndex(0);status->setCurrentIndex(0);timeFilter->setChecked(false);
    for(int i=0;i<players->count();++i)players->item(i)->setCheckState(Qt::Checked);unknown->setChecked(true);
    if(pane.model->kind=="episodes")evidenceFilter={{"episode_id",r.value("id")}};
    else if(!r.value("episode_id").isNull())evidenceFilter={{"episode_id",r.value("episode_id")}};
    else evidenceFilter={{"ids",QJsonArray{r.value("first_event"),r.value("last_event")}}};
    tabs->setCurrentWidget(events.widget);loading=false;refresh();statusBar()->showMessage("Showing exact supporting events. Reset filters to return to the complete stream.");
}

void Window::exportView(const QString &view,const QString &format){
    if(database.isEmpty()||!validateFilters())return;auto path=QFileDialog::getSaveFileName(this,"Export "+view,{},format=="csv"?"CSV (*.csv)":format=="json"?"JSON (*.json)":"Summary (*.txt)");if(path.isEmpty())return;
    statusBar()->showMessage("Exporting all filtered rows…");backend.exportData(database,path,format,request(view),[this](QJsonObject data){statusBar()->showMessage("Exported "+data.value("exported").toString());},[this](QString e){fail(e);});
}
void Window::assignLabel(){
    auto *item=players->currentItem();if(!item)return;const auto id=QString::number(item->data(Qt::UserRole).toInt());bool ok=false;
    const auto name=QInputDialog::getText(this,"Player label","User-provided label (for example Baseline AI)",QLineEdit::Normal,labels.value(id).toObject().value("label").toString(),&ok);if(!ok)return;
    const auto identity=QInputDialog::getItem(this,"Manual identity","User-provided designation",{"unspecified","AI","human"},0,false,&ok);if(!ok)return;
    labels[id]=QJsonObject{{"label",name},{"designation",identity},{"basis","user-provided"}};QSettings s;s.setValue("labels/"+overview.value("provenance").toObject().value("replay_sha256").toString(),QJsonDocument(labels).toJson(QJsonDocument::Compact));
    renderOverview();refresh();
}
void Window::loadProfile(){
    auto path=QFileDialog::getOpenFileName(this,"Optional profile or game-data names",{},"JSON (*.json)");if(path.isEmpty())return;
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){fail(file.errorString());return;}
    if(file.size()>1024*1024){fail("Profile exceeds the 1 MiB size limit.");return;}
    QJsonParseError error;auto doc=QJsonDocument::fromJson(file.readAll(),&error);auto p=doc.object();
    if(error.error!=QJsonParseError::NoError||p.value("schema_version").toInt()!=1||!p.value("name").isString()){fail("Profile requires schema_version 1 and a name.");return;}
    profile=p;profile["source_file"]=path;profileLabel->setText("Profile: "+p.value("name").toString()+" (user supplied)");
    events.model->typeLabels=p;renderOverview();refresh();
}

void Window::updateStats(const QJsonObject &data){
    QString text="Observed decoded packet counts for the current event filters\n\n";
    std::map<Owner,QMap<QString,qint64>> counts;
    for(const auto v:data.value("counts").toArray()){const auto r=v.toObject();counts[owner(r.value("player_id"))][r.value("category").toString()]+=r.value("count").toInteger();}
    for(const auto &[id,categories]:counts){
        text+=ownerText(id)+"\n";qint64 total=0;
        for(auto c=categories.begin();c!=categories.end();++c){text+="  "+c.key()+": "+QString::number(c.value())+"\n";total+=c.value();}
        text+="  Total recorded events: "+QString::number(total)+"\n\n";
    }
    text+="Production, construction and research values count requests. They do not count completed units, buildings or technologies.\n\n";
    text+="Inferred: Episodes and Diagnostics describe evidence-linked command patterns. Generic rules are printed with every finding.\n\nUnavailable: Resources gathered, actual production, kills, idle-unit time and winner.\n\n"+data.value("basis").toString();analysis->setPlainText(text);
}
void Window::updateComparison(const QJsonObject &data){
    struct Row{QString replay,identity;Owner player;QMap<QString,qint64> counts;QJsonValue duration;};QList<Row> rows;
    const auto currentIdentity=overview.value("provenance").toObject().value("replay_sha256").toString();
    auto append=[&](const QJsonObject &meta,const QJsonObject &stats){
        std::map<Owner,QMap<QString,qint64>> counts;
        for(const auto v:stats.value("counts").toArray()){const auto r=v.toObject();counts[owner(r.value("player_id"))][r.value("category").toString()]+=r.value("count").toInteger();}
        const auto replay=meta.value("replay").toObject();
        for(const auto &[id,categories]:counts)rows.append({replay.value("filename").toString(),meta.value("provenance").toObject().value("replay_sha256").toString(),id,categories,replay.value("duration_ms")});
    };
    append(overview,data);if(!comparisonOverview.isEmpty())append(comparisonOverview,comparisonOverview.value("statistics").toObject());
    QStringList categories;for(const auto &row:rows)for(auto it=row.counts.begin();it!=row.counts.end();++it)if(!categories.contains(it.key()))categories.append(it.key());categories.sort();
    comparison->clear();comparison->clearSpans();comparison->setColumnCount(4+categories.size());comparison->setRowCount(rows.size()+1);
    QStringList headers{"Replay / comparison context","Player","User label","Duration (min)"};headers+=categories;comparison->setHorizontalHeaderLabels(headers);
    QString context="Current counts use active filters. Values count packets, not completed actions.";
    if(!comparisonOverview.isEmpty()){
        const auto a=overview.value("replay").toObject(),b=comparisonOverview.value("replay").toObject();
        const auto sa=a.value("settings").toObject(),sb=b.value("settings").toObject();
        context+=" Reference counts use ALL players/events.\n";
        context+="Map ID: "+comparisonState(sa.value("rms_map_id"),sb.value("rms_map_id"))+"; RMS filename: "+comparisonState(sa.value("rms_filename"),sb.value("rms_filename"))+"; exact map identity: unavailable.\n";
        context+="Game version: "+comparisonState(sa.value("game_version"),sb.value("game_version"))+"; build: "+comparisonState(sa.value("build"),sb.value("build"))+"; save version: "+comparisonState(sa.value("save_version"),sb.value("save_version"))+"; log version: "+comparisonState(sa.value("log_version"),sb.value("log_version"))+".\n";
        context+="Game mode ID: "+comparisonState(sa.value("game_type_id"),sb.value("game_type_id"))+".\n";
        QStringList matching,different,unavailable;
        auto fields=sa.keys();for(const auto &key:sb.keys())if(!fields.contains(key))fields.append(key);fields.sort();
        for(const auto &key:fields){const auto state=comparisonState(sa.value(key),sb.value(key));
            (state=="match"?matching:state=="different"?different:unavailable).append(key);
        }
        if(fields.isEmpty())context+="Decoded settings: unavailable.\n";
        else context+=QString("Decoded settings fields: %1 match").arg(matching.size())+
            (different.isEmpty()?QString{}:"; different: "+different.join(", "))+
            (unavailable.isEmpty()?QString{}:"; unavailable: "+unavailable.join(", "))+".\n";
        context+="Duration: "+comparisonState(a.value("duration_ms"),b.value("duration_ms"))+"; coverage: "+comparisonState(overview.value("coverage"),comparisonOverview.value("coverage"))+". Inspect both replay settings and warnings before drawing conclusions.";
    }
    comparison->setItem(0,0,new QTableWidgetItem(context));comparison->setSpan(0,0,1,headers.size());
    comparison->setRowHeight(0,comparisonOverview.isEmpty()?60:170);
    for(int i=0;i<rows.size();++i){const auto &r=rows[i];comparison->setItem(i+1,0,new QTableWidgetItem(r.replay));comparison->setItem(i+1,1,new QTableWidgetItem(r.player?QString::number(*r.player):"Unknown ownership"));
        const bool ownsLabel=r.player&&!currentIdentity.isEmpty()&&r.identity==currentIdentity;
        comparison->setItem(i+1,2,new QTableWidgetItem(ownsLabel?labels.value(QString::number(*r.player)).toObject().value("label").toString():""));
        comparison->setItem(i+1,3,new QTableWidgetItem(r.duration.isDouble()?QString::number(r.duration.toDouble()/60000.0,'f',2):"Unavailable"));for(int c=0;c<categories.size();++c)comparison->setItem(i+1,c+4,new QTableWidgetItem(QString::number(r.counts.value(categories[c]))));}
    comparison->resizeColumnsToContents();comparison->setColumnWidth(0,300);comparison->horizontalHeader()->setStretchLastSection(true);
}
void Window::checkSmoke(){
    if(!smokeComplete)return;
    // Visit the same lazy views an interactive user opens, then check the model.
    QWidget *needed=!smokeEvents?events.widget:!smokeEpisodes?episodes.widget:!smokeDiagnostics?diagnostics.widget:
        !smokeTimeline?timeline->parentWidget():!smokeStats?analysis:nullptr;
    if(needed){tabs->setCurrentWidget(needed);refresh();return;}
    // Check paging through the actual asynchronous event model, then restore it.
    if(smokeStage==0&&events.model->total>250){smokeStage=1;smokeEvents=false;refreshPage(events,250);return;}
    if(smokeStage==1){if(events.model->offset!=250){fail("Native paging did not reach offset 250");return;}smokeStage=2;smokeEvents=false;refreshPage(events,0);return;}
    if(smokeStage<=2&&players->count()>0){
        const auto selected=players->item(0)->data(Qt::UserRole).toInt();smokeExpected=0;
        for(const auto v:overview.value("statistics").toObject().value("counts").toArray()){auto r=v.toObject();if(!r.value("player_id").isNull()&&r.value("player_id").toInt()==selected)smokeExpected+=r.value("count").toInteger();}
        loading=true;for(int i=0;i<players->count();++i)players->item(i)->setCheckState(i==0?Qt::Checked:Qt::Unchecked);unknown->setChecked(false);loading=false;
        smokeStage=3;smokeEvents=false;refreshPage(events);return;
    }
    if(smokeStage==3){
        if(events.model->total!=smokeExpected){fail("Native player filter count differs from observed counts");return;}
        loading=true;for(int i=0;i<players->count();++i)players->item(i)->setCheckState(Qt::Checked);unknown->setChecked(true);loading=false;
        smokeStage=4;smokeEvents=false;refreshPage(events);return;
    }
    if(smokeStage==4&&episodes.model->rowCount()>0){
        smokeExpected=episodes.model->record(0).value("event_count").toInteger();episodes.table->selectRow(0);smokeStage=5;smokeEvents=false;evidence(episodes);return;
    }
    if(smokeStage==5&&events.model->total!=smokeExpected){fail("Native episode evidence navigation returned the wrong membership");return;}
    if(smokeStage<=5&&overview.value("coverage").toObject().value("events").toInteger()>0){
        smokeStage=6;smokeEvents=false;loading=true;evidenceFilter={};timeFilter->setChecked(false);
        tabs->setCurrentWidget(timeline->parentWidget());loading=false;
        QTimer::singleShot(0,this,[this]{
            const auto area=timeline->rect().adjusted(12,35,-12,-28);
            const QPointF local(area.left()+area.width()*0.8,area.center().y());
            QMouseEvent click(QEvent::MouseButtonPress,local,QPointF(timeline->mapToGlobal(local.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(timeline,&click);
        });
        return;
    }
    if(smokeStage==6){
        if(!navigationTime||!timeFilter->isChecked()){fail("Timeline click did not set a time window");return;}
        const auto f=filters(true);
        for(int row=0;row<events.model->rowCount();++row){const auto time=events.model->record(row).value("time_ms").toInteger();
            if(time<f.value("from_ms").toInteger()||time>f.value("to_ms").toInteger()){fail("Timeline navigation returned events outside its time window");return;}
        }
        if(events.model->rowCount()>0){
            const auto selected=events.table->selectionModel()->selectedRows();
            if(selected.isEmpty()){fail("Timeline navigation did not select an event");return;}
            const auto chosen=std::abs(events.model->record(selected.first().row()).value("time_ms").toInteger()-*navigationTime);
            for(int row=0;row<events.model->rowCount();++row){
                if(std::abs(events.model->record(row).value("time_ms").toInteger()-*navigationTime)<chosen){fail("Timeline navigation selected an event farther from the clicked time");return;}
            }
        }
    }
    bool good=!overview.isEmpty()&&events.model->rowCount()<=250&&events.model->columnCount()==10;
    if(!screenshot.isEmpty()){loading=true;tabs->setCurrentWidget(events.widget);loading=false;good=grab().save(screenshot)&&good;}
    auto done=smokeComplete;smokeComplete={};done(good,QString("Loaded %1 events, %2 episodes, %3 findings; native paging, player filtering, evidence and timeline navigation passed").arg(overview.value("coverage").toObject().value("events").toInteger()).arg(episodes.model->total).arg(diagnostics.model->total));
}
