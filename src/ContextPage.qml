import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
  id: root
  signal read(string title, string text)
  spacing: 12
  Label { text: qsTr("Inspect what Lore prepares and what its hooks returned to Claude Code. A recorded hook response does not prove the model followed or read every reference."); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true }
  RowLayout { Layout.fillWidth: true
    TextField { id: query; objectName: "contextQuery"; Layout.fillWidth: true; placeholderText: qsTr("Describe your next task…"); selectByMouse: true }
    Button { Material.roundedScale: theme.radius; objectName: "previewContextButton"; text: qsTr("Preview context"); enabled: !backend.busy && !backend.capturing; onClicked: backend.previewContext(query.text) }
  }
  Button { Material.roundedScale: theme.radius; visible: !!backend.preview.text; text: qsTr("Open prepared briefing (%1)").arg(backend.preview.retrieval || ""); onClicked: root.read(qsTr("Context preview · not sent"),backend.preview.text) }
  Label { visible: !!backend.preview.warning; text: backend.preview.warning || ""; color: "#d8b888"; wrapMode: Text.Wrap; Layout.fillWidth: true }
  Label { text: qsTr("Recent hook responses"); color: "#e8ede5"; font.pixelSize: 17 }
  ListView { Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; clip: true; spacing: 8; model: backend.deliveries
    delegate: Button {
      required property var modelData
      width: ListView.view.width; text: modelData.event + " · " + modelData.time + " · " + qsTr("%1 excerpts").arg(modelData.evidence.length)
      onClicked: root.read(qsTr("Hook context · %1").arg(modelData.event),modelData.text)
    }
    Label { visible: backend.deliveries.length === 0; anchors.centerIn: parent; width: parent.width - 20; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; text: qsTr("No hook responses recorded yet. Start a new Claude Code session in the connected project. Capture alone does not confirm context delivery."); color: "#9aa69b" }
  }
}
