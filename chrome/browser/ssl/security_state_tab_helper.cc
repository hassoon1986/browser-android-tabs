// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ssl/security_state_tab_helper.h"

#include <string>

#include "base/bind.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/pattern.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lookalikes/safety_tips/reputation_web_contents_observer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/safe_browsing/ui_manager.h"
#include "chrome/common/secure_origin_whitelist.h"
#include "components/omnibox/browser/omnibox_field_trial.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/password_manager/core/browser/password_manager_metrics_util.h"
#include "components/safe_browsing/buildflags.h"
#include "components/security_state/content/content_utils.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/origin_util.h"
#include "net/base/net_errors.h"
#include "net/cert/x509_certificate.h"
#include "net/ssl/ssl_cipher_suite_names.h"
#include "net/ssl/ssl_connection_status_flags.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "url/origin.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/policy/policy_cert_service.h"
#include "chrome/browser/chromeos/policy/policy_cert_service_factory.h"
#endif  // defined(OS_CHROMEOS)

#if BUILDFLAG(FULL_SAFE_BROWSING)
#include "chrome/browser/safe_browsing/chrome_password_protection_service.h"
#endif

namespace {

void RecordSecurityLevel(
    const security_state::VisibleSecurityState& visible_security_state,
    security_state::SecurityLevel security_level) {
  if (security_state::IsSchemeCryptographic(visible_security_state.url)) {
    UMA_HISTOGRAM_ENUMERATION("Security.SecurityLevel.CryptographicScheme",
                              security_level,
                              security_state::SECURITY_LEVEL_COUNT);
  } else {
    UMA_HISTOGRAM_ENUMERATION("Security.SecurityLevel.NoncryptographicScheme",
                              security_level,
                              security_state::SECURITY_LEVEL_COUNT);
  }
}

}  // namespace

using password_manager::metrics_util::PasswordType;
using safe_browsing::SafeBrowsingUIManager;

SecurityStateTabHelper::SecurityStateTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {}

SecurityStateTabHelper::~SecurityStateTabHelper() {}

security_state::SecurityLevel SecurityStateTabHelper::GetSecurityLevel() const {
  return security_state::GetSecurityLevel(
      *GetVisibleSecurityState(), UsedPolicyInstalledCertificate(),
      base::BindRepeating(&content::IsOriginSecure));
}

std::unique_ptr<security_state::VisibleSecurityState>
SecurityStateTabHelper::GetVisibleSecurityState() const {
  auto state = security_state::GetVisibleSecurityState(web_contents());

  // Malware status might already be known even if connection security
  // information is still being initialized, thus no need to check for that.
  state->malicious_content_status = GetMaliciousContentStatus();

  safety_tips::ReputationWebContentsObserver* reputation_web_contents_observer =
      safety_tips::ReputationWebContentsObserver::FromWebContents(
          web_contents());
  state->safety_tip_status =
      reputation_web_contents_observer
          ? reputation_web_contents_observer
                ->GetSafetyTipStatusForVisibleNavigation()
          : security_state::SafetyTipStatus::kUnknown;
  return state;
}

void SecurityStateTabHelper::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsFormSubmission()) {
    UMA_HISTOGRAM_ENUMERATION("Security.SecurityLevel.FormSubmission",
                              GetSecurityLevel(),
                              security_state::SECURITY_LEVEL_COUNT);
    UMA_HISTOGRAM_ENUMERATION("Security.SafetyTips.FormSubmission",
                              GetVisibleSecurityState()->safety_tip_status);
  }
}

void SecurityStateTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  // Ignore subframe navigations, same-document navigations, and navigations
  // that did not commit (e.g. HTTP/204 or file downloads).
  if (!navigation_handle->IsInMainFrame() ||
      navigation_handle->IsSameDocument() ||
      !navigation_handle->HasCommitted()) {
    return;
  }

  content::NavigationEntry* entry =
      web_contents()->GetController().GetLastCommittedEntry();
  if (entry) {
    UMA_HISTOGRAM_ENUMERATION(
        "Security.CertificateTransparency.MainFrameNavigationCompliance",
        entry->GetSSL().ct_policy_compliance,
        net::ct::CTPolicyCompliance::CT_POLICY_COUNT);
  }

  std::unique_ptr<security_state::VisibleSecurityState> visible_security_state =
      GetVisibleSecurityState();
  if (net::IsCertStatusError(visible_security_state->cert_status) &&
      !navigation_handle->IsErrorPage()) {
    // Record each time a user visits a site after having clicked through a
    // certificate warning interstitial. This is used as a baseline for
    // interstitial.ssl.did_user_revoke_decision2 in order to determine how
    // many times the re-enable warnings button is clicked, as a fraction of
    // the number of times it was available.
    UMA_HISTOGRAM_BOOLEAN("interstitial.ssl.visited_site_after_warning", true);
  }
}

void SecurityStateTabHelper::DidChangeVisibleSecurityState() {
  RecordSecurityLevel(*GetVisibleSecurityState(), GetSecurityLevel());
}

bool SecurityStateTabHelper::UsedPolicyInstalledCertificate() const {
#if defined(OS_CHROMEOS)
  policy::PolicyCertService* service =
      policy::PolicyCertServiceFactory::GetForProfile(
          Profile::FromBrowserContext(web_contents()->GetBrowserContext()));
  if (service && service->UsedPolicyCertificates())
    return true;
#endif
  return false;
}

security_state::MaliciousContentStatus
SecurityStateTabHelper::GetMaliciousContentStatus() const {
  content::NavigationEntry* entry =
      web_contents()->GetController().GetVisibleEntry();
  if (!entry)
    return security_state::MALICIOUS_CONTENT_STATUS_NONE;
  safe_browsing::SafeBrowsingService* sb_service =
      g_browser_process->safe_browsing_service();
  if (!sb_service)
    return security_state::MALICIOUS_CONTENT_STATUS_NONE;
  scoped_refptr<SafeBrowsingUIManager> sb_ui_manager = sb_service->ui_manager();
  safe_browsing::SBThreatType threat_type;
  if (sb_ui_manager->IsUrlWhitelistedOrPendingForWebContents(
          entry->GetURL(), false, entry, web_contents(), false, &threat_type)) {
    switch (threat_type) {
      case safe_browsing::SB_THREAT_TYPE_UNUSED:
      case safe_browsing::SB_THREAT_TYPE_SAFE:
      case safe_browsing::SB_THREAT_TYPE_URL_PHISHING:
      case safe_browsing::SB_THREAT_TYPE_URL_CLIENT_SIDE_PHISHING:
        return security_state::MALICIOUS_CONTENT_STATUS_SOCIAL_ENGINEERING;
      case safe_browsing::SB_THREAT_TYPE_URL_MALWARE:
      case safe_browsing::SB_THREAT_TYPE_URL_CLIENT_SIDE_MALWARE:
        return security_state::MALICIOUS_CONTENT_STATUS_MALWARE;
      case safe_browsing::SB_THREAT_TYPE_URL_UNWANTED:
        return security_state::MALICIOUS_CONTENT_STATUS_UNWANTED_SOFTWARE;
      case safe_browsing::SB_THREAT_TYPE_SIGNED_IN_SYNC_PASSWORD_REUSE:
#if BUILDFLAG(FULL_SAFE_BROWSING)
        if (safe_browsing::ChromePasswordProtectionService::
                ShouldShowPasswordReusePageInfoBubble(
                    web_contents(), PasswordType::PRIMARY_ACCOUNT_PASSWORD)) {
          return security_state::
              MALICIOUS_CONTENT_STATUS_SIGNED_IN_SYNC_PASSWORD_REUSE;
        }
        // If user has already changed Gaia password, returns the regular
        // social engineering content status.
        return security_state::MALICIOUS_CONTENT_STATUS_SOCIAL_ENGINEERING;
#endif
      case safe_browsing::SB_THREAT_TYPE_SIGNED_IN_NON_SYNC_PASSWORD_REUSE:
#if BUILDFLAG(FULL_SAFE_BROWSING)
        if (safe_browsing::ChromePasswordProtectionService::
                ShouldShowPasswordReusePageInfoBubble(
                    web_contents(), PasswordType::OTHER_GAIA_PASSWORD)) {
          return security_state::
              MALICIOUS_CONTENT_STATUS_SIGNED_IN_NON_SYNC_PASSWORD_REUSE;
        }
        // If user has already changed Gaia password, returns the regular
        // social engineering content status.
        return security_state::MALICIOUS_CONTENT_STATUS_SOCIAL_ENGINEERING;
#endif
      case safe_browsing::SB_THREAT_TYPE_ENTERPRISE_PASSWORD_REUSE:
#if BUILDFLAG(FULL_SAFE_BROWSING)
        if (safe_browsing::ChromePasswordProtectionService::
                ShouldShowPasswordReusePageInfoBubble(
                    web_contents(), PasswordType::ENTERPRISE_PASSWORD)) {
          return security_state::
              MALICIOUS_CONTENT_STATUS_ENTERPRISE_PASSWORD_REUSE;
        }
        // If user has already changed Gaia password, returns the regular
        // social engineering content status.
        return security_state::MALICIOUS_CONTENT_STATUS_SOCIAL_ENGINEERING;
#endif
      case safe_browsing::SB_THREAT_TYPE_BILLING:
        return security_state::MALICIOUS_CONTENT_STATUS_BILLING;
      case safe_browsing::
          DEPRECATED_SB_THREAT_TYPE_URL_PASSWORD_PROTECTION_PHISHING:
      case safe_browsing::SB_THREAT_TYPE_URL_BINARY_MALWARE:
      case safe_browsing::SB_THREAT_TYPE_EXTENSION:
      case safe_browsing::SB_THREAT_TYPE_BLACKLISTED_RESOURCE:
      case safe_browsing::SB_THREAT_TYPE_API_ABUSE:
      case safe_browsing::SB_THREAT_TYPE_SUBRESOURCE_FILTER:
      case safe_browsing::SB_THREAT_TYPE_CSD_WHITELIST:
      case safe_browsing::SB_THREAT_TYPE_AD_SAMPLE:
      case safe_browsing::SB_THREAT_TYPE_BLOCKED_AD_POPUP:
      case safe_browsing::SB_THREAT_TYPE_BLOCKED_AD_REDIRECT:
      case safe_browsing::SB_THREAT_TYPE_SUSPICIOUS_SITE:
      case safe_browsing::SB_THREAT_TYPE_APK_DOWNLOAD:
      case safe_browsing::SB_THREAT_TYPE_HIGH_CONFIDENCE_ALLOWLIST:
        // These threat types are not currently associated with
        // interstitials, and thus resources with these threat types are
        // not ever whitelisted or pending whitelisting.
        NOTREACHED();
        break;
    }
  }
  return security_state::MALICIOUS_CONTENT_STATUS_NONE;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(SecurityStateTabHelper)
