// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/signin_interaction/signin_interaction_controller_egtest_util.h"

#import "base/test/ios/wait_util.h"
#include "components/unified_consent/feature.h"
#import "ios/chrome/browser/ui/authentication/signin_earlgrey_utils.h"
#include "ios/chrome/grit/ios_strings.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey_ui.h"
#import "ios/chrome/test/earl_grey/chrome_matchers.h"
#import "ios/chrome/test/earl_grey/chrome_test_case.h"
#

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

void TapButtonWithAccessibilityLabel(NSString* label) {
  id<GREYMatcher> matcher =
      chrome_test_util::ButtonWithAccessibilityLabel(label);
  [[EarlGrey selectElementWithMatcher:matcher] performAction:grey_tap()];
}

void TapButtonWithLabelId(int message_id) {
  id<GREYMatcher> matcher =
      chrome_test_util::ButtonWithAccessibilityLabelId(message_id);
  [[EarlGrey selectElementWithMatcher:matcher] performAction:grey_tap()];
}

void VerifyChromeSigninViewVisible() {
  id<GREYMatcher> signin_matcher =
      chrome_test_util::StaticTextWithAccessibilityLabelId(
          unified_consent::IsUnifiedConsentFeatureEnabled()
              ? IDS_IOS_ACCOUNT_UNIFIED_CONSENT_SYNC_SUBTITLE
              : IDS_IOS_ACCOUNT_CONSISTENCY_SETUP_DESCRIPTION);
  [[EarlGrey selectElementWithMatcher:signin_matcher]
      assertWithMatcher:grey_sufficientlyVisible()];
}

void WaitForMatcher(id<GREYMatcher> matcher) {
  ConditionBlock condition = ^{
    NSError* error = nil;
    [[EarlGrey selectElementWithMatcher:matcher] assertWithMatcher:grey_notNil()
                                                             error:&error];
    return error == nil;
  };
  GREYAssert(base::test::ios::WaitUntilConditionOrTimeout(
                 base::test::ios::kWaitForUIElementTimeout, condition),
             @"Waiting for matcher %@ failed.", matcher);
}
