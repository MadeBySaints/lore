import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A labelled number on the Home page.
Rectangle {
  property string caption
  property string value
  property string detail: ""
  Layout.fillWidth: true
  implicitHeight: 96
  color: theme.card; border.color: theme.edge; radius: theme.radius
  Column { anchors.fill: parent; anchors.margins: 16; spacing: 6
    Caption { text: caption }
    Label { text: value; color: theme.ink; font.pixelSize: 26 }
    Label { visible: detail.length > 0; text: detail; color: theme.muted; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width }
  }
}
