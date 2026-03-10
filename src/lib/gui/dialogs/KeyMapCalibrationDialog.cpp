/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "KeyMapCalibrationDialog.h"

#include "common/Constants.h"
#include "common/Settings.h"

#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSaveFile>
#include <QVBoxLayout>

namespace {

constexpr auto kCalibrationFileName = "keyboard-calibration.json";

QString formatHex(quint32 value)
{
  auto width = 4;
  if (value > 0xffffu) {
    width = 8;
  }

  return QStringLiteral("0x%1").arg(QStringLiteral("%1").arg(value, width, 16, QChar('0')).toUpper());
}

QStringList modifierNames(Qt::KeyboardModifiers modifiers)
{
  QStringList names;

  if (modifiers.testFlag(Qt::ShiftModifier)) {
    names.append(QStringLiteral("shift"));
  }
  if (modifiers.testFlag(Qt::ControlModifier)) {
    names.append(QStringLiteral("ctrl"));
  }
  if (modifiers.testFlag(Qt::AltModifier)) {
    names.append(QStringLiteral("alt"));
  }
  if (modifiers.testFlag(Qt::MetaModifier)) {
    names.append(QStringLiteral("meta"));
  }
  if (modifiers.testFlag(Qt::KeypadModifier)) {
    names.append(QStringLiteral("keypad"));
  }
  if (modifiers.testFlag(Qt::GroupSwitchModifier)) {
    names.append(QStringLiteral("group"));
  }

  if (names.isEmpty()) {
    names.append(QStringLiteral("none"));
  }

  return names;
}

quint32 preferredKeyCode(const KeyCaptureButton::Capture &capture)
{
  if (capture.nativeVirtualKey != 0) {
    return capture.nativeVirtualKey;
  }

  if (capture.nativeScanCode != 0) {
    return capture.nativeScanCode;
  }

  return static_cast<quint32>(capture.key);
}

QString displayKeyText(const KeyCaptureButton::Capture &capture)
{
  if (!capture.valid) {
    return {};
  }

  if (!capture.text.isEmpty()) {
    if (capture.text == QStringLiteral(" ")) {
      return QStringLiteral("Space");
    }
    if (capture.text == QStringLiteral("\t")) {
      return QStringLiteral("Tab");
    }
    if (capture.text == QStringLiteral("\r") || capture.text == QStringLiteral("\n")) {
      return QStringLiteral("Return");
    }

    return capture.text;
  }

  return QKeySequence(static_cast<int>(capture.modifiers) | capture.key).toString(QKeySequence::NativeText);
}

QFrame *separator(QWidget *parent)
{
  auto *line = new QFrame(parent);
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  return line;
}

} // namespace

QString KeyCaptureButton::Capture::displayText() const
{
  return displayKeyText(*this);
}

QString KeyCaptureButton::Capture::displayKeyCode() const
{
  if (!valid) {
    return {};
  }

  return formatHex(preferredKeyCode(*this));
}

QString KeyCaptureButton::Capture::displayModifiers() const
{
  return modifierNames(modifiers).join(QStringLiteral("+"));
}

QString KeyCaptureButton::Capture::signature() const
{
  if (!valid) {
    return {};
  }

  return QStringLiteral("%1|%2").arg(displayKeyCode(), displayModifiers());
}

QJsonObject KeyCaptureButton::Capture::toJsonObject() const
{
  auto object = QJsonObject{};
  object.insert(QStringLiteral("text"), text);
  object.insert(QStringLiteral("display_text"), displayText());
  object.insert(QStringLiteral("signature"), signature());
  object.insert(QStringLiteral("qt_key"), formatHex(static_cast<quint32>(key)));
  object.insert(QStringLiteral("qt_key_name"), QKeySequence(key).toString(QKeySequence::PortableText));
  object.insert(QStringLiteral("modifiers"), displayModifiers());
  object.insert(QStringLiteral("modifier_names"), QJsonArray::fromStringList(modifierNames(modifiers)));
  object.insert(QStringLiteral("preferred_keycode"), displayKeyCode());
  object.insert(QStringLiteral("native_scan_code"), formatHex(nativeScanCode));
  object.insert(QStringLiteral("native_virtual_key"), formatHex(nativeVirtualKey));
  object.insert(QStringLiteral("native_modifiers"), formatHex(nativeModifiers));
  return object;
}

KeyCaptureButton::KeyCaptureButton(const QString &idleText, QWidget *parent) : QPushButton(parent), m_idleText(idleText)
{
  setFocusPolicy(Qt::StrongFocus);
  updateOutput();
}

bool KeyCaptureButton::event(QEvent *event)
{
  if (m_recording) {
    switch (event->type()) {
    case QEvent::KeyPress:
      keyPressEvent(static_cast<QKeyEvent *>(event));
      return true;

    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::ShortcutOverride:
      event->accept();
      return true;

    case QEvent::FocusOut:
      stopRecording();
      return true;

    default:
      break;
    }
  }

  return QPushButton::event(event);
}

void KeyCaptureButton::keyPressEvent(QKeyEvent *event)
{
  event->accept();

  if (!m_recording || event->isAutoRepeat()) {
    return;
  }

  if (isModifierOnly(event->key())) {
    return;
  }

  m_capture.text = event->text();
  m_capture.key = event->key();
  m_capture.nativeModifiers = event->nativeModifiers();
  m_capture.nativeScanCode = event->nativeScanCode();
  m_capture.nativeVirtualKey = event->nativeVirtualKey();
  m_capture.modifiers = event->modifiers();
  m_capture.valid = true;

  stopRecording();
  Q_EMIT captureChanged();
}

void KeyCaptureButton::mousePressEvent(QMouseEvent *event)
{
  event->accept();

  if (!m_recording) {
    startRecording();
  }
}

bool KeyCaptureButton::isModifierOnly(int key) const
{
  switch (key) {
  case Qt::Key_Shift:
  case Qt::Key_Control:
  case Qt::Key_Meta:
  case Qt::Key_Alt:
  case Qt::Key_AltGr:
  case Qt::Key_CapsLock:
  case Qt::Key_NumLock:
  case Qt::Key_ScrollLock:
    return true;

  default:
    return false;
  }
}

void KeyCaptureButton::startRecording()
{
  m_recording = true;
  setDown(true);
  setText(tr("Press a key..."));
  setFocus(Qt::MouseFocusReason);
  grabKeyboard();
}

void KeyCaptureButton::stopRecording()
{
  m_recording = false;
  setDown(false);
  releaseKeyboard();
  updateOutput();
}

void KeyCaptureButton::updateOutput()
{
  if (!m_capture.valid) {
    setText(m_idleText);
    return;
  }

  setText(m_capture.displayText());
}

KeyMapCalibrationDialog::KeyMapCalibrationDialog(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(tr("Keyboard Calibration"));
  setMinimumWidth(640);

  auto *layout = new QVBoxLayout(this);

  auto *grid = new QGridLayout();
  grid->setColumnStretch(1, 1);

  grid->addWidget(new QLabel(tr("Physical key"), this), 0, 0);
  m_localCapture = new KeyCaptureButton(tr("Click to capture"), this);
  grid->addWidget(m_localCapture, 0, 1);

  grid->addWidget(new QLabel(tr("Key code"), this), 1, 0);
  m_localCode = new QLineEdit(this);
  m_localCode->setReadOnly(true);
  grid->addWidget(m_localCode, 1, 1);

  grid->addWidget(separator(this), 2, 0, 1, 2);

  grid->addWidget(new QLabel(tr("Deskflow key"), this), 3, 0);
  m_remoteCapture = new KeyCaptureButton(tr("Click to capture"), this);
  grid->addWidget(m_remoteCapture, 3, 1);

  grid->addWidget(new QLabel(tr("Key code"), this), 4, 0);
  m_remoteCode = new QLineEdit(this);
  m_remoteCode->setReadOnly(true);
  grid->addWidget(m_remoteCode, 4, 1);

  grid->addWidget(separator(this), 5, 0, 1, 2);

  layout->addLayout(grid);

  m_statusLabel = new QLabel(this);
  m_statusLabel->setWordWrap(true);
  layout->addWidget(m_statusLabel);

  auto *description = new QLabel(
      tr("Use the same key for each capture. Mouse clicks are ignored while recording.\n"
         "Saved to ~/Library/Deskflow/keyboard-calibration.json\n\n"
         "Overrides can also be added manually in keyboard-calibration.json.\n"
         "Example: \"overrides\": [ { \"match\": { \"id\": \"F13\" }, \"send\": { \"id\": \"Control_L\" } }, "
         "{ ... } ]"),
      this
  );
  description->setWordWrap(true);
  layout->addWidget(description);

  auto *buttonsLayout = new QHBoxLayout();
  buttonsLayout->addStretch();

  m_saveButton = new QPushButton(tr("Save"), this);
  buttonsLayout->addWidget(m_saveButton);

  auto *closeButton = new QPushButton(tr("Close"), this);
  buttonsLayout->addWidget(closeButton);
  layout->addLayout(buttonsLayout);

  connect(m_localCapture, &KeyCaptureButton::captureChanged, this, &KeyMapCalibrationDialog::updateState);
  connect(m_remoteCapture, &KeyCaptureButton::captureChanged, this, &KeyMapCalibrationDialog::updateState);
  connect(m_saveButton, &QPushButton::clicked, this, &KeyMapCalibrationDialog::saveMapping);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

  updateState();
}

QJsonObject KeyMapCalibrationDialog::createMappingObject() const
{
  auto object = QJsonObject{};
  object.insert(QStringLiteral("local"), m_localCapture->capture().toJsonObject());
  object.insert(QStringLiteral("remote"), m_remoteCapture->capture().toJsonObject());
  object.insert(
      QStringLiteral("text_match"), m_localCapture->capture().displayText() == m_remoteCapture->capture().displayText()
  );
  object.insert(
      QStringLiteral("signature_match"), m_localCapture->capture().signature() == m_remoteCapture->capture().signature()
  );
  object.insert(QStringLiteral("saved_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  return object;
}

QString KeyMapCalibrationDialog::outputFilePath() const
{
  return QDir(Settings::UserDir).filePath(QString::fromLatin1(kCalibrationFileName));
}

void KeyMapCalibrationDialog::saveMapping()
{
  if (!m_localCapture->capture().valid || !m_remoteCapture->capture().valid) {
    return;
  }

  auto outputDirectory = QDir(Settings::UserDir);
  if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral("."))) {
    QMessageBox::warning(this, tr("Unable to Save"), tr("Could not create the selected folder."));
    return;
  }

  auto root = QJsonObject{};
  auto filePath = outputFilePath();

  if (QFile::exists(filePath)) {
    QFile input(filePath);
    if (!input.open(QIODevice::ReadOnly)) {
      QMessageBox::warning(
          this, tr("Unable to Save"), tr("The JSON file is corrupted or you do not have permission to read it.")
      );
      return;
    }

    QJsonParseError parseError;
    auto document = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      QMessageBox::warning(this, tr("Unable to Save"), tr("The JSON file is invalid and could not be opened."));
      return;
    }

    root = document.object();
  }

  auto mappings = root.value(QStringLiteral("mappings")).toObject();
  mappings.insert(m_localCapture->capture().signature(), createMappingObject());

  root.insert(QStringLiteral("app"), kAppName);
  root.insert(QStringLiteral("mappings"), mappings);
  root.insert(QStringLiteral("saved_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

  QSaveFile output(filePath);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, tr("Unable to Save"), tr("You do not have permission to write to the JSON file."));
    return;
  }

  output.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  if (!output.commit()) {
    QMessageBox::warning(this, tr("Unable to Save"), tr("Writing to the JSON file failed."));
    return;
  }

  QMessageBox::information(this, tr("Saved successfully"), tr("Saved to %1").arg(QDir::toNativeSeparators(filePath)));
}

void KeyMapCalibrationDialog::updateState()
{
  if (m_localCapture->capture().valid) {
    m_localCode->setText(QStringLiteral("%1 | %2").arg(
        m_localCapture->capture().displayKeyCode(), m_localCapture->capture().displayModifiers()
    ));
  } else {
    m_localCode->setText(tr("Not captured"));
  }

  if (m_remoteCapture->capture().valid) {
    m_remoteCode->setText(QStringLiteral("%1 | %2").arg(
        m_remoteCapture->capture().displayKeyCode(), m_remoteCapture->capture().displayModifiers()
    ));
  } else {
    m_remoteCode->setText(tr("Not captured"));
  }

  if (!m_localCapture->capture().valid && !m_remoteCapture->capture().valid) {
    m_statusLabel->setText(tr("Capture a physical key and a Deskflow key."));
  } else if (!m_localCapture->capture().valid) {
    m_statusLabel->setText(tr("Capture the physical key first."));
  } else if (!m_remoteCapture->capture().valid) {
    m_statusLabel->setText(tr("Capture the Deskflow key next."));
  } else if (m_localCapture->capture().displayText() == m_remoteCapture->capture().displayText() &&
             m_localCapture->capture().signature() == m_remoteCapture->capture().signature()) {
    m_statusLabel->setText(tr("Both captures match. Saving will still update the JSON."));
  } else if (m_localCapture->capture().displayText() == m_remoteCapture->capture().displayText()) {
    m_statusLabel->setText(tr("The text matches, but the key code is different."));
  } else {
    m_statusLabel->setText(tr("Difference detected. Saving will append or replace this mapping in the JSON."));
  }

  m_saveButton->setEnabled(m_localCapture->capture().valid && m_remoteCapture->capture().valid);
}
