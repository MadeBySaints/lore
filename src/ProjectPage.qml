import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
  id: root
  signal read(string title, string text)
  spacing: 12
  Label { text: qsTr("Index code and documents to connect memory to files. Git ignore rules are respected; common secrets and generated folders are excluded. Review your files before using Ask: selected excerpts may be sent to your coding tool."); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true }
  RowLayout {
    Button { Material.roundedScale: theme.radius; objectName: "indexProjectButton"; text: backend.files.length ? qsTr("Refresh file index") : qsTr("Index project files"); enabled: !backend.busy && !backend.capturing; onClicked: backend.indexProject() }
    Label { text: qsTr("%1 files · Last indexed: %2").arg(backend.files.length).arg(backend.stats.indexedAt || qsTr("Never")); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true }
  }
  Label { text: qsTr("After the first index, Lore checks for changes each minute while capture is on. A changed file flags related memories for review; it does not prove a decision is wrong."); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true }
  ListView { Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; clip: true; spacing: 8; model: backend.files
    delegate: Rectangle {
      required property var modelData
      width: ListView.view.width; implicitHeight: fileColumn.implicitHeight + 24; color: "#192019"; border.color: "#303a31"; radius: theme.radius
      ColumnLayout { id: fileColumn; anchors.fill: parent; anchors.margins: 12; spacing: 4
        Label { text: modelData.path; color: "#e8ede5"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { text: modelData.symbols || qsTr("Code or document"); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true; maximumLineCount: 2; elide: Text.ElideRight }
      }
    }
  }
  Label { text: qsTr("Search also finds indexed file excerpts. File indexing is local and does not call a model."); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true }
}
