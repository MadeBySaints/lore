#include "store.h"
#include "backend.h"
#include "theme.h"
#include "translator.h"
#include "paths.h"
#include <QDir>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTextStream>
#include <QQuickItem>
#include <QStandardPaths>
#include <QSqlQuery>
#include <QLockFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <filesystem>
#include <QtTest>

static QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n'; }
static QByteArray claude(const QString &root, const QString &role, const QString &text) {
  return json({{"type",role},{"cwd",root},{"timestamp","2026-11-18T10:00:00Z"},{"message",QJsonObject{{"role",role},{"content",QJsonArray{QJsonObject{{"type","text"},{"text",text}}}}}}});
}
static QJsonObject entry(const QString &ref) {
  return {{"entries",QJsonArray{QJsonObject{{"title","Database choice"},{"kind","decision"},{"body","Use SQLite for local storage."},{"refs",QJsonArray{ref}}}}}};
}
class ReleaseUrlRecorder : public QObject {
  Q_OBJECT
public:
  QUrl opened;
  Q_INVOKABLE void record(const QUrl &url) { opened=url; }
};
class FakePicker : public FilePicker {
public:
  bool opened = false;
  using FilePicker::FilePicker;
  void choose(bool, const QString &) override { opened = true; }
  void pick(const QString &path) { emit selected(path); }
};
class EmbeddingFixture : public QTcpServer {
public:
  int requests=0;
  bool invalid=false;
  QString digest="fixture-v1";
  EmbeddingFixture() {
    listen(QHostAddress::LocalHost);
    connect(this,&QTcpServer::newConnection,this,[this] {
      auto *socket=nextPendingConnection();
      connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
      connect(socket,&QTcpSocket::readyRead,socket,[this,socket] {
        QByteArray bytes=socket->property("request").toByteArray()+socket->readAll(); socket->setProperty("request",bytes);
        const int split=bytes.indexOf("\r\n\r\n"); if(split<0 || socket->property("responded").toBool()) return;
        const auto match=QRegularExpression("Content-Length: (\\d+)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(bytes.left(split)));
        const int size=match.hasMatch()?match.captured(1).toInt():0; if(bytes.size()-split-4<size) return;
        socket->setProperty("responded",true); ++requests;
        const auto body=QJsonDocument::fromJson(bytes.mid(split+4)).object(); QJsonArray embeddings;
        for(const auto &text:body.value("input").toArray()) {
          const QString s=text.toString().toLower();
          const bool local=s.contains("hosted service") || s.section(" | text: ",1).contains("information must stay");
          embeddings.append(invalid ? QJsonArray{0,0} : local ? QJsonArray{1,0} : QJsonArray{0,1});
        }
        const QJsonObject result=bytes.startsWith("GET /api/tags") ? QJsonObject{{"models",QJsonArray{QJsonObject{{"name","embeddinggemma:latest"},{"digest",digest}}}}} : QJsonObject{{"embeddings",embeddings}};
        const QByteArray response=QJsonDocument(result).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(response.size())+"\r\nConnection: close\r\n\r\n"+response); socket->disconnectFromHost();
      });
    });
  }
  QByteArray url() const { return "http://127.0.0.1:"+QByteArray::number(serverPort()); }
};
class LoreTests : public QObject {
  Q_OBJECT
private slots:
  void importsDeduplicatesAndPreservesRaw() {
    QTemporaryDir project, logs; Store store(project.path(),true);
    const QByteArray raw = claude(project.path(),"user","Use SQLite with WAL.") + claude(project.path(),"assistant","Agreed. Next: build the index.");
    const QString path = logs.filePath("session.jsonl"); Store::write(path,raw);
    QCOMPARE(store.ingest(path,"claude"),2); QCOMPARE(store.ingest(path,"claude"),0);
    QCOMPARE(store.stats().value("pending").toInt(),2); store.render();
    const auto id = store.sessions().first().toObject().value("id").toString();
    QCOMPARE(Store::read(project.filePath(".lore/sources/" + id + ".jsonl")).toUtf8(),raw);
    Store::write(path,raw + claude(project.path(),"user","Add regression coverage."));
    QCOMPARE(store.ingest(path,"claude"),1); QCOMPARE(store.stats().value("events").toInt(),3);
    QCOMPARE(store.search("SQLite").size(),1);
  }
  void codexCapturesMessagesAndTools() {
    QTemporaryDir p, logs; Store store(p.path(),true);
    const QByteArray raw = json({{"type","session_meta"},{"payload",QJsonObject{{"cwd",p.path()},{"id","fixture"}}}})
      + json({{"type","response_item"},{"payload",QJsonObject{{"type","message"},{"role","assistant"},{"content",QJsonArray{QJsonObject{{"type","output_text"},{"text","Use an FTS5 search index."}}}}}}})
      + json({{"type","response_item"},{"payload",QJsonObject{{"type","function_call"},{"name","exec_command"},{"arguments","{\"cmd\":\"ls\"}"}}}})
      + json({{"type","response_item"},{"payload",QJsonObject{{"type","function_call_output"},{"output","README.md"}}}});
    Store::write(logs.filePath("rollout.jsonl"),raw);
    QCOMPARE(store.ingest(logs.filePath("rollout.jsonl"),"codex"),4);
    QCOMPARE(store.search("FTS5").size(),1); QCOMPARE(store.search("exec_command").size(),1);
    QCOMPARE(store.stats().value("pending").toInt(),1);
  }
  void isolatesProjectsAndHandlesPartialWrites() {
    QTemporaryDir p, sibling, logs; Store store(p.path(),true);
    const QString path = logs.filePath("s.jsonl");
    Store::write(path,claude(sibling.path(),"user","Private unrelated record") + "broken unrelated data\n");
    QCOMPARE(store.ingest(path,"claude"),0);
    Store::write(path,claude(p.path(),"user","One") + "{\"message\":");
    QCOMPARE(store.ingest(path,"claude"),1);
    Store::write(path,claude(p.path(),"user","One") + "bad\n");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.ingest(path,"claude"));
    QCOMPARE(store.stats().value("events").toInt(),1);
  }
  void learningValidatesCitationsAndRetainsHistory() {
    QTemporaryDir p, logs; Store store(p.path(),true);
    Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Use SQLite"));
    store.ingest(logs.filePath("s.jsonl"),"claude");
    const auto batch = store.learningBatch().value("events").toArray();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.learn(entry("invented"),batch,"fake"));
    QCOMPARE(store.stats().value("pending").toInt(),1); QCOMPARE(store.memories().size(),0);
    const QString id = batch.first().toObject().value("id").toString();
    store.learn(entry(id),batch,"fake");
    QCOMPARE(store.stats().value("pending").toInt(),0); QCOMPARE(store.memories().size(),1);
    QVERIFY(Store::read(p.filePath(".lore/wiki/1.md")).contains(".md#" + id));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.learn(entry(id),batch,"fake"));
    QCOMPARE(store.memories().size(),1);
  }
  void preservesInstructionsAndExistingHooks() {
    QTemporaryDir p; Store store(p.path(),true);
    Store::write(p.filePath("AGENTS.md"),"# Existing rules\nNever remove me.\n");
    Store::write(p.filePath(".claude/settings.local.json"),"{\"permissions\":{\"allow\":[\"Read\"]},\"hooks\":{\"Stop\":[{\"hooks\":[{\"type\":\"command\",\"command\":\"echo existing\"}]}]}}");
    store.connectAgents("/usr/bin/lore"); const QString agents = Store::read(p.filePath("AGENTS.md"));
    store.connectAgents("/usr/bin/lore");
    QCOMPARE(Store::read(p.filePath("AGENTS.md")),agents); QVERIFY(agents.contains("Never remove me."));
    const auto settings = QJsonDocument::fromJson(Store::read(p.filePath(".claude/settings.local.json")).toUtf8()).object();
    QVERIFY(settings.contains("permissions")); QCOMPARE(settings.value("hooks").toObject().value("Stop").toArray().size(),2);
    QVERIFY(Store::read(p.filePath(".gitignore")).contains("/.lore/"));
    Store::write(p.filePath("BRAIN.md"),Store::read(p.filePath("BRAIN.md")).toUtf8() + "\nUser notes stay here.\n");
    store.render(); QVERIFY(store.context().contains("User notes stay here."));
  }
  void hookArgumentsPreserveWindowsPaths() {
    const QString binary = QString::fromUtf8("C:\\Program Files\\Lore's $tools\\lore.exe");
    const QString project = QString::fromUtf8("D:\\Work\\Atlas's & mémoire (draft)\\");
    const auto hook = Store::claudeHook(binary,project);
    QCOMPARE(hook.value("command").toString(),binary);
    QCOMPARE(hook.value("args").toArray(),QJsonArray({"hook","--project",project}));
    QVERIFY(!hook.contains("shell"));
    const auto roundtrip = QJsonDocument::fromJson(QJsonDocument(hook).toJson()).object();
    QCOMPARE(roundtrip,hook);
  }
  void reconnectMigratesOwnedHooksAndPreservesOtherCallbacks() {
    QTemporaryDir p; Store store(p.path(),true);
    const QString binary = "/usr/bin/lore";
    const QString legacy = "'/usr/bin/lore' hook --project '" + p.path() + "'";
    const QJsonObject unrelated{{"type","command"},{"command","echo user hook"},{"timeout",42}};
    const QJsonObject original{{"permissions",QJsonObject{{"allow",QJsonArray{"Read"}}}},
      {"hooks",QJsonObject{{"Stop",QJsonArray{QJsonObject{{"matcher","custom"},{"hooks",QJsonArray{
        QJsonObject{{"type","command"},{"command",legacy}},unrelated}}}}}}}};
    const QString path = p.filePath(".claude/settings.local.json");
    Store::write(path,QJsonDocument(original).toJson());
    store.connectAgents(binary);
    const QString once = Store::read(path); store.connectAgents(binary); QCOMPARE(Store::read(path),once);
    const auto settings = QJsonDocument::fromJson(once.toUtf8()).object();
    QCOMPARE(settings.value("permissions"),original.value("permissions"));
    const auto stop = settings.value("hooks").toObject().value("Stop").toArray();
    QCOMPARE(stop.size(),2);
    QCOMPARE(stop.first().toObject().value("matcher").toString(),QString("custom"));
    QCOMPARE(stop.first().toObject().value("hooks").toArray(),QJsonArray{unrelated});
    QCOMPARE(stop.last().toObject().value("hooks").toArray(),QJsonArray{Store::claudeHook(binary,store.root())});
    // A moved/reinstalled binary updates Lore's marked entry rather than
    // leaving another invocation of the old executable behind.
    store.connectAgents("C:/Program Files/Lore/lore.exe");
    const auto moved = QJsonDocument::fromJson(Store::read(path).toUtf8()).object().value("hooks").toObject().value("Stop").toArray();
    QCOMPARE(moved.size(),2);
    QCOMPARE(moved.last().toObject().value("hooks").toArray().first().toObject().value("command").toString(),QString("C:/Program Files/Lore/lore.exe"));
  }
  void invalidHooksDoNotChangeProjectInstructions() {
    QTemporaryDir p; Store store(p.path(),true);
    Store::write(p.filePath("AGENTS.md"),"Keep these instructions.");
    Store::write(p.filePath(".claude/settings.local.json"),"{\"hooks\":[]}");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.connectAgents("/usr/bin/lore"));
    QCOMPARE(Store::read(p.filePath("AGENTS.md")),QString("Keep these instructions."));
  }
  void projectPathsRespectWindowsAndPosixBoundaries() {
    using Paths::Style;
    QVERIFY(Paths::contains("C:\\Work\\Atlas", "c:/work/ATLAS/src", Style::Windows));
    QVERIFY(Paths::contains("C:/", "c:/Work/Atlas", Style::Windows));
    QVERIFY(Paths::contains("\\\\server\\share\\Atlas", "//SERVER/share/atlas/src", Style::Windows));
    QVERIFY(!Paths::contains("C:/Work/Atlas", "C:/Work/Atlas-other", Style::Windows));
    QVERIFY(!Paths::contains("C:/Work/Atlas", "D:/Work/Atlas", Style::Windows));
    QVERIFY(!Paths::contains("C:/Work/Atlas", "C:/Work/Atlas/../private", Style::Windows));
    QVERIFY(!Paths::contains("//server/share", "//server/share-other/file", Style::Windows));
    QVERIFY(Paths::contains("/", "/work/atlas", Style::Posix));
    QVERIFY(Paths::contains("/work/atlas", "/work/atlas/src", Style::Posix));
    QVERIFY(!Paths::contains("/work/Atlas", "/work/atlas", Style::Posix));
    QVERIFY(!Paths::contains("/work/atlas", "/work/atlas-other", Style::Posix));
    QVERIFY(!Paths::contains("", "/work/atlas", Style::Posix));
  }
  void completedTasksLeaveEntryPointAndKeepEvidence() {
    QTemporaryDir p, logs; Store store(p.path(),true);
    const QString path = logs.filePath("s.jsonl");
    Store::write(path,claude(p.path(),"user","Next implement search")); store.ingest(path,"claude");
    auto batch = store.learningBatch().value("events").toArray();
    store.learn({{"entries",QJsonArray{QJsonObject{{"title","Implement search"},{"kind","next"},{"body","Implement search."},{"refs",QJsonArray{batch.first().toObject().value("id")}}}}}},batch,"fake");
    QVERIFY(store.context().contains("[Implement search]"));
    Store::write(path,claude(p.path(),"user","Next implement search") + claude(p.path(),"assistant","Search is implemented and tested."));
    store.ingest(path,"claude"); batch = store.learningBatch().value("events").toArray();
    store.learn({{"entries",QJsonArray{QJsonObject{{"title","Search complete"},{"kind","fact"},{"body","Search now works."},{"refs",QJsonArray{batch.first().toObject().value("id")}},{"supersedes",QJsonArray{1}}}}}},batch,"fake");
    QVERIFY(!store.context().contains("[Implement search]"));
    QCOMPARE(store.memories().size(),2);
    QVERIFY(Store::read(p.filePath(".lore/wiki/1.md")).contains("Superseded by a newer entry"));
  }
  void completesFinalLineWithoutDuplicatingRecord() {
    QTemporaryDir p, logs; Store store(p.path(),true);
    const QString path = logs.filePath("s.jsonl");
    const QByteArray raw = claude(p.path(),"user","Choose SQLite");
    Store::write(path,raw.trimmed()); QCOMPARE(store.ingest(path,"claude"),1);
    Store::write(path,raw); QCOMPARE(store.ingest(path,"claude"),0);
  }
  void searchTreatsPunctuationAsInput() {
    QTemporaryDir p, logs; Store store(p.path(),true);
    Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","PreCompact works with SQLite."));
    store.ingest(logs.filePath("s.jsonl"),"claude");
    QCOMPARE(store.search("PreCompact").size(),1);
    QCOMPARE(store.search("\"SQLite\" OR 'DROP'").size(),0);
    QCOMPARE(store.search("((").size(),1);
    QCOMPARE(store.search("unknown").size(),0);
  }
  void refusesSymlinkedStorageAndOutputs() {
    QTemporaryDir p, outside;
#ifdef Q_OS_WIN
    std::error_code error;
    std::filesystem::create_directory_symlink(outside.path().toStdWString(),p.filePath(".lore").toStdWString(),error);
    if (error) QSKIP("Windows does not permit symlink creation for this user");
#else
    QVERIFY(QFile::link(outside.path(),p.filePath(".lore")));
#endif
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,Store(p.path(),true));
#ifdef Q_OS_WIN
    QVERIFY(std::filesystem::remove(p.filePath(".lore").toStdWString()));
#else
    QFile::remove(p.filePath(".lore"));
#endif
    Store store(p.path(),true);
    Store::write(outside.filePath("target"),"keep");
#ifdef Q_OS_WIN
    std::filesystem::create_symlink(outside.filePath("target").toStdWString(),p.filePath("BRAIN.md").toStdWString(),error);
    QVERIFY(!error);
#else
    QVERIFY(QFile::link(outside.filePath("target"),p.filePath("BRAIN.md")));
#endif
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.render());
    QCOMPARE(Store::read(outside.filePath("target")),QString("keep"));
  }
  void themeAndTranslationFollowFiles() {
    QTemporaryDir p;
    Theme theme(p.path()); QCOMPARE(theme.accent(),QString("#FFD60A"));
    Store::write(p.filePath("theme/colors.toml"),"accent = \"#112233\"\n");
    QTRY_COMPARE(theme.accent(),QString("#112233"));
    Store::write(p.filePath("language"),"en\n"); Translator translator(p.filePath("language"));
    QCOMPARE(translator.language(),QString("en"));
    Store::write(p.filePath("language"),"da\n");
    QTRY_COMPARE(translator.language(),QString("da")); QCOMPARE(translator.translate("Main","Open"),QString("Åbn"));
  }
  void backendLearnsUsingProviderProcess() {
    QTemporaryDir p, logs, fake; Store store(p.path(),true);
    Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Choose SQLite")); store.ingest(logs.filePath("s.jsonl"),"claude"); store.render();
    const QString executable = fake.filePath(
#ifdef Q_OS_WIN
      "codex.exe"
#else
      "codex"
#endif
    );
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),executable));
    QFile::setPermissions(executable,QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QByteArray oldPath = qgetenv("PATH");
    const auto restorePath = qScopeGuard([&] { qputenv("PATH",oldPath); });
    qputenv("PATH",fake.path().toUtf8() + QDir::listSeparator().toLatin1() + oldPath);
    Backend backend; QVERIFY(backend.openProject(p.path())); QSignalSpy finished(&backend,&Backend::learningFinished);
    backend.learn("codex"); QTRY_COMPARE(finished.count(),1);
    qputenv("PATH",oldPath);
    QVERIFY2(finished.first().first().toBool(),qPrintable(backend.status()));
    QCOMPARE(backend.memories().size(),1);
  }
  void migratesAnExistingArchiveWithoutChangingRecords() {
    QTemporaryDir p,logs;
    { Store old(p.path(),true); Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Keep this original record.")); old.ingest(logs.filePath("s.jsonl"),"claude"); old.render(); }
    const QString name="migration-fixture";
    { auto db=QSqlDatabase::addDatabase("QSQLITE",name); db.setDatabaseName(p.filePath(".lore/brain.sqlite")); QVERIFY(db.open()); QSqlQuery q(db);
      for(const auto &table:{"memory_review","memory_links","memory_conflicts","project_files","project_settings","forgotten_events","embeddings","deliveries","activity"}) QVERIFY(q.exec("DROP TABLE "+QString(table)));
      QVERIFY(q.exec("PRAGMA user_version=1")); db.close();
    }
    QSqlDatabase::removeDatabase(name);
    Store upgraded(p.path()); QCOMPARE(upgraded.stats().value("events").toInt(),1); QCOMPARE(upgraded.search("original").size(),1); QVERIFY(upgraded.deliveries().isEmpty()); upgraded.render(); QVERIFY(upgraded.context().contains("1 records"));
  }
  void indexesCodeLinksMemoryAndDetectsChanges() {
    QTemporaryDir p,logs; Store store(p.path(),true);
    Store::write(p.filePath("src/auth.cpp"),"class SessionManager {};\n// authentication entry point\n");
    Store::write(p.filePath(".env"),"SENSITIVE=never index\n");
    Store::write(p.filePath("node_modules/private.js"),"never index\n");
    QCOMPARE(store.indexProject().value("files").toInt(),1);
    QVERIFY(store.projectFiles().first().toObject().value("symbols").toString().contains("SessionManager"));
    Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Keep authentication in src/auth.cpp")); store.ingest(logs.filePath("s.jsonl"),"claude");
    const auto batch=store.learningBatch().value("events").toArray(); store.learn(entry(batch.first().toObject().value("id").toString()),batch,"fake");
    QCOMPARE(store.memories().first().toObject().value("files").toArray(),QJsonArray{"src/auth.cpp"});
    QVERIFY(!store.memories().first().toObject().value("stale").toBool());
    Store::write(p.filePath("src/auth.cpp"),"class UpdatedSessionManager {};\n"); store.indexProject();
    QVERIFY(store.memories().first().toObject().value("stale").toBool()); QCOMPARE(store.stats().value("stale").toInt(),1);
    store.reviewMemory(1,"confirmed"); QVERIFY(!store.memories().first().toObject().value("stale").toBool());
    QVERIFY(store.context().contains("Project file map"));
    const auto brief=store.briefing("UpdatedSessionManager",8,false); QVERIFY(!brief.value("evidence").toArray().isEmpty());
    QVERIFY(Store::read(p.filePath(brief.value("evidence").toArray().first().toObject().value("url").toString())).contains("UpdatedSessionManager"));
  }
  void longArchivesKeepRecentCorrectionsInLearningContext() {
    QTemporaryDir p,logs; Store store(p.path(),true); Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","A documented storage decision")); store.ingest(logs.filePath("s.jsonl"),"claude");
    const auto batch=store.learningBatch().value("events").toArray(); QJsonArray entries;
    for(int i=0;i<12;++i) { auto item=entry(batch.first().toObject().value("id").toString()).value("entries").toArray().first().toObject(); item["title"]="Historical topic "+QString::number(i); item["body"]=QString(4000,'x'); entries.append(item); }
    store.learn(QJsonObject{{"entries",entries}},batch,"fake"); store.reviewMemory(12,"corrected","LATEST_USER_CORRECTION: use local sessions.");
    const auto prompt=store.learningBatch().value("prompt").toString(); QVERIFY(prompt.contains("LATEST_USER_CORRECTION")); QVERIFY(prompt.size()<35000);
  }
  void correctionsConflictsAndForgetControlRecall() {
    QTemporaryDir p,logs; Store store(p.path(),true); const QString path=logs.filePath("s.jsonl");
    Store::write(path,claude(p.path(),"user","Use SQLite for the database.")); store.ingest(path,"claude"); auto batch=store.learningBatch().value("events").toArray();
    store.learn(entry(batch.first().toObject().value("id").toString()),batch,"fake");
    Store::write(path,claude(p.path(),"user","Use SQLite for the database.")+claude(p.path(),"user","We might use PostgreSQL instead.")); store.ingest(path,"claude"); batch=store.learningBatch().value("events").toArray();
    auto changed=entry(batch.first().toObject().value("id").toString()); auto e=changed["entries"].toArray().first().toObject(); e["body"]="Use PostgreSQL for storage."; changed["entries"]=QJsonArray{e}; store.learn(changed,batch,"fake");
    QVERIFY(!store.memories().first().toObject().value("conflicts").toArray().isEmpty());
    store.reviewMemory(2,"outdated");
    for(const auto &v:store.evidence()) if(v.toObject().value("type").toString()=="conversation") QVERIFY(!v.toObject().value("body").toString().contains("We might use PostgreSQL"));
    QVERIFY(store.search("PostgreSQL").isEmpty());
    store.reviewMemory(1,"corrected","Use SQLite for the database. PostgreSQL was only a proposal.");
    const auto corrected=store.memories().first().toObject(); QCOMPARE(corrected.value("review").toString(),QString("corrected"));
    const auto recall=store.briefing("database",3,false).value("evidence").toArray(); QCOMPARE(recall.first().toObject().value("id").toString(),QString("memory:3"));
    QVERIFY(store.memories()[2].toObject().value("supersededBy").toInt()==3);
    store.reviewMemory(3,"forgotten");
    for(const auto &v:store.evidence()) { const auto o=v.toObject(); QVERIFY(o.value("id").toString()!="memory:1" && o.value("id").toString()!="memory:3"); }
    QVERIFY(store.search("SQLite").isEmpty()); QVERIFY(!store.sessions().isEmpty());
    QCOMPARE(store.briefing("meaninglessxyz",8,false).value("evidence").toArray().size(),0);
  }
  void semanticRetrievalFindsDifferentWordingAndRejectsBadVectors() {
    EmbeddingFixture server; QVERIFY(server.isListening()); const auto old=qgetenv("LORE_EMBED_URL"); const auto restore=qScopeGuard([&] { qputenv("LORE_EMBED_URL",old); }); qputenv("LORE_EMBED_URL",server.url());
    QTemporaryDir p,logs; Store store(p.path(),true); Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","All information must stay on this computer.")+claude(p.path(),"user","The interface uses yellow buttons.")); store.ingest(logs.filePath("s.jsonl"),"claude");
    QVERIFY(store.briefing("Why avoid a hosted service?",3,false).value("evidence").toArray().isEmpty());
    QCOMPARE(store.buildSemanticIndex().value("updated").toInt(),2);
    const auto recall=store.briefing("Why avoid a hosted service?",3); QCOMPARE(recall.value("retrieval").toString(),QString("hybrid"));
    QVERIFY(recall.value("evidence").toArray().first().toObject().value("body").toString().contains("information must stay"));
    QCOMPARE(store.buildSemanticIndex().value("updated").toInt(),0);
    server.invalid=true; const auto fallback=store.briefing("yellow"); QCOMPARE(fallback.value("retrieval").toString(),QString("keyword fallback")); QVERIFY(!fallback.value("warning").toString().isEmpty());
    store.setSemanticEnabled(false); QVERIFY(!store.semanticStatus().value("enabled").toBool());
  }
  void answersRequireCitationsAndRejectChangedEvidence() {
    QTemporaryDir p; Store store(p.path(),true); Store::write(p.filePath("auth.cpp"),"// Authentication uses local sessions\n"); store.indexProject();
    const auto evidence=store.briefing("authentication",8,false).value("evidence").toArray(); QVERIFY(!evidence.isEmpty()); const QString id=evidence.first().toObject().value("id").toString();
    QJsonObject answer{{"answer","Authentication uses local sessions ["+id+"]"},{"refs",QJsonArray{id}},{"insufficient",false}};
    QVERIFY(!store.saveAnswer(answer,evidence,"How does authentication work?").isEmpty());
    auto bad=answer; bad["refs"]=QJsonArray{"invented"}; QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.saveAnswer(bad,evidence,"question"));
    bad=answer; bad["answer"]="Uncited claim"; QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.saveAnswer(bad,evidence,"question"));
    Store::write(p.filePath("auth.cpp"),"// Authentication changed\n"); QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.saveAnswer(answer,evidence,"question"));
    store.recordDelivery("UserPromptSubmit","fixture-session","authentication",store.briefing("authentication",8,false));
    QCOMPARE(store.deliveries().first().toObject().value("session").toString(),QString("fixture-session"));
    QVERIFY(store.deliveries().first().toObject().value("text").toString().contains(id));
  }
  void transientExportContentionRecoversAndNotificationsExpire() {
    QTemporaryDir p,logs,data; Store store(p.path(),true); store.render(); QLockFile lock(p.filePath(".lore/render.lock")); QVERIFY(lock.tryLock());
    Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","A durable captured message")); store.ingest(logs.filePath("s.jsonl"),"claude"); store.render(); QCOMPARE(store.stats().value("events").toInt(),1);
    lock.unlock(); store.sync(logs.path(),logs.path()); QVERIFY(store.context().contains("1 conversations"));
    const auto old=qgetenv("LORE_NOTIFICATION_TIMEOUT_MS"); const auto restore=qScopeGuard([&] { qputenv("LORE_NOTIFICATION_TIMEOUT_MS",old); }); qputenv("LORE_NOTIFICATION_TIMEOUT_MS","100");
    const QString preferences=data.filePath("preferences.json");
    { Backend backend(nullptr,nullptr,preferences,{logs.path(),logs.path()}); QVERIFY(!backend.selectProject(data.filePath("missing"))); QVERIFY(!backend.error().isEmpty()); QTRY_VERIFY(backend.error().isEmpty()); QVERIFY(!backend.notices().isEmpty()); backend.setNotificationsEnabled(false); QVERIFY(!backend.selectProject(data.filePath("missing"))); QVERIFY(backend.error().isEmpty()); }
    Backend restored(nullptr,nullptr,preferences,{logs.path(),logs.path()}); QVERIFY(!restored.notificationsEnabled());
  }
  void backendAnswersThroughProviderAndAbstainsWithoutEvidence() {
    QTemporaryDir p,logs,tools;
    const QString executable=tools.filePath(
#ifdef Q_OS_WIN
      "claude.exe"
#else
      "claude"
#endif
    );
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),executable)); QFile::setPermissions(executable,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    Store store(p.path(),true); Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Authentication uses local sessions.")); store.ingest(logs.filePath("s.jsonl"),"claude"); store.render();
    Backend backend(nullptr,nullptr,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); backend.setLearningProvider("claude"); backend.tools()->setExecutable("claude",executable); QTRY_VERIFY(!backend.tools()->checking()); QVERIFY(backend.tools()->ready("claude"));
    QSignalSpy finished(&backend,&Backend::learningFinished); backend.ask("How does authentication work?"); QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000); QVERIFY2(finished.first().first().toBool(),qPrintable(backend.status())); QVERIFY(!backend.answer().value("refs").toList().isEmpty());
    backend.ask("xyzunrelatedunknown"); QTRY_COMPARE_WITH_TIMEOUT(finished.size(),2,10000); QVERIFY(backend.answer().value("insufficient").toBool());
  }
  void guidedConnectionRemembersTheProjectAndSettings() {
    QTemporaryDir p, data, logs;
    const QString preferences = data.filePath("preferences.json");
    {
      FakePicker picker;
      Backend backend(nullptr,&picker,preferences,{logs.path(),logs.path()});
      backend.chooseProject(); QVERIFY(picker.opened); QVERIFY(backend.selecting());
      picker.pick(p.path()); QVERIFY(!backend.selecting());
      QCOMPARE(backend.selectedProject(),QFileInfo(p.path()).canonicalFilePath());
      QVERIFY(!QFileInfo::exists(p.filePath(".lore")));
      QVERIFY(backend.connectProject()); QVERIFY(backend.connected()); QVERIFY(backend.watching());
      QTRY_VERIFY(!backend.capturing());
      backend.setLearningProvider("claude"); backend.setAutoLearning(true);
      QVERIFY(backend.autoLearning());
    }
    Backend restored(nullptr,nullptr,preferences,{logs.path(),logs.path()});
    QCOMPARE(restored.project(),QFileInfo(p.path()).canonicalFilePath());
    QVERIFY(restored.connected()); QVERIFY(restored.watching()); QVERIFY(restored.autoLearning());
    QCOMPARE(restored.learningProvider(),QString("claude"));
    QCOMPARE(restored.recentProjects().size(),1);
    restored.setWatching(false);
    QVERIFY(!restored.autoLearning());
  }
  void toolChecksUseNativeExecutablesAndDoNotCallModels() {
    QTemporaryDir fake;
    const QString suffix =
#ifdef Q_OS_WIN
      ".exe";
#else
      "";
#endif
    for (const auto &id : {"claude","codex"}) {
      const QString executable = fake.filePath(QString(id) + suffix);
      QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),executable));
      QFile::setPermissions(executable,QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    const auto previous = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH",previous); });
    qputenv("PATH",fake.path().toUtf8() + QDir::listSeparator().toLatin1() + previous);
    Toolbox tools; tools.check(); QTRY_VERIFY(!tools.checking());
    for (const auto &entry : tools.entries()) QCOMPARE(entry.toMap().value("state").toString(),QString("ready"));
    QVERIFY(!Toolbox::resolve("claude",fake.filePath("missing")).available());
  }
  void windowsNpmWrapperUsesNodeWithoutAShell() {
#ifdef Q_OS_WIN
    if (QStandardPaths::findExecutable("node.exe").isEmpty()) QSKIP("Node is not installed");
    QTemporaryDir fake;
    const QString wrapper = fake.filePath("Claude's tools/claude.cmd");
    const QString script = fake.filePath("Claude's tools/node_modules/@anthropic-ai/claude-code/cli.js");
    Store::write(wrapper,"@echo Never run this wrapper\r\nexit /b 99\r\n");
    Store::write(script,"console.log(JSON.stringify({loggedIn:true}))\n");
    const auto command = Toolbox::resolve("claude",wrapper);
    QVERIFY(command.available()); QCOMPARE(command.arguments,QStringList{script});
    QProcess process; process.start(command.program,command.arguments + QStringList{"auth","status","--json"});
    process.closeWriteChannel();
    QVERIFY2(process.waitForStarted(10000),qPrintable(process.errorString()));
    // Allow the runner's first Node launch to finish security scanning. Match
    // the app's closed input channel so a provider cannot wait for stdin.
    const bool finished = process.waitForFinished(30000);
    const QString diagnostic = process.errorString() + "\n" + process.readAllStandardError();
    if (!finished) { process.kill(); process.waitForFinished(); }
    QVERIFY2(finished,qPrintable(diagnostic)); QCOMPARE(process.exitCode(),0);
    QVERIFY(QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("loggedIn").toBool());
#endif
  }
  void realQmlLoadsAndKeyboardSearchWorks() {
    QTemporaryDir p, logs; FakePicker picker;
    Backend backend(nullptr,&picker,{}, {logs.path(),logs.path()});
    Theme theme(p.path()); Translator translator(p.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator); engine.rootContext()->setContextProperty("initialProject",p.path());
    engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->show(); QTest::qWait(200);
    auto click = [&](const QString &name) {
      auto *item = window->findChild<QQuickItem *>(name);
      std::function<QQuickItem *(QQuickItem *)> find=[&](QQuickItem *parent) -> QQuickItem * { if(parent->objectName()==name) return parent; for(auto *child:parent->childItems()) if(auto *found=find(child)) return found; return nullptr; };
      if(!item) item=find(window->contentItem());
      if (!item) return false;
      const QPoint point = item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point); return true;
    };
    QVERIFY(click("chooseProjectButton")); QVERIFY(picker.opened);
    picker.pick(p.path()); QTest::qWait(50);
    QVERIFY(click("connectProjectButton")); QTRY_VERIFY(backend.connected()); QTRY_VERIFY(!backend.capturing());
    QVERIFY(backend.watching());
    QTest::keyClick(window,Qt::Key_K,Qt::ControlModifier);
    QCOMPARE(window->property("page").toInt(),3);
    for (const auto key : {Qt::Key_S,Qt::Key_Q,Qt::Key_L}) QTest::keyClick(window,key);
    QTest::keyClick(window,Qt::Key_Escape); QVERIFY(!window->property("helpVisible").toBool());
    ReleaseUrlRecorder releases;
    QDesktopServices::setUrlHandler("https",&releases,"record");
    const auto restoreBrowser=qScopeGuard([] { QDesktopServices::unsetUrlHandler("https"); });
    window->setProperty("helpVisible",true);
    QTest::qWait(50); window->grabWindow().save(QCoreApplication::applicationDirPath()+"/updates-help.png");
    QVERIFY(click("checkUpdatesButton")); QCOMPARE(releases.opened,QUrl("https://github.com/MadeBySaints/lore/releases"));
    window->setProperty("helpVisible",false);
    QVERIFY(click("nav6")); QCOMPARE(window->property("page").toInt(),6);
    auto *question=window->findChild<QQuickItem *>("askQuestion"); QVERIFY(question); question->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Q); QVERIFY(window->isVisible());
    QVERIFY(click("nav8")); QCOMPARE(window->property("page").toInt(),8);
    backend.setWatching(false); QVERIFY(click("previewContextButton")); QTRY_VERIFY(!backend.busy()); QVERIFY(!backend.preview().value("text").toString().isEmpty());
  }
};

int main(int argc, char **argv) {
  const QString provider = QFileInfo(QString::fromLocal8Bit(argv[0])).completeBaseName();
  if (provider == "codex" || provider == "claude") {
    // The test binary doubles as a native fake provider, including on Windows
    // where QProcess cannot execute a Unix shebang or a .cmd shim directly.
    QCoreApplication app(argc,argv);
    QTextStream input(stdin), output(stdout);
    const auto args = app.arguments();
    if ((provider == "claude" && args.contains("auth") && args.contains("status")) || (provider == "codex" && args.contains("login") && args.contains("status"))) {
      output << "{\"loggedIn\":true}\n"; return 0;
    }
    const QString prompt = input.readAll(), marker = "NEW EVENTS:\n";
    if (provider == "codex" && (!args.contains("--ephemeral") || !args.contains("--sandbox"))) return 1;
    if (provider == "claude" && (!args.contains("--no-session-persistence") || !args.contains("--strict-mcp-config"))) return 1;
    QJsonObject payload;
    if(prompt.startsWith("Answer the user's project question")) {
      const auto found=QRegularExpression("\\[((?:event|memory|file):[^\\]]+)\\]").match(prompt);
      if(!found.hasMatch()) return 1;
      const QString ref=qEnvironmentVariableIsSet("LORE_TEST_INVALID_REFS")?"invented":found.captured(1);
      payload={{"answer","Use local sessions ["+ref+"]"},{"refs",QJsonArray{ref}},{"insufficient",false}};
    } else {
      const int start=prompt.lastIndexOf(marker);
      const auto events=QJsonDocument::fromJson(prompt.mid(start+marker.size()).toUtf8()).array();
      if(start<0 || events.isEmpty()) return 1;
      const QString ref=qEnvironmentVariableIsSet("LORE_TEST_INVALID_REFS")?"invented":events.first().toObject().value("id").toString();
      payload=entry(ref);
    }
    const QString result=QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    const QJsonObject response = provider == "codex"
      ? QJsonObject{{"type","item.completed"},{"item",QJsonObject{{"type","agent_message"},{"text",result}}}}
      : QJsonObject{{"result",result},{"is_error",false}};
    output << QJsonDocument(response).toJson(QJsonDocument::Compact) << '\n';
    return 0;
  }
  QGuiApplication app(argc,argv); QQuickStyle::setStyle("Material");
  LoreTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "lore_tests.moc"
