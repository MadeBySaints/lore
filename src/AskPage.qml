import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
  id: root
  signal read(string title, string text)
  signal previewRequested()
  spacing: 12
  Label { text: qsTr("Ask about decisions, unfinished work, or where something lives. Answers use your selected coding tool and its model quota."); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true }
  ScrollView { Layout.fillWidth: true; implicitHeight: 95
    TextArea { id: question; objectName: "askQuestion"; placeholderText: qsTr("Why did we choose this approach?"); wrapMode: TextEdit.Wrap; selectByMouse: true }
  }
  RowLayout {
    Button { Material.roundedScale: theme.radius; objectName: "askProjectButton"; text: qsTr("Ask project"); highlighted: true; enabled: question.text.trim().length > 0 && !backend.showBusy && !backend.showCapturing; onClicked: backend.ask(question.text) }
    Button { Material.roundedScale: theme.radius; text: qsTr("Preview evidence first"); enabled: question.text.trim().length > 0 && !backend.showBusy && !backend.showCapturing; onClicked: { backend.previewContext(question.text); root.previewRequested() } }
    Label { visible: backend.showBusy; text: qsTr("Working… You can cancel from the top bar."); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true }
  }
  Label { text: backend.answer.insufficient ? qsTr("More evidence needed") : qsTr("Answer with references"); visible: !!backend.answer.answer; color: theme.accent; font.pixelSize: 16 }
  ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; clip: true
    TextArea { text: backend.answer.answer || qsTr("Capture conversations or index your project files to give Lore evidence. It will say when it cannot support an answer."); readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: "#e8ede5"; background: null; padding: 12 }
  }
  ListView { visible: (backend.answer.refs || []).length > 0; Layout.fillWidth: true; Layout.preferredHeight: Math.min(160,(backend.answer.refs || []).length * 44); clip: true; model: backend.answer.refs || []
    delegate: Button { Material.roundedScale: theme.radius; required property var modelData; width: ListView.view.width; text: modelData.title; onClicked: root.read(modelData.title, modelData.body + "\n\n" + qsTr("Evidence ID: %1").arg(modelData.id)) }
  }
}
