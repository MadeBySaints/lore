import QtQuick
import QtQuick.Controls

ModelSelector {
  id: box
  required property string kind
  readonly property string selectedId: kind === "build" ? backend.buildPreset : backend.verifyPreset
  readonly property var presets: [
    { id: "", name: qsTr("Balanced"), text: qsTr("Use Lore’s standard prompt.") }
  ].concat(backend.promptPresets.filter(function(preset) { return preset.purpose === box.kind }))
  readonly property int selectedIndex: Math.max(0, presets.findIndex(function(preset) { return preset.id === box.selectedId }))
  readonly property var selected: presets[selectedIndex]

  Accessible.name: kind === "build" ? qsTr("Wiki build preset") : qsTr("Verification preset")
  model: presets.map(function(preset) { return preset.name })
  currentIndex: selectedIndex
  implicitWidth: 230
  displayText: kind === "build" ? qsTr("Build preset · %1").arg(selected.name) : qsTr("Check preset · %1").arg(selected.name)

  ToolTip.visible: hovered
  ToolTip.delay: 600
  ToolTip.text: selected.text

  onActivated: function(index) {
    const id = presets[index].id
    if (kind === "build") backend.buildPreset = id
    else backend.verifyPreset = id
    currentIndex = Qt.binding(function() { return box.selectedIndex })
  }
}
