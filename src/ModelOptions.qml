import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
  id: root
  required property string provider
  property string task: ""
  readonly property var tools: backend.tools.entries
  readonly property var entry: tools.find(function(tool) { return tool.id === provider }) || {}
  readonly property var choice: {
    const state = backend.modelRevision
    const connectedTools = tools
    if (task) return backend.modelChoice(task, provider)
    return { modelOverride: entry.agentModel || "", effortOverride: entry.agentEffort || "",
      levels: backend.tools.effortLevels(provider, "") }
  }
  readonly property string defaultModelLabel: choice.defaultModel ? qsTr("Default (%1)").arg(choice.defaultModel) : qsTr("Default")
  readonly property string defaultEffortLabel: choice.defaultEffort ? qsTr("Default (%1)").arg(choice.defaultEffort) : qsTr("Default")
  readonly property var modelIds: {
    if (provider === "local") return Array.from(entry.models || [])
    const connectedTools = tools
    return Array.from(backend.tools.agentModels(provider))
  }
  readonly property var modelValues: {
    const ids = [""].concat(modelIds)
    const saved = choice.modelOverride || ""
    if (provider !== "local" && !ids.includes(saved)) ids.push(saved)
    return ids
  }
  readonly property var efforts: [defaultEffortLabel].concat(Array.from(choice.levels || []))
  spacing: 5
  Layout.fillWidth: true

  function save(field, value) {
    if (task) backend.setModelChoice(task, provider, field, value)
    else if (field === "model") backend.tools.setAgentModel(provider, value)
    else backend.tools.setAgentEffort(provider, value)
  }
  GridLayout {
    id: controls
    Layout.fillWidth: true
    columns: width >= 645 && effort.visible ? 4 : 2
    columnSpacing: 10
    rowSpacing: 6
    Label { text: qsTr("Model"); color: theme.muted }
    ModelSelector {
      objectName: root.provider === "local" ? root.task + "LocalModel"
        : root.task ? root.task + "ModelPicker" : "agentModel_" + root.provider
      Layout.fillWidth: true
      model: [root.defaultModelLabel].concat(root.modelValues.slice(1))
      currentIndex: Math.max(0, root.modelValues.indexOf(root.choice.modelOverride || ""))
      onActivated: function(index) { root.save("model", root.modelValues[index]) }
    }
    Label { visible: effort.visible; text: qsTr("Effort"); color: theme.muted }
    ModelSelector {
      id: effort
      objectName: (root.task || "agent") + "Effort_" + root.provider
      visible: root.efforts.length > 1
      Layout.fillWidth: true
      Layout.preferredWidth: controls.columns === 4 ? 135 : 220
      Layout.maximumWidth: controls.columns === 4 ? 185 : Infinity
      model: root.efforts
      currentIndex: Math.max(0, root.efforts.indexOf(root.choice.effortOverride || ""))
      onActivated: function(index) { root.save("effort", index === 0 ? "" : root.efforts[index]) }
      ToolTip.visible: hovered
      ToolTip.text: qsTr("Available effort levels depend on the model and provider.")
    }
  }
}
