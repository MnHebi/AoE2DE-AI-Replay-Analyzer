#include "helpdialog.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <iterator>

namespace {
struct Topic {
    const char *title;
    const char *group;
    const char *text;
};

// Compiled into the application: help is available offline, without a replay,
// a Python interpreter, or a separate documentation file beside the EXE.
const Topic guide[] = {
    {"Start here", "Getting started",
     "Open a .aoe2record file with File > Open Replay. Check the players to include, then use Timeline to find a time and Event explorer to inspect individual records. Select a row to see its decoded fields.\n\n"
     "Episodes group repeated commands. Diagnostics highlight patterns worth inspecting. Double-click either to open its supporting events. Reset filters returns to the full stream for the selected players.\n\n"
     "A replay records requests and other data. A request does not prove that a unit moved, a building finished, research completed, or an AI made a mistake. Evidence labels explain these limits.\n\n"
     "Search this guide by term or description, for example episode, actor, research, unknown, or cache. Help > Terms and guide (F1) opens this window at any time."},
    {"Action / command / request", "Events",
     "Action is the decoded packet kind, such as MOVE, BUILD or RESEARCH. A command or request records an instruction issued in the replay. Its name describes that instruction; it does not establish the simulation result.\n\n"
     "The action filter selects one packet kind. All actions includes every kind present in this replay. An action name alone does not identify the player's AI or decision conditions."},
    {"Actor / Actors / Actor ID", "Events",
     "An actor is an object ID referenced as acting in a decoded command, often a unit or building. A packet may refer to several actors, so one event can appear in more than one actor's episode.\n\n"
     "Actor ID filters events containing that numeric reference. An actor ID is an object instance reference, not a unit type ID. The analyzer does not reconstruct that object's life, ownership changes or current state from the reference alone."},
    {"AI name / AI_ORDER", "Players and labels",
     "AI name is header metadata when available. AI_ORDER is the name of a decoded packet kind in the orders category. Its presence does not prove that a player is computer-controlled or expose an AI script's goals, conditions or intent.\n\n"
     "Use the header designation and explicit user-provided labels as separate information. Neither packet kind nor player color is used to guess AI identity."},
    {"Backend / Python", "Files and settings",
     "The backend decodes replays, builds the SQLite index, queries records and exports data. It uses a working Python 3.12+ interpreter. The native interface displays those results. Help itself needs no Python.\n\n"
     "File > Backend settings lets you select the Python executable if automatic discovery fails. Windows Store aliases and older interpreters are skipped. Loading and queries run asynchronously so the interface can remain responsive."},
    {"Byte offset / offset", "Raw evidence",
     "The source replay position recorded for an event, measured in bytes. It helps locate the original evidence in the replay file. It is separate from game time and the event's index ID.\n\n"
     "In a table-page response, offset instead means how many matching rows were skipped before that page. The Event explorer's Byte offset column always refers to the replay position."},
    {"Cache / derived cache directory", "Files and settings",
     "The cache stores derived replay indices so the same replay need not be decoded again. It does not modify the source replay. Choose its location in Backend settings.\n\n"
     "Reuse requires the same replay hash, parser pipeline fingerprint and supported index schema. A .partial file is unfinished and is never reused. A finished index can still have partial decode coverage; inspect Coverage separately."},
    {"Cancel loading / progress", "Files and settings",
     "Progress describes stages of decoding and indexing, not match progress or analysis accuracy. Cancel loading stops the current build; the previously loaded replay remains available.\n\n"
     "An interrupted .partial index is not a reusable cache. A finished index is published only after its build completes. Cancellation does not change the original replay."},
    {"Category / All categories", "Events",
     "A category groups related action names for filtering and packet counts. The categories are movement, combat commands, work commands, construction requests, production requests, research requests, economy commands, chat, orders and other.\n\n"
     "These are analyzer groupings of packet kinds. Category counts measure recorded events, not units involved, completed actions or success rates. All categories includes every category present in the replay."},
    {"Chat", "Command categories",
     "The chat category contains CHAT records. These are recorded messages, not unit commands. Chat is excluded when measuring recorded command gaps. A message does not establish who controlled a player or what an AI intended."},
    {"Civilization ID / civ", "Players and labels",
     "The numeric civilization identifier decoded from the replay header. An optional profile can add a civilization name while retaining the ID. Without appropriate name metadata the program shows the numeric value.\n\n"
     "A civilization ID is separate from player number, selected color, team, and unit/building/technology type IDs. It does not identify an AI script."},
    {"Combat commands", "Command categories",
     "PATROL, DE_ATTACK_MOVE and ATTACK_GROUND packets. Counts record these instructions; they do not count battles, hits, damage or kills. Other kinds such as ORDER may also be relevant to combat, so this category is not a complete measure of combat activity."},
    {"Compare / comparison replay / reference", "Views",
     "Compare displays decoded packet counts by player and category. Open comparison replay adds a reference replay. The current replay uses active event filters; the reference uses ALL its players and events.\n\n"
     "The context row compares available map indicators, versions, settings, duration and coverage. Match means the decoded values agree, different means they differ, and unavailable means at least one value is missing.\n\n"
     "Matching map indicators do not establish an identical generated map. Different filters, settings or decode coverage can change counts. No score or verdict of better AI is calculated."},
    {"Construction requests", "Command categories",
     "BUILD and WALL packets request construction. Their counts do not establish placed foundations, completed buildings, successful placement or builders reaching a site. A building type ID is the requested type, separate from any actor object ID."},
    {"Copy selected / Export JSON / Export CSV", "Files and settings",
     "Copy selected copies the selected rows on the current table page as JSON. JSON and CSV exports include ALL rows matching the current view's filters, across every page. Export summary writes metadata and packet counts as text.\n\n"
     "Exports include provenance, selection and optional labels/profile metadata. JSON preserves structured data; CSV places fields in columns. Exporting does not convert command requests into successful outcomes."},
    {"Coverage / complete / partial decode", "Raw evidence",
     "Coverage describes how much of the replay the parser decoded. Event and operation counts describe the decoded portion; unresolved_packets counts events that could not be fully decoded. Warnings explain problems encountered.\n\n"
     "Partial decode means decoding stopped before all operations could be read. There is no attempt to skip damaged bytes and guess where parsing resumes. Later activity may therefore be absent.\n\n"
     "The metadata flag complete means index creation finished, not that the entire replay decoded successfully. Check coverage status and warnings before interpreting gaps or counts."},
    {"Decoded fields / raw_json / raw_hex", "Raw evidence",
     "Decoded fields are values extracted by the parser from recorded data. raw_json retains the decoded packet fields in JSON form, including fields not promoted to table columns. raw_hex holds original packet bytes where captured.\n\n"
     "Selecting an event shows its indexed record and decoded payload. Missing fields remain unavailable. Original absent-target sentinel values can remain in the payload even when the indexed Target column is unavailable. Decoded data is not a reconstruction of all simulation state."},
    {"Designation / human / computer", "Players and labels",
     "Designation is the human/computer classification supplied by the replay header when available. It is displayed separately from any manual identity or user label. Unknown header values remain unavailable.\n\n"
     "Player number, color, packet names and chat are not used to infer designation. A header designation does not expose the computer player's script or decisions."},
    {"Diagnostics / Finding / Kind", "Views",
     "A finding is an inspection aid generated by a stated rule. Kind names the rule: repeated identical commands, recorded command gap, or unresolved packet. Start and End bound its evidence. Evidence description explains the rule; Uncertainty states what it cannot establish.\n\n"
     "Double-click a finding to open supporting events. A repeated-command finding links to an episode's exact membership. A command gap links to the two endpoint events; the time between them is not a list of observed idle actions. An unresolved-packet finding links to that packet.\n\n"
     "A finding does not establish an AI defect. Inspect the underlying records and warnings before drawing conclusions."},
    {"Duration / last decoded synchronization", "Time and navigation",
     "The displayed replay duration comes from the last decoded synchronization time. Synchronization records advance the parser's replay game clock. Duration is not your computer's elapsed analysis time.\n\n"
     "If decoding stops early, this value may not cover the whole match. It does not establish a winner, resignation or successful match completion."},
    {"Economy commands", "Command categories",
     "BUY, SELL, TRIBUTE and DE_TRIBUTE packets. They record market or tribute instructions. Counts do not measure resources gathered, total resource holdings or economic efficiency. Packet details should be inspected for the values actually recorded."},
    {"Episode / Episodes", "Views",
     "An episode is a run of at least TWO consecutive commands with matching decoded command fields for the same player and actor, with at most 10 seconds between each neighboring pair. Consecutive is evaluated within that player/actor's command stream; other actors can issue commands in between.\n\n"
     "Example: actor 400 receives the same order at 12, 17 and 23 seconds. Those three commands form one episode. A different command for actor 400, or a gap greater than 10 seconds, starts a new run. An episode can last longer than 10 seconds if every neighboring gap stays within the rule.\n\n"
     "Episode is its index ID. Start and End are the first and last supporting command times. Commands is the number of supporting events for that actor. Evidence is inferred; Execution outcome remains unresolved.\n\n"
     "Matching ignores selection membership for per-actor grouping. Unexposed packet bytes may differ. This does not prove identical binary packets, an internal AI loop or failed execution. Double-click an episode to see its exact supporting events."},
    {"Event / Event explorer / Event ID", "Views",
     "An event is one indexed decoded action or chat record. Event is its stable ID within that replay index. It is not a unit ID, episode ID or count of completed actions.\n\n"
     "Event explorer displays Time, Player, Action, Category, Actors, Target, Type ID, Evidence and Byte offset. Select a row for its full decoded fields. One event can refer to multiple actors. Event-specific filters select these records, not simulation outcomes."},
    {"Evidence / All evidence", "Evidence levels",
     "The Evidence column describes the status of a record. Observed means recorded data was decoded. Inferred means a grouping or interpretation was derived. Unresolved means decoding or an outcome remains uncertain. Unavailable describes information not exposed and is normally shown as a missing value.\n\n"
     "All evidence disables the status filter; it does not certify every field. The status filter applies to the records in the current view. An inferred episode can be supported by observed events and still have an unresolved execution outcome."},
    {"Execution outcome / match outcome", "Evidence levels",
     "Execution outcome asks whether the issued command actually succeeded in the simulation. It remains unresolved for episodes. Match outcome and winner are unavailable here.\n\n"
     "Commands alone do not establish completed production or research, kills, resources gathered, idle time, successful movement or a winner. Unavailable values are not zero measurements."},
    {"Filters / Reset filters", "Time and navigation",
     "Player selection and unknown ownership apply across views. Action, category, actor, target, type ID and decoded-field text search apply to events, the timeline and packet counts. Episodes and diagnostics use player, overlapping time range and evidence status; event-specific filters do not rebuild their groupings. Overview retains replay metadata.\n\n"
     "Reset filters clears text, IDs, action/category/status, time range and any supporting-event selection. It keeps the current player selection. Changing filters refreshes the visible view; other views update when opened. Invalid numeric IDs pause queries and exports until corrected or cleared."},
    {"Game settings / game mode ID / difficulty", "Players and labels",
     "Game settings are decoded header fields describing the recorded setup. Numeric game mode, map, difficulty and other identifiers are retained as IDs when reliable names are unavailable. A null or missing setting is unavailable.\n\n"
     "Inspect parser warnings before interpreting settings: some save versions have uncertain difficulty-byte mappings. Settings do not establish how a match played out. Compare checks available field values without inventing missing ones."},
    {"Generic AoE2 / Profile / name metadata", "Players and labels",
     "Generic AoE2 is the default with no custom AI or game-data naming assumptions. A profile is optional JSON metadata supplied under Sources, for example names for unit, building, technology and civilization IDs, plus notes.\n\n"
     "Profile labels are marked as such. The namespaces remain separate and raw IDs are retained. A profile does not change decoded packets, grouping rules or evidence, and does not prove that its names apply to the replay's data version. Use Generic AoE2 clears the active profile."},
    {"Indexed replay / SQLite / schema version", "Files and settings",
     "An indexed replay is a derived SQLite database containing decoded events, header metadata, episodes, diagnostics and provenance. File > Open indexed replay opens a supported existing .sqlite index without decoding the replay again.\n\n"
     "The schema version identifies the database structure. Unsupported versions are rejected. Index provenance retains the original replay and parser identity. An index is an analysis cache, not a playable replay file."},
    {"Inferred", "Evidence levels",
     "Derived from observations by a stated rule or interpretation. Episode grouping and repeated-command or command-gap diagnostics are inferred. Their supporting event membership is retained so you can inspect the basis.\n\n"
     "Inferred does not establish an internal AI decision, a defect or a successful simulation outcome. Read the rule and uncertainty together with the original events."},
    {"Map ID / RMS filename / exact map identity", "Players and labels",
     "Map ID is a decoded map identifier. RMS means random map script; RMS filename is the decoded script filename when available. These are map indicators, not the full generated map.\n\n"
     "Two replays can share these indicators without having identical terrain, resources or starting positions. Compare therefore reports exact map identity as unavailable."},
    {"Movement", "Command categories",
     "MOVE, ADD_WAYPOINT, GROUP_MULTI_WAYPOINTS and DE_RETREAT packets. These record movement or waypoint instructions. They do not establish a traversed path, arrival, successful pathfinding or distance actually traveled. Inspect the decoded fields for recorded coordinates and actors."},
    {"Observed", "Evidence levels",
     "Data decoded from the replay, such as a recorded command, timestamp, player field or header value. Observed describes the recorded evidence.\n\n"
     "An observed BUILD request establishes that the request was recorded. It does not establish a completed building. Decoding warnings and individual unavailable fields still matter."},
    {"Operation / sequence", "Raw evidence",
     "An operation is a record processed from the replay stream. The operation sequence preserves its original parsing order. Some operations, such as synchronization records, advance time without producing an Event explorer row.\n\n"
     "Coverage operations can therefore exceed coverage events. An event's sequence and Event ID are separate values. Their difference does not by itself mean commands were lost."},
    {"Orders", "Command categories",
     "ORDER, AI_ORDER, STOP, SPECIAL, GUARD, FOLLOW and UNGARRISON packets. These are grouped by their decoded packet kind. Inspect each record's fields to understand the instruction and referenced objects.\n\n"
     "ORDER alone does not establish the detailed simulation result, and AI_ORDER alone does not prove a computer-controlled player."},
    {"Other", "Command categories",
     "Decoded action kinds outside the named categories. Other is a category fallback, not automatically a parse error or unresolved packet. Check Action, Evidence and the decoded payload for the individual record."},
    {"Overview", "Views",
     "Replay filename and source, last decoded duration, header players, game settings, coverage, parser warnings, limitations and provenance. It describes the loaded replay as a whole rather than just the current event page.\n\n"
     "Use Overview to check context and decode coverage before interpreting packet counts or diagnostic patterns."},
    {"Packet / packet counts / Total recorded events", "Events",
     "A packet is a recorded data item decoded by the parser. Player analysis and Compare count indexed event packets by category and owner. A packet referring to many actors still counts once in these totals.\n\n"
     "Counts do not measure completed actions, units produced or actions per minute. A QUEUE packet can include a requested quantity; one packet is not necessarily one requested unit. Coverage limits which packets are available."},
    {"Parser / helper tools / parser package root", "Files and settings",
     "The parser reads the replay format. Bundled helper tools decode additional packet fields and header metadata. Backend settings identifies these directories; packaged defaults point to the supplied backend files.\n\n"
     "Parser version and file hashes identify how the replay was decoded. Warnings may describe unsupported or uncertain fields. The analyzer does not guess simulation state that the parser does not expose."},
    {"Parser warnings / limitations", "Raw evidence",
     "Warnings report decoding problems or uncertain metadata, such as an unsupported operation or difficulty mapping. Limitations describe information the analyzer cannot establish even when decoding finishes.\n\n"
     "None reported means no parser warning was emitted; it is not proof that every field, replay format or gameplay outcome was verified. Review Coverage and the individual event evidence as well."},
    {"Pipeline fingerprint / SHA-256 / replay hash", "Raw evidence",
     "A SHA-256 hash identifies file bytes. The replay hash identifies the complete source replay; identical filenames can contain different replays. User labels belong to the replay hash.\n\n"
     "The pipeline fingerprint hashes the adapter, parser source/reference files and helper sources. It identifies the decoding pipeline for provenance and cache reuse. A fingerprint identifies bytes, not correctness or successful gameplay."},
    {"Player / P number / header identity", "Players and labels",
     "Player is the numeric owner decoded for an event, or the header player number in the sidebar. P followed by a number is a compact header identity. Checking a player includes its records in the selected views.\n\n"
     "Player number, selected color, team, civilization and designation are separate header fields. Do not assume they are interchangeable. Player 0 is a known numeric value and is kept separate from unknown ownership."},
    {"Player analysis", "Views",
     "Packet counts for the currently selected players and event filters, grouped by command category. Total recorded events sums those counts. Unknown ownership has its own group when included.\n\n"
     "Production, construction and research values count requests. Resources gathered, actual completed production, kills, idle-unit time and winner remain unavailable. These counts are not a performance score."},
    {"Previous 250 / Next 250 / page", "Time and navigation",
     "Tables display up to 250 matching rows per page. Previous and Next move through the filtered rows. The page label reports position and total matches. Switching tabs preserves the page when filters have not changed.\n\n"
     "A timeline click opens a page around the nearest recorded event within the selected time window, so that page may start partway through the results. Copy selected uses the displayed page; JSON/CSV export includes all matching pages."},
    {"Production requests", "Command categories",
     "MAKE, QUEUE, MULTIQUEUE, DE_QUEUE and CREATE packets. Counts record these request kinds; they do not count completed units. Requested quantities, actors and unit type IDs should be read from the decoded packet where available.\n\n"
     "A request does not prove that resources were available, the queue progressed, or a unit appeared."},
    {"Provenance / opened source", "Raw evidence",
     "Provenance records where evidence came from: replay filename/path and hash, parser identity and pipeline fingerprint, plus export selection and optional metadata. It lets you trace results back to the exact replay and decoding pipeline.\n\n"
     "Opened source is the replay path opened now; an index can retain the original cache-build path even if identical replay bytes were later opened elsewhere. Paths and labels describe sources; they do not change the observations."},
    {"Recorded command gap", "Diagnostics",
     "A diagnostic for at least 60 seconds between neighboring recorded non-chat events for a known player. Start and End are the two endpoint times. Double-click opens those endpoint events.\n\n"
     "This measures a gap in recorded commands, not idle-unit time. Units may continue autonomous actions, and partial decode coverage may omit later activity. Unknown-owner events cannot be assigned to a known player to fill its gap."},
    {"Repeated identical commands", "Diagnostics",
     "A diagnostic for an episode with at least 10 supporting commands and at most 10 seconds from its first to its last command. This is stricter than the ordinary episode rule, which limits each neighboring gap.\n\n"
     "Identical means matching decoded fields under the per-actor rule, not complete binary packet equality. Repetition may be intentional. The finding does not prove an AI loop, a failed order or an internal cause. Double-click to inspect exact episode membership."},
    {"Replay / .aoe2record / Recent replays", "Files and settings",
     "A replay is the recorded match file. File > Open Replay accepts Definitive Edition .aoe2record files; other formats are disabled. Recent replays lists previously opened paths.\n\n"
     "The parser reads the file and creates a separate derived index. It does not modify the original replay or require an AI source checkout or game-data installation. A valid extension alone does not establish supported decode coverage."},
    {"Research requests", "Command categories",
     "RESEARCH packets request a technology. Technology type IDs identify the requested technology where decoded. Counts do not establish that research started successfully or completed, or that its effects applied."},
    {"Search decoded event fields", "Time and navigation",
     "The main-window search matches literal text in the full decoded event payload (raw_json). It does not search episode descriptions, profile labels, AI source files or unexposed simulation state. Percent and underscore are treated literally.\n\n"
     "Use numeric Actor ID, Target ID or Type ID filters for exact indexed references. The search field in this help window instead searches glossary titles, groups and descriptions."},
    {"Selected color ID / color", "Players and labels",
     "The player's selected color identifier decoded from header metadata when available. It is preserved independently of player number and team.\n\n"
     "The analyzer does not use color to infer ownership, player slot, human/computer designation or AI identity. A missing color value stays unavailable."},
    {"Start / End / Commands", "Time and navigation",
     "Start and End mark the first and last supporting evidence times for an episode or finding. They are replay game times. For an episode, Commands counts supporting events for that actor.\n\n"
     "These bounds do not imply continuous work or motion throughout the interval. Episode membership is an explicit set of events, not every event between Start and End. Time-range filters include episodes/findings whose intervals overlap the chosen range."},
    {"Supporting events / exact evidence membership", "Raw evidence",
     "The records used to construct an episode or finding. Double-clicking an episode shows its explicit event membership. A repeated-command diagnostic uses the same membership; a command-gap diagnostic shows its two endpoints; an unresolved-packet diagnostic shows that packet.\n\n"
     "The interface clears event-specific filters to show the linked evidence. Reset filters clears this evidence selection. Unrelated events inside the same time bounds are not automatically supporting evidence."},
    {"Target / Target ID", "Events",
     "A decoded object reference that a command targets, when present. It is separate from the actor and from the requested unit/building/technology type ID. Some commands target coordinates or have no object target.\n\n"
     "The Target ID filter matches the indexed numeric object reference. Absent-target sentinel values become unavailable in the index, while the original payload is retained. A target reference does not prove successful interaction, a hit or a kill."},
    {"Team / resolved team ID", "Players and labels",
     "Team information decoded from the header, with the helper's resolved team ID displayed when available. It is separate from player number and selected color.\n\n"
     "A team field describes recorded setup metadata. It does not reconstruct every later diplomacy change or establish cooperation during play. Missing team values remain unavailable."},
    {"Time / timestamp / milliseconds", "Time and navigation",
     "Event Time is the parser's replay game time in milliseconds, displayed as hours:minutes:seconds.milliseconds. It is not the local clock or time spent analyzing. Multiple events can share a timestamp; event ordering retains their IDs and original operation sequence.\n\n"
     "Time range controls use whole seconds. For example, a From/To range of 60 to 60 includes events during that whole second, from 60.000 through 60.999."},
    {"Time range / Window length (s)", "Time and navigation",
     "Enable Time range to filter events between the From and To seconds. Episodes and findings are included when their Start/End interval overlaps this range.\n\n"
     "Window length (s) sets the interval around a timeline click. A click selects that time window, opens Event explorer and selects the nearest matching event on a bounded page. The chart retains its broader span as context; its selected interval is highlighted."},
    {"Timeline / bin / recorded activity", "Views",
     "The chart groups matching recorded events into time bins. Each bar's height is a count of event packets in that interval, not a count of active units, completed actions or successful commands. Bin widths adapt to the time span.\n\n"
     "The chart honors selected players and event filters but keeps the time span as navigation context instead of shrinking to the selected range. Click a bar to inspect a time window in Event explorer. An empty interval does not establish that units were idle."},
    {"Type ID / unit / building / technology", "Events",
     "Type ID is an explicitly decoded requested unit, building or technology identifier. The optional profile can add a name from the corresponding namespace, with the raw numeric ID retained.\n\n"
     "It describes a requested type, not the actor object's own type. Unit, building and technology namespaces are separate and may reuse numbers. Actor ID and Target ID refer to object instances instead. No type is guessed from those references."},
    {"Uncertainty", "Evidence levels",
     "The stated limit on a finding's interpretation: what the records cannot establish, or another possible explanation. Read it together with Evidence description and the supporting events.\n\n"
     "It is an explanation, not a numeric confidence score. For example, repeated commands may be intentional and a command gap may occur while units keep working."},
    {"Unavailable", "Evidence levels",
     "The information is missing or not exposed by the parser. It is displayed as Unavailable or retained as null in structured data. This is not zero, false, a known negative result or an unknown-owner player number.\n\n"
     "Resources gathered, completed production, kills, idle time and winner are unavailable simulation metrics. A missing target/type/setting is also unavailable even when other fields in the same event are observed."},
    {"Unknown ownership / Include unknown ownership", "Players and labels",
     "The event's player owner could not be established from the decoded packet. It remains a null value rather than being assigned to a player. Include unknown ownership adds those records to the current selection.\n\n"
     "Unknown ownership is separate from player 0. The analyzer does not guess owners from actor ID, color, chat or packet kind. Unknown-owner command runs can form their own episodes without attributing them to a known player."},
    {"Unresolved / unresolved packet", "Evidence levels",
     "Unresolved means decoding or a claimed outcome is not established. An unresolved packet could not be fully decoded; its finding points to the recorded event and available original bytes or parser warning. Actor or target fields may be unavailable.\n\n"
     "An episode's execution outcome is also unresolved even when its supporting commands were decoded. This does not mean that execution failed; the evidence cannot settle it."},
    {"User label / manual identity", "Players and labels",
     "A name or designation you assign, such as Baseline AI or Candidate AI. It is explicitly user-provided and belongs to the replay hash and player number, rather than only its filename.\n\n"
     "Labels help organize comparisons and are carried in export metadata. They remain separate from observed header identity and do not alter raw packets, player ownership or inferred groupings."},
    {"Version / build / save version / log version", "Players and labels",
     "Decoded identifiers for the game's version/build and replay save/log format. They help explain parser compatibility and compare recorded context. They are distinct from the analyzer application version, index schema version and pipeline fingerprint.\n\n"
     "Compare reports available values as match, different or unavailable. Equal versions do not establish equal game settings, maps or match outcomes."},
    {"Work commands", "Command categories",
     "WORK, REPAIR and BACK_TO_WORK packets. They record work or repair instructions. Counts do not measure resources gathered, time spent working, successful repairs or completed work. Inspect each packet's actor, target and decoded fields for its recorded request."},
};
}

HelpDialog::HelpDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("termsGuide");
    setWindowTitle("Terms and guide — AoE2 Replay Analysis");
    resize(960,650);
    setMinimumSize(720,480);
    auto *layout=new QVBoxLayout(this);
    auto *intro=new QLabel("Find a term or choose a topic. Definitions explain recorded evidence and its limits.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    search=new QLineEdit;
    search->setObjectName("helpSearch");
    search->setPlaceholderText("Search terms and explanations, e.g. episode or actor");
    search->setAccessibleName("Search terms and explanations");
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    auto *splitter=new QSplitter;
    topics=new QListWidget;
    topics->setObjectName("helpTopics");
    topics->setAccessibleName("Help topics");
    topics->setMinimumWidth(240);
    topics->setWordWrap(true);
    description=new QTextBrowser;
    description->setObjectName("helpDescription");
    description->setAccessibleName("Topic explanation");
    description->setOpenExternalLinks(false);
    splitter->addWidget(topics);
    splitter->addWidget(description);
    splitter->setSizes({310,630});
    splitter->setStretchFactor(1,1);
    layout->addWidget(splitter,1);
    count=new QLabel;
    layout->addWidget(count);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::close);
    for(int i=0;i<int(std::size(guide));++i){
        auto *item=new QListWidgetItem(QString::fromUtf8(guide[i].title),topics);
        item->setData(Qt::UserRole,i);
        item->setToolTip(QString::fromUtf8(guide[i].group)+" — "+item->text());
    }
    connect(search,&QLineEdit::textChanged,this,&HelpDialog::filterTopics);
    connect(topics,&QListWidget::currentRowChanged,this,&HelpDialog::showTopic);
    filterTopics();
    search->setFocus();
}

void HelpDialog::filterTopics(){
    const auto words=search->text().simplified().split(' ',Qt::SkipEmptyParts);
    QListWidgetItem *first=nullptr,*titleMatch=nullptr;
    int matches=0;
    for(int i=0;i<topics->count();++i){
        auto *item=topics->item(i);
        const auto &topic=guide[item->data(Qt::UserRole).toInt()];
        const auto haystack=QString::fromUtf8(topic.title)+" "+QString::fromUtf8(topic.group)+" "+QString::fromUtf8(topic.text);
        bool match=true;
        for(const auto &word:words)if(!haystack.contains(word,Qt::CaseInsensitive)){match=false;break;}
        item->setHidden(!match);
        if(match){
            ++matches;if(!first)first=item;
            bool titleMatches=!words.isEmpty();
            for(const auto &word:words)if(!item->text().contains(word,Qt::CaseInsensitive)){titleMatches=false;break;}
            if(titleMatches&&!titleMatch)titleMatch=item;
        }
    }
    count->setText(QString("%1 of %2 topics · Search matches titles and explanations").arg(matches).arg(topics->count()));
    if(titleMatch)topics->setCurrentItem(titleMatch);
    else if(!topics->currentItem()||topics->currentItem()->isHidden())topics->setCurrentItem(first);
    showTopic();
}

void HelpDialog::showTopic(){
    const auto *item=topics->currentItem();
    if(!item||item->isHidden()){
        description->setHtml("<h2>No matching terms</h2><p>Try fewer words, another term, or clear the search.</p>");
        return;
    }
    const auto &topic=guide[item->data(Qt::UserRole).toInt()];
    const auto body=QString::fromUtf8(topic.text).toHtmlEscaped().replace("\n\n","</p><p>");
    description->setHtml("<p>"+QString::fromUtf8(topic.group).toHtmlEscaped()+"</p><h2>"+
        QString::fromUtf8(topic.title).toHtmlEscaped()+"</h2><p>"+body+"</p>");
}
