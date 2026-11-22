import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
  id: root
  objectName: "personalWikiSettings"
  clip: true
  contentWidth: availableWidth
  ColumnLayout {
    width: root.availableWidth
    spacing: 16
    Card {
      heading: qsTr("Personal wiki")
      Body { text: backend.project; textFormat: Text.PlainText }
      Switch { objectName: "personalCaptureSwitch"; text: qsTr("Capture saved conversations from live hooks"); checked: backend.personalCapture; onToggled: backend.personalCapture = checked }
      Button { text: qsTr("Reconnect capture hooks"); enabled: !backend.busy; onClicked: backend.reconnectPersonalHooks() }
      Body { text: qsTr("Capture can run while Lore is closed. In Codex, review and trust the hooks when prompted.") }
    }
    Card {
      heading: qsTr("AI for wiki builds")
      RowLayout {
        Button { text: qsTr("Check tools"); enabled: !backend.tools.checking; onClicked: backend.tools.check() }
      }
      Repeater { model: backend.tools.entries; delegate: ToolRow { required property var modelData; entry: modelData } }
      ModelOptions { task: "build"; provider: backend.learningProvider }
    }
  }
}
