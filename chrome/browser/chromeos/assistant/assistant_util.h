// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_ASSISTANT_ASSISTANT_UTIL_H_
#define CHROME_BROWSER_CHROMEOS_ASSISTANT_ASSISTANT_UTIL_H_

#include "ash/public/mojom/voice_interaction_controller.mojom.h"

class Profile;

namespace assistant {

// Returns whether Google Assistant feature is allowed for given |profile|.
ash::mojom::AssistantAllowedState IsAssistantAllowedForProfile(
    const Profile* profile);

}  // namespace assistant

#endif  // CHROME_BROWSER_CHROMEOS_ASSISTANT_ASSISTANT_UTIL_H_
