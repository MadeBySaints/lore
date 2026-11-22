import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A panel with an optional caption and heading; its children stack in a column.
Rectangle {
  id: card
  property string caption: ""
  property string heading: ""
  property bool highlighted: false
  property bool done: false
  property int padding: 18
  default property alias content: column.data
  property alias spacing: column.spacing
  Layout.fillWidth: true
  implicitHeight: column.implicitHeight + padding * 2
  color: theme.card; border.color: highlighted ? theme.accent : theme.edge; radius: theme.radius
  ColumnLayout {
    id: column
    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: card.padding
    spacing: 10
    Caption { visible: card.caption.length > 0; text: (card.done ? "✓  " : "") + card.caption; color: card.done ? theme.accent : theme.muted; Layout.fillWidth: true; elide: Text.ElideRight }
    Label { visible: card.heading.length > 0; text: card.heading; textFormat: Text.PlainText; color: theme.ink; font.pixelSize: 17; font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true }
  }
}
