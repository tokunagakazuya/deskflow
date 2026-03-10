/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QPushButton>

class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMouseEvent;

class KeyCaptureButton : public QPushButton
{
  Q_OBJECT

public:
  struct Capture
  {
    QString text;
    int key = 0;
    quint32 nativeModifiers = 0;
    quint32 nativeScanCode = 0;
    quint32 nativeVirtualKey = 0;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    bool valid = false;

    QString displayText() const;
    QString displayKeyCode() const;
    QString displayModifiers() const;
    QString signature() const;
    QJsonObject toJsonObject() const;
  };

  explicit KeyCaptureButton(const QString &idleText, QWidget *parent = nullptr);

  const Capture &capture() const
  {
    return m_capture;
  }

Q_SIGNALS:
  void captureChanged();

protected:
  bool event(QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;

private:
  bool isModifierOnly(int key) const;
  void startRecording();
  void stopRecording();
  void updateOutput();

  QString m_idleText;
  bool m_recording = false;
  Capture m_capture;
};

class KeyMapCalibrationDialog : public QDialog
{
  Q_OBJECT

public:
  explicit KeyMapCalibrationDialog(QWidget *parent = nullptr);

private:
  QJsonObject createMappingObject() const;
  QString outputFilePath() const;
  void saveMapping();
  void updateState();

  KeyCaptureButton *m_localCapture = nullptr;
  KeyCaptureButton *m_remoteCapture = nullptr;
  QLineEdit *m_localCode = nullptr;
  QLineEdit *m_remoteCode = nullptr;
  QLabel *m_statusLabel = nullptr;
  QPushButton *m_saveButton = nullptr;
};
