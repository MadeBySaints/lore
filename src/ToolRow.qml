import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One AI tool: whether it is ready, what it builds and answers with, and the
// actions to install, sign in, or locate it. A local model also gets its
// server address and model.
ColumnLayout {
  id: row
  property var entry
  Layout.fillWidth: true; spacing: 6
  RowLayout {
    Layout.fillWidth: true
    Rectangle { width: 8; height: 8; radius: 4; color: row.entry.state === "ready" ? theme.accent : row.entry.state === "missing" ? "#bd9971" : theme.muted }
    Label { text: row.entry.name; color: theme.ink; font.pixelSize: 16; font.bold: true; Layout.fillWidth: true }
    RadioButton { objectName: "learnWith_" + row.entry.id; autoExclusive: false; text: qsTr("Build wiki"); checked: backend.learningProvider === row.entry.id; enabled: row.entry.available; onClicked: { backend.learningProvider = row.entry.id; checked = Qt.binding(function() { return backend.learningProvider === row.entry.id }) } }
    RadioButton { objectName: "answerWith_" + row.entry.id; visible: !backend.personalSetup; autoExclusive: false; text: qsTr("Ask project"); checked: backend.answerProvider === row.entry.id; enabled: row.entry.available; onClicked: { backend.answerProvider = row.entry.id; checked = Qt.binding(function() { return backend.answerProvider === row.entry.id }) } }
    Button { text: qsTr("Install"); visible: !row.entry.available; onClicked: backend.tools.install(row.entry.id) }
    Button { text: qsTr("Sign in"); visible: row.entry.id !== "local" && row.entry.available && row.entry.state !== "ready"; enabled: !backend.tools.checking; onClicked: backend.tools.signIn(row.entry.id) }
    Button { text: qsTr("Locate…"); flat: true; visible: row.entry.id !== "local"; enabled: !backend.selecting && !backend.tools.checking; onClicked: backend.locateTool(row.entry.id) }
  }
  Body { text: row.entry.detail }
  // Every Ollama server Lore found and its models; a task's model runs on
  // the server that has it.
  ColumnLayout {
    objectName: "localServers"; visible: row.entry.id === "local" && (row.entry.servers || []).length > 0; Layout.fillWidth: true; spacing: 2
    Repeater { model: row.entry.servers || []
      delegate: Label { required property var modelData; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.soft; font.pixelSize: 12
        text: modelData.kind + " · " + String(modelData.url).replace("http://", "") + " · "
          + (modelData.models.length ? modelData.models.map(function(m) { return m.name }).join(", ") : qsTr("no chat model")) } }
  }
  RowLayout {
    visible: row.entry.id === "local"; Layout.fillWidth: true; spacing: 8
    Label { text: qsTr("Default"); color: theme.ink; font.pixelSize: 13 }
    ModelSelector { objectName: "localModelBox"; model: row.entry.models || []; Layout.fillWidth: true; enabled: (row.entry.models || []).length > 0
      currentIndex: (row.entry.models || []).indexOf(row.entry.model); displayText: currentIndex < 0 ? qsTr("Choose a model") : currentText
      onActivated: function(index) { backend.tools.setLocalModel(row.entry.models[index]) } }
    Button { objectName: "findLocalModels"; text: backend.tools.checking ? qsTr("Looking…") : qsTr("Find models"); enabled: !backend.tools.checking; onClicked: backend.tools.check() }
  }
  RowLayout {
    visible: row.entry.id === "local"; Layout.fillWidth: true; spacing: 8
    Label { text: qsTr("Also look at"); color: theme.ink; font.pixelSize: 13 }
    TextField { objectName: "localUrlField"; text: row.entry.url || ""; placeholderText: "http://127.0.0.1:11434"; selectByMouse: true; Layout.fillWidth: true; enabled: !backend.tools.checking; onEditingFinished: backend.tools.setLocalUrl(text) }
  }
  ColumnLayout {
    visible: row.entry.id === "local"; Layout.fillWidth: true; spacing: 6
    Label { text: qsTr("Backup when the local model is unavailable"); color: theme.muted; font.pixelSize: 13 }
    ModelSelector { objectName: "localBackupBox"; Layout.fillWidth: true
      readonly property var choices: ["auto", "claude", "codex", "none"]
      model: [qsTr("Claude Code or Codex, whichever is ready"), "Claude Code", "Codex", qsTr("Nothing; wait for it")]
      currentIndex: Math.max(0, choices.indexOf(row.entry.backup || "auto"))
      onActivated: function(index) { backend.tools.setLocalBackup(choices[index]) } }
  }
  Body { visible: row.entry.id === "local"; text: qsTr("Lore runs local models on Ollama. It looks for Ollama on its usual port and at the address above, and again every few minutes, so starting Ollama later is enough. Each task's model runs on the server that has it. With a backup chosen, a task whose local model is not running uses Claude Code or Codex instead, and a call whose server stops or is too busy continues there.") }
  ModelOptions { visible: !!row.entry.id && row.entry.id !== "local"; provider: row.entry.id || "" }
}
