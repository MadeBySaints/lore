#pragma once
#include <QObject>
#include <QVariantMap>

class FilePicker : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void choose(bool directory, const QString &title) = 0;
signals:
  void selected(const QString &path);
  void cancelled();
  void failed(const QString &reason);
};

class NativeFilePicker : public FilePicker {
  Q_OBJECT
public:
  using FilePicker::FilePicker;
  void choose(bool directory, const QString &title) override;
private slots:
  void portalResponse(uint response, const QVariantMap &results);
private:
  QString m_request;
};
