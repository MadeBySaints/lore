#pragma once

#include <QDir>
#include <QString>

namespace Paths {
enum class Style { Posix, Windows };
#ifdef Q_OS_WIN
constexpr Style nativeStyle = Style::Windows;
#else
constexpr Style nativeStyle = Style::Posix;
#endif

inline bool contains(QString root, QString path, Style style = nativeStyle) {
  if (root.isEmpty() || path.isEmpty()) return false;
  if (style == Style::Windows) {
    root.replace('\\', '/'); path.replace('\\', '/');
  }
  root = QDir::cleanPath(root); path = QDir::cleanPath(path);
  const auto sensitivity = style == Style::Windows ? Qt::CaseInsensitive : Qt::CaseSensitive;
  if (root.compare(path, sensitivity) == 0) return true;
  if (!root.endsWith('/')) root += '/';
  return path.startsWith(root, sensitivity);
}
}
