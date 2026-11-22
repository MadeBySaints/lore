#include "store.h"
#include "backend.h"
#include "theme.h"
#include "translator.h"
#include "paths.h"
#include <QDirIterator>
#include <QTimeZone>
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QScopeGuard>
#include <QTextStream>
#include <QTextBlock>
#include <QTextLayout>
#include <QQuickItem>
#include <QStandardPaths>
#include <QSqlQuery>
#include <QLockFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <filesystem>
#include <QMutex>
#include <QThread>
#include <QtConcurrent>
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
        const QByteArray response=bytes.startsWith("POST /api/pull") ? QByteArray("{\"status\":\"pulling\",\"total\":2097152,\"completed\":1048576}\n{\"status\":\"success\",\"total\":2097152,\"completed\":2097152}\n") : QJsonDocument(result).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(response.size())+"\r\nConnection: close\r\n\r\n"+response); socket->disconnectFromHost();
      });
    });
  }
  QByteArray url() const { return "http://127.0.0.1:"+QByteArray::number(serverPort()); }
};
class LocalModelFixture : public QTcpServer {
public:
  bool ollama=true, responses=true;
  QStringList models{"qwen3:8b"};  // What /api/tags lists, beside the embedding model.
  QByteArray content;
  QList<QByteArray> replies;
  QString lastPath; QJsonObject lastBody; int chats=0;
  int delayMs=0, failChats=0;  // Slow replies, and chat requests that fail with a server error.
  explicit LocalModelFixture(bool isOllama=true) : ollama(isOllama) {
    listen(QHostAddress::LocalHost);
    connect(this,&QTcpServer::newConnection,this,[this] {
      auto *socket=nextPendingConnection();
      connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
      connect(socket,&QTcpSocket::readyRead,socket,[this,socket] {
        QByteArray bytes=socket->property("request").toByteArray()+socket->readAll(); socket->setProperty("request",bytes);
        const int split=bytes.indexOf("\r\n\r\n"); if(split<0 || socket->property("responded").toBool()) return;
        const auto match=QRegularExpression("Content-Length: (\\d+)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(bytes.left(split)));
        const int size=match.hasMatch()?match.captured(1).toInt():0; if(bytes.size()-split-4<size) return;
        socket->setProperty("responded",true);
        const QString path=QString::fromLatin1(bytes.left(bytes.indexOf("\r\n"))).section(' ',1,1).section('?',0,0);
        QByteArray status="200 OK"; QJsonObject result;
        if(path=="/api/tags") { if(ollama) { QJsonArray listed; for(const auto &m:models) listed.append(QJsonObject{{"name",m}}); listed.append(QJsonObject{{"name","embeddinggemma:latest"}}); result={{"models",listed}}; } else status="404 Not Found"; }
        else if(path=="/v1/models") result={{"data",QJsonArray{QJsonObject{{"id","local-model"},{"context_length",16384}}}}};
        else if(path=="/v1/responses" && bytes.mid(split+4).trimmed()=="{}") { status=responses ? "400 Bad Request" : "404 Not Found"; result={{"error","probe"}}; }  // Lore asking whether the route exists.
        else { lastPath=path; lastBody=QJsonDocument::fromJson(bytes.mid(split+4)).object(); ++chats;
          QByteArray content=replies.isEmpty() ? this->content : replies.takeFirst();
          if(content.isEmpty()) {  // Answer any learning batch by citing its first event.
            const QString prompt=lastBody.value("messages").toArray().first().toObject().value("content").toString();
            const auto events=QJsonDocument::fromJson(prompt.mid(prompt.lastIndexOf("NEW EVENTS:\n")+12).toUtf8()).array();
            content=QJsonDocument(entry(events.first().toObject().value("id").toString())).toJson(QJsonDocument::Compact); }
          if(failChats>0) { --failChats; status="500 Internal Server Error"; }
          result = status!="200 OK" ? QJsonObject{{"error","model crashed"}} : path=="/api/chat" ? QJsonObject{{"message",QJsonObject{{"role","assistant"},{"content",QString::fromUtf8(content)}}},{"done",true},{"prompt_eval_count",1200},{"eval_count",80},{"eval_duration",2000000000}} : QJsonObject{{"choices",QJsonArray{QJsonObject{{"message",QJsonObject{{"role","assistant"},{"content",QString::fromUtf8(content)}}}}}}}; }
        const QByteArray response=bytes.startsWith("POST /api/pull") ? QByteArray("{\"status\":\"pulling\",\"total\":2097152,\"completed\":1048576}\n{\"status\":\"success\",\"total\":2097152,\"completed\":2097152}\n") : QJsonDocument(result).toJson(QJsonDocument::Compact);
        if(path!="/api/tags" && path!="/v1/models" && delayMs>0) {
          QTimer::singleShot(delayMs,socket,[socket,status,response] { socket->write("HTTP/1.1 "+status+"\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(response.size())+"\r\nConnection: close\r\n\r\n"+response); socket->disconnectFromHost(); });
          return;
        }
        socket->write("HTTP/1.1 "+status+"\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(response.size())+"\r\nConnection: close\r\n\r\n"+response); socket->disconnectFromHost();
      });
    });
  }
  QByteArray url() const { return "http://127.0.0.1:"+QByteArray::number(serverPort()); }
};
static QString fakeAgent(const QTemporaryDir &bin, const QString &name) {
#ifdef Q_OS_WIN
  const QString executable = bin.filePath(name + ".exe");
#else
  const QString executable = bin.filePath(name);
#endif
  if (!QFileInfo::exists(executable)) QFile::copy(QCoreApplication::applicationFilePath(), executable);
  QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  return executable;
}
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
  void codexCapturesMessagesAndSkipsTools() {
    QTemporaryDir p, logs; Store store(p.path(),true);
    const QByteArray raw = json({{"type","session_meta"},{"payload",QJsonObject{{"cwd",p.path()},{"id","fixture"}}}})
      + json({{"type","response_item"},{"payload",QJsonObject{{"type","message"},{"role","assistant"},{"content",QJsonArray{QJsonObject{{"type","output_text"},{"text","Use an FTS5 search index."}}}}}}})
      + json({{"type","response_item"},{"payload",QJsonObject{{"type","function_call"},{"name","exec_command"},{"arguments","{\"cmd\":\"ls\"}"}}}})
      + json({{"type","response_item"},{"payload",QJsonObject{{"type","function_call_output"},{"output","README.md"}}}});
    Store::write(logs.filePath("rollout.jsonl"),raw);
    QCOMPARE(store.ingest(logs.filePath("rollout.jsonl"),"codex"),1);
    QCOMPARE(store.search("FTS5").size(),1); QCOMPARE(store.search("exec_command").size(),0);
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
  void captureReadsOnlyNewLinesAndDetectsRewrites() {
    QTemporaryDir p, logs; Store store(p.path(),true);
    const QString path = logs.filePath("s.jsonl");
    const QByteArray first = claude(p.path(),"user","First message"), second = claude(p.path(),"assistant","Second message");
    Store::write(path,first+second); QCOMPARE(store.ingest(path,"claude"),2);
    // Damage an already-read line in place. A full re-read would reject the
    // file; reading from the saved position only sees the appended record.
    QByteArray damaged = first + QByteArray(second.size()-1,'x') + '\n' + claude(p.path(),"user","Third message");
    Store::write(path,damaged); QCOMPARE(store.ingest(path,"claude"),1); QCOMPARE(store.ingest(path,"claude"),0);
    QCOMPARE(store.stats().value("events").toInt(),3);
    // A different first line means the file was replaced: read it again.
    Store::write(path,claude(p.path(),"user","Replacement session")+claude(p.path(),"assistant","Fresh reply"));
    QCOMPARE(store.ingest(path,"claude"),2);
    // Large transcripts are read in batches without losing records.
    QByteArray big; for(int i=0;i<5000;++i) big += claude(p.path(),i%2?"assistant":"user","Message number "+QString::number(i));
    Store::write(logs.filePath("big.jsonl"),big); QCOMPARE(store.ingest(logs.filePath("big.jsonl"),"claude"),5000);
    QCOMPARE(store.ingest(logs.filePath("big.jsonl"),"claude"),0);
    store.render();
    for(const auto &v:store.sessions()) if(v.toObject().value("count").toInt()==5000) QCOMPARE(v.toObject().value("title").toString(),QString("Message number 0"));
  }
  void manyConcurrentSessionsCaptureEveryRecordOnce() {
    // Power users run several Claude/Codex windows at once: each window's
    // hook opens its own connection and captures its own transcript while
    // the app polls every transcript in the background.
    QTemporaryDir p, logs; { Store store(p.path(),true); store.render(); }
    const QString root=p.path(), folder=logs.path();
    constexpr int windows=6, turns=40;
    std::atomic_int failures{0}; std::atomic_bool done{false}; QStringList errors; QMutex lock;
    QList<QFuture<void>> sessions;
    for(int w=0;w<windows;++w) sessions.append(QtConcurrent::run([&,w] {
      const QString path=folder+"/window-"+QString::number(w)+".jsonl";
      for(int t=0;t<turns;++t) {
        QFile file(path); if(!file.open(QIODevice::Append)) { ++failures; return; }
        file.write(claude(root,"user","Window "+QString::number(w)+" turn "+QString::number(t))); file.close();
        QString step="open";
        try { Store store(root); step="ingest"; store.ingest(path,"claude"); step="render"; if(t%10==9) store.render(); }
        catch(const std::exception &e) { ++failures; QMutexLocker guard(&lock); errors << step+": "+QString::fromUtf8(e.what()); }
      }
    }));
    auto poller=QtConcurrent::run([&] {
      while(!done) { QString step="poll-open"; try { Store store(root); step="poll-sync"; store.sync(folder,folder); } catch(const std::exception &e) { ++failures; QMutexLocker guard(&lock); errors << step+": "+QString::fromUtf8(e.what()); } QThread::msleep(20); }
    });
    for(auto &session:sessions) session.waitForFinished();
    done=true; poller.waitForFinished();
    QVERIFY2(failures==0,qPrintable(errors.join("; ")));
    Store store(root); store.sync(folder,folder);
    QCOMPARE(store.stats().value("events").toInt(),windows*turns);
    QCOMPARE(store.sessions().size(),windows);
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
  void functionIndexGroundsRelationshipsAndTracksChanges() {
    QTemporaryDir project;
    Store store(project.path(), true);
    Store::write(project.filePath("player.gd"), "func play():\n    tick()\n    FileAccess.open(\"README.md\")\n    player_state = 1\n");
    Store::write(project.filePath("engine.go"), "package engine\nfunc tick() {}\n");
    Store::write(project.filePath("Widget.tsx"), "export function Widget() { return null; }\n");
    Store::write(project.filePath("README.md"), "# Notes\n");
    store.indexProject();
    const auto plan = store.functionPlan();
    QCOMPARE(plan.size(), 3);
    for (const auto &entry : plan) {
      const auto file = entry.toObject();
      const QString path = file.value("path").toString();
      const auto chunk = store.functionChunk(file, 1);
      const int line = path == "engine.go" ? 2 : 1;
      QString name = "Widget";
      if (path == "player.gd") name = "play";
      if (path == "engine.go") name = "tick";
      QJsonObject record{{"name", name}, {"qualified", name}, {"line", line}, {"summary", "Describe this callable."}};
      if (name == "play") {
        record["calls"] = QJsonArray{QJsonObject{{"name", "tick"}, {"line", 2}},
          QJsonObject{{"name", "invented"}, {"line", 2}}};
        record["touches"] = QJsonArray{
          QJsonObject{{"name", "README.md"}, {"kind", "file"}, {"access", "reads"}, {"line", 3}, {"detail", "Opens the local readme."}},
          QJsonObject{{"name", "player_state"}, {"kind", "state"}, {"access", "writes"}, {"line", 4}, {"detail", "Sets the player state."}},
          QJsonObject{{"name", "player_state"}, {"kind", "state"}, {"access", "reads"}, {"line", 3}, {"detail", "Invented access."}}};
      }
      store.saveFunctions(file, store.checkedFunctions({{"functions", QJsonArray{record}}}, chunk));
    }
    QCOMPARE(store.indexedFunctionFiles(), 3);
    QVERIFY(store.functionPlan().isEmpty());
    const auto records = store.functions();
    QCOMPARE(records.size(), 3);
    for (const auto &value : records) {
      const auto record = value.toObject();
      if (record.value("name").toString() == "play") {
        QCOMPARE(record.value("calls").toArray().size(), 1);
        QCOMPARE(record.value("calls").toArray().first().toObject().value("path").toString(), QString("engine.go"));
        const auto touches = record.value("touches").toArray();
        QCOMPARE(touches.size(), 2);
        const auto file = touches.first().toObject();
        QCOMPARE(file.value("targetPath").toString(), QString("README.md"));
        QVERIFY(QFileInfo::exists(project.filePath(file.value("targetPage").toString())));
        QCOMPARE(touches.last().toObject().value("access").toString(), QString("writes"));
      }
      if (record.value("name").toString() == "tick") QCOMPARE(record.value("calledBy").toArray().size(), 1);
    }
    store.exportFunctions();
    store.render();
    QVERIFY(store.context().contains(".lore/functions/index.md"));
    QVERIFY(Store::read(project.filePath(".lore/functions/index.md")).contains("player.gd:1"));
    QVERIFY(Store::read(project.filePath(".lore/functions/index.md")).contains("writes state player_state at [player.gd:4]"));
    QVERIFY(store.briefing("Where is player_state written?", 5, false).value("text").toString().contains("Sets the player state."));
    const QString question = "Where is tick called?";
    const auto brief = store.briefing(question, 5, false, 1, 3600);
    const auto evidence = brief.value("evidence").toArray();
    QVERIFY(!evidence.isEmpty());
    const auto function = evidence.first().toObject();
    QCOMPARE(function.value("type").toString(), QString("function"));
    QCOMPARE(function.value("path").toString(), QString("engine.go"));
    QVERIFY(function.value("body").toString().contains("Candidate caller: play at player.gd:2"));
    QVERIFY(QFileInfo::exists(project.filePath(function.value("url").toString())));
    const QString id = function.value("id").toString();
    const QJsonObject answer{{"answer", "There is a candidate caller in player.gd [" + id + "]."},
      {"refs", QJsonArray{id}}, {"insufficient", false}};
    QCOMPARE(store.saveAnswer(answer, evidence, question).value("refs").toArray().size(), 1);
    const auto local = store.answerPrompt(question + QString(480, QChar(0x8a9e)), 4096);
    QVERIFY(local.value("prompt").toString().toUtf8().size() <= (4096 - Store::localAnswerTokens - 512) * 2);
    QCOMPARE(local.value("evidence").toArray().first().toObject().value("id").toString(), id);
    Store::write(project.filePath("player.gd"), "func play_again():\n    tick()\n");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, store.saveAnswer(answer, evidence, question));
    auto fresh = store.briefing(question, 5, false).value("evidence").toArray();
    for (const auto &value : fresh) QVERIFY(value.toObject().value("type") != "function");
    store.indexProject();
    QCOMPARE(store.functionPlan().size(), 1);
    QVERIFY(!Store::read(project.filePath(".lore/functions/index.md")).contains("player.gd:1"));
  }

  void transcriptThresholdAndForgettingHideDerivedMemory() {
    QTemporaryDir project, logs;
    Store store(project.path(), true);
    const QString path = logs.filePath("session.jsonl");
    Store::write(path, claude(project.path(), "user", "Sensitive project decision."));
    QCOMPARE(store.ingest(path, "claude"), 1);
    const QString source = store.sessions().first().toObject().value("id").toString();
    auto selected = store.transcriptPlan(source, false).first().toObject();
    auto chunk = store.transcriptChunk(selected, 0);
    const QString reference = chunk.value("events").toArray().first().toObject().value("id").toString();
    const auto finding = [&](const QString &title, int confidence) {
      return QJsonObject{{"title", title}, {"body", "Sensitive project decision."}, {"kind", "decision"},
        {"confidence", confidence}, {"refs", QJsonArray{reference}}};
    };
    store.saveTranscriptFindings(chunk, {{"entries", QJsonArray{finding("Review this first", 100)}}}, "fixture");
    QVERIFY(store.memories().isEmpty());
    QCOMPARE(store.transcriptFindings().size(), 1);
    QVERIFY(store.transcriptPlan(source, false).isEmpty());

    store.setSetting("transcriptAutoPublish", "on");
    store.setSetting("transcriptThreshold", "90");
    store.saveTranscriptFindings(chunk, {{"entries", QJsonArray{finding("High confidence", 90), finding("Below threshold", 89)}}}, "fixture");
    QCOMPARE(store.memories().size(), 1);
    QCOMPARE(store.transcriptFindings().size(), 2);
    QCOMPARE(store.reusePublishedTranscriptLearning(), 0);
    auto pending = store.transcriptFindings().first().toObject();
    store.reviewTranscriptFinding(pending.value("id").toString(), true);
    QCOMPARE(store.memories().size(), 2);
    const auto remaining = store.transcriptFindings().first().toObject().value("id").toString();
    store.reviewTranscriptFinding(remaining, false);
    QCOMPARE(store.reusePublishedTranscriptLearning(), 1);
    QCOMPARE(store.reusePublishedTranscriptLearning(), 0);
    QVERIFY(store.learningBatch().value("events").toArray().isEmpty());
    store.render();
    QVERIFY(QFileInfo::exists(project.filePath(".lore/sources/" + source + ".jsonl")));
    store.forgetConversations(source);
    QVERIFY(store.sessions().isEmpty());
    QVERIFY(store.search("Sensitive").isEmpty());
    QVERIFY(store.transcriptFindings().isEmpty());
    QVERIFY(store.memories().first().toObject().value("forgottenSource").toBool());
    QVERIFY(!QFileInfo::exists(project.filePath(".lore/wiki/1.md")));
    QVERIFY(!QFileInfo::exists(project.filePath(".lore/sources/" + source + ".jsonl")));
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(store.ingest(path, "claude"), 0);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, store.saveTranscriptFindings(chunk, {{"entries", QJsonArray{finding("Retry", 100)}}}, "fixture"));

    QCOMPARE(store.importConversation("User: Forget this pasted conversation."), 1);
    store.forgetConversations({});
    QVERIFY(store.sessions().isEmpty());
    QCOMPARE(store.importConversation("User: Forget this pasted conversation."), 0);
    QCOMPARE(store.stats().value("forgottenConversations").toInt(), 2);
    QCOMPARE(store.importConversation("User: Keep this unrelated conversation."), 1);
    const auto active = store.learningBatch().value("events").toArray();
    QCOMPARE(active.size(), 1);
    store.learn(entry(active.first().toObject().value("id").toString()), active, "fixture");
    store.deleteForgottenConversations();
    QCOMPARE(store.stats().value("forgottenConversations").toInt(), 0);
    QCOMPARE(store.stats().value("captureExclusions").toInt(), 2);
    QCOMPARE(store.memories().size(), 1);
    QVERIFY(!store.memories().first().toObject().value("forgottenSource").toBool());
    QCOMPARE(store.search("unrelated").size(), 1);
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(store.ingest(path, "claude"), 0);
    QCOMPARE(store.importConversation("User: Forget this pasted conversation."), 0);
    store.deleteForgottenConversations();
    QCOMPARE(store.sessions().size(), 1);
  }

  void wikiQueuesSkippedLearningByConversation() {
    QTemporaryDir project, logs;
    FakePicker picker;
    QString source;
    {
      Store store(project.path(), true);
      QCOMPARE(store.importConversation("User: Retry this failed message."), 1);
      source = store.sessions().first().toObject().value("id").toString();
      store.skipMessages(store.learningBatch().value("events").toArray());
      QCOMPARE(store.skippedLearning().size(), 1);
      QCOMPARE(store.retrySkippedLearning("unknown-source"), 0);
    }
    Backend backend(nullptr, &picker, logs.filePath("preferences.json"), {logs.path(), logs.path()});
    QVERIFY(backend.openProject(project.path()));
    QVERIFY(backend.connectProject());
    QTRY_VERIFY(backend.connected());
    backend.setWatching(false);
    QTRY_VERIFY(!backend.capturing());
    QCOMPARE(backend.stats().value("pending").toInt(), 0);
    Theme theme(project.path());
    Translator translator(project.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("initialProject", project.path());
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->show();
    window->setProperty("page", "wiki");
    auto *button = window->findChild<QQuickItem *>("retrySkippedLearning");
    QVERIFY(button);
    QTRY_VERIFY(button->isVisible() && button->isEnabled());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
      button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
    QTRY_COMPARE(backend.stats().value("pending").toInt(), 1);
    QVERIFY(!button->isVisible());
    Store store(project.path());
    store.skipMessages(store.learningBatch().value("events").toArray());
    store.forgetConversations(source);
    QVERIFY(store.skippedLearning().isEmpty());
    QCOMPARE(store.retrySkippedLearning(source), 0);
  }
  void modelJobsIndexCodeAndAnalyzeTranscript() {
    LocalModelFixture server;
    QVERIFY(server.isListening());
    const auto oldUrl = qgetenv("LORE_LLM_URL"), oldModel = qgetenv("LORE_LLM_MODEL");
    const auto restore = qScopeGuard([&] { qputenv("LORE_LLM_URL", oldUrl); qputenv("LORE_LLM_MODEL", oldModel); });
    qputenv("LORE_LLM_URL", server.url());
    qputenv("LORE_LLM_MODEL", "qwen3:8b");
    QTemporaryDir project, logs;
    QString eventId;
    {
      Store store(project.path(), true);
      Store::write(project.filePath("broken.py"), QByteArray("# A comment\n").repeated(12));
      Store::write(project.filePath("math.py"), "def total():\n    return 2\n");
      Store::write(project.filePath("ui/FunctionIndexPage.qml"), "import QtQuick\nItem { onVisibleChanged: console.log(visible) }\n");
      Store::write(project.filePath("ui/Main.qml"), "import QtQuick\nItem {\n  function openPage() { return true }\n}\n");
      Store::write(project.filePath("README.md"), "# Example\n");
      Store::write(logs.filePath("session.jsonl"), claude(project.path(), "user", "Keep the API stable."));
      store.ingest(logs.filePath("session.jsonl"), "claude");
      eventId = store.learningBatch().value("events").toArray().first().toObject().value("id").toString();
      store.render();
    }
    Backend backend(nullptr, nullptr, logs.filePath("preferences.json"), {logs.path(), logs.path()});
    QVERIFY(backend.openProject(project.path()));
    QVERIFY(backend.connectProject());
    QTRY_VERIFY(backend.connected());
    backend.setWatching(false);
    QTRY_VERIFY(!backend.capturing());
    backend.tools()->check();
    QTRY_VERIFY(backend.localReady("functions"));
    server.replies = {"invalid initial reply", R"({"functions":null})",
      R"({"functions":[{"name":"total","qualified":"math.total","line":1,"endLine":2,"summary":"Return the total.","calls":[]}]})",
      R"({"functions":[]})",
      R"({"functions":[{"name":"openPage","line":3,"endLine":3,"summary":"Return true.","calls":[],"touches":[]}]})"};
    backend.startFunctionIndex("local", false);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.knowledgeProgress().value("active").toBool(), 10000);
    QCOMPARE(backend.functions().size(), 2);
    QCOMPARE(backend.indexedFunctionFiles(), 3);
    QCOMPARE(backend.eligibleFunctionFiles(), 4);
    const auto schema = server.lastBody.value("format").toObject();
    QCOMPARE(schema.value("required").toArray(), QJsonArray{"functions"});
    const auto functions = schema.value("properties").toObject().value("functions").toObject();
    QCOMPARE(functions.value("type").toString(), QString("array"));
    const auto function = functions.value("items").toObject();
    QCOMPARE(function.value("required").toArray(), (QJsonArray{"name", "line", "summary"}));
    {
      Store saved(project.path());
      const auto remaining = saved.functionPlan(false);
      QCOMPARE(remaining.size(), 1);
      QCOMPARE(remaining.first().toObject().value("path").toString(), QString("broken.py"));
    }
    Theme theme(project.path());
    Translator translator(logs.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("initialProject", project.path());
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(1300, 1000);
    window->show();
    window->setProperty("page", "functionIndex");
    const auto click = [window](QQuickItem *item) {
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    };
    auto *issues = window->findChild<QQuickItem *>("viewFunctionScanIssues");
    QTRY_VERIFY(issues && issues->isVisible() && issues->width() > 0);
    click(issues);
    QTRY_COMPARE(window->property("page").toString(), QString("output"));
    auto *output = window->findChild<QQuickItem *>("modelOutputText");
    QVERIFY(output);
    QTRY_VERIFY(output->property("text").toString().contains("Skipped broken.py:"));
    QVERIFY(output->property("text").toString().contains("must contain a functions array"));
    QVERIFY(!output->property("text").toString().contains("Scanning functions"));
    auto *document = output->property("textDocument").value<QQuickTextDocument *>();
    QVERIFY(document);
    const auto skip = document->textDocument()->findBlockByNumber(0);
    QTRY_VERIFY(!skip.layout()->formats().isEmpty());
    QCOMPARE(skip.layout()->formats().first().format.foreground().color(), QColor(theme.property("caution").toString()));
    auto *filter = window->findChild<QQuickItem *>("terminalIssuesOnly");
    QVERIFY(filter && filter->property("checked").toBool());
    click(filter);
    QTRY_VERIFY(output->property("text").toString().contains("Scanning functions"));
    QVERIFY(output->property("text").toString().contains("Retrying broken.py"));
    const QString functionId = "function:" + backend.functions().first().toMap().value("id").toString();
    server.content = QJsonDocument(QJsonObject{{"answer", "See [" + functionId + "]."},
      {"refs", QJsonArray{functionId}}, {"insufficient", false}}).toJson(QJsonDocument::Compact);
    backend.setAnswerProvider("local");
    backend.tools()->setLocalContextTokens(4096);
    QSignalSpy answered(&backend, &Backend::learningFinished);
    backend.ask("Where is total() defined?");
    QTRY_COMPARE(answered.size(), 1);
    QVERIFY2(answered.first().first().toBool(), qPrintable(backend.status()));
    QCOMPARE(backend.answer().value("refs").toList().size(), 1);
    const auto options = server.lastBody.value("options").toObject();
    QCOMPARE(options.value("num_ctx").toInt(), 4096);
    QCOMPARE(options.value("num_predict").toInt(), Store::localAnswerTokens);

    server.content = QJsonDocument(QJsonObject{{"entries", QJsonArray{QJsonObject{{"title", "Stable API"}, {"kind", "decision"},
      {"body", "Keep the API stable."}, {"confidence", 95}, {"refs", QJsonArray{eventId}}}}}}).toJson(QJsonDocument::Compact);
    backend.startTranscriptAnalysis({}, "local", false);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.knowledgeProgress().value("active").toBool(), 10000);
    QCOMPARE(backend.transcriptFindings().size(), 1);
    QCOMPARE(server.chats, 7);
  }

  void codexFunctionScanRetriesTimeoutsAndKeepsGoing() {
    QTemporaryDir project, data, bin;
    {
      Store store(project.path(), true);
      Store::write(project.filePath("a.cpp"), QByteArray("// First file\n").repeated(48));
      Store::write(project.filePath("b.cpp"), "// Second file\n");
      Store::write(project.filePath("c.cpp"), "// Third file\n");
      store.render();
    }
    const auto old = qgetenv("LORE_TEST_FUNCTION_TIMEOUTS");
    qputenv("LORE_TEST_FUNCTION_TIMEOUTS", "1");
    const auto restore = qScopeGuard([&] {
      if (old.isNull()) qunsetenv("LORE_TEST_FUNCTION_TIMEOUTS");
      else qputenv("LORE_TEST_FUNCTION_TIMEOUTS", old);
    });
    Backend backend(nullptr, nullptr, data.filePath("preferences.json"), {data.path(), data.path()});
    QVERIFY(backend.openProject(project.path()));
    backend.setWatching(false);
    backend.tools()->setExecutable("codex", fakeAgent(bin, "codex"));
    QTRY_VERIFY(backend.tools()->ready("codex"));
    auto *deadline = backend.findChild<QTimer *>("modelCallTimeout");
    QVERIFY(deadline);
    const auto scanning = [&](const QString &path) {
      return backend.knowledgeProgress().value("current").toString() == path && deadline->isActive();
    };

    backend.startFunctionIndex("codex", false);
    QTRY_VERIFY(scanning("a.cpp"));
    QCOMPARE(deadline->interval(), 600000);
    deadline->start(20);
    QTRY_VERIFY(scanning("b.cpp"));
    QVERIFY(backend.liveOutput().contains("Retrying a.cpp at line 1"));
    QCOMPARE(deadline->interval(), 600000);
    deadline->start(20);
    QTRY_VERIFY(!backend.knowledgeProgress().value("active").toBool());
    QCOMPARE(backend.indexedFunctionFiles(), 2);
    QCOMPARE(backend.liveIssues().size(), 1);
    QVERIFY(backend.liveOutput().contains("Skipped b.cpp: The model timed out"));
    QVERIFY(!backend.liveOutput().contains("cancelled"));

    backend.startFunctionIndex("codex", false);
    QTRY_VERIFY(scanning("b.cpp"));
    backend.cancel();
    QTRY_VERIFY(!backend.knowledgeProgress().value("active").toBool());
    QCOMPARE(backend.indexedFunctionFiles(), 2);
    QCOMPARE(backend.liveIssues().size(), 1);
    QVERIFY(backend.liveOutput().contains("cancelled"));
  }

  void themeAndTranslationFollowFiles() {
    QTemporaryDir p;
    Theme theme(p.path()); QCOMPARE(theme.accent(),QString("#f2c35b"));
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
  void reviewsRewriteOnlyChangedExports() {
    QTemporaryDir p,logs; Store store(p.path(),true); const QString path=logs.filePath("s.jsonl");
    Store::write(path,claude(p.path(),"user","Use SQLite for the database.")); store.ingest(path,"claude");
    const auto batch=store.learningBatch().value("events").toArray(); store.learn(entry(batch.first().toObject().value("id").toString()),batch,"fake");
    const QString id=store.sessions().first().toObject().value("id").toString();
    const QString raw=p.filePath(".lore/sources/"+id+".jsonl"), page=p.filePath(".lore/sources/"+id+".md");
    const QByteArray original=Store::read(raw).toUtf8(); QVERIFY(original.contains("Use SQLite"));
    const QDateTime old=QDateTime::currentDateTimeUtc().addDays(-3);
    { QFile f(raw); QVERIFY(f.open(QIODevice::Append)); QVERIFY(f.setFileTime(old,QFileDevice::FileModificationTime)); }
    store.reviewMemory(1,"confirmed");
    QCOMPARE(QFileInfo(raw).lastModified().toSecsSinceEpoch(),old.toSecsSinceEpoch());
    QVERIFY(Store::read(p.filePath(".lore/wiki/1.md")).contains("Review: confirmed"));
    QVERIFY(Store::read(p.filePath(".lore/wiki/1.md")).contains("../sources/"+id+".md#"));
    QVERIFY(QFile::remove(page)); store.render(); QVERIFY(Store::read(page).contains("Use SQLite"));
    QCOMPARE(Store::read(raw).toUtf8(),original);
    Store::write(path,claude(p.path(),"user","Use SQLite for the database.")+claude(p.path(),"user","Add a backup command.")); store.ingest(path,"claude"); store.render();
    QVERIFY(Store::read(raw).contains("Add a backup command."));
  }
  void localModelStuckInALoopIsStopped() {
    LocalModelFixture server(true); QVERIFY(server.isListening());
    const auto oldUrl=qgetenv("LORE_LLM_URL"), oldModel=qgetenv("LORE_LLM_MODEL"); const auto restore=qScopeGuard([&] { qputenv("LORE_LLM_URL",oldUrl); qputenv("LORE_LLM_MODEL",oldModel); });
    qputenv("LORE_LLM_URL",server.url()); qunsetenv("LORE_LLM_MODEL");
    QTemporaryDir p,logs,prefs;
    { Store store(p.path(),true); Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Use SQLite for local storage.")); store.ingest(logs.filePath("s.jsonl"),"claude"); }
    Backend backend(nullptr,nullptr,prefs.filePath("prefs.json"),{logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); backend.setWatching(false);
    backend.tools()->check(); QTRY_VERIFY(!backend.tools()->checking()); backend.tools()->setLocalModel("qwen3:8b"); QVERIFY(backend.tools()->ready("local")); backend.setLearningProvider("local");
    QByteArray loop; for(int i=0;i<60;++i) loop+="Also we saw QVERIFY(Store::read(page).contains(\"Review: confirmed\")) in the test. ";
    server.content=loop;
    backend.learn("local");
    // Each call is restarted fresh up to three times; the round is retried once;
    // then the message is skipped so an unattended build keeps going.
    QTRY_VERIFY_WITH_TIMEOUT(!backend.buildProgress().value("active").toBool() && backend.stats().value("pending").toInt()==0,30000);
    QCOMPARE(server.chats,8);
    QVERIFY(backend.liveOutput().contains("kept repeating")); QVERIFY(backend.liveOutput().contains("attempt 3 of 3"));
    QVERIFY(server.lastBody.value("messages").toArray().first().toObject().value("content").toString().contains("Start fresh"));
    QVERIFY(server.lastBody.value("options").toObject().value("temperature").toDouble()>0.5);  // Warmer, to leave the groove.
    QCOMPARE(backend.stats().value("memories").toInt(),0);
    bool skipped=false; for(const auto &a:backend.activity()) if(a.toMap().value("body").toString().contains("Skipped 1 messages")) skipped=true; QVERIFY(skipped);
    // The request caps the answer and discourages repetition.
    const auto options=server.lastBody.value("options").toObject(); QVERIFY(options.value("num_predict").toInt()>0); QVERIFY(options.value("repeat_penalty").toDouble()>1.0);
  }
  void localModelBuildsWikiWithoutAPlan() {
    QVERIFY(!Toolbox::loopback("http://192.168.1.5:11434").isValid()); QVERIFY(!Toolbox::loopback("https://127.0.0.1:11434").isValid()); QVERIFY(Toolbox::loopback("http://localhost:1234").isValid());
    const auto oldUrl=qgetenv("LORE_LLM_URL"), oldModel=qgetenv("LORE_LLM_MODEL"); const auto restore=qScopeGuard([&] { qputenv("LORE_LLM_URL",oldUrl); qputenv("LORE_LLM_MODEL",oldModel); }); qunsetenv("LORE_LLM_MODEL");
    for(const bool ollama:{true}) {  // Local models run on Ollama only.
      LocalModelFixture server(ollama); QVERIFY(server.isListening()); qputenv("LORE_LLM_URL",server.url());
      QTemporaryDir p,logs,prefs; QString ref;
      { Store store(p.path(),true); Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Use SQLite for local storage.")); store.ingest(logs.filePath("s.jsonl"),"claude"); ref=store.learningBatch().value("events").toArray().first().toObject().value("id").toString(); }
      Backend backend(nullptr,nullptr,prefs.filePath("prefs.json"),{logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); backend.setWatching(false);
      backend.tools()->check(); QTRY_VERIFY(!backend.tools()->checking());
      QVERIFY(backend.tools()->available("local")); QVERIFY(!backend.tools()->ready("local"));
      QVariantMap local; for(const auto &e:backend.tools()->entries()) if(e.toMap().value("id")=="local") local=e.toMap();
      const QString model=ollama ? "qwen3:8b" : "local-model";
      QCOMPARE(local.value("models").toStringList(),QStringList{model});  // Embedding models are not offered for chat.
      backend.tools()->setLocalModel(model); QVERIFY(backend.tools()->ready("local"));
      const QString answering=backend.answerProvider(); QVERIFY(answering!="local"); backend.setLearningProvider("local"); QCOMPARE(backend.answerProvider(),answering);
      server.content="<think>Sorting the message.</think>\nHere is the JSON:\n```json\n"+QJsonDocument(entry(ref)).toJson(QJsonDocument::Compact)+"\n```";
      QSignalSpy done(&backend,&Backend::learningFinished); backend.learn("local");
      QTRY_COMPARE(done.count(),1); QVERIFY2(done.first().first().toBool(),qPrintable(backend.status()));
      QCOMPARE(backend.memories().first().toMap().value("provider").toString(),"local:"+model);
      QCOMPARE(backend.stats().value("pending").toInt(),0);
      QCOMPARE(server.lastBody.value("model").toString(),model);
      if(ollama) { QVERIFY2(backend.liveOutput().contains("Database choice"),qPrintable(backend.liveOutput()));  // The model's own words reach the Model output page.
        QVERIFY(backend.usage().value("session").toMap().value("local").toMap().value("input").toLongLong()>=1200);
        QCOMPARE(server.lastPath,QString("/api/chat")); QVERIFY(server.lastBody.value("format").toObject().value("required").toArray().contains("entries")); const int context=server.lastBody.value("options").toObject().value("num_ctx").toInt(); QVERIFY(context>=8192 && context*3>server.lastBody.value("messages").toArray().first().toObject().value("content").toString().size()); }
      Backend reopened(nullptr,nullptr,prefs.filePath("prefs.json"),{logs.path(),logs.path()});
      QCOMPARE(reopened.tools()->localModel(),model); QCOMPARE(reopened.learningProvider(),QString("local")); QCOMPARE(reopened.answerProvider(),answering);
    }
  }
  void backendReviewsRunInOrderWithoutBlocking() {
    QTemporaryDir p,logs;
    { Store store(p.path(),true); const QString path=logs.filePath("s.jsonl"); Store::write(path,claude(p.path(),"user","Use SQLite for the database.")); store.ingest(path,"claude");
      const auto batch=store.learningBatch().value("events").toArray(); store.learn(entry(batch.first().toObject().value("id").toString()),batch,"fake"); }
    Backend backend(nullptr,nullptr,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); backend.setWatching(false); QTRY_VERIFY(!backend.capturing());
    QSignalSpy sessions(&backend,&Backend::sessionsChanged), memories(&backend,&Backend::memoriesChanged);
    backend.reviewMemory(1,"outdated"); backend.reviewMemory(1,"confirmed");
    QCOMPARE(backend.memories().first().toMap().value("review").toString(),QString("unreviewed"));
    QTRY_COMPARE(backend.memories().first().toMap().value("review").toString(),QString("confirmed")); QTRY_VERIFY(!backend.busy());
    QVERIFY(Store::read(p.filePath(".lore/wiki/1.md")).contains("Review: confirmed"));
    QVERIFY(memories.count()>=1); QCOMPARE(sessions.count(),0);
  }
  void idleCaptureTicksNotifyNothing() {
    QTemporaryDir p,logs;
    { Store store(p.path(),true); const QString path=logs.filePath("s.jsonl"); Store::write(path,claude(p.path(),"user","Use SQLite for the database.")); store.ingest(path,"claude");
      const auto batch=store.learningBatch().value("events").toArray(); store.learn(entry(batch.first().toObject().value("id").toString()),batch,"fake"); }
    Backend backend(nullptr,nullptr,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); backend.setWatching(false); QTRY_VERIFY(!backend.capturing());
    backend.sync(); QTRY_VERIFY(!backend.capturing());
    QSignalSpy stats(&backend,&Backend::statsChanged), memories(&backend,&Backend::memoriesChanged), sessions(&backend,&Backend::sessionsChanged), brain(&backend,&Backend::brainChanged), project(&backend,&Backend::changed);
    for(int i=0;i<3;++i) { backend.sync(); QTRY_VERIFY(!backend.capturing()); }
    QCOMPARE(stats.count(),0); QCOMPARE(memories.count(),0); QCOMPARE(sessions.count(),0); QCOMPARE(brain.count(),0); QCOMPARE(project.count(),0);
    // A record saved by another process (a hook) still reaches the window.
    { Store other(p.path()); const QString path=logs.filePath("t.jsonl"); Store::write(path,claude(p.path(),"user","Also keep a changelog.")); other.ingest(path,"claude"); }
    backend.sync(); QTRY_VERIFY(!backend.capturing());
    QVERIFY(stats.count()>=1); QTRY_COMPARE(backend.sessions().size(),2);
    QCOMPARE(backend.nextSteps().size(), int(std::count_if(backend.memories().cbegin(),backend.memories().cend(),[](const QVariant &m) { return m.toMap().value("kind").toString()=="next"; })));
  }
  void migrationAddsIndexesAndConvertsVectors() {
    QTemporaryDir p; { Store store(p.path(),true); }
    const QString name="index-check";
    { auto db=QSqlDatabase::addDatabase("QSQLITE",name); db.setDatabaseName(p.filePath(".lore/brain.sqlite")); QVERIFY(db.open()); QSqlQuery q(db);
      QVERIFY(q.exec("SELECT name FROM sqlite_master WHERE type='index'")); QStringList names; while(q.next()) names << q.value(0).toString();
      for(const auto &index:{"events_source","events_pending","memory_conflicts_b"}) QVERIFY2(names.contains(index),index);
      QVERIFY(q.exec("PRAGMA user_version")); q.next(); QVERIFY(q.value(0).toInt()>=9);
      QVERIFY(q.exec("PRAGMA table_info(embeddings)")); QStringList columns; while(q.next()) columns << q.value(1).toString(); QVERIFY(columns.contains("data"));
      db.close(); }
    QSqlDatabase::removeDatabase(name);
  }
  void verificationChecksOnlyFilesAndNeverChangesReview() {
    QTemporaryDir p,logs; Store store(p.path(),true);
    Store::write(p.filePath("src/session.cpp"),"// Sessions are stored locally in SQLite.\nclass LocalSessionStore {};\n");
    Store::write(p.filePath("docs/notes.md"),"Unrelated release checklist.\n");
    store.indexProject();
    Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Sessions are stored locally in SQLite via src/session.cpp")); store.ingest(logs.filePath("s.jsonl"),"claude");
    const auto batch=store.learningBatch().value("events").toArray(); auto learned=entry(batch.first().toObject().value("id").toString());
    auto item=learned["entries"].toArray().first().toObject(); item["title"]="Sessions stored locally"; item["body"]="Sessions are stored locally in SQLite."; item["files"]=QJsonArray{"src/session.cpp"}; learned["entries"]=QJsonArray{item};
    store.learn(learned,batch,"fake");
    const auto prompt=store.verifyPrompt(1); const auto evidence=prompt.value("evidence").toArray();
    QVERIFY(!evidence.isEmpty());
    for(const auto &v:evidence) QCOMPARE(v.toObject().value("type").toString(),QString("file"));
    QCOMPARE(evidence.first().toObject().value("path").toString(),QString("src/session.cpp"));
    QVERIFY(!prompt.value("prompt").toString().contains("[event:")); QVERIFY(!prompt.value("prompt").toString().contains("[memory:"));
    const QString ref=evidence.first().toObject().value("id").toString();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.saveVerification(1,QJsonObject{{"verdict","true"},{"answer","x ["+ref+"]"},{"refs",QJsonArray{ref}}},evidence,"fake"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.saveVerification(1,QJsonObject{{"verdict","supported"},{"answer","No citations"},{"refs",QJsonArray{}}},evidence,"fake"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.saveVerification(1,QJsonObject{{"verdict","supported"},{"answer","Invented [file:invented]"},{"refs",QJsonArray{"file:invented"}}},evidence,"fake"));
    store.saveVerification(1,QJsonObject{{"verdict","unclear"},{"answer","The excerpts do not mention it."},{"refs",QJsonArray{}}},evidence,"fake");
    QCOMPARE(store.memories().first().toObject().value("verification").toObject().value("verdict").toString(),QString("unclear"));
    store.saveVerification(1,QJsonObject{{"verdict","supported"},{"answer","The Quartz marker identifies local storage ["+ref+"]"},{"refs",QJsonArray{ref}}},evidence,"fake");
    auto memory=store.memories().first().toObject(); auto check=memory.value("verification").toObject();
    QCOMPARE(check.value("verdict").toString(),QString("supported")); QVERIFY(check.value("current").toBool());
    QCOMPARE(memory.value("review").toString(),QString("unreviewed"));
    const QString page=Store::read(p.filePath(".lore/wiki/1.md")); QVERIFY(page.contains("## Code check")); QVERIFY(page.contains("../project/")); QVERIFY(page.contains("not proof"));
    const auto learnedReview = store.learningBatch().value("prompt").toString();
    QVERIFY(learnedReview.contains("Current model checks from wiki review")); QVERIFY(learnedReview.contains("Quartz marker"));
    const auto recalledCheck = store.briefing("Quartz marker", 6, false).value("evidence").toArray();
    QCOMPARE(recalledCheck.size(), 1);
    QCOMPARE(recalledCheck.first().toObject().value("id").toString(), QString("verification:1"));
    QCOMPARE(recalledCheck.first().toObject().value("origin").toString(), QString("model interpretation of cited evidence"));
    const QJsonObject cited{{"answer", "The check mentions a Quartz marker [verification:1]."}, {"refs", QJsonArray{"verification:1"}}, {"insufficient", false}};
    QVERIFY(!store.saveAnswer(cited, recalledCheck, "What did the check report?").isEmpty());
    Store::write(p.filePath("src/session.cpp"),"class RemoteSessionStore {};\n");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, store.saveAnswer(cited, recalledCheck, "What did the check report?"));
    store.indexProject();
    QVERIFY(!store.memories().first().toObject().value("verification").toObject().value("current").toBool());
    QVERIFY(Store::read(p.filePath(".lore/wiki/1.md")).contains("cited evidence changed since this check"));
    QVERIFY(store.briefing("Quartz marker", 6, false).value("evidence").toArray().isEmpty());
    QVERIFY(!store.learningBatch().value("prompt").toString().contains("Quartz marker"));

    const auto newer = store.verifyPrompt(1).value("evidence").toArray();
    const QString newerRef = newer.first().toObject().value("id").toString();
    store.saveVerification(1, {{"verdict", "contradicted"}, {"answer", "The Sapphire configuration is remote [" + newerRef + "]"}, {"refs", QJsonArray{newerRef}}}, newer, "fake");
    QVERIFY(store.briefing("Sessions stored locally", 10, false).value("text").toString().contains("contested by a model check"));
    store.reviewMemory(1, "outdated");
    QVERIFY(store.briefing("Sapphire configuration", 6, false).value("text").toString().contains("Model verdict: contradicted"));
    store.reviewMemory(1, "forgotten");
    QVERIFY(store.briefing("Sapphire configuration", 6, false).value("evidence").toArray().isEmpty());
    QVERIFY(!store.learningBatch().value("prompt").toString().contains("Sapphire configuration"));
  }
  void verifyChecksAgainstNewerFactsAndVerifyAllWorksThroughTheQueue() {
    QTemporaryDir p,logs,tools;
    const QString executable=tools.filePath(
#ifdef Q_OS_WIN
      "claude.exe"
#else
      "claude"
#endif
    );
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),executable)); QFile::setPermissions(executable,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    {
      Store store(p.path(),true); const QString transcript=logs.filePath("s.jsonl"); QByteArray records;
      const auto teach=[&](const QString &said,const QJsonArray &entries) {
        records+=claude(p.path(),"user",said); Store::write(transcript,records); store.ingest(transcript,"claude");
        const auto batch=store.learningBatch().value("events").toArray(); QJsonArray cited;
        for(auto v:entries) { auto e=v.toObject(); e["refs"]=QJsonArray{batch.first().toObject().value("id").toString()}; cited.append(e); }
        store.learn(QJsonObject{{"entries",cited}},batch,"fake");
      };
      teach("Plan backups.",QJsonArray{QJsonObject{{"title","Add a backup command"},{"kind","next"},{"body","Add lore backup to copy the archive."}}});
      QTest::qWait(1100);  // Newer means recorded later.
      teach("Backups shipped.",QJsonArray{QJsonObject{{"title","Backup command shipped"},{"kind","fact"},{"body","The lore backup command copies the archive."}},QJsonObject{{"title","Unrelated"},{"kind","fact"},{"body","Colours are green."}}});
      const auto prompt=store.verifyPrompt(1); const QString text=prompt.value("prompt").toString();
      QVERIFY(text.contains("[memory:2]")); QVERIFY(text.contains("newer project memory")); QVERIFY(!text.contains("[memory:3]"));  // Only related, newer facts.
      QVERIFY(!store.verifyPrompt(2).value("prompt").toString().contains("[memory:1]"));  // Older memories never check newer ones.
      store.saveVerification(1,QJsonObject{{"verdict","contradicted"},{"answer","It shipped [memory:2]"},{"refs",QJsonArray{"memory:2"}}},prompt.value("evidence").toArray(),"fake");
      QCOMPARE(store.reviewQueue(),(QJsonArray{2,3}));  // Checked or reviewed entries leave the queue.
      store.reviewMemory(3,"confirmed"); QCOMPARE(store.reviewQueue(),(QJsonArray{2}));
      Store::write(p.filePath("src/backup.cpp"),"// The backup command copies the archive.\n"); store.indexProject(); store.render();
    }
    Backend backend(nullptr,nullptr,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); backend.setWatching(false);
    backend.tools()->setExecutable("claude",executable); QTRY_VERIFY(!backend.tools()->checking()); QVERIFY(backend.tools()->ready("claude"));
    backend.setVerifyProvider("claude"); QCOMPARE(backend.reviewWaiting(),1);
    backend.verifyAll(); QVERIFY(backend.verifyProgress().value("active").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.verifyProgress().value("active").toBool(),15000);
    QVERIFY2(backend.status().contains("Agent review finished: 1 of 1"),qPrintable(backend.status()));
    QCOMPARE(backend.reviewWaiting(),0);
    QCOMPARE(backend.memories().first().toMap().value("review").toString().isEmpty(),false);
    backend.verifyAll(); QVERIFY(backend.status().contains("No memories are ready for verification"));
  }
  void verificationWaitsForNewEvidenceAcrossRestarts() {
    QTemporaryDir p, logs;
    const QString transcript = logs.filePath("s.jsonl");
    const QByteArray records = claude(p.path(), "user", "Consider zebra stripes.");
    {
      Store store(p.path(), true);
      Store::write(transcript, records);
      store.ingest(transcript, "claude");
      const auto batch = store.learningBatch().value("events").toArray();
      const auto ref = batch.first().toObject().value("id");
      const QJsonObject claim{{"title", "Zebra stripes"}, {"kind", "question"}, {"body", "Compare zebra stripes."}, {"refs", QJsonArray{ref}}};
      store.learn({{"entries", QJsonArray{claim}}}, batch, "fake");
      QCOMPARE(store.reviewQueue(), QJsonArray{1});
      QVERIFY(store.prepareVerification(1, true).value("evidence").toArray().isEmpty());
      QVERIFY(store.reviewQueue().isEmpty());
      QVERIFY(!store.memories().first().toObject().contains("verification"));
    }
    Store reopened(p.path());
    QVERIFY(reopened.reviewQueue().isEmpty());
    QVERIFY(reopened.prepareVerification(1, true).value("skipped").toBool());
    Store::write(transcript, records + claude(p.path(), "user", "Anything else?"));
    reopened.ingest(transcript, "claude");
    QVERIFY(reopened.reviewQueue().isEmpty());
    Store::write(p.filePath("animals.md"), "Zebra stripes form unique patterns.\n");
    reopened.indexProject();
    QCOMPARE(reopened.reviewQueue(), QJsonArray{1});
    QVERIFY(!reopened.prepareVerification(1, true).value("evidence").toArray().isEmpty());
  }
  void acceptedCheckClearsTheWikiWarningUntilEvidenceChanges() {
    QTemporaryDir p, logs;
    Store store(p.path(), true);
    Store::write(p.filePath("notes.md"), "Use local files.\n");
    store.indexProject();
    const QString transcript = logs.filePath("s.jsonl");
    Store::write(transcript, claude(p.path(), "user", "Check notes.md for local storage."));
    store.ingest(transcript, "claude");
    const auto batch = store.learningBatch().value("events").toArray();
    store.learn(entry(batch.first().toObject().value("id").toString()), batch, "fake");
    Store::write(p.filePath("notes.md"), "Use SQLite for local storage.\n");
    store.indexProject();
    QVERIFY(store.memories().first().toObject().value("needsChecking").toBool());
    store.setAutoReview(90);
    const auto evidence = store.verifyPrompt(1).value("evidence").toArray();
    QVERIFY(!evidence.isEmpty());
    const QString ref = evidence.first().toObject().value("id").toString();
    store.saveVerification(1, {{"verdict", "supported"}, {"answer", "SQLite is used [" + ref + "]"}, {"refs", QJsonArray{ref}}, {"confidence", 95}}, evidence, "fake");
    const auto memory = store.memories().first().toObject();
    QCOMPARE(memory.value("review").toString(), QString("accepted"));
    QVERIFY(!memory.value("needsChecking").toBool());
    QCOMPARE(store.stats().value("stale").toInt(), 0);
    QVERIFY(!Store::read(p.filePath(".lore/wiki/1.md")).contains("Needs checking"));
    QVERIFY(store.reviewQueue().isEmpty());
    Store::write(p.filePath("notes.md"), "Local storage uses a different format.\n");
    store.indexProject();
    QVERIFY(store.memories().first().toObject().value("needsChecking").toBool());
  }
  void modelSettlesEntriesAboveTheConfidenceThreshold() {
    QTemporaryDir p,logs; try { Store store(p.path(),true);
    Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Use SQLite. Maybe add sync later. Colours are green.")); store.ingest(logs.filePath("s.jsonl"),"claude");
    const auto batch=store.learningBatch(); const QString ref=batch.value("events").toArray().first().toObject().value("id").toString();
    QVERIFY(batch.value("prompt").toString().contains("confidence (0-100)"));
    store.setAutoReview(80); QCOMPARE(store.autoReview(),80);
    store.learn(QJsonObject{{"entries",QJsonArray{
      QJsonObject{{"title","Use SQLite"},{"kind","decision"},{"body","SQLite stores memory."},{"refs",QJsonArray{ref}},{"confidence",92}},
      QJsonObject{{"title","Sync later"},{"kind","question"},{"body","Maybe sync."},{"refs",QJsonArray{ref}},{"confidence",50}},
      QJsonObject{{"title","Green"},{"kind","fact"},{"body","Colours are green."},{"refs",QJsonArray{ref}}}}}},batch.value("events").toArray(),"fake");
    const auto byTitle=[&](const QString &t) { for(const auto &v:store.memories()) if(v.toObject().value("title").toString()==t) return v.toObject(); return QJsonObject(); };
    QCOMPARE(byTitle("Use SQLite").value("review").toString(),QString("accepted")); QVERIFY(byTitle("Use SQLite").value("byModel").toBool()); QCOMPARE(byTitle("Use SQLite").value("confidence").toInt(),92);
    QCOMPARE(byTitle("Sync later").value("review").toString(),QString("unreviewed")); QCOMPARE(byTitle("Green").value("confidence").toInt(),-1);
    QCOMPARE(store.reviewQueue().size(),2);  // Accepted entries leave the queue.
    // A sure contradiction from Verify marks an entry outdated; the user's own decision is never changed.
    const int green=byTitle("Green").value("id").toInt(), sync=byTitle("Sync later").value("id").toInt();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.saveVerification(green,QJsonObject{{"verdict","contradicted"},{"answer","x"},{"refs",QJsonArray{}},{"confidence",99}},QJsonArray(),"fake"));  // Still needs a citation.
    QCOMPARE(byTitle("Green").value("review").toString(),QString("unreviewed"));
    Store::write(p.filePath("src/colours.cpp"),"// Colours are blue, never green.\n"); store.indexProject();
    const auto check=store.verifyPrompt(green); QVERIFY(check.value("prompt").toString().contains("confidence (0-100)"));
    const QString file=check.value("evidence").toArray().first().toObject().value("id").toString();
    store.saveVerification(green,QJsonObject{{"verdict","contradicted"},{"answer","They are blue ["+file+"]"},{"refs",QJsonArray{file}},{"confidence",95}},check.value("evidence").toArray(),"fake");
    QCOMPARE(byTitle("Green").value("review").toString(),QString("outdated")); QVERIFY(byTitle("Green").value("byModel").toBool()); QCOMPARE(byTitle("Green").value("confidenceSource").toString(),QString("check"));
    store.reviewMemory(sync,"confirmed"); store.saveVerification(sync,QJsonObject{{"verdict","unclear"},{"answer","Unclear."},{"refs",QJsonArray{}},{"confidence",99}},QJsonArray(),"fake");
    QCOMPARE(byTitle("Sync later").value("review").toString(),QString("confirmed"));
    // Reopen undoes the model's decision; a lower threshold settles entries already rated.
    store.reviewMemory(byTitle("Use SQLite").value("id").toInt(),"reopen"); QCOMPARE(byTitle("Use SQLite").value("review").toString(),QString("unreviewed"));
    store.setAutoReview(95); QCOMPARE(byTitle("Use SQLite").value("review").toString(),QString("unreviewed"));
    store.setAutoReview(90); QCOMPARE(byTitle("Use SQLite").value("review").toString(),QString("accepted"));
    store.reviewMemory(sync,"reopen"); store.setAutoReview(0); QCOMPARE(byTitle("Sync later").value("review").toString(),QString("unreviewed"));  // Off settles nothing.
    // The user's reviews: calibration against the model's confidence, and lessons for the next build.
    store.reviewMemory(byTitle("Use SQLite").value("id").toInt(),"confirmed"); store.reviewMemory(sync,"outdated");
    const auto levels=store.stats().value("calibration").toObject().value("levels").toArray();
    QCOMPARE(store.stats().value("calibration").toObject().value("judged").toInt(),2);
    QCOMPARE(levels[0].toObject().value("total").toInt(),2); QCOMPARE(levels[0].toObject().value("agreed").toInt(),1);  // At 50%+: one of two.
    QCOMPARE(levels[4].toObject().value("total").toInt(),1); QCOMPARE(levels[4].toObject().value("agreed").toInt(),1);  // At 90%+: the one kept.
    Store::write(logs.filePath("s.jsonl"),Store::read(logs.filePath("s.jsonl")).toUtf8()+claude(p.path(),"user","More.")); store.ingest(logs.filePath("s.jsonl"),"claude");
    const QString next=store.learningBatch().value("prompt").toString();
    QVERIFY(next.contains("Kept (confirmed by the user):\n- decision: Use SQLite")); QVERIFY(next.contains("Rejected (marked outdated by the user):\n- question: Sync later"));
    QVERIFY(!next.contains("- fact: Green"));  // The model's own decisions are not lessons.
    } catch(const std::exception &e) { QFAIL(e.what()); }
  }
  void promptPresetsTravelWithBuildsAndChecks() {
    QTemporaryDir p, logs, prefs;
    const QString preferences = prefs.filePath("preferences.json");
    {
      Store store(p.path(), true);
      Store::write(logs.filePath("s.jsonl"), claude(p.path(), "user", "Use SQLite."));
      store.ingest(logs.filePath("s.jsonl"), "claude");
      const auto batch = store.learningBatch().value("events").toArray();
      store.learn(entry(batch.first().toObject().value("id").toString()), batch, "fake");
      const QString stock = Store::promptPresets().first().toObject().value("text").toString();
      store.setSetting("buildInstructions", stock);
      store.setSetting("verifyInstructions", "An obsolete custom instruction.");
      QCOMPARE(store.promptPresetId("build"), QString("t-decisions"));
      QVERIFY(!store.verifyPrompt(1).value("prompt").toString().contains("obsolete"));
    }
    {
      Backend backend(nullptr, nullptr, preferences, {logs.path(), logs.path()});
      QVERIFY(backend.openProject(p.path()));
      backend.setWatching(false);
      QCOMPARE(backend.buildPreset(), QString("t-decisions"));
      QVERIFY(backend.verifyPreset().isEmpty());
      backend.setBuildPreset("t-security");
      backend.setVerifyPreset("v-code-truth");
      backend.setVerifyPreset("t-security");
      QCOMPARE(backend.verifyPreset(), QString("v-code-truth"));
      Store store(p.path());
      const QString build = store.learningBatch().value("prompt").toString();
      QVERIFY(build.contains("Lore wiki build preset"));
      QVERIFY(build.contains("Never copy a secret value"));
      QVERIFY(!build.contains("Treat the source files as the final word"));
      const QString check = store.verifyPrompt(1).value("prompt").toString();
      QVERIFY(check.contains("Lore verification preset"));
      QVERIFY(check.contains("Treat the source files as the final word"));
      QVERIFY(!check.contains("obsolete"));
      QVERIFY(!check.contains("Never copy a secret value"));
      QVERIFY_THROWS_EXCEPTION(std::runtime_error, store.setPromptPreset("build", "c-legacy"));

      const int commands = backend.quickCommands().size();
      QVERIFY(!backend.saveQuickCommand({}, "Lint", "Run the linter and fix warnings.").isEmpty());
      QCOMPARE(backend.quickCommands().size(), commands + 1);
      backend.setQuickAgent("codex");
    }
    Backend again(nullptr, nullptr, preferences, {logs.path(), logs.path()});
    QTRY_VERIFY(!again.project().isEmpty());
    QCOMPARE(again.buildPreset(), QString("t-security"));
    QCOMPARE(again.verifyPreset(), QString("v-code-truth"));
    QCOMPARE(again.quickAgent(), QString("codex"));
    again.setBuildPreset({});
    again.setVerifyPreset({});
    Store store(p.path());
    QVERIFY(store.promptPresetId("build").isEmpty());
    QVERIFY(store.promptPresetId("verify").isEmpty());
    QVERIFY(!store.learningBatch().value("prompt").toString().contains("Lore wiki build preset"));
    QVERIFY(!store.verifyPrompt(1).value("prompt").toString().contains("Lore verification preset"));
  }
  void idleVerificationRunsOnItsOwnTriggersAndModel() {
    QTemporaryDir p,logs,tools;
    const QString executable=tools.filePath(
#ifdef Q_OS_WIN
      "claude.exe"
#else
      "claude"
#endif
    );
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),executable)); QFile::setPermissions(executable,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    {
      Store store(p.path(),true); Store::write(p.filePath("src/session.cpp"),"// Sessions are stored locally in SQLite.\n"); store.indexProject();
      Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Sessions are stored locally in SQLite.")); store.ingest(logs.filePath("s.jsonl"),"claude");
      const auto batch=store.learningBatch().value("events").toArray(); const QString ref=batch.first().toObject().value("id").toString();
      store.learn(QJsonObject{{"entries",QJsonArray{QJsonObject{{"title","Sessions stored locally"},{"kind","fact"},{"body","Sessions are stored locally in SQLite."},{"refs",QJsonArray{ref}}},
        QJsonObject{{"title","Session store in SQLite"},{"kind","decision"},{"body","Keep the session store in SQLite."},{"refs",QJsonArray{ref}}}}}},batch,"fake");
    }
    qputenv("LORE_AUTO_CHECK_MS","150"); const auto unset=qScopeGuard([] { qunsetenv("LORE_AUTO_CHECK_MS"); });
    Backend backend(nullptr,nullptr,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); backend.setWatching(false);
    backend.tools()->setExecutable("claude",executable); QTRY_VERIFY(!backend.tools()->checking()); QVERIFY(backend.tools()->ready("claude"));
    backend.setVerifyProvider("codex"); backend.setAutoVerifyProvider("claude"); backend.setAutoVerifyAfter(3); backend.setAutoVerifyDelay(2);
    backend.setAutoVerify(true); QTest::qWait(600);
    QCOMPARE(backend.reviewWaiting(),2); QVERIFY(backend.autoVerifyStatus().contains("when 3 entries"));  // Not due yet.
    backend.setAutoVerifyAfter(2);  // Due now: after Lore stays idle for the delay, checks run with the automatic model, not Verify's.
    QTRY_COMPARE_WITH_TIMEOUT(backend.countdown().value("kind").toString(),QString("verify"),5000);
    QVERIFY2(backend.status().contains("Automatic verification starts in"),qPrintable(backend.status()));
    QCOMPARE(backend.reviewWaiting(),2);  // Nothing ran yet.
    QTRY_COMPARE_WITH_TIMEOUT(backend.reviewWaiting(),0,20000);
    QTRY_VERIFY(!backend.verifyProgress().value("active").toBool());
    QVERIFY2(backend.status().contains("Automatic verification finished: 2 of 2"),qPrintable(backend.status()));
    QCOMPARE(backend.verifyProvider(),QString("codex"));
  }
  void backendVerifiesThroughProviderWithoutModelCallWhenNoFilesMatch() {
    QTemporaryDir p,logs,tools;
    const QString executable=tools.filePath(
#ifdef Q_OS_WIN
      "claude.exe"
#else
      "claude"
#endif
    );
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),executable)); QFile::setPermissions(executable,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    { Store store(p.path(),true); Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Sessions are stored locally in SQLite.")); store.ingest(logs.filePath("s.jsonl"),"claude");
      const auto batch=store.learningBatch().value("events").toArray(); auto learned=entry(batch.first().toObject().value("id").toString());
      auto item=learned["entries"].toArray().first().toObject(); item["title"]="Sessions stored locally"; item["body"]="Sessions are stored locally in SQLite."; learned["entries"]=QJsonArray{item};
      store.learn(learned,batch,"fake"); store.render(); }
    Backend backend(nullptr,nullptr,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); backend.setWatching(false); backend.setLearningProvider("claude"); backend.tools()->setExecutable("claude",executable); QTRY_VERIFY(!backend.tools()->checking()); QVERIFY(backend.tools()->ready("claude"));
    backend.setVerifyProvider("claude");
    QSignalSpy finished(&backend,&Backend::learningFinished);
    backend.verifyMemory(1); QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000); QVERIFY(finished.first().first().toBool());
    QVERIFY(!backend.memories().first().toMap().contains("verification")); QVERIFY(backend.status().contains("No model call"));
    Store::write(p.filePath("src/session.cpp"),"// Sessions are stored locally in SQLite.\nclass LocalSessionStore {};\n");
    backend.indexProject(); QTRY_VERIFY(!backend.busy());
    backend.verifyMemory(1); QTRY_COMPARE_WITH_TIMEOUT(finished.size(),2,10000); QVERIFY2(finished.last().first().toBool(),qPrintable(backend.status()));
    const auto check=backend.memories().first().toMap().value("verification").toMap();
    QCOMPARE(check.value("verdict").toString(),QString("supported")); QCOMPARE(backend.memories().first().toMap().value("review").toString(),QString("unreviewed"));
    // Model and effort reach Claude Code as arguments, and only safe values are kept.
    QTemporaryDir argsDir; qputenv("LORE_TEST_ARGS_FILE",argsDir.filePath("args.txt").toUtf8()); const auto unsetArgs=qScopeGuard([] { qunsetenv("LORE_TEST_ARGS_FILE"); });
    backend.tools()->setAgentModel("claude","opus"); backend.tools()->setAgentEffort("claude","high");
    backend.tools()->setAgentModel("claude","opus --dangerously-skip-permissions"); backend.tools()->setAgentEffort("claude","extreme");
    QCOMPARE(backend.tools()->agentModel("claude"),QString("opus")); QCOMPARE(backend.tools()->agentEffort("claude"),QString("high"));
    backend.tools()->setAgentModel("codex","gpt-test"); backend.tools()->setAgentEffort("codex","high");
    QCOMPARE(backend.tools()->agentArguments("codex"),(QStringList{"-m","gpt-test","-c","model_reasoning_effort=\"high\""}));
    backend.verifyMemory(1); QTRY_COMPARE_WITH_TIMEOUT(finished.size(),3,10000);
    QVERIFY2(Store::read(argsDir.filePath("args.txt")).contains("--model opus --effort high"),qPrintable(Store::read(argsDir.filePath("args.txt"))));
    QVERIFY(backend.liveOutput().contains("claude:opus (high effort)"));
    backend.setModelChoice("verify","claude","model","sonnet");
    backend.setModelChoice("verify","claude","effort","low");
    backend.verifyMemory(1); QTRY_COMPARE_WITH_TIMEOUT(finished.size(),4,10000);
    QVERIFY(backend.liveOutput().contains("claude:sonnet (low effort)"));
    QCOMPARE(backend.modelChoice("ask","claude").value("model").toString(),QString("opus"));
    // The Model output page logs each call: what ran, with which model, and how it ended.
    QVERIFY(backend.liveOutput().contains("Verifying memory 1 · claude")); QVERIFY(backend.liveOutput().contains("finished"));
    QVERIFY(backend.liveOutput().contains("The session module stores sessions locally"));
    // Token use as Claude Code reports it: input includes cache reads, with the context window.
    const auto claudeUse=backend.usage().value("session").toMap().value("claude").toMap();
    QCOMPARE(claudeUse.value("lastInput").toLongLong(),1000LL); QCOMPARE(claudeUse.value("lastOutput").toLongLong(),50LL); QCOMPARE(claudeUse.value("calls").toLongLong(),3LL); QCOMPARE(claudeUse.value("window").toLongLong(),200000LL);
    QVERIFY2(backend.liveOutput().contains("tokens: "+QLocale().toString(1000)+" in"),qPrintable(backend.liveOutput().right(300)));
    QVERIFY(backend.usage().value("session").toMap().value("claude").toMap().value("lastTokensPerSecond").toDouble() > 0);
  }
  void separateWorkingDirectoryKeepsSourceClean() {
    QTemporaryDir source,working,logs,other;
    Store::write(source.filePath("src/auth.cpp"),"class SessionManager {};\n");
    { Store store(working.path(),true);
      QVERIFY_THROWS_EXCEPTION(std::runtime_error,store.setSource(working.filePath("missing")));
      store.setSource(source.path()); QVERIFY(store.separate()); QCOMPARE(store.source(),QFileInfo(source.path()).canonicalFilePath());
      QCOMPARE(store.indexProject().value("files").toInt(),1);
      Store::write(logs.filePath("s.jsonl"),claude(source.path(),"user","Keep authentication in src/auth.cpp")); QCOMPARE(store.ingest(logs.filePath("s.jsonl"),"claude"),1);
      Store::write(logs.filePath("w.jsonl"),claude(working.path(),"user","A session in Lore's own folder")); QCOMPARE(store.ingest(logs.filePath("w.jsonl"),"claude"),0);
      store.connectAgents(QCoreApplication::applicationFilePath()); store.render();
      QCOMPARE(store.stats().value("name").toString(),QFileInfo(source.path()).fileName()); }
    QVERIFY(!QFileInfo::exists(source.filePath(".lore"))); QVERIFY(!QFileInfo::exists(source.filePath("BRAIN.md")));
    QVERIFY(QFileInfo::exists(working.filePath("BRAIN.md"))); QVERIFY(QFileInfo::exists(working.filePath(".lore/project/index.md")));
    QVERIFY(!QFileInfo::exists(working.filePath("AGENTS.md"))); QVERIFY(!QFileInfo::exists(working.filePath(".claude")));
    const QString pointer=Store::read(source.filePath("AGENTS.md")); QVERIFY(pointer.contains("Read BRAIN.md")); QVERIFY(pointer.contains(QDir::toNativeSeparators(QFileInfo(working.path()).canonicalFilePath()+"/BRAIN.md")));
    QVERIFY(!Store::read(source.filePath(".gitignore")).contains("/.lore/"));
    const auto hook=QJsonDocument::fromJson(Store::read(source.filePath(".claude/settings.local.json")).toUtf8()).object().value("hooks").toObject().value("SessionStart").toArray().first().toObject().value("hooks").toArray().first().toObject();
    QCOMPARE(hook.value("args").toArray().last().toString(),QFileInfo(working.path()).canonicalFilePath());
    QVERIFY(Store::read(working.filePath("BRAIN.md")).contains("--project \""));
    { Store reopened(working.path()); QCOMPARE(reopened.source(),QFileInfo(source.path()).canonicalFilePath()); QCOMPARE(reopened.projectFiles().size(),1);
      QVERIFY_THROWS_EXCEPTION(std::runtime_error,reopened.setSource(working.path()+"/..")); }
    Backend backend(nullptr,nullptr,{}, {logs.path(),logs.path()});
    QVERIFY(backend.selectProject(other.path())); QVERIFY(!backend.setWorkingDirectory(other.filePath("inside")));
    QVERIFY(backend.setWorkingDirectory(other.path())); QCOMPARE(backend.workingDirectory(),QFileInfo(other.path()).canonicalFilePath());
    QVERIFY(backend.connectProject()); QVERIFY(backend.connected()); QTRY_VERIFY(!backend.capturing()); backend.setWatching(false);
    QVERIFY(QFileInfo::exists(other.filePath(".lore/brain.sqlite"))); QCOMPARE(backend.source(),backend.project());
  }
  void settingsMoveLoreFilesAndChangeTheSourceFolder() {
    QTemporaryDir p,w,q,logs; Store::write(p.filePath("src/auth.cpp"),"class SessionManager {};\n"); Store::write(q.filePath("src/other.cpp"),"class Other {};\n");
    Store::write(p.filePath("CLAUDE.md"),"# Team rules\nKeep these.\n");
    { Store store(p.path(),true); Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Keep authentication in src/auth.cpp")); store.ingest(logs.filePath("s.jsonl"),"claude");
      store.indexProject(); const auto batch=store.learningBatch().value("events").toArray(); store.learn(entry(batch.first().toObject().value("id").toString()),batch,"fake"); }
    Backend backend(nullptr,nullptr,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); QVERIFY(backend.connectProject()); QTRY_VERIFY(!backend.capturing()); backend.setWatching(false);
    const QString source=QFileInfo(p.path()).canonicalFilePath(), working=QFileInfo(w.path()).canonicalFilePath();
    QVERIFY(!backend.changeWorkingDirectory(p.filePath("src")));
    QVERIFY(backend.changeWorkingDirectory(w.path()));
    QCOMPARE(backend.project(),working); QCOMPARE(backend.source(),source); QVERIFY(backend.connected()); QCOMPARE(backend.memories().size(),1);
    QVERIFY(!QFileInfo::exists(p.filePath(".lore"))); QVERIFY(!QFileInfo::exists(p.filePath("BRAIN.md"))); QVERIFY(QFileInfo::exists(w.filePath("BRAIN.md")));
    QVERIFY(Store::read(p.filePath("CLAUDE.md")).contains("Keep these.")); QVERIFY(Store::read(p.filePath("CLAUDE.md")).contains(QDir::toNativeSeparators(working+"/BRAIN.md")));
    QVERIFY(!Store::read(p.filePath(".gitignore")).contains("/.lore/"));
    const auto hookArgs=[](const QString &folder) { return QJsonDocument::fromJson(Store::read(folder+"/.claude/settings.local.json").toUtf8()).object().value("hooks").toObject().value("SessionStart").toArray().last().toObject().value("hooks").toArray().first().toObject().value("args").toArray(); };
    QCOMPARE(hookArgs(p.path()).last().toString(),working);
    QVERIFY(backend.changeSource(q.path())); QCOMPARE(backend.source(),QFileInfo(q.path()).canonicalFilePath()); QVERIFY(backend.connected()); QTRY_VERIFY(!backend.busy());
    QVERIFY(Store::read(p.filePath("CLAUDE.md")).contains("Keep these.")); QVERIFY(!Store::read(p.filePath("CLAUDE.md")).contains("BRAIN.md"));
    QVERIFY(!QFileInfo::exists(p.filePath("AGENTS.md"))); QVERIFY(!QFileInfo::exists(p.filePath(".claude/settings.local.json")));
    QCOMPARE(hookArgs(q.path()).last().toString(),working);
    QTRY_VERIFY(backend.memories().first().toMap().value("stale").toBool());
    QVERIFY(!backend.changeSource(w.path()));
    QVERIFY(backend.changeWorkingDirectory(q.path())); QCOMPARE(backend.project(),QFileInfo(q.path()).canonicalFilePath()); QCOMPARE(backend.source(),backend.project());
    QVERIFY(QFileInfo::exists(q.filePath(".lore/brain.sqlite"))); QVERIFY(!QFileInfo::exists(w.filePath(".lore"))); QCOMPARE(backend.memories().size(),1);
    QVERIFY(Store::read(q.filePath("CLAUDE.md")).contains("Read BRAIN.md before project work"));
  }
  void settingsPageConfirmsBeforeMovingFiles() {
    QTemporaryDir p,w,logs; FakePicker picker;
    { Store store(p.path(),true); store.render(); }
    Backend backend(nullptr,&picker,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); QVERIFY(backend.connectProject()); QTRY_VERIFY(!backend.capturing()); backend.setWatching(false);
    Theme theme(p.path()); Translator translator(p.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator); engine.rootContext()->setContextProperty("initialProject",p.path());
    engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->show(); QTest::qWait(100);
    std::function<QQuickItem *(QQuickItem *,const QString &)> find=[&](QQuickItem *parent,const QString &name) -> QQuickItem * { if(parent->objectName()==name) return parent; for(auto *child:parent->childItems()) if(auto *found=find(child,name)) return found; return nullptr; };
    auto *nav=find(window->contentItem(),"nav_settings"); QVERIFY(nav);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,nav->mapToScene(QPointF(nav->width()/2,nav->height()/2)).toPoint());
    QCOMPARE(window->property("page").toString(),QString("settings"));
    auto *sourceLabel=find(window->contentItem(),"settingsSourcePath"); QVERIFY(sourceLabel); QTRY_VERIFY(sourceLabel->isVisible());
    QCOMPARE(QDir::fromNativeSeparators(sourceLabel->property("text").toString()),QFileInfo(p.path()).canonicalFilePath());
    auto *change=find(window->contentItem(),"settingsChangeWorking"); QVERIFY(change);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,change->mapToScene(QPointF(change->width()/2,change->height()/2)).toPoint());
    QVERIFY(picker.opened); picker.pick(w.path());
    auto *dialog=engine.rootObjects().first()->findChild<QObject *>("settingsConfirm"); QVERIFY(dialog); QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(backend.project(),QFileInfo(p.path()).canonicalFilePath());
    QVERIFY(QMetaObject::invokeMethod(dialog,"accept"));
    QTRY_COMPARE(backend.project(),QFileInfo(w.path()).canonicalFilePath());
    auto *workingLabel=find(window->contentItem(),"settingsWorkingPath"); QVERIFY(workingLabel);
    QTRY_COMPARE(QDir::fromNativeSeparators(workingLabel->property("text").toString()),QFileInfo(w.path()).canonicalFilePath());
  }
  void projectBrowserRemembersFoldersAndFindsProjects() {
    QTemporaryDir inProject,separateSource,unused,logs,prefs;
    const auto canon=[](const QString &p) { return QFileInfo(p).canonicalFilePath(); };
    { Store store(inProject.path(),true); store.render(); }
    const QString working=Store::defaultWorkingDirectory(separateSource.path()); QVERIFY(QDir().mkpath(working));
    { Store store(working,true); store.setSource(separateSource.path()); store.render(); }
    for(const auto &folder:{separateSource.path(),unused.path()}) Store::write(logs.filePath("claude/"+QFileInfo(folder).fileName()+"/s.jsonl"),claude(folder,"user","hello"));
    const QString preferences=prefs.filePath("prefs.json");
    {
      Backend backend(nullptr,nullptr,preferences,{logs.filePath("codex"),logs.filePath("claude")}); QVERIFY(backend.openProject(inProject.path())); backend.setWatching(false);
      const auto find=[&](const QString &key,const QString &path) { for(const auto &v:key=="path"?backend.suggestedProjects():backend.knownProjects()) if(canon(v.toMap().value(key).toString())==canon(path)) return v.toMap(); return QVariantMap(); };
      auto here=find("working",inProject.path()); QVERIFY(!here.isEmpty()); QVERIFY(here.value("together").toBool()); QVERIFY(here.value("current").toBool()); QVERIFY(!here.value("lastOpened").toString().isEmpty());
      backend.scanProjects(); QTRY_VERIFY(!backend.scanningProjects());
      auto found=find("working",working); QVERIFY(!found.isEmpty()); QCOMPARE(canon(found.value("source").toString()),canon(separateSource.path())); QVERIFY(!found.value("together").toBool());
      QVERIFY(!find("path",unused.path()).isEmpty()); QCOMPARE(find("path",unused.path()).value("tools").toString(),QString("Claude Code"));
      QVERIFY(find("path",separateSource.path()).isEmpty());  // Already remembered through its working directory.
      QVERIFY(backend.openKnownProject(working)); QCOMPARE(backend.project(),canon(working)); QCOMPARE(backend.source(),canon(separateSource.path()));
      backend.forgetProject(working); QVERIFY(!find("working",working).isEmpty());  // The open project stays listed.
      backend.forgetProject(canon(inProject.path())); QVERIFY(find("working",inProject.path()).isEmpty());
      backend.forgetProject(canon(unused.path())); backend.scanProjects(); QTRY_VERIFY(!backend.scanningProjects());
      QVERIFY(find("path",unused.path()).isEmpty()); QVERIFY(find("working",inProject.path()).isEmpty());
    }
    const auto saved=QJsonDocument::fromJson(Store::read(preferences).toUtf8()).object().value("projects").toObject().value(canon(working)).toObject();
    QCOMPARE(canon(saved.value("source").toString()),canon(separateSource.path()));
    Backend reopened(nullptr,nullptr,preferences,{logs.filePath("codex"),logs.filePath("claude")});
    QCOMPARE(reopened.project(),canon(working)); QCOMPARE(reopened.source(),canon(separateSource.path()));
    bool listed=false; for(const auto &v:reopened.knownProjects()) { if(canon(v.toMap().value("working").toString())==canon(working)) listed=true; QVERIFY(canon(v.toMap().value("working").toString())!=canon(inProject.path())); }
    QVERIFY(listed);
  }
  void projectsPageOpensARememberedProject() {
    QTemporaryDir first,second,logs,prefs;
    for(const auto &folder:{first.path(),second.path()}) { Store store(folder,true); store.render(); }
    Backend backend(nullptr,nullptr,prefs.filePath("prefs.json"),{logs.filePath("codex"),logs.filePath("claude")});
    // Connected projects: the guide stays closed and the sidebar shows.
    QVERIFY(backend.openProject(second.path())); QVERIFY(backend.connectProject()); QTRY_VERIFY(!backend.capturing());
    QVERIFY(backend.openProject(first.path())); QVERIFY(backend.connectProject()); QTRY_VERIFY(!backend.capturing()); backend.setWatching(false);
    Theme theme(first.path()); Translator translator(first.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator); engine.rootContext()->setContextProperty("initialProject",first.path());
    engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->resize(1200,900); window->show(); QTest::qWait(100);
    std::function<QQuickItem *(QQuickItem *,const QString &)> find=[&](QQuickItem *parent,const QString &name) -> QQuickItem * { if(parent->objectName()==name && parent->isVisible()) return parent; for(auto *child:parent->childItems()) if(auto *found=find(child,name)) return found; return nullptr; };
    const auto click=[&](QQuickItem *item) { QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint()); };
    QQuickItem *all=nullptr; QTRY_VERIFY((all=find(window->contentItem(),"switchProjectButton"))); click(all);
    QTRY_COMPARE(window->property("page").toString(),QString("projects"));
    QQuickItem *open=nullptr; QTRY_VERIFY((open=find(window->contentItem(),"openProject_"+QFileInfo(second.path()).fileName())));
    QVERIFY(!find(window->contentItem(),"openProject_"+QFileInfo(first.path()).fileName()));  // The open project has no Open button.
    click(open);
    QTRY_COMPARE(backend.project(),QFileInfo(second.path()).canonicalFilePath()); QTRY_COMPARE(window->property("page").toString(),QString("home"));
  }
  void automationBuildsTheWikiOnItsOwnTriggersAndModel() {
    LocalModelFixture server; QVERIFY(server.isListening());
    const QList<QByteArray> names{"LORE_LLM_URL","LORE_LLM_MODEL","LORE_AUTO_QUIET_MS","LORE_AUTO_CHECK_MS"}; QList<QByteArray> old; for(const auto &n:names) old << qgetenv(n.constData());
    const auto restore=qScopeGuard([&] { for(int i=0;i<names.size();++i) qputenv(names[i].constData(),old[i]); });
    qputenv("LORE_LLM_URL",server.url()); qputenv("LORE_LLM_MODEL","qwen3:8b"); qputenv("LORE_AUTO_QUIET_MS","50"); qputenv("LORE_AUTO_CHECK_MS","50");
    QTemporaryDir p,logs,prefs; const QString transcript=logs.filePath("claude/project/s.jsonl"), preferences=prefs.filePath("prefs.json");
    { Store store(p.path(),true); store.render(); }
    QByteArray records=claude(p.path(),"user","First message.");
    Store::write(transcript,records);
    {
      Backend backend(nullptr,nullptr,preferences,{logs.filePath("codex"),logs.filePath("claude")}); QVERIFY(backend.openProject(p.path())); QVERIFY(backend.connectProject()); QTRY_VERIFY(!backend.capturing());
      backend.tools()->check(); QTRY_VERIFY(!backend.tools()->checking()); QVERIFY(backend.tools()->ready("local"));
      backend.setLearningProvider("codex"); backend.setAutoProvider("local"); backend.setAutoAfter(3); backend.setAutoEvery(0); backend.setAutoBuildDelay(0);
      backend.sync(); QTRY_VERIFY(!backend.capturing()); QCOMPARE(backend.stats().value("pending").toInt(),1);
      backend.setAutoLearning(true); QVERIFY(backend.autoStatus().contains("3"));
      QTest::qWait(400); QCOMPARE(server.chats,0); QCOMPARE(backend.stats().value("pending").toInt(),1);  // Below the threshold.
      for(const char *text:{"Second message.","Third message.","Fourth message."}) records+=claude(p.path(),"user",text);
      Store::write(transcript,records); backend.sync();
      QTRY_COMPARE_WITH_TIMEOUT(backend.stats().value("pending").toInt(),0,10000);  // Drains everything once triggered.
      QVERIFY(server.chats>=1); QVERIFY(!backend.memories().isEmpty());
      for(const auto &m:backend.memories()) QCOMPARE(m.toMap().value("provider").toString(),QString("local:qwen3:8b"));
      QCOMPARE(backend.learningProvider(),QString("codex"));  // Build wiki keeps its own choice.
      backend.setAutoAfter(0); backend.setAutoEvery(2);
      records+=claude(p.path(),"user","After the build."); Store::write(transcript,records); backend.sync(); QTRY_VERIFY(!backend.capturing());
      const int chats=server.chats; QTest::qWait(400); QCOMPARE(server.chats,chats); QCOMPARE(backend.stats().value("pending").toInt(),1);  // Not due for two hours.
    }
    // Two hours later: the schedule is due, so reopening Lore builds again.
    auto saved=QJsonDocument::fromJson(Store::read(preferences).toUtf8()).object(); auto projects=saved.value("projects").toObject();
    const QString key=QFileInfo(p.path()).canonicalFilePath(); auto options=projects.value(key).toObject();
    QCOMPARE(options.value("autoProvider").toString(),QString("local")); QCOMPARE(options.value("autoEvery").toInt(),2);
    options["provider"]="local"; options["lastAutoBuild"]=QDateTime::currentDateTimeUtc().addSecs(-3*3600).toString(Qt::ISODate); projects[key]=options; saved["projects"]=projects;
    Store::write(preferences,QJsonDocument(saved).toJson());
    Backend reopened(nullptr,nullptr,preferences,{logs.filePath("codex"),logs.filePath("claude")});
    QVERIFY(reopened.autoLearning()); QCOMPARE(reopened.autoProvider(),QString("local")); QCOMPARE(reopened.autoEvery(),2);
    reopened.tools()->check();
    QTRY_COMPARE_WITH_TIMEOUT(reopened.stats().value("pending").toInt(),0,10000);
  }
  void learningSendsWhatWasSaidNotToolOutput() {
    QTemporaryDir p,logs; Store store(p.path(),true);
    const auto record=[&](const QString &type,const QJsonValue &content,bool meta=false) { QJsonObject o{{"type",type},{"cwd",p.path()},{"timestamp","2026-11-18T10:00:00Z"},{"message",QJsonObject{{"role",type},{"content",content}}}}; if(meta) o["isMeta"]=true; return json(o); };
    QByteArray records;
    records+=record("user","<local-command-caveat>Caveat: run locally.</local-command-caveat>\n<command-name>/clear</command-name>");
    records+=record("user","<system-reminder>Hook context.</system-reminder>",true);
    records+=record("user","Add a backup command.<system-reminder>Injected.</system-reminder>");
    records+=record("assistant",QJsonArray{QJsonObject{{"type","thinking"},{"thinking","hmm"}},QJsonObject{{"type","text"},{"text","I'll add it."}},QJsonObject{{"type","tool_use"},{"name","PowerShell"},{"input",QJsonObject{{"command","git status"}}}}});
    records+=record("user",QJsonArray{QJsonObject{{"type","tool_result"},{"content","On branch main\nTotals: 9 passed"}}});
    records+=record("assistant",QJsonArray{QJsonObject{{"type","tool_use"},{"name","Read"},{"input",QJsonObject{{"file_path","x"}}}}});
    Store::write(logs.filePath("s.jsonl"),records); store.ingest(logs.filePath("s.jsonl"),"claude");
    QCOMPARE(store.sessions().first().toObject().value("title").toString(),QString("Add a backup command."));  // Not the command banner.
    const auto batch=store.learningBatch(); const auto events=batch.value("events").toArray();
    QCOMPARE(events.size(),2);
    QCOMPARE(events[0].toObject().value("text").toString(),QString("Add a backup command."));
    QCOMPARE(events[1].toObject().value("text").toString(),QString("I'll add it."));
    const QString prompt=batch.value("prompt").toString();
    QVERIFY(!prompt.contains("git status")); QVERIFY(!prompt.contains("Totals")); QVERIFY(!prompt.contains("Hook context")); QVERIFY(!prompt.contains("hmm"));
    store.learn(entry(events[0].toObject().value("id").toString()),events,"fake");
    QCOMPARE(store.stats().value("pending").toInt(),0);  // Tool records were set aside, not left waiting.
    QVERIFY(store.search("branch").isEmpty());
    QCOMPARE(store.stats().value("events").toInt(), 2);
    // A conversation an earlier build titled after its banner is renamed on sync.
    { QSqlQuery q(QSqlDatabase::addDatabase("QSQLITE","titles")); QSqlDatabase db=QSqlDatabase::database("titles"); db.setDatabaseName(p.filePath(".lore/brain.sqlite")); QVERIFY(db.open()); QSqlQuery u(db); QVERIFY(u.exec("UPDATE sources SET title='<local-command-caveat>Caveat'")); db.close(); } QSqlDatabase::removeDatabase("titles");
    store.sync(logs.filePath("none"),logs.filePath("none")); QCOMPARE(store.sessions().first().toObject().value("title").toString(),QString("Add a backup command."));
  }
  void localModelsAreFoundOnEveryServerAndBackedUp() {
    QTemporaryDir p, logs, bin;
    LocalModelFixture ollama(true), other(true), openai(false);
    QVERIFY(ollama.isListening()); QVERIFY(other.isListening()); QVERIFY(openai.isListening());
    other.models = {"gemma4:26b"};
    const QString argsLog = logs.filePath("args.txt");
    const auto oldUrl = qgetenv("LORE_LLM_URL"), oldUrls = qgetenv("LORE_LLM_URLS"), oldModel = qgetenv("LORE_LLM_MODEL"), oldArgs = qgetenv("LORE_TEST_ARGS_FILE");
    const auto restore = qScopeGuard([&] { qputenv("LORE_LLM_URL", oldUrl); qputenv("LORE_LLM_URLS", oldUrls); qputenv("LORE_LLM_MODEL", oldModel); qputenv("LORE_TEST_ARGS_FILE", oldArgs); });
    qputenv("LORE_LLM_URL", ollama.url()); qputenv("LORE_LLM_URLS", other.url() + "," + openai.url()); qputenv("LORE_LLM_MODEL", "qwen3:8b"); qputenv("LORE_TEST_ARGS_FILE", argsLog.toUtf8());
    const QString executable = fakeAgent(bin, "claude");
    const auto oldBackup = qgetenv("LORE_LOCAL_BACKUP"); qputenv("LORE_LOCAL_BACKUP", "auto");
    const auto restoreBackup = qScopeGuard([&] { qputenv("LORE_LOCAL_BACKUP", oldBackup); });
    {
      Store store(p.path(), true);
      const QString transcript = logs.filePath("a.jsonl");
      Store::write(transcript, claude(p.path(), "user", "Use SQLite for local storage, session a."));
      QCOMPARE(store.ingest(transcript, "claude"), 1);
    }
    Backend backend(nullptr, nullptr, logs.filePath("prefs.json"), {logs.filePath("codex"), logs.filePath("claude")});
    QVERIFY(backend.openProject(p.path())); backend.setWatching(false);
    backend.tools()->setExecutable("claude", executable);
    QTRY_VERIFY(backend.tools()->ready("claude") && !backend.tools()->checking());
    // Both Ollama servers' models are found, each where it runs; a server
    // without Ollama's API is not used.
    QVariantMap local; for (const auto &e : backend.tools()->entries()) if (e.toMap().value("id") == "local") local = e.toMap();
    QCOMPARE(local.value("models").toStringList(), (QStringList{"qwen3:8b", "gemma4:26b"}));
    QCOMPARE(local.value("servers").toList().size(), 2);
    QCOMPARE(backend.tools()->chatServer("gemma4:26b"), QString::fromUtf8(other.url()));
    QVERIFY(backend.tools()->chatServer("local-model").isEmpty());
    QCOMPARE(ollama.chats + other.chats + openai.chats, 0);  // Looking starts no generation.
    // A task's model runs on the server that has it.
    backend.setModelChoice("build", "local", "model", "gemma4:26b");
    backend.setLearningProvider("local");
    QSignalSpy finished(&backend, &Backend::learningFinished);
    backend.learn("local");
    QTRY_VERIFY(finished.size() >= 1);
    QCOMPARE(other.chats, 1); QCOMPARE(ollama.chats, 0); QCOMPARE(other.lastPath, QString("/api/chat"));
    // The server stops: the call continues with the backup, Claude Code here.
    other.close();
    QTRY_VERIFY(!backend.busy());
    { Store store(p.path()); const QString transcript = logs.filePath("b.jsonl");
      Store::write(transcript, claude(p.path(), "user", "Keep the index next to the database, session b."));
      QCOMPARE(store.ingest(transcript, "claude"), 1); }
    const int before = int(finished.size());
    backend.learn("local");
    QTRY_VERIFY_WITH_TIMEOUT(finished.size() > before, 20000);
    QVERIFY2(backend.liveOutput().contains("continuing with Claude Code") || backend.liveOutput().contains("using Claude Code as the backup"), qPrintable(backend.liveOutput().right(800)));
    QVERIFY(QFileInfo::exists(argsLog));
    QTRY_COMPARE(backend.stats().value("pending").toInt(), 0);
    // Without a backup, a local model that is not running is not ready.
    backend.tools()->rescanLocal(); QTRY_VERIFY(!backend.tools()->checking());
    QVERIFY(!backend.localReady("build"));
    QVERIFY(backend.modelReady("build", "local"));
    backend.tools()->setLocalBackup("none");
    QVERIFY(!backend.modelReady("build", "local"));
    QVERIFY(backend.modelReady("ask", "local"));  // Its own model, qwen3:8b on Ollama, still runs.
  }
  void wikiPageSearchesAndScrolls() {
    QTemporaryDir p,logs;
    const QString prefs = logs.filePath("preferences.json");
    {
      Store store(p.path(),true); Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","Many topics.")); store.ingest(logs.filePath("s.jsonl"),"claude");
      const auto batch=store.learningBatch().value("events").toArray(); const QString ref=batch.first().toObject().value("id").toString(); QJsonArray entries;
      store.setAutoReview(90);
      for (int i = 0; i < 24; ++i) {
        entries.append(QJsonObject{{"title", QString("Topic %1").arg(i)}, {"kind", i%2 ? "fact" : "decision"},
          {"body", i == 7 ? "The backup lives in Documents." : "Ordinary detail."}, {"refs", QJsonArray{ref}}, {"confidence", i == 7 || i == 8 ? 95 : 60}});
      }
      store.learn(QJsonObject{{"entries",entries}},batch,"fake");
      Store::write(p.filePath("guide.md"), "# Project guide\n");
      store.indexProject();
      store.importConversation("User: An earlier discussion that is still searchable.");
    }
    Backend backend(nullptr,nullptr,prefs, {logs.filePath("codex"),logs.filePath("claude")}); QVERIFY(backend.openProject(p.path())); QVERIFY(backend.connectProject()); QTRY_VERIFY(!backend.capturing()); backend.setWatching(false);
    Theme theme(p.path()); Translator translator(p.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator); engine.rootContext()->setContextProperty("initialProject",p.path());
    engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->resize(1200,800); window->show(); window->setProperty("page","wiki"); QTest::qWait(150);
    std::function<QQuickItem *(QQuickItem *,const QString &)> find=[&](QQuickItem *parent,const QString &name) -> QQuickItem * { if(parent->objectName()==name && parent->isVisible()) return parent; for(auto *child:parent->childItems()) if(auto *found=find(child,name)) return found; return nullptr; };
    QQuickItem *list=nullptr; QTRY_VERIFY((list=find(window->contentItem(),"wikiList"))); QTRY_COMPARE(list->property("count").toInt(),24);
    auto *bar=find(window->contentItem(),"wikiScrollBar"); QVERIFY(bar); QVERIFY(bar->property("size").toDouble()<1.0);  // More cards than fit.
    window->setProperty("page","wiki"); QTest::qWait(50);
    // The help page names the running version.
    window->setProperty("helpVisible",true); QQuickItem *version=nullptr; QTRY_VERIFY((version=find(window->contentItem(),"appVersion")));
    QVERIFY(version->property("text").toString().startsWith("Lore ")); window->setProperty("helpVisible",false);
    const auto click = [&](const QString &name) {
      auto *item = find(window->contentItem(), name);
      if (!item || !item->isEnabled()) return false;
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
      return true;
    };
    // The verification preset is set for the project in the Agent review dialog.
    QVERIFY(find(window->contentItem(),"buildPresetBox"));
    QVERIFY(click("agentReviewButton"));
    QQuickItem *checks = nullptr;
    QTRY_VERIFY((checks = find(window->contentItem(), "verifyPresetBox")));
    QVERIFY(checks->property("count").toInt()>=7);  // Balanced and the built-in verification presets.
    QVERIFY(QMetaObject::invokeMethod(checks,"activated",Q_ARG(int,1))); QTRY_VERIFY(!backend.verifyPreset().isEmpty());
    QVERIFY(checks->property("displayText").toString().startsWith("Check preset"));
    auto *dialog = window->findChild<QObject *>("agentReviewDialog");
    QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
    QTRY_VERIFY(!checks->isVisible());
    QCOMPARE(find(window->contentItem(),"wikiCount")->property("text").toString(),QString("24 entries"));
    auto *attention = find(window->contentItem(), "wikiAttentionOnly");
    QVERIFY(attention && !attention->property("checked").toBool());
    const auto toggleAttention = [&] {
      const QPoint point = attention->mapToScene(QPointF(attention->width()/2, attention->height()/2)).toPoint();
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    };
    toggleAttention();
    QTRY_COMPARE(list->property("count").toInt(),22);
    auto *search=find(window->contentItem(),"wikiSearch"); QVERIFY(search); search->forceActiveFocus();
    for(const QChar c:QString("backup documents")) QTest::keyClick(window,c.toLatin1());
    QTRY_COMPARE(list->property("count").toInt(),0);
    toggleAttention();
    QTRY_VERIFY(!backend.memoryModel()->attentionOnly());
    QTRY_COMPARE(list->property("count").toInt(),1); QCOMPARE(find(window->contentItem(),"wikiCount")->property("text").toString(),QString("1 of 24"));
    search->setProperty("text","decision"); QTRY_COMPARE(list->property("count").toInt(),12);
    toggleAttention();
    QTRY_COMPARE(list->property("count").toInt(),11);
    search->setProperty("text","nothing like this"); QTRY_COMPARE(list->property("count").toInt(),0);
    search->setProperty("text",""); QTRY_COMPARE(list->property("count").toInt(),22);
    toggleAttention();
    QTRY_COMPARE(list->property("count").toInt(),24);

    {
      Backend reopened(nullptr, nullptr, prefs, {logs.filePath("codex"), logs.filePath("claude")});
      QVERIFY(!reopened.memoryModel()->attentionOnly());
      QCOMPARE(reopened.memoryModel()->rowCount(), 24);
    }
    QVERIFY(click("deleteWikiButton"));
    QTRY_VERIFY(find(window->contentItem(), "cancelDeleteWiki"));
    QVERIFY(click("cancelDeleteWiki"));
    QTRY_VERIFY(!find(window->contentItem(), "cancelDeleteWiki"));
    QCOMPARE(backend.memoryModel()->rowCount(), 24);
    QVERIFY(click("deleteWikiButton"));
    QTRY_VERIFY(find(window->contentItem(), "confirmDeleteWiki"));
    QVERIFY(click("confirmDeleteWiki"));
    QTRY_VERIFY(!find(window->contentItem(), "confirmDeleteWiki"));
    QTRY_COMPARE(backend.memoryModel()->rowCount(), 0);
    Store cleared(p.path());
    QVERIFY(cleared.memories().isEmpty());
    QVERIFY(cleared.learningBatch().value("events").toArray().isEmpty());
    QCOMPARE(cleared.search("earlier discussion").size(), 1);
    QVERIFY(!cleared.projectFiles().isEmpty());
    QVERIFY(!QFileInfo::exists(p.filePath(".lore/wiki/1.md")));
    QVERIFY(!Store::read(p.filePath(".lore/wiki/index.md")).contains("Topic 7"));
    QVERIFY(!cleared.context().contains(".lore/wiki/1.md"));
    QVERIFY(cleared.importConversation("User: A new decision starts the wiki.") > 0);
    const auto fresh = cleared.learningBatch().value("events").toArray();
    QCOMPARE(fresh.size(), 1);
    cleared.learn(entry(fresh.first().toObject().value("id").toString()), fresh, "fake");
    QVERIFY(backend.openProject(p.path()));
    QTRY_COMPARE(list->property("count").toInt(), 1);
    backend.forgetConversations({});
    QTRY_COMPARE(list->property("count").toInt(), 0);
    QCOMPARE(backend.hiddenWikiEntries(), 1);
    auto *hidden = find(window->contentItem(), "hiddenWikiEntries");
    QVERIFY(hidden && hidden->property("text").toString().contains("forgotten conversation"));
    QVERIFY(!QFileInfo::exists(p.filePath(".lore/wiki/25.md")));
    QVERIFY(click("nav_sources"));
    QTRY_VERIFY(find(window->contentItem(), "deleteForgottenConversations"));
    QVERIFY(click("deleteForgottenConversations"));
    QTRY_VERIFY(find(window->contentItem(), "cancelDeleteForgotten"));
    QVERIFY(click("cancelDeleteForgotten"));
    QVERIFY(!cleared.memories().isEmpty());
    QVERIFY(click("deleteForgottenConversations"));
    QTRY_VERIFY(find(window->contentItem(), "confirmDeleteForgotten"));
    QVERIFY(click("confirmDeleteForgotten"));
    QTRY_VERIFY(!find(window->contentItem(), "deleteForgottenConversations"));
    QCOMPARE(backend.hiddenWikiEntries(), 0);
    QVERIFY(cleared.memories().isEmpty());
    QVERIFY(find(window->contentItem(), "captureExclusionsNotice"));
  }
  void buildShowsProgressAndLeavesTheRestUsable() {
    LocalModelFixture server; QVERIFY(server.isListening()); server.delayMs=250;
    const auto oldUrl=qgetenv("LORE_LLM_URL"), oldModel=qgetenv("LORE_LLM_MODEL"); const auto restore=qScopeGuard([&] { qputenv("LORE_LLM_URL",oldUrl); qputenv("LORE_LLM_MODEL",oldModel); });
    qputenv("LORE_LLM_URL",server.url()); qputenv("LORE_LLM_MODEL","qwen3:8b");
    QTemporaryDir p,logs; const QString transcript=logs.filePath("claude/project/s.jsonl"); QByteArray records;
    { Store store(p.path(),true); store.render(); }
    for(int i=0;i<40;++i) records+=claude(p.path(),"user","Message "+QString::number(i));
    Store::write(transcript,records);
    Backend backend(nullptr,nullptr,{}, {logs.filePath("codex"),logs.filePath("claude")}); QVERIFY(backend.openProject(p.path())); QVERIFY(backend.connectProject()); QTRY_VERIFY(!backend.capturing());
    QCOMPARE(backend.stats().value("pending").toInt(),40);
    backend.setAutoLearning(true); backend.setAutoAfter(1000); backend.setLearningProvider("local");
    Theme theme(p.path()); Translator translator(p.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator); engine.rootContext()->setContextProperty("initialProject",p.path());
    engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->resize(1200,900); window->show(); window->setProperty("page","home"); QTest::qWait(100);
    std::function<QQuickItem *(QQuickItem *,const QString &)> find=[&](QQuickItem *parent,const QString &name) -> QQuickItem * { if(parent->objectName()==name && parent->isVisible()) return parent; for(auto *child:parent->childItems()) if(auto *found=find(child,name)) return found; return nullptr; };
    QVERIFY(!find(window->contentItem(),"buildProgressBar"));
    server.failChats=1;  // The first round fails once and is retried.
    backend.learn("local");
    QQuickItem *text=nullptr; QTRY_VERIFY((text=find(window->contentItem(),"buildProgressText")));
    QVERIFY(text->property("text").toString().contains("0 of 40 messages")); QVERIFY(text->property("text").toString().contains("the local model"));
    auto *terminal = find(window->contentItem(), "nav_output");
    QVERIFY(terminal);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
      terminal->mapToScene(QPointF(terminal->width()/2, terminal->height()/2)).toPoint());
    QTRY_COMPARE(window->property("page").toString(), QString("output"));
    QQuickItem *output = nullptr;
    QTRY_VERIFY((output = find(window->contentItem(), "modelOutputText")));
    QTRY_VERIFY(output->property("text").toString().contains("Building the wiki"));
    auto *active = find(window->contentItem(), "activeModelLabel");
    QVERIFY(active);
    QTRY_VERIFY(active->property("text").toString().contains("qwen3:8b"));
    QTRY_VERIFY_WITH_TIMEOUT(output->property("text").toString().contains("Database choice"), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(backend.buildProgress().value("done").toInt()>=15,10000);
    QVERIFY(backend.buildProgress().value("active").toBool());
    // While the build continues: a review applies and capture still saves.
    backend.reviewMemory(1,"confirmed");
    records+=claude(p.path(),"user","Captured during the build."); Store::write(transcript,records); backend.sync();
    QTRY_VERIFY_WITH_TIMEOUT(backend.memories().last().toMap().value("review").toString()=="confirmed",10000);
    QTRY_VERIFY(backend.stats().value("events").toInt()==41);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.buildProgress().value("active").toBool(),20000);
    QCOMPARE(backend.stats().value("pending").toInt(),0); QVERIFY(backend.status().contains("Wiki build finished"));
    QTRY_VERIFY(!find(window->contentItem(),"buildProgressBar"));
    // Cancel stops a build and pauses automatic builds without turning them off.
    for(int i=0;i<20;++i) records+=claude(p.path(),"user","Later "+QString::number(i));
    Store::write(transcript,records); backend.sync(); QTRY_VERIFY(backend.stats().value("pending").toInt()==20);
    backend.learn("local"); QTRY_VERIFY(backend.modelBusy()); backend.cancel();
    QTRY_VERIFY(!backend.buildProgress().value("active").toBool()); QVERIFY(backend.autoLearning()); QVERIFY(backend.autoStatus().contains("Paused"));
    // Two failures in a row: a local build tries a smaller batch and still finishes everything.
    server.failChats=2; const int before=server.chats; backend.learn("local");
    QTRY_VERIFY_WITH_TIMEOUT(!backend.buildProgress().value("active").toBool() && backend.stats().value("pending").toInt()==0,20000);
    QVERIFY(server.chats-before>=3);
    QVERIFY(backend.liveOutput().contains("failed"));  // Each failed attempt is logged on the Model output page.
  }
  void buildWikiRunsUntilNothingIsWaiting() {
    QTemporaryDir p,logs,tools; const QString codex=tools.filePath(
#ifdef Q_OS_WIN
      "codex.exe"
#else
      "codex"
#endif
    );
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),codex)); QFile::setPermissions(codex,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    { Store store(p.path(),true); QByteArray records; for(int i=0;i<90;++i) records+=claude(p.path(),"user","Message "+QString::number(i)); Store::write(logs.filePath("s.jsonl"),records); store.ingest(logs.filePath("s.jsonl"),"claude"); }
    Backend backend(nullptr,nullptr,{}, {logs.filePath("codex"),logs.filePath("claude")}); QVERIFY(backend.openProject(p.path())); backend.setWatching(false);
    backend.tools()->setExecutable("codex",codex); QTRY_VERIFY(!backend.tools()->checking()); backend.setLearningProvider("codex");
    QSignalSpy rounds(&backend,&Backend::learningFinished);
    backend.learn("codex");  // One press: 40 messages a round, so three rounds.
    QTRY_COMPARE_WITH_TIMEOUT(backend.stats().value("pending").toInt(),0,30000);
    QCOMPARE(rounds.count(),3); for(const auto &r:rounds) QVERIFY(r.first().toBool());
    QTRY_VERIFY(!backend.busy());
  }
  void manualLocalBuildFollowsTheBuildWikiChoice() {
    LocalModelFixture server; QVERIFY(server.isListening());
    const auto oldUrl=qgetenv("LORE_LLM_URL"), oldModel=qgetenv("LORE_LLM_MODEL"); const auto restore=qScopeGuard([&] { qputenv("LORE_LLM_URL",oldUrl); qputenv("LORE_LLM_MODEL",oldModel); });
    qputenv("LORE_LLM_URL",server.url()); qputenv("LORE_LLM_MODEL","qwen3:8b");
    for(const bool switchAway:{true,false}) {
      QTemporaryDir p,logs;
      { Store store(p.path(),true); QByteArray records; for(int i=0;i<40;++i) records+=claude(p.path(),"user","Message "+QString::number(i)); Store::write(logs.filePath("s.jsonl"),records); store.ingest(logs.filePath("s.jsonl"),"claude"); }
      Backend backend(nullptr,nullptr,{}, {logs.filePath("codex"),logs.filePath("claude")}); QVERIFY(backend.openProject(p.path())); backend.setWatching(false);
      backend.setLearningProvider("local"); const int start=server.chats;
      QMetaObject::Connection once; once=QObject::connect(&backend,&Backend::learningFinished,&backend,[&] { QObject::disconnect(once); if(switchAway) backend.setLearningProvider("codex"); });
      backend.learn("local");
      if(switchAway) { QTRY_VERIFY(!backend.busy()); QTest::qWait(300); QCOMPARE(server.chats-start,1); QVERIFY(backend.stats().value("pending").toInt()>0); }
      else { QTRY_COMPARE_WITH_TIMEOUT(backend.stats().value("pending").toInt(),0,10000); QVERIFY(server.chats-start>=3); }  // 15 messages a round.
    }
  }
  void connectionsChooseALocalModelForBuildingTheWiki() {
    LocalModelFixture server; const auto oldUrl=qgetenv("LORE_LLM_URL"), oldModel=qgetenv("LORE_LLM_MODEL"); const auto restore=qScopeGuard([&] { qputenv("LORE_LLM_URL",oldUrl); qputenv("LORE_LLM_MODEL",oldModel); });
    const QString modelName = "local-engine/" + QString(145, 'x') + ":quantized-preview";
    server.models = QStringList{modelName};
    qputenv("LORE_LLM_URL",server.url()); qunsetenv("LORE_LLM_MODEL");
    QTemporaryDir p,logs; { Store store(p.path(),true); store.render(); }
    Backend backend(nullptr,nullptr,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); backend.setWatching(false);
    const QString answering=backend.answerProvider();
    Theme theme(p.path()); Translator translator(p.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator); engine.rootContext()->setContextProperty("initialProject",p.path());
    engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->resize(1400,1000); window->show(); window->setProperty("page","setup"); QTest::qWait(100);
    backend.tools()->check(); QTRY_VERIFY(!backend.tools()->checking());
    std::function<QQuickItem *(QQuickItem *,const QString &)> find=[&](QQuickItem *parent,const QString &name) -> QQuickItem * { if(parent->objectName()==name && parent->isVisible()) return parent; for(auto *child:parent->childItems()) if(auto *found=find(child,name)) return found; return nullptr; };
    auto *aiStep = find(window->contentItem(), "setupStep_ai");
    QVERIFY(aiStep);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                     aiStep->mapToScene(QPointF(aiStep->width()/2, aiStep->height()/2)).toPoint());
    QQuickItem *models=nullptr; QTRY_VERIFY((models=find(window->contentItem(),"localModelBox"))); QTRY_VERIFY(models->isEnabled()); QCOMPARE(models->property("count").toInt(),1);
    QVERIFY(QMetaObject::invokeMethod(models,"activated",Q_ARG(int,0)));
    QTRY_VERIFY(backend.tools()->ready("local"));
    QTRY_VERIFY((models=find(window->contentItem(),"localModelBox")));
    QCOMPARE(models->property("displayText").toString(), modelName);
    auto *modelLabel = find(window->contentItem(), "localModelBoxText");
    QVERIFY(modelLabel);
    QTRY_VERIFY(modelLabel->property("lineCount").toInt() > 1);
    auto *learnLocal=find(window->contentItem(),"learnWith_local"); QVERIFY(learnLocal); QTRY_VERIFY(learnLocal->isEnabled() && learnLocal->isVisible());
    QVERIFY(QMetaObject::invokeMethod(learnLocal,"clicked"));  // The row can sit below the fold of the offscreen window.
    QTRY_COMPARE(backend.learningProvider(),QString("local")); QVERIFY(learnLocal->property("checked").toBool()); QCOMPARE(backend.answerProvider(),answering);
    auto *answerLocal=find(window->contentItem(),"answerWith_local"); QVERIFY(answerLocal); QVERIFY(!answerLocal->property("checked").toBool());
    auto *learnOther=find(window->contentItem(),"learnWith_"+answering); QVERIFY(learnOther); QVERIFY(!learnOther->property("checked").toBool());
  }
  void providerModelDropdownsAcrossPages() {
    QTemporaryDir project, data;
    QVERIFY(project.isValid() && data.isValid());
    const auto oldHome = qgetenv("CODEX_HOME");
    qputenv("CODEX_HOME", data.path().toUtf8());
    const auto restoreHome = qScopeGuard([&] {
      if (oldHome.isNull()) qunsetenv("CODEX_HOME");
      else qputenv("CODEX_HOME", oldHome);
    });
    Store::write(data.filePath("models_cache.json"), R"({"models":[
      {"slug":"codex-test","visibility":"list"},
      {"slug":"test-hidden","visibility":"hide"},
      {"slug":"bad model id","visibility":"list"},
      {"slug":"codex-test","visibility":"list"}
    ]})");
    const QString codex = data.filePath(
#ifdef Q_OS_WIN
      "codex.exe"
#else
      "codex"
#endif
    );
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), codex));
    QVERIFY(QFile::setPermissions(codex, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    {
      Store store(project.path(), true);
      const QString transcript = data.filePath("review.jsonl");
      Store::write(transcript, claude(project.path(), "user", "Use SQLite for local storage."));
      store.ingest(transcript, "claude");
      const auto batch = store.learningBatch().value("events").toArray();
      store.learn(entry(batch.first().toObject().value("id").toString()), batch, "fake");
      Store::write(project.filePath("storage.cpp"), "// Use SQLite for local storage.\n");
      store.indexProject();
      store.render();
    }
    Backend backend(nullptr, nullptr, data.filePath("preferences.json"), {data.path(), data.path()});
    QVERIFY(backend.openProject(project.path()));
    QVERIFY(backend.connectProject());
    QTRY_VERIFY(backend.connected());
    backend.setWatching(false);
    QTRY_VERIFY(!backend.capturing());
    QTRY_VERIFY(!backend.tools()->checking());
    backend.tools()->setExecutable("codex", codex);
    QTRY_VERIFY(!backend.tools()->checking());
    QVERIFY(backend.tools()->ready("codex"));
    backend.setKnowledgeProvider("functions", "codex");
    backend.setModelChoice("functions", "codex", "model", "previous-codex-model");

    Theme theme(project.path());
    Translator translator(data.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("initialProject", project.path());
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(1300, 1000);
    window->show();
    window->setProperty("page", "functionIndex");
    const std::function<QQuickItem *(QQuickItem *, const QString &)> find =
      [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
        if (!parent->isVisible()) return nullptr;
        if (parent->objectName() == name) return parent;
        for (auto *child : parent->childItems())
          if (auto *found = find(child, name)) return found;
        return nullptr;
      };
    QQuickItem *model = nullptr;
    QTRY_VERIFY((model = find(window->contentItem(), "functionsModelPicker")) && model->isEnabled());
    QCOMPARE(model->property("editable").toBool(), false);
    QCOMPARE(model->property("count").toInt(), 3);
    QCOMPARE(model->property("currentText").toString(), QString("previous-codex-model"));

    const auto click = [window](QQuickItem *item) {
      const auto center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    };
    click(model);
    auto *popup = model->property("popup").value<QObject *>();
    QTRY_VERIFY(popup && popup->property("visible").toBool());
    QTest::keyClick(window, Qt::Key_Up);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(backend.modelChoice("functions", "codex").value("modelOverride").toString(), QString("codex-test"));

    backend.setKnowledgeProvider("functions", "claude");
    QTRY_VERIFY(!popup->property("visible").toBool());
    QTRY_COMPARE(model->property("count").toInt(), 4);
    QTRY_COMPARE(model->property("currentIndex").toInt(), 0);
    model->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Down);
    QTRY_COMPARE(backend.modelChoice("functions", "claude").value("modelOverride").toString(), QString("sonnet"));
    QTest::keyClick(window, Qt::Key_Up);
    QTRY_VERIFY(backend.modelChoice("functions", "claude").value("modelOverride").toString().isEmpty());
    QCOMPARE(backend.modelChoice("functions", "codex").value("modelOverride").toString(), QString("codex-test"));

    backend.setVerifyProvider("local");
    window->setProperty("page", "wiki");
    QCOMPARE(backend.reviewWaiting(), 1);
    QQuickItem *review = nullptr;
    QTRY_VERIFY((review = find(window->contentItem(), "agentReviewButton")) && review->isEnabled());
    QTest::qWait(50);
    click(review);
    QQuickItem *agent = nullptr;
    QTRY_VERIFY((agent = find(window->contentItem(), "verifyProviderBox")));
    QQuickItem *start = nullptr;
    QTRY_VERIFY((start = find(window->contentItem(), "startAgentReview")));
    QCOMPARE(agent->property("currentText").toString(), QString("Local model"));
    QVERIFY(!start->isEnabled());
    agent->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Up);
    QTRY_COMPARE(agent->property("currentText").toString(), QString("Codex"));
    QTRY_VERIFY(start->isEnabled());
    QCOMPARE(backend.verifyProvider(), QString("local"));
    QQuickItem *verify = nullptr;
    QTRY_VERIFY((verify = find(window->contentItem(), "verifyModelPicker")) && verify->isEnabled());
    QCOMPARE(verify->property("editable").toBool(), false);
    QCOMPARE(verify->property("count").toInt(), 2);
    verify->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Down);
    QTRY_COMPARE(backend.modelChoice("verify", "codex").value("modelOverride").toString(), QString("codex-test"));
    click(start);
    QTRY_COMPARE(backend.verifyProvider(), QString("codex"));
    QTRY_COMPARE_WITH_TIMEOUT(backend.reviewWaiting(), 0, 15000);
    QTRY_VERIFY(!backend.verifyProgress().value("active").toBool());
    const auto verification = backend.memories().first().toMap().value("verification").toMap();
    QCOMPARE(verification.value("provider").toString(), QString("codex"));

    backend.setKnowledgeProvider("transcript", "claude");
    window->setProperty("page", "transcriptAnalysis");
    QQuickItem *transcript = nullptr;
    QTRY_VERIFY((transcript = find(window->contentItem(), "transcriptModelPicker")));
    QCOMPARE(transcript->property("editable").toBool(), false);
    QCOMPARE(transcript->property("count").toInt(), 4);

    backend.setLearningProvider("codex");
    window->setProperty("page", "settings");
    QQuickItem *settings = nullptr;
    QTRY_VERIFY((settings = find(window->contentItem(), "settingsPage")));
    QVERIFY(settings->setProperty("tab", 1));
    for (const QString &name : {"agentModel_claude", "agentModel_codex", "buildModelPicker"}) {
      QQuickItem *picker = nullptr;
      QTRY_VERIFY2((picker = find(window->contentItem(), name)), qPrintable(name));
      QCOMPARE(picker->property("editable").toBool(), false);
      QVERIFY(picker->property("count").toInt() > 1);
    }
  }
  void settingsIncreasesLocalModelContextAndBatchSize() {
    LocalModelFixture server;
    QVERIFY(server.isListening());
    const auto oldUrl = qgetenv("LORE_LLM_URL"), oldModel = qgetenv("LORE_LLM_MODEL");
    const auto restore = qScopeGuard([&] { qputenv("LORE_LLM_URL",oldUrl); qputenv("LORE_LLM_MODEL",oldModel); });
    qputenv("LORE_LLM_URL", server.url());
    qputenv("LORE_LLM_MODEL", "qwen3:8b");
    QTemporaryDir project, logs, prefs;
    const QString preferences = prefs.filePath("preferences.json");
    {
      Store store(project.path(), true);
      QByteArray transcript;
      for (int i = 0; i < 24; ++i)
        transcript += claude(project.path(), "user", QString("Project indexing decision number %1.").arg(i));
      const QString path = logs.filePath("session.jsonl");
      Store::write(path, transcript);
      store.ingest(path, "claude");
      store.render();
      QCOMPARE(store.learningBatch(true).value("events").toArray().size(), 15);
      const auto evidenceCount = [&](int tokens) { return store.answerPrompt("Project indexing decision", tokens).value("evidence").toArray().size(); };
      QCOMPARE(evidenceCount(8192), 8);
      QVERIFY(evidenceCount(32768) > evidenceCount(8192));
    }
    Backend backend(nullptr, nullptr, preferences, {logs.path(), logs.path()});
    QVERIFY(backend.openProject(project.path()));
    QVERIFY(backend.connectProject());
    backend.setWatching(false);
    QTRY_VERIFY(!backend.capturing());
    backend.tools()->check();
    QTRY_VERIFY(backend.tools()->ready("local"));
    Theme theme(project.path());
    Translator translator(project.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("initialProject", project.path());
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(1200, 900);
    window->show();
    window->setProperty("page", "settings");
    auto *models = window->findChild<QQuickItem *>("settingsModelsTab");
    QVERIFY(models);
    QTRY_VERIFY(models->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, models->mapToScene(QPointF(models->width()/2, models->height()/2)).toPoint());
    auto *slider = window->findChild<QQuickItem *>("localContextSlider");
    QVERIFY(slider);
    QTRY_VERIFY(slider->isVisible());
    QCOMPARE(slider->property("value").toInt(), 0);
    slider->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Right);
    QTRY_COMPARE(backend.tools()->localContextTokens(), 4096);
    for (int i = 0; i < 7; ++i) QTest::keyClick(window, Qt::Key_Right);
    QTRY_COMPARE(backend.tools()->localContextTokens(), 32768);
    {
      Backend reopened(nullptr, nullptr, preferences, {logs.path(), logs.path()});
      QCOMPARE(reopened.tools()->localContextTokens(), 32768);
    }

    QSignalSpy done(&backend, &Backend::learningFinished);
    backend.learn("local");
    QTRY_COMPARE(done.count(), 1);
    QVERIFY2(done.first().first().toBool(), qPrintable(backend.status()));
    QCOMPARE(server.chats, 1);
    QCOMPARE(server.lastBody.value("options").toObject().value("num_ctx").toInt(), 32768);
    const QString prompt = server.lastBody.value("messages").toArray().first().toObject().value("content").toString();
    const auto events = QJsonDocument::fromJson(prompt.section("\nNEW EVENTS:\n", -1).toUtf8()).array();
    QCOMPARE(events.size(), 24);

    for (int i = 0; i < 8; ++i) QTest::keyClick(window, Qt::Key_Left);
    QTRY_COMPARE(backend.tools()->localContextTokens(), 0);
  }
  void settingsMigratesAnInProjectArchive() {
    QTemporaryDir p,logs; FakePicker picker;
    { Store store(p.path(),true); store.render(); }
    Backend backend(nullptr,&picker,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); QVERIFY(backend.connectProject()); QTRY_VERIFY(!backend.capturing()); backend.setWatching(false);
    QCOMPARE(backend.project(),backend.source());
    Theme theme(p.path()); Translator translator(p.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator); engine.rootContext()->setContextProperty("initialProject",p.path());
    engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->show(); window->setProperty("page","settings"); QTest::qWait(100);
    std::function<QQuickItem *(QQuickItem *,const QString &)> find=[&](QQuickItem *parent,const QString &name) -> QQuickItem * { if(parent->objectName()==name) return parent; for(auto *child:parent->childItems()) if(auto *found=find(child,name)) return found; return nullptr; };
    auto *card=find(window->contentItem(),"settingsMigrateCard"); QVERIFY(card); QTRY_VERIFY(card->isVisible());
    auto *button=find(window->contentItem(),"settingsMigrate"); QVERIFY(button);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
    auto *dialog=engine.rootObjects().first()->findChild<QObject *>("settingsConfirm"); QVERIFY(dialog); QTRY_VERIFY(dialog->property("opened").toBool());
    const QString expected=Backend::defaultWorkingDirectory(p.path());
    QVERIFY(QMetaObject::invokeMethod(dialog,"accept"));
    QTRY_COMPARE(backend.project(),QFileInfo(expected).canonicalFilePath());
    QCOMPARE(backend.source(),QFileInfo(p.path()).canonicalFilePath());
    QVERIFY(!QFileInfo::exists(p.filePath(".lore"))); QTRY_VERIFY(!card->isVisible());
  }
  void semanticRetrievalFindsDifferentWordingAndRejectsBadVectors() {
    EmbeddingFixture server; QVERIFY(server.isListening()); const auto old=qgetenv("LORE_EMBED_URL"); const auto restore=qScopeGuard([&] { qputenv("LORE_EMBED_URL",old); }); qputenv("LORE_EMBED_URL",server.url());
    QTemporaryDir p,logs; Store store(p.path(),true); Store::write(logs.filePath("s.jsonl"),claude(p.path(),"user","All information must stay on this computer.")+claude(p.path(),"user","The interface uses yellow buttons.")); store.ingest(logs.filePath("s.jsonl"),"claude");
    QVERIFY(store.briefing("Why avoid a hosted service?",3,false).value("evidence").toArray().isEmpty());
    // The download streams progress; indexing counts what it embeds and turns search on as it goes.
    const auto progress=std::make_shared<Store::Progress>(); store.setProgress(progress);
    store.downloadSemanticModel(); QCOMPARE(progress->stage.load(),1); QCOMPARE(progress->total.load(),qint64(2097152)); QCOMPARE(progress->done.load(),qint64(2097152));
    QVERIFY(!store.semanticStatus().value("enabled").toBool());
    QCOMPARE(store.buildSemanticIndex().value("updated").toInt(),2);
    QCOMPARE(progress->stage.load(),2); QCOMPARE(progress->total.load(),qint64(2)); QCOMPARE(progress->done.load(),qint64(2)); QVERIFY(store.semanticStatus().value("enabled").toBool());
    const auto recall=store.briefing("Why avoid a hosted service?",3); QCOMPARE(recall.value("retrieval").toString(),QString("hybrid"));
    QVERIFY(recall.value("evidence").toArray().first().toObject().value("body").toString().contains("information must stay"));
    QCOMPARE(store.buildSemanticIndex().value("updated").toInt(),0);
    // Vectors saved as JSON by older versions still rank, and the next index
    // turns them into bytes without embedding again.
    { auto db=QSqlDatabase::addDatabase("QSQLITE","old-vectors"); db.setDatabaseName(p.filePath(".lore/brain.sqlite")); QVERIFY(db.open());
      QSqlQuery q(db); QVERIFY(q.exec("SELECT id,data FROM embeddings")); QList<QPair<QString,QString>> rows;
      while(q.next()) { const QByteArray bytes=q.value(1).toByteArray(); const auto *f=reinterpret_cast<const float *>(bytes.constData()); QJsonArray values; for(qsizetype i=0;i<bytes.size()/qsizetype(sizeof(float));++i) values.append(double(f[i])); rows.append({q.value(0).toString(),QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact))}); }
      q.finish(); QVERIFY(!rows.isEmpty());
      for(const auto &[id,vector]:rows) { QSqlQuery u(db); u.prepare("UPDATE embeddings SET data=NULL,vector=? WHERE id=?"); u.addBindValue(vector); u.addBindValue(id); QVERIFY(u.exec()); }
      db.close(); }
    QSqlDatabase::removeDatabase("old-vectors");
    const auto older=store.briefing("Why avoid a hosted service?",3); QCOMPARE(older.value("retrieval").toString(),QString("hybrid"));
    QVERIFY(older.value("evidence").toArray().first().toObject().value("body").toString().contains("information must stay"));
    QCOMPARE(store.buildSemanticIndex().value("updated").toInt(),0);
    { auto db=QSqlDatabase::addDatabase("QSQLITE","new-vectors"); db.setDatabaseName(p.filePath(".lore/brain.sqlite")); QVERIFY(db.open());
      QSqlQuery q(db); QVERIFY(q.exec("SELECT count(*) FROM embeddings WHERE data IS NULL OR vector<>''")); q.next(); QCOMPARE(q.value(0).toInt(),0); q.finish(); db.close(); }
    QSqlDatabase::removeDatabase("new-vectors");
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
    backend.setAnswerProvider("claude");
    QSignalSpy finished(&backend,&Backend::learningFinished); backend.ask("How does authentication work?"); QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000); QVERIFY2(finished.first().first().toBool(),qPrintable(backend.status())); QVERIFY(!backend.answer().value("refs").toList().isEmpty());
    backend.ask("xyzunrelatedunknown"); QTRY_COMPARE_WITH_TIMEOUT(finished.size(),2,10000); QVERIFY(backend.answer().value("insufficient").toBool());
  }
  void backgroundCaptureDoesNotFlashControls() {
    QTemporaryDir p, logs; { Store store(p.path(),true); store.render(); }
    Backend backend(nullptr,nullptr,{}, {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); QTRY_VERIFY(!backend.capturing());
    bool sawBackground=false, shown=false;
    connect(&backend,&Backend::statusChanged,&backend,[&] { if(backend.capturing()) { sawBackground=true; if(backend.showCapturing() || backend.showBusy()) shown=true; } });
    backend.setWatching(true); QTRY_VERIFY_WITH_TIMEOUT(sawBackground,8000); QTRY_VERIFY(!backend.capturing()); QVERIFY(!shown);
    backend.setWatching(false);
    backend.sync(); QVERIFY(backend.capturing()); QVERIFY(backend.showCapturing()); QTRY_VERIFY(!backend.capturing()); QVERIFY(!backend.showCapturing());
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
      QCOMPARE(backend.workingDirectory(),Backend::defaultWorkingDirectory(p.path()));
      QVERIFY(backend.workingDirectory().endsWith("/Lore-"+QFileInfo(p.path()).fileName()));
      QVERIFY(!QFileInfo::exists(p.filePath(".lore")));
      QVERIFY(backend.connectProject()); QVERIFY(backend.connected()); QVERIFY(backend.watching());
      QVERIFY(!QFileInfo::exists(p.filePath(".lore"))); QVERIFY(!QFileInfo::exists(p.filePath("BRAIN.md")));
      QVERIFY(QFileInfo::exists(backend.project()+"/.lore/brain.sqlite")); QVERIFY(QFileInfo::exists(backend.project()+"/BRAIN.md"));
      QCOMPARE(backend.source(),QFileInfo(p.path()).canonicalFilePath());
      QVERIFY(Store::read(p.filePath("CLAUDE.md")).contains(QDir::toNativeSeparators(backend.project()+"/BRAIN.md")));
      QVERIFY(QFileInfo::exists(p.filePath(".claude/settings.local.json")));
      QTRY_VERIFY(!backend.capturing());
      backend.setLearningProvider("claude"); backend.setAutoLearning(true);
      QVERIFY(backend.autoLearning());
    }
    Backend restored(nullptr,nullptr,preferences,{logs.path(),logs.path()});
    QCOMPARE(restored.project(),Backend::defaultWorkingDirectory(p.path())); QCOMPARE(restored.source(),QFileInfo(p.path()).canonicalFilePath());
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
    for (const auto &entry : tools.entries()) if (entry.toMap().value("id") != "local") QCOMPARE(entry.toMap().value("state").toString(),QString("ready"));
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
  void wikiReviewKeepsCardsAndScrollPosition() {
    QTemporaryDir p, logs; FakePicker picker;
    { Store store(p.path(),true); const QString path=logs.filePath("s.jsonl"); Store::write(path,claude(p.path(),"user","A documented storage decision")); store.ingest(path,"claude");
      const auto batch=store.learningBatch().value("events").toArray(); QJsonArray entries;
      for(int i=0;i<30;++i) { auto item=entry(batch.first().toObject().value("id").toString()).value("entries").toArray().first().toObject(); item["title"]="Wiki topic "+QString::number(i); item["body"]="Body for topic "+QString::number(i); entries.append(item); }
      store.learn(QJsonObject{{"entries",entries}},batch,"fake"); }
    Backend backend(nullptr,&picker,logs.filePath("preferences.json"), {logs.path(),logs.path()}); QVERIFY(backend.openProject(p.path())); QVERIFY(backend.connectProject()); QTRY_VERIFY(backend.connected()); backend.setWatching(false); QTRY_VERIFY(!backend.capturing());
    backend.memoryModel()->setAttentionOnly(true);
    QCOMPARE(backend.memoryModel()->rowCount(),30);
    Theme theme(p.path()); Translator translator(p.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator); engine.rootContext()->setContextProperty("initialProject",p.path());
    engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->show(); window->setProperty("page","wiki"); QTest::qWait(200);
    auto *list = window->findChild<QQuickItem *>("wikiList"); QVERIFY(list); QTRY_VERIFY(list->isVisible());
    QTRY_VERIFY(list->property("contentHeight").toReal() > list->height() + 400);
    list->setProperty("contentY",400); QTest::qWait(100);
    const qreal scrolled=list->property("contentY").toReal(); QVERIFY(scrolled > 0);
    std::function<QQuickItem *(QQuickItem *)> confirm=[&](QQuickItem *parent) -> QQuickItem * {
      if(parent->property("text").toString()=="Confirm" && parent->isVisible() && parent->isEnabled()) {
        const QPointF centre=parent->mapToItem(list,QPointF(parent->width()/2,parent->height()/2));
        if(centre.y()>0 && centre.y()<list->height()) return parent;
      }
      for(auto *child:parent->childItems()) if(auto *found=confirm(child)) return found;
      return nullptr;
    };
    QVERIFY(list->findChild<QQuickItem *>("verifyMemoryButton") || [&] { std::function<bool(QQuickItem *)> has=[&](QQuickItem *i) { if(i->objectName()=="verifyMemoryButton") return true; for(auto *c:i->childItems()) if(has(c)) return true; return false; }; return has(list); }());
    window->grabWindow().save(QCoreApplication::applicationDirPath()+"/wiki-verify.png");
    auto *button=confirm(list); QVERIFY(button);
    const QPoint point=button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint();
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point);
    QTRY_VERIFY(!backend.busy()); QTRY_COMPARE(backend.memoryModel()->lastChange(),QString("remove"));
    QTRY_COMPARE(list->property("count").toInt(),29);
    int confirmed=0; for(const auto &v:backend.memories()) if(v.toMap().value("review").toString()=="confirmed") ++confirmed;
    QCOMPARE(confirmed,1);
    QTest::qWait(100); QVERIFY(list->property("contentY").toReal() > 0);
  }
  void warningToastKeepsTheWikiLayoutInPlace() {
    QTemporaryDir p, logs;
    FakePicker picker;
    { Store store(p.path(), true); store.render(); }
    Backend backend(nullptr, &picker, logs.filePath("preferences.json"), {logs.filePath("codex"), logs.filePath("claude")});
    QVERIFY(backend.openProject(p.path()));
    QVERIFY(backend.connectProject());
    QTRY_VERIFY(!backend.capturing());
    backend.setWatching(false);

    Theme theme(p.path());
    Translator translator(p.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("initialProject", p.path());
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->setProperty("page", "wiki");
    auto *list = window->findChild<QQuickItem *>("wikiList");
    auto *toast = window->findChild<QQuickItem *>("notificationToast");
    auto *dismiss = window->findChild<QQuickItem *>("dismissNotification");
    QVERIFY(list && toast && dismiss);
    QTRY_VERIFY(list->isVisible() && list->height() > 0);
    backend.clearError();
    QVERIFY(!toast->isVisible());
    const auto geometry = [](QQuickItem *item) {
      return QRectF(item->mapToScene(QPointF()), item->size());
    };
    const auto wikiGeometry = geometry(list);

    const QString warning = QStringLiteral("A lengthy warning for the project.\n").repeated(12).trimmed();
    picker.failed(warning);
    QTRY_VERIFY(toast->isVisible());
    QTRY_COMPARE(toast->mapToScene(QPointF()).y(), 12.0);
    QVERIFY(toast->height() < 120);
    QCOMPARE(geometry(list), wikiGeometry);
    const QPoint click = dismiss->mapToScene(QPointF(dismiss->width()/2, dismiss->height()/2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, click);
    QTRY_VERIFY(!toast->isVisible());
    QCOMPARE(geometry(list), wikiGeometry);
    QCOMPARE(backend.notices().first().toMap().value("text").toString(), warning);
  }
  void sidebarOpensEveryPageAndTheGuideWalksThrough() {
    QTemporaryDir p, logs, prefs; FakePicker picker;
    Store::write(p.filePath("example.py"), "def example():\n    return 1\n");
    Backend backend(nullptr,&picker,prefs.filePath("prefs.json"), {logs.path(),logs.path()});
    Theme theme(p.path()); Translator translator(p.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator); engine.rootContext()->setContextProperty("initialProject",p.path());
    engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->resize(1300,1000); window->show(); QTest::qWait(100);
    std::function<QQuickItem *(QQuickItem *,const QString &)> find=[&](QQuickItem *parent,const QString &name) -> QQuickItem * { if(parent->objectName()==name && parent->isVisible()) return parent; for(auto *child:parent->childItems()) if(auto *found=find(child,name)) return found; return nullptr; };
    const auto click=[&](const QString &name) { QQuickItem *item=nullptr; for(int i=0;i<50 && !(item=find(window->contentItem(),name));++i) QTest::qWait(20); if(!item) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint()); QTest::qWait(30); return true; };
    const auto heading=[&] { auto *h=find(window->contentItem(),"setupHeading"); return h ? h->property("text").toString() : QString(); };
    // A first start opens the guide at Welcome; nothing changes before Connect.
    QTRY_VERIFY(find(window->contentItem(),"setupWizard")); QVERIFY(!find(window->contentItem(),"sidebar"));
    QVERIFY(heading().contains("memory")); QVERIFY(find(window->contentItem(),"setupLoop"));
    QVERIFY(click("wizardNext")); QTRY_VERIFY(heading().contains("Which project"));
    QVERIFY(!find(window->contentItem(),"wizardNext")->isEnabled());  // A folder first.
    QVERIFY(click("chooseProjectButton")); QVERIFY(picker.opened); picker.pick(p.path()); QTRY_VERIFY(!backend.selectedProject().isEmpty());
    QVERIFY(click("wizardNext")); QTRY_VERIFY(heading().contains("files"));
    QVERIFY(find(window->contentItem(),"workingDirectoryPanel"));
    QVERIFY(click("wizardNext")); QTRY_VERIFY(heading().contains("AI"));
    QVERIFY(click("wizardNext")); QTRY_VERIFY(heading().contains("Initialize"));
    QTRY_VERIFY(find(window->contentItem(),"connectionPreview")); QVERIFY(!backend.connected());
    QVERIFY(!QFileInfo::exists(p.filePath("CLAUDE.md")));
    QVERIFY(click("wizardBack")); QVERIFY(click("wizardBack"));
    QVERIFY(click("initializeProjectButton")); QTRY_VERIFY(backend.connected()); QTRY_VERIFY(heading().contains("Build the wiki"));
    QVERIFY(QFileInfo::exists(p.filePath("CLAUDE.md")) || QFileInfo::exists(QFileInfo(backend.source()).filePath() + "/CLAUDE.md"));
    // Optional steps can be skipped.
    QVERIFY(click("wizardSkip")); QTRY_VERIFY(heading().contains("set up"));
    QVERIFY(find(window->contentItem(),"setupSummary"));
    QVERIFY(click("wizardNext")); QTRY_COMPARE(window->property("page").toString(),QString("home"));
    QTRY_VERIFY(!find(window->contentItem(),"setupWizard")); QTRY_VERIFY(find(window->contentItem(),"sidebar"));
    backend.setWatching(false); QTRY_VERIFY(!backend.capturing());
    // Every sidebar entry opens its page.
    const QList<QPair<QString,QString>> pages{{"wiki","wikiPage"},{"index","projectFilesPage"},{"ask","askSearchPage"},{"sources","sourcesPage"},{"settings","settingsPage"},{"home","homePage"}};
    for(const auto &[id,item]:pages) { QVERIFY2(click("nav_"+id),qPrintable(id)); QTRY_COMPARE(window->property("page").toString(),id); QTRY_VERIFY2(find(window->contentItem(),item),qPrintable(item)); }
    QVERIFY(click("nav_index"));
    QVERIFY(click("section_functionWiki")); QTRY_VERIFY(find(window->contentItem(),"functionWikiPage"));
    QVERIFY(click("section_functionIndex")); QTRY_VERIFY(find(window->contentItem(),"functionIndexPage"));
    QVERIFY(click("nav_wiki"));
    QVERIFY(click("section_transcriptAnalysis")); QTRY_VERIFY(find(window->contentItem(),"transcriptAnalysisPage"));
    QVERIFY(click("nav_index"));
    QVERIFY(click("indexProjectButton"));
    QTRY_VERIFY(!backend.busy());
    QVERIFY(click("filesGridButton"));
    QTRY_VERIFY(find(window->contentItem(),"projectFileGrid"));
    QVERIFY(click("filesListButton"));
    QTRY_VERIFY(find(window->contentItem(),"projectFileList"));
    QVERIFY(click("indexedFile_example.py"));
    QTRY_VERIFY(find(window->contentItem(),"documentView"));
    QVERIFY(window->property("document").toString().contains("def example"));
    QTest::keyClick(window,Qt::Key_O,Qt::ControlModifier); QTRY_COMPARE(window->property("page").toString(),QString("projects"));
    QTest::keyClick(window,Qt::Key_K,Qt::ControlModifier); QTRY_COMPARE(window->property("page").toString(),QString("ask"));
    // Older page numbers still open the right page.
    window->setProperty("page",1); QTRY_COMPARE(window->property("page").toString(),QString("wiki"));
    // The guide opens again from Settings and resumes after Connect.
    QVERIFY(click("nav_settings")); QVERIFY(click("settingsAboutTab")); QVERIFY(click("runSetupAgain"));
    QTRY_VERIFY(find(window->contentItem(),"setupWizard")); QTRY_VERIFY(heading().contains("Build the wiki"));
    QVERIFY(click("closeSetup")); QTRY_COMPARE(window->property("page").toString(),QString("home"));
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
    QVERIFY(click("wizardNext")); QTest::qWait(50);
    QVERIFY(click("chooseProjectButton")); QVERIFY(picker.opened);
    picker.pick(p.path()); QTest::qWait(50);
    QVERIFY(click("connectProjectButton")); QTRY_VERIFY(backend.connected()); QTRY_VERIFY(!backend.capturing());
    QVERIFY(backend.watching());
    QTest::keyClick(window,Qt::Key_K,Qt::ControlModifier);
    QCOMPARE(window->property("page").toString(),QString("ask"));
    for (const auto key : {Qt::Key_S,Qt::Key_Q,Qt::Key_L}) QTest::keyClick(window,key);
    QTest::keyClick(window,Qt::Key_Escape); QVERIFY(!window->property("helpVisible").toBool());
    ReleaseUrlRecorder releases;
    QDesktopServices::setUrlHandler("https",&releases,"record");
    const auto restoreBrowser=qScopeGuard([] { QDesktopServices::unsetUrlHandler("https"); });
    window->setProperty("helpVisible",true);
    QTest::qWait(50); window->grabWindow().save(QCoreApplication::applicationDirPath()+"/updates-help.png");
    QVERIFY(click("checkUpdatesButton")); QCOMPARE(releases.opened,QUrl("https://github.com/MadeBySaints/lore/releases"));
    window->setProperty("helpVisible",false);
    QVERIFY(click("nav_ask")); QCOMPARE(window->property("page").toString(),QString("ask"));
    auto *question=window->findChild<QQuickItem *>("askQuestion"); QVERIFY(question); question->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Q); QVERIFY(window->isVisible());
    question->setProperty("text",QString()); QTest::qWait(300);
    backend.setWatching(false);
    QVERIFY(click("nav_sources")); QCOMPARE(window->property("page").toString(),QString("sources"));
    QVERIFY(click("openConversationImport"));
    auto *conversation = window->findChild<QQuickItem *>("conversationText");
    QVERIFY(conversation);
    conversation->setProperty("text", "User: Keep the checkpoint local.\nAssistant: Saved the checkpoint.");
    QVERIFY(click("saveConversationButton"));
    QTRY_COMPARE(backend.stats().value("events").toInt(), 2);
    backend.search("checkpoint");
    QTRY_VERIFY(!backend.results().isEmpty());
    const auto result = backend.results().first().toMap();
    const QString citation = result.value("url").toString();
    QVERIFY(citation.contains('#'));
    QVERIFY(backend.readPage(citation).contains("Read the full conversation"));
    QTRY_VERIFY(!conversation->isVisible());
    QVERIFY(click("nav_home"));
    QTRY_COMPARE(window->property("page").toString(), QString("home"));
    QTRY_VERIFY(window->findChild<QQuickItem *>("resumeSessionPicker"));
    QTRY_VERIFY(window->findChild<QQuickItem *>("editResumeHandoff")->isVisible());
    QTest::qWait(150);
    QVERIFY(click("editResumeHandoff"));
    auto *nextAction = window->findChild<QQuickItem *>("handoffNextAction");
    QVERIFY(nextAction);
    QTRY_VERIFY(nextAction->isVisible());
    QTest::qWait(200);
    nextAction->setProperty("text", "Check captured turn IDs.");
    QVERIFY(click("saveResumeHandoff"));
    QTRY_VERIFY(backend.readHandoff().value("text").toString().contains("Check captured turn IDs."));
  }
  void searchKeepsLatestQueryAndRequiresExplicitCrossProjectScope() {
    QTemporaryDir current, other, logs, preferences;
    Store first(current.path(), true);
    Store second(other.path(), true);
    first.importConversation("User: The orchard is ready.");
    second.importConversation("User: The scalene triangle is documented.");
    Backend backend(nullptr, nullptr, preferences.filePath("preferences.json"), {logs.path(), logs.path()});
    QVERIFY(backend.openProject(other.path()));
    QVERIFY(backend.openProject(current.path()));
    backend.sync();
    backend.search("orchard");
    backend.search("scalene");
    QTRY_VERIFY(!backend.searching());
    QVERIFY(backend.results().isEmpty());
    backend.searchProjects("scalene", true);
    QTRY_VERIFY(!backend.searching());
    QVERIFY(!backend.results().isEmpty());
    const auto entry = backend.results().first().toMap();
    QCOMPARE(entry.value("archive").toString(), second.root());
    QCOMPARE(backend.project(), first.root());
    QVERIFY(backend.readPage(entry.value("url").toString(), second.root()).contains("scalene triangle"));
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
    if (qEnvironmentVariableIsSet("LORE_TEST_ARGS_FILE")) { QFile log(qEnvironmentVariable("LORE_TEST_ARGS_FILE")); if (log.open(QIODevice::Append)) log.write((args.join(" ") + "\n").toUtf8()); }
    if (provider == "codex" && (!args.contains("--ephemeral") || !args.contains("--sandbox"))) return 1;
    if (provider == "claude" && (!args.contains("--no-session-persistence") || !args.contains("--strict-mcp-config"))) return 1;
    QJsonObject payload;
    if(prompt.startsWith("Verify one recorded project memory")) {
      const auto found=QRegularExpression("\\[((?:file|memory):[^\\]]+)\\]").match(prompt);
      if(!found.hasMatch() || prompt.contains("[event:")) return 1;
      payload={{"verdict","supported"},{"answer","The session module stores sessions locally ["+found.captured(1)+"]"},{"refs",QJsonArray{found.captured(1)}}};
    } else if (prompt.startsWith("FUNCTION INDEX") && qEnvironmentVariableIsSet("LORE_TEST_FUNCTION_TIMEOUTS")) {
      const auto excerpt = prompt.section("\nMain excerpt:\n", 1);
      if (prompt.contains("File (data): b.cpp\n")
          || (prompt.contains("File (data): a.cpp\n") && excerpt.startsWith("1 | ") && excerpt.contains("\n25 | ")))
        QThread::sleep(30);
      payload = {{"functions", QJsonArray{}}};
    } else if(prompt.startsWith("Answer the user's project question")) {
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
      : QJsonObject{{"type","result"},{"result",result},{"is_error",false},{"usage",QJsonObject{{"input_tokens",900},{"cache_read_input_tokens",100},{"output_tokens",50}}},{"modelUsage",QJsonObject{{"claude-test",QJsonObject{{"contextWindow",200000}}}}}};
    output << QJsonDocument(response).toJson(QJsonDocument::Compact) << '\n';
    if (provider == "codex") output << QJsonDocument(QJsonObject{{"type","turn.completed"},{"usage",QJsonObject{{"input_tokens",700},{"output_tokens",40}}}}).toJson(QJsonDocument::Compact) << '\n';
    return 0;
  }
  QGuiApplication app(argc,argv); QQuickStyle::setStyle("Material");
  // Only tests with an HTTP fixture should probe a local model server.
  qputenv("LORE_LLM_URL", "disabled-for-tests");
  qunsetenv("LORE_LLM_MODEL");
  QTemporaryDir workspaces; qputenv("LORE_WORKSPACE_ROOT",workspaces.path().toUtf8());
  qputenv("LORE_BOARD_STREAM", "0");
  qputenv("LORE_LLM_DISCOVER", "0");
  qputenv("LORE_LOCAL_BACKUP", "none");  // A test opts in; no test falls back to a real Claude Code or Codex.  // Only the fixtures a test names, never real servers on this machine.
  LoreTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "lore_tests.moc"
