/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXKeyCalibration.h"

#include "base/Log.h"
#include "common/Constants.h"
#include "deskflow/KeyMap.h"

#include <Foundation/Foundation.h>

#include <cstdlib>
#include <iterator>
#include <string>

namespace {

constexpr auto kCalibrationFileName = "keyboard-calibration.json";
constexpr KeyModifierMask kCalibrationModifierMask = KeyModifierShift | KeyModifierControl | KeyModifierAlt |
                                                     KeyModifierMeta | KeyModifierSuper | KeyModifierAltGr |
                                                     KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock;

KeyModifierMask normalizeModifiers(KeyModifierMask modifiers)
{
  return modifiers & kCalibrationModifierMask;
}

bool isBackslashAlias(KeyID id)
{
  return id == '\\' || id == 0x00A5u;
}

std::string calibrationFilePath()
{
  NSString *path = [NSHomeDirectory()
      stringByAppendingPathComponent:[NSString stringWithFormat:@"Library/%s/%s", kAppName, kCalibrationFileName]];
  if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
    return [path UTF8String];
  }

  return {};
}

bool parseHexString(NSString *text, uint32_t &value)
{
  if (text == nil) {
    return false;
  }

  if (const char *chars = [text UTF8String]; chars != nullptr) {
    char *end = nullptr;
    const auto parsed = std::strtoul(chars, &end, 0);
    if (end != chars && *end == '\0') {
      value = static_cast<uint32_t>(parsed);
      return true;
    }
  }

  return false;
}

KeyID parseTargetID(NSDictionary *local)
{
  uint32_t value = 0;
  if (parseHexString([local objectForKey:@"qt_key"], value)) {
    return static_cast<KeyID>(value);
  }

  NSString *text = [local objectForKey:@"text"];
  if ([text isKindOfClass:[NSString class]] && [text length] > 0) {
    return static_cast<KeyID>([text characterAtIndex:0]);
  }

  return kKeyNone;
}

bool targetIDMatches(KeyID expected, KeyID actual)
{
  if (expected == kKeyNone || actual == kKeyNone) {
    return false;
  }

  if (expected == actual) {
    return true;
  }

  return isBackslashAlias(expected) && isBackslashAlias(actual);
}

bool parseNamedKey(NSDictionary *object, NSString *field, KeyID &keyID)
{
  keyID = kKeyNone;

  NSString *value = [object objectForKey:field];
  if (![value isKindOfClass:[NSString class]]) {
    return false;
  }

  const char *text = [value UTF8String];
  if (text == nullptr) {
    return false;
  }

  std::string keyName = text;
  return deskflow::KeyMap::parseKey(keyName, keyID);
}

KeyModifierMask parseModifiers(NSString *text)
{
  if (text == nil || [text length] == 0 || [text isEqualToString:@"none"]) {
    return 0;
  }

  KeyModifierMask modifiers = 0;
  for (NSString *value in [text componentsSeparatedByString:@"+"]) {
    NSString *token =
        [[value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
    if ([token isEqualToString:@"shift"]) {
      modifiers |= KeyModifierShift;
    } else if ([token isEqualToString:@"control"] || [token isEqualToString:@"ctrl"]) {
      modifiers |= KeyModifierControl;
    } else if ([token isEqualToString:@"alt"]) {
      modifiers |= KeyModifierAlt;
    } else if ([token isEqualToString:@"meta"]) {
      modifiers |= KeyModifierMeta;
    } else if ([token isEqualToString:@"super"]) {
      modifiers |= KeyModifierSuper;
    } else if ([token isEqualToString:@"altgr"]) {
      modifiers |= KeyModifierAltGr;
    } else if ([token isEqualToString:@"capslock"]) {
      modifiers |= KeyModifierCapsLock;
    } else if ([token isEqualToString:@"numlock"]) {
      modifiers |= KeyModifierNumLock;
    } else if ([token isEqualToString:@"scrolllock"]) {
      modifiers |= KeyModifierScrollLock;
    }
  }

  return normalizeModifiers(modifiers);
}

} // namespace

OSXKeyCalibration::OSXKeyCalibration()
{
  const auto path = calibrationFilePath();
  if (path.empty()) {
    return;
  }

  NSData *data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
  if (data == nil) {
    LOG_WARN("failed to read key calibration file: %s", path.c_str());
    return;
  }

  NSError *error = nil;
  id root = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
  if (error != nil || ![root isKindOfClass:[NSDictionary class]]) {
    LOG_WARN("failed to parse key calibration file: %s", path.c_str());
    return;
  }

  NSDictionary *mappings = [(NSDictionary *)root objectForKey:@"mappings"];
  if ([mappings isKindOfClass:[NSDictionary class]]) {
    for (id value in [mappings allValues]) {
      if (![value isKindOfClass:[NSDictionary class]]) {
        continue;
      }

      NSDictionary *local = [(NSDictionary *)value objectForKey:@"local"];
      NSDictionary *remote = [(NSDictionary *)value objectForKey:@"remote"];
      if (![local isKindOfClass:[NSDictionary class]] || ![remote isKindOfClass:[NSDictionary class]]) {
        continue;
      }

      uint32_t localKeycode = 0;
      uint32_t remoteKeycode = 0;
      const auto targetID = parseTargetID(local);
      const auto localModifiers = parseModifiers([local objectForKey:@"modifiers"]);
      const auto remoteModifiers = parseModifiers([remote objectForKey:@"modifiers"]);
      if (!parseHexString([local objectForKey:@"preferred_keycode"], localKeycode) ||
          !parseHexString([remote objectForKey:@"preferred_keycode"], remoteKeycode)) {
        continue;
      }

      m_entries.emplace(
          Signature{static_cast<KeyButton>(remoteKeycode + 1), remoteModifiers},
          Entry{targetID, static_cast<KeyButton>(localKeycode + 1), localModifiers}
      );
    }
  }

  NSArray *overrides = [(NSDictionary *)root objectForKey:@"overrides"];
  if ([overrides isKindOfClass:[NSArray class]]) {
    for (id value in overrides) {
      if (![value isKindOfClass:[NSDictionary class]]) {
        continue;
      }

      NSDictionary *match = [(NSDictionary *)value objectForKey:@"match"];
      NSDictionary *send = [(NSDictionary *)value objectForKey:@"send"];
      if (![match isKindOfClass:[NSDictionary class]] || ![send isKindOfClass:[NSDictionary class]]) {
        continue;
      }

      KeyID matchID = kKeyNone;
      KeyID sendID = kKeyNone;
      if (!parseNamedKey(match, @"id", matchID) || !parseNamedKey(send, @"id", sendID)) {
        continue;
      }

      m_overrides[matchID] = sendID;
    }
  }

  LOG_INFO(
      "loaded %zu key calibration mappings and %zu overrides from %s", m_entries.size(), m_overrides.size(),
      path.c_str()
  );
}

const OSXKeyCalibration::Entry *
OSXKeyCalibration::find(KeyButton button, KeyModifierMask modifiers, KeyID targetID) const
{
  const auto range = m_entries.equal_range({button, normalizeModifiers(modifiers)});
  if (range.first == range.second) {
    return nullptr;
  }

  const auto count = std::distance(range.first, range.second);
  if (count == 1) {
    return &range.first->second;
  }

  for (auto it = range.first; it != range.second; ++it) {
    if (targetIDMatches(it->second.m_targetID, targetID)) {
      return &it->second;
    }
  }

  return nullptr;
}

KeyID OSXKeyCalibration::remapKeyID(KeyID id) const
{
  auto it = m_overrides.find(id);
  if (it == m_overrides.end()) {
    return id;
  }

  return it->second;
}

bool OSXKeyCalibration::empty() const
{
  return m_entries.empty();
}
