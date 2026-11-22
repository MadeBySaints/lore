#include "filepicker.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QUrl>
#include <QUuid>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

void NativeFilePicker::choose(bool directory, const QString &title) {
#ifdef Q_OS_WIN
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) { emit failed(tr("Windows could not open the file chooser.")); return; }
  IFileDialog *dialog = nullptr;
  const auto cleanup = qScopeGuard([&] { if (dialog) dialog->Release(); if (SUCCEEDED(initialized)) CoUninitialize(); });
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    emit failed(tr("Windows could not open the file chooser.")); return;
  }
  DWORD flags = 0; dialog->GetOptions(&flags);
  dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (directory ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
  dialog->SetTitle(title.toStdWString().c_str());
  const HWND parent = QGuiApplication::focusWindow() ? reinterpret_cast<HWND>(QGuiApplication::focusWindow()->winId()) : nullptr;
  const HRESULT shown = dialog->Show(parent);
  if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) { emit cancelled(); return; }
  IShellItem *item = nullptr;
  if (FAILED(shown) || FAILED(dialog->GetResult(&item))) { emit failed(tr("The selected location could not be read.")); return; }
  const auto releaseItem = qScopeGuard([&] { item->Release(); });
  PWSTR path = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { emit failed(tr("Choose a folder or program on your computer.")); return; }
  const QString selectedPath = QString::fromWCharArray(path); CoTaskMemFree(path);
  emit selected(selectedPath);
#elif defined(Q_OS_LINUX)
  auto bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) { emit failed(tr("The desktop file chooser is unavailable. You can enter a folder path instead.")); return; }
  QString sender = bus.baseService().mid(1); sender.replace('.', '_');
  const QString token = "lore" + QUuid::createUuid().toString(QUuid::Id128);
  m_request = "/org/freedesktop/portal/desktop/request/" + sender + '/' + token;
  if (!bus.connect("org.freedesktop.portal.Desktop", m_request, "org.freedesktop.portal.Request", "Response", this, SLOT(portalResponse(uint,QVariantMap)))) {
    m_request.clear(); emit failed(tr("The desktop file chooser is unavailable. You can enter a folder path instead.")); return;
  }
  auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.FileChooser", "OpenFile");
  call << QString() << title << QVariantMap{{"handle_token", token}, {"directory", directory}, {"multiple", false}, {"modal", true}};
  auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
    const QDBusPendingReply<QDBusObjectPath> reply = *finished;
    finished->deleteLater();
    if (reply.isError()) {
      QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_request, "org.freedesktop.portal.Request", "Response", this, SLOT(portalResponse(uint,QVariantMap)));
      m_request.clear(); emit failed(tr("The desktop file chooser is unavailable. You can enter a folder path instead."));
    }
  });
#else
  Q_UNUSED(directory); Q_UNUSED(title);
  emit failed(tr("Use the folder path field on this platform."));
#endif
}

void NativeFilePicker::portalResponse(uint response, const QVariantMap &results) {
#ifdef Q_OS_LINUX
  QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_request, "org.freedesktop.portal.Request", "Response", this, SLOT(portalResponse(uint,QVariantMap)));
  m_request.clear();
#endif
  if (response == 1) { emit cancelled(); return; }
  const auto uris = results.value("uris").toStringList();
  if (response != 0 || uris.isEmpty() || !QUrl(uris.first()).isLocalFile()) { emit failed(tr("No local folder or program was selected.")); return; }
  emit selected(QUrl(uris.first()).toLocalFile());
}
