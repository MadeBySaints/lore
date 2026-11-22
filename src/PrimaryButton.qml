import QtQuick
import QtQuick.Controls

// The one main action of a view, in the accent colour.
Button {
  id: primary
  font.pixelSize: 13
  font.bold: true
  leftPadding: 16; rightPadding: 16; topPadding: 12; bottomPadding: 12
  background: Rectangle {
    implicitWidth: 110
    implicitHeight: 42
    radius: Math.min(theme.radius, 10)
    border.color: primary.enabled && primary.visualFocus ? theme.ink : theme.edge
    gradient: Gradient {
      GradientStop { position: 0; color: primary.enabled ? Qt.lighter(theme.accent, primary.hovered ? 1.26 : 1.14) : theme.disabled }
      GradientStop { position: 1; color: primary.enabled ? Qt.darker(theme.accent, primary.down ? 1.36 : 1.20) : theme.disabled }
    }
  }
  contentItem: Label { text: primary.text; font: primary.font; color: primary.enabled ? theme.accentForeground : theme.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
}
