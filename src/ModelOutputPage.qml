import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
  id: root
  objectName: "modelOutputPage"
  spacing: 12
  readonly property var call: backend.activeModelCall
  readonly property bool running: !!call.provider
  readonly property bool jobActive: running || backend.knowledgeProgress.active || backend.buildProgress.active || backend.verifyProgress.active
  property bool issuesOnly: false
  onIssuesOnlyChanged: outputText.updateFromBackend(true)

  function providerName(id) {
    if (id === "claude") return "Claude Code"
    if (id === "codex") return "Codex"
    return "Ollama"
  }

  Card {
    padding: 12
    spacing: 4
    RowLayout {
      Layout.fillWidth: true
      ColumnLayout {
        Layout.fillWidth: true
        spacing: 4
        Label {
          objectName: "activeModelLabel"
          Layout.fillWidth: true
          text: root.running
            ? qsTr("%1 · %2").arg(root.providerName(root.call.provider)).arg(root.call.model || qsTr("Provider default"))
            : qsTr("Model output")
          textFormat: Text.PlainText
          color: root.running ? theme.accent : theme.ink
          font.bold: true
          wrapMode: Text.Wrap
        }
        Body {
          text: root.running
            ? root.call.task + (root.call.effort ? qsTr(" · %1 effort").arg(root.call.effort) : "")
            : root.jobActive ? qsTr("Preparing the next model call…") : qsTr("No model running")
        }
      }
      CheckBox {
        objectName: "terminalIssuesOnly"
        text: qsTr("Issues only (%1)").arg(backend.liveIssues.length)
        checked: root.issuesOnly
        onToggled: root.issuesOnly = checked
      }
      Button {
        text: qsTr("Jump to latest")
        flat: true
        visible: outputText.length > 0 && !output.atEnd
        onClicked: output.toEnd()
      }
      Button {
        objectName: "stopTerminalModel"
        text: qsTr("Stop")
        visible: root.jobActive
        onClicked: backend.cancel()
      }
    }
  }

  Rectangle {
    Layout.fillWidth: true
    Layout.fillHeight: true
    radius: theme.radius
    color: theme.well
    border.color: theme.edge
    Flickable {
      id: output
      anchors.fill: parent
      anchors.margins: 14
      clip: true
      boundsBehavior: Flickable.StopAtBounds
      contentWidth: width
      contentHeight: outputText.height
      readonly property bool atEnd: contentY >= contentHeight - height - 24

      function toEnd() { contentY = Math.max(0, contentHeight - height) }
      ScrollBar.vertical: ScrollBar {}

      TextEdit {
        id: outputText
        objectName: "modelOutputText"
        width: output.width - 8
        height: Math.max(output.height, contentHeight)
        readOnly: true
        selectByMouse: true
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.WrapAnywhere
        color: theme.ink
        selectionColor: theme.selected
        selectedTextColor: theme.ink
        font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"
        font.pixelSize: 13
        Accessible.name: qsTr("Live model output")

        function applyTerminalColors() {
          backend.styleTerminal(textDocument, theme.accent, theme.warning, theme.caution)
        }

        function updateFromBackend(jump) {
          const follow = jump || output.atEnd
          const snapshot = root.issuesOnly
            ? backend.liveIssues.map(function(issue) { return issue.text }).join("\n")
            : backend.liveOutput
          if (snapshot.length >= length && snapshot.startsWith(text)) insert(length, snapshot.slice(length))
          else text = snapshot
          if (follow || !snapshot) Qt.callLater(output.toEnd)
        }
        Component.onCompleted: {
          applyTerminalColors()
          updateFromBackend(true)
        }
      }
      Connections {
        target: backend
        function onLiveChanged() { outputText.updateFromBackend(false) }
      }
      Connections { target: theme; function onChanged() { outputText.applyTerminalColors() } }
    }
    Label {
      anchors.top: parent.top
      anchors.left: parent.left
      anchors.margins: 14
      width: parent.width - 28
      visible: outputText.length === 0
      text: root.issuesOnly ? qsTr("No terminal issues recorded.")
        : qsTr("Start a wiki build, verification, function scan, or transcript analysis to see the model’s output here.")
      color: theme.muted
      wrapMode: Text.Wrap
    }
  }
}
