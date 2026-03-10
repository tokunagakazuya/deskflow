/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyTypes.h"

#include <map>

class OSXKeyCalibration
{
public:
  struct Entry
  {
    KeyID m_targetID{};
    KeyButton m_targetButton{};
    KeyModifierMask m_targetModifiers{};
  };

  OSXKeyCalibration();

  const Entry *find(KeyButton button, KeyModifierMask modifiers, KeyID targetID) const;
  KeyID remapKeyID(KeyID id) const;
  bool empty() const;

private:
  struct Signature
  {
    KeyButton m_button{};
    KeyModifierMask m_modifiers{};

    bool operator<(const Signature &other) const
    {
      if (m_button != other.m_button) {
        return m_button < other.m_button;
      }

      return m_modifiers < other.m_modifiers;
    }
  };

private:
  std::multimap<Signature, Entry> m_entries;
  std::map<KeyID, KeyID> m_overrides;
};
