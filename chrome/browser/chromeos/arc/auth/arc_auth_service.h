// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_ARC_AUTH_ARC_AUTH_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_ARC_AUTH_ARC_AUTH_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "chrome/browser/chromeos/arc/arc_session_manager.h"
#include "chrome/browser/chromeos/arc/auth/arc_active_directory_enrollment_token_fetcher.h"
#include "components/arc/mojom/auth.mojom.h"
#include "components/arc/session/connection_observer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/signin/public/identity_manager/identity_manager.h"

class Profile;

namespace content {
class BrowserContext;
}  // namespace content

namespace signin {
class IdentityManager;
}  // namespace signin

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace arc {

class ArcAuthCodeFetcher;
class ArcBackgroundAuthCodeFetcher;
class ArcBridgeService;
class ArcFetcherBase;

// Implementation of ARC authorization.
class ArcAuthService : public KeyedService,
                       public mojom::AuthHost,
                       public ConnectionObserver<mojom::AuthInstance>,
                       public signin::IdentityManager::Observer,
                       public ArcSessionManager::Observer {
 public:
  using GetGoogleAccountsInArcCallback =
      base::OnceCallback<void(std::vector<mojom::ArcAccountInfoPtr>)>;

  // Returns singleton instance for the given BrowserContext,
  // or nullptr if the browser |context| is not allowed to use ARC.
  static ArcAuthService* GetForBrowserContext(content::BrowserContext* context);

  ArcAuthService(content::BrowserContext* profile,
                 ArcBridgeService* bridge_service);
  ~ArcAuthService() override;

  // Gets the list of Google accounts currently stored in ARC. This is used by
  // the one-time migration flow for migrating Google accounts in ARC to Chrome
  // OS Account Manager.
  void GetGoogleAccountsInArc(GetGoogleAccountsInArcCallback callback);

  // For supporting ArcServiceManager::GetService<T>().
  static const char kArcServiceName[];

  // ConnectionObserver<mojom::AuthInstance>:
  void OnConnectionReady() override;
  void OnConnectionClosed() override;

  // mojom::AuthHost:
  void OnAuthorizationComplete(
      mojom::ArcSignInStatus status,
      bool initial_signin,
      const base::Optional<std::string>& account_name) override;
  void OnSignInCompleteDeprecated() override;
  void OnSignInFailedDeprecated(mojom::ArcSignInStatus reason) override;
  void RequestAccountInfoDeprecated(bool initial_signin) override;
  void ReportMetrics(mojom::MetricsType metrics_type, int32_t value) override;
  void ReportAccountCheckStatus(mojom::AccountCheckStatus status) override;
  void ReportSupervisionChangeStatus(
      mojom::SupervisionChangeStatus status) override;
  void RequestPrimaryAccountInfo(
      RequestPrimaryAccountInfoCallback callback) override;
  void RequestAccountInfo(const std::string& account_name,
                          RequestAccountInfoCallback callback) override;
  void IsAccountManagerAvailable(
      IsAccountManagerAvailableCallback callback) override;
  void HandleAddAccountRequest() override;
  void HandleRemoveAccountRequest(const std::string& email) override;
  void HandleUpdateCredentialsRequest(const std::string& email) override;

  void SetURLLoaderFactoryForTesting(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  // IdentityManager::Observer:
  void OnRefreshTokenUpdatedForAccount(
      const CoreAccountInfo& account_info) override;
  void OnExtendedAccountInfoRemoved(const AccountInfo& account_info) override;

  // ArcSessionManager::Observer:
  void OnArcInitialStart() override;

  // KeyedService:
  void Shutdown() override;

  void SkipMergeSessionForTesting();

 private:
  // Callback when Active Directory Enrollment Token is fetched.
  // |callback| is completed with |ArcSignInStatus| and |AccountInfo| depending
  // on the success / failure of the operation.
  void OnActiveDirectoryEnrollmentTokenFetched(
      ArcActiveDirectoryEnrollmentTokenFetcher* fetcher,
      RequestPrimaryAccountInfoCallback callback,
      ArcActiveDirectoryEnrollmentTokenFetcher::Status status,
      const std::string& enrollment_token,
      const std::string& user_id);

  // Issues a request for fetching AccountInfo for the Device Account.
  // |initial_signin| denotes whether this is the initial ARC provisioning flow
  // or a subsequent sign-in.
  // |callback| is completed with |ArcSignInStatus| and |AccountInfo| depending
  // on the success / failure of the operation.
  void FetchPrimaryAccountInfo(bool initial_signin,
                               RequestPrimaryAccountInfoCallback callback);

  // Callback for |FetchPrimaryAccountInfo|.
  // |fetcher| is a pointer to the object that issues this callback. Used for
  // deleting pending requests from |pending_token_requests_|.
  // |success| and |auth_code| are the callback parameters passed by
  // |ArcBackgroundAuthCodeFetcher::Fetch|.
  // |callback| is completed with |ArcSignInStatus| and |AccountInfo| depending
  // on the success / failure of the operation.
  void OnPrimaryAccountAuthCodeFetched(
      ArcAuthCodeFetcher* fetcher,
      RequestPrimaryAccountInfoCallback callback,
      bool success,
      const std::string& auth_code);

  // Called to let ARC container know the account info.
  void OnAccountInfoReadyDeprecated(mojom::ArcSignInStatus status,
                                    mojom::AccountInfoPtr account_info);

  // Issues a request for fetching AccountInfo for a Secondary Account
  // represented by |account_name|. |account_name| is the account identifier
  // used by ARC/Android.
  void FetchSecondaryAccountInfo(const std::string& account_name,
                                 RequestAccountInfoCallback callback);

  // Callback for |FetchSecondaryAccountInfo|, issued by
  // |ArcBackgroundAuthCodeFetcher::Fetch|.
  // |account_name| is the account identifier used by ARC/Android.
  // |fetcher| is used to identify the |ArcBackgroundAuthCodeFetcher| instance
  // that completed the request. |callback| is completed with |ArcSignInStatus|
  // and |AccountInfo| depending on the success / failure of the operation.
  // |success| and |auth_code| are arguments passed by
  // |ArcBackgroundAuthCodeFetcher::Fetch| callback.
  void OnSecondaryAccountAuthCodeFetched(const std::string& account_name,
                                         ArcBackgroundAuthCodeFetcher* fetcher,
                                         RequestAccountInfoCallback callback,
                                         bool success,
                                         const std::string& auth_code);

  // Callback for data removal confirmation.
  void OnDataRemovalAccepted(bool accepted);

  // Creates an |ArcBackgroundAuthCodeFetcher| for |account_id|. Can be used for
  // Device Account and Secondary Accounts. |initial_signin| denotes whether the
  // fetcher is being created for the initial ARC provisioning flow or for a
  // subsequent sign-in.
  std::unique_ptr<ArcBackgroundAuthCodeFetcher>
  CreateArcBackgroundAuthCodeFetcher(const std::string& account_id,
                                     bool initial_signin);

  // Deletes a completed enrollment token / auth code fetch request from
  // |pending_token_requests_|.
  void DeletePendingTokenRequest(ArcFetcherBase* fetcher);

  // Triggers an async push of the accounts in IdentityManager to ARC.
  // If |filter_primary_account| is set to |true|, the Primary Account in Chrome
  // OS Account Manager will not be pushed to ARC as part of this call.
  void TriggerAccountsPushToArc(bool filter_primary_account);

  // Issues a request to ARC, which will complete callback with the list of
  // Google accounts in ARC.
  void DispatchAccountsInArc(GetGoogleAccountsInArcCallback callback);

  // Response for |mojom::GetMainAccountResolutionStatus|.
  void OnMainAccountResolutionStatus(mojom::MainAccountResolutionStatus status);

  // Non-owning pointers.
  Profile* const profile_;
  signin::IdentityManager* const identity_manager_;
  ArcBridgeService* const arc_bridge_service_;

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  bool url_loader_factory_for_testing_set_ = false;

  // A list of pending enrollment token / auth code requests.
  std::vector<std::unique_ptr<ArcFetcherBase>> pending_token_requests_;

  // Pending callback for |GetGoogleAccountsInArc| if ARC bridge is not yet
  // ready.
  GetGoogleAccountsInArcCallback pending_get_arc_accounts_callback_;

  bool skip_merge_session_for_testing_ = false;

  base::WeakPtrFactory<ArcAuthService> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(ArcAuthService);
};

}  // namespace arc

#endif  // CHROME_BROWSER_CHROMEOS_ARC_AUTH_ARC_AUTH_SERVICE_H_
