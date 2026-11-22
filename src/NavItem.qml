import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts

// One entry in the sidebar: icon, name, and an optional count or dot.
Rectangle {
  id: item
  property string pageId
  property string label
  property string icon
  property bool current: false
  property bool available: true
  property int badge: 0
  property bool dot: false
  signal activated()
  objectName: "nav_" + pageId
  Layout.fillWidth: true; implicitHeight: 32; radius: theme.radius
  color: current ? theme.selected : mouse.containsMouse && available ? theme.hover : "transparent"
  Accessible.role: Accessible.Button; Accessible.name: label
  Rectangle { visible: item.current; width: 3; height: 16; radius: 2; color: theme.accent; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
  RowLayout {
    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8; spacing: 10
    opacity: item.available ? 1 : 0.45
    IconImage { source: "qrc:/icons/" + item.icon + ".svg"; sourceSize: Qt.size(16, 16); Layout.preferredWidth: 16; Layout.preferredHeight: 16; color: item.current ? theme.accent : theme.muted }
    Label { text: item.label; color: theme.ink; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
    Rectangle { visible: item.dot; width: 7; height: 7; radius: 4; color: theme.accent }
    Rectangle { visible: item.badge > 0; radius: 9; color: item.current ? theme.accent : theme.edge; implicitWidth: Math.max(20, badgeText.implicitWidth + 10); implicitHeight: 18
      Label { id: badgeText; anchors.centerIn: parent; text: item.badge > 999 ? "999+" : item.badge; font.pixelSize: 10; color: item.current ? theme.accentForeground : theme.ink } }
  }
  MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; enabled: item.available; cursorShape: Qt.PointingHandCursor; onClicked: item.activated() }
}
