#include "store.h"
#include "backend.h"
#include "theme.h"
#include "translator.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

static QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n'; }
static QByteArray claude(const QString &root, const QString &role, const QString &text) {
  return json({{"type",role},{"cwd",root},{"timestamp","2026-11-18T10:00:00Z"},{"message",QJsonObject{{"role",role},{"content",QJsonArray{QJsonObject{{"type","text"},{"text",text}}}}}}});
}
static QJsonObject entry(const QString &ref) {
  return {{"entries",QJsonArray{QJsonObject{{"title","Database choice"},{"kind","decision"},{"body","Use SQLite for local storage."},{"refs",QJsonArray{ref}}}}}};
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
    QVERIFY(QFile::link(outside.path(),p.filePath(".lore")));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,Store(p.path(),true));
    QFile::remove(p.filePath(".lore")); Store store(p.path(),true);
    Store::write(outside.filePath("target"),"keep");
    QVERIFY(QFile::link(outside.filePath("target"),p.filePath("BRAIN.md")));
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
    const QString executable = fake.filePath("codex");
    Store::write(executable,R"PY(#!/usr/bin/python3
import sys,json
prompt=sys.stdin.read()
events=json.loads(prompt.split('NEW EVENTS:\n',1)[1])
result={'entries':[{'title':'Storage','kind':'decision','body':'SQLite','refs':[events[0]['id']]}]}
print(json.dumps({'type':'item.completed','item':{'type':'agent_message','text':json.dumps(result)}}))
)PY");
    QFile::setPermissions(executable,QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QByteArray oldPath = qgetenv("PATH"); qputenv("PATH",fake.path().toUtf8());
    Backend backend; QVERIFY(backend.openProject(p.path())); QSignalSpy finished(&backend,&Backend::learningFinished);
    backend.learn("codex"); QTRY_COMPARE(finished.count(),1);
    qputenv("PATH",oldPath);
    QVERIFY2(finished.first().first().toBool(),qPrintable(backend.status()));
    QCOMPARE(backend.memories().size(),1);
  }
  void realQmlLoadsAndKeyboardSearchWorks() {
    QTemporaryDir p; Backend backend; QVERIFY(backend.openProject(p.path(),true));
    Theme theme(p.path()); Translator translator(p.filePath("language"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator); engine.rootContext()->setContextProperty("initialProject",p.path());
    engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->show(); QTest::qWait(200); QTest::keyClick(window,Qt::Key_K,Qt::ControlModifier);
    QCOMPARE(window->property("page").toInt(),3);
    for (const auto key : {Qt::Key_S,Qt::Key_Q,Qt::Key_L}) QTest::keyClick(window,key);
    QTest::keyClick(window,Qt::Key_Escape); QVERIFY(!window->property("helpVisible").toBool());
  }
};

int main(int argc, char **argv) {
  QGuiApplication app(argc,argv); QQuickStyle::setStyle("Material");
  LoreTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "lore_tests.moc"
