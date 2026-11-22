import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
  id: root
  objectName: "transcriptAnalysisPage"
  signal read(string title, string text)
  readonly property var job: backend.knowledgeProgress
  readonly property bool analyzing: job.kind === "transcript"
  readonly property var conversations: [{ id: "", title: qsTr("All saved conversations") }].concat(backend.sessions)
  clip: true
  contentWidth: availableWidth

  ColumnLayout {
    width: root.availableWidth
    spacing: 12
    Card {
      heading: qsTr("Analyze saved conversations")
      Body { text: qsTr("Extract decisions, corrections, results, and open questions. Each finding cites saved turns.") }
      ModelSelector {
        id: selector
        objectName: "analysisConversation"
        Layout.fillWidth: true
        model: root.conversations.map(function(session) { return session.title })
        currentIndex: 0
        enabled: !backend.busy
      }
      KnowledgeModelOptions { id: modelChoice; task: "transcript"; enabled: !backend.busy }
      Body { visible: modelChoice.provider !== "local"; text: qsTr("The selected provider will receive excerpts of saved conversations.") }
      RowLayout {
        Layout.fillWidth: true
        Switch {
          id: autoPublish
          objectName: "transcriptAutoPublish"
          text: qsTr("Automatically publish findings")
          checked: backend.transcriptAutoPublish
          enabled: !backend.busy
          onToggled: backend.setTranscriptPublishing(checked, threshold.value)
        }
        Label { text: qsTr("Minimum confidence"); color: theme.muted }
        SpinBox {
          id: threshold
          objectName: "transcriptThreshold"
          from: 1
          to: 100
          value: backend.transcriptThreshold
          enabled: autoPublish.checked && !backend.busy
          editable: true
          onValueModified: backend.setTranscriptPublishing(autoPublish.checked, value)
        }
      }
      Body { text: autoPublish.checked
        ? qsTr("Findings below the threshold go to review. Confidence is the model’s estimate.")
        : qsTr("Review each finding before adding it to the wiki.") }
      CheckBox { id: rerun; text: qsTr("Analyze already processed turns again"); enabled: !backend.busy }
      RowLayout {
        PrimaryButton {
          objectName: "analyzeTranscriptsButton"
          text: qsTr("Analyze")
          enabled: backend.sessions.length > 0 && modelChoice.ready && !backend.busy && !backend.capturing
          onClicked: backend.startTranscriptAnalysis(root.conversations[Math.max(0, selector.currentIndex)].id, modelChoice.provider, rerun.checked)
        }
        Button { visible: root.analyzing; text: qsTr("Stop"); onClicked: backend.cancel() }
      }
      Body { visible: root.analyzing; text: root.job.current || qsTr("Preparing conversations…") }
      ProgressBar {
        visible: root.analyzing
        Layout.fillWidth: true
        indeterminate: !root.job.total
        to: Math.max(1, root.job.total || 0)
        value: root.job.done || 0
      }
    }
    Caption { text: qsTr("Findings to review (%1)").arg(backend.transcriptFindings.length) }
    Body { visible: !backend.transcriptFindings.length; text: qsTr("No findings waiting for review.") }
    Repeater {
      model: backend.transcriptFindings
      delegate: Card {
        id: finding
        required property var modelData
        heading: modelData.title
        caption: qsTr("%1 · %2% confidence").arg(modelData.kind).arg(modelData.confidence)
        Body { text: finding.modelData.body; textFormat: Text.PlainText; color: theme.ink }
        Body { text: finding.modelData.conversation; textFormat: Text.PlainText }
        RowLayout {
          Button {
            text: qsTr("Read cited turns")
            onClicked: root.read(finding.modelData.conversation, backend.transcriptEvidence(finding.modelData.id))
          }
          Button { text: qsTr("Dismiss"); enabled: !backend.busy && !backend.capturing; onClicked: backend.reviewTranscriptFinding(finding.modelData.id, false) }
          PrimaryButton { text: qsTr("Add to wiki"); enabled: !backend.busy && !backend.capturing; onClicked: backend.reviewTranscriptFinding(finding.modelData.id, true) }
        }
      }
    }
  }
}
