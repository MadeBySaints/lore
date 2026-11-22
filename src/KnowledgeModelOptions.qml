import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
  id: root
  required property string task
  readonly property string provider: task === "functions" ? backend.functionScanProvider : backend.transcriptAnalysisProvider
  readonly property bool ready: {
    const revision = backend.modelRevision
    const tools = backend.tools.entries
    return provider === "local" ? backend.localReady(task) : backend.modelReady(task, provider)
  }
  Layout.fillWidth: true
  spacing: 6

  RowLayout {
    Layout.fillWidth: true
    Label { text: qsTr("Provider"); color: theme.muted }
    ModelSelector {
      objectName: root.task + "Provider"
      Layout.fillWidth: true
      model: ["Claude Code", "Codex", qsTr("Ollama")]
      currentIndex: ["claude", "codex", "local"].indexOf(root.provider)
      onActivated: function(index) { backend.setKnowledgeProvider(root.task, ["claude", "codex", "local"][index]) }
    }
  }
  ModelOptions { task: root.task; provider: root.provider }
}
