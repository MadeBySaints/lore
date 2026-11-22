import QtQuick
import QtQuick.Controls

// Model IDs can be longer than the control. Wrap both the selected value
// and the popup choices, including names with no spaces.
ComboBox {
  id: control
  Accessible.name: displayText
  implicitWidth: 220
  padding: 10
  leftPadding: mirrored ? 30 : 10
  rightPadding: mirrored ? 10 : 30
  contentItem: Label {
    objectName: control.objectName + "Text"
    text: control.displayText
    textFormat: Text.PlainText
    color: control.enabled ? theme.ink : theme.muted
    font: control.font
    wrapMode: Text.Wrap
    verticalAlignment: Text.AlignVCenter
  }
  background: Rectangle {
    implicitWidth: 170
    implicitHeight: 42
    radius: Math.min(theme.radius, 9)
    color: control.hovered ? theme.hover : theme.inset
    border.color: control.visualFocus || control.popup.visible ? theme.accent : theme.edge
  }
  delegate: ItemDelegate {
    id: option
    required property int index
    width: ListView.view.width
    text: control.textAt(index)
    highlighted: control.highlightedIndex === index
    leftPadding: 12
    rightPadding: 12
    contentItem: Label {
      text: option.text
      textFormat: Text.PlainText
      color: control.currentIndex === option.index ? theme.accent : theme.ink
      wrapMode: Text.Wrap
      verticalAlignment: Text.AlignVCenter
    }
  }
}
