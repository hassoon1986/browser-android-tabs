// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_URL_LOADER_H_
#define SERVICES_NETWORK_URL_LOADER_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/component_export.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "base/unguessable_token.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "net/base/load_states.h"
#include "net/http/http_raw_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_request.h"
#include "services/network/cross_origin_read_blocking.h"
#include "services/network/initiator_lock_compatibility.h"
#include "services/network/keepalive_statistics_recorder.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/resource_scheduler/resource_scheduler.h"
#include "services/network/resource_scheduler/resource_scheduler_client.h"
#include "services/network/upload_progress_tracker.h"

namespace net {
class HttpResponseHeaders;
class URLRequestContext;
}

namespace network {

namespace mojom {
class OriginPolicyManager;
}

constexpr size_t kMaxFileUploadRequestsPerBatch = 64;

class NetToMojoPendingBuffer;
class NetworkUsageAccumulator;
class KeepaliveStatisticsRecorder;
struct ResourceResponse;
class ScopedThrottlingToken;
struct OriginPolicy;

class COMPONENT_EXPORT(NETWORK_SERVICE) URLLoader
    : public mojom::URLLoader,
      public net::URLRequest::Delegate,
      public mojom::AuthChallengeResponder,
      public mojom::ClientCertificateResponder {
 public:
  using DeleteCallback = base::OnceCallback<void(mojom::URLLoader* loader)>;

  // |delete_callback| tells the URLLoader's owner to destroy the URLLoader.
  // The URLLoader must be destroyed before the |url_request_context|.
  // The |origin_policy_manager| must always be provided for requests that
  // have the |obey_origin_policy| flag set.
  URLLoader(
      net::URLRequestContext* url_request_context,
      mojom::NetworkServiceClient* network_service_client,
      mojom::NetworkContextClient* network_context_client,
      DeleteCallback delete_callback,
      mojom::URLLoaderRequest url_loader_request,
      int32_t options,
      const ResourceRequest& request,
      mojom::URLLoaderClientPtr url_loader_client,
      const net::NetworkTrafficAnnotationTag& traffic_annotation,
      const mojom::URLLoaderFactoryParams* factory_params,
      uint32_t request_id,
      scoped_refptr<ResourceSchedulerClient> resource_scheduler_client,
      base::WeakPtr<KeepaliveStatisticsRecorder> keepalive_statistics_recorder,
      base::WeakPtr<NetworkUsageAccumulator> network_usage_accumulator,
      mojom::TrustedURLLoaderHeaderClient* url_loader_header_client,
      mojom::OriginPolicyManager* origin_policy_manager);
  ~URLLoader() override;

  // mojom::URLLoader implementation:
  void FollowRedirect(const std::vector<std::string>& removed_headers,
                      const net::HttpRequestHeaders& modified_headers,
                      const base::Optional<GURL>& new_url) override;
  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override;
  void PauseReadingBodyFromNet() override;
  void ResumeReadingBodyFromNet() override;

  // net::URLRequest::Delegate implementation:
  void OnReceivedRedirect(net::URLRequest* url_request,
                          const net::RedirectInfo& redirect_info,
                          bool* defer_redirect) override;
  void OnAuthRequired(net::URLRequest* request,
                      const net::AuthChallengeInfo& info) override;
  void OnCertificateRequested(net::URLRequest* request,
                              net::SSLCertRequestInfo* info) override;
  void OnSSLCertificateError(net::URLRequest* request,
                             int net_error,
                             const net::SSLInfo& info,
                             bool fatal) override;
  void OnResponseStarted(net::URLRequest* url_request, int net_error) override;
  void OnReadCompleted(net::URLRequest* url_request, int bytes_read) override;

  // These methods are called by the network delegate to forward these events to
  // the |header_client_|.
  int OnBeforeStartTransaction(net::CompletionOnceCallback callback,
                               net::HttpRequestHeaders* headers);
  int OnHeadersReceived(
      net::CompletionOnceCallback callback,
      const net::HttpResponseHeaders* original_response_headers,
      scoped_refptr<net::HttpResponseHeaders>* override_response_headers,
      GURL* allowed_unsafe_redirect_url);

  // mojom::AuthChallengeResponder:
  void OnAuthCredentials(
      const base::Optional<net::AuthCredentials>& credentials) override;

  // mojom::ClientCertificateResponder:
  void ContinueWithCertificate(
      const scoped_refptr<net::X509Certificate>& x509_certificate,
      const std::string& provider_name,
      const std::vector<uint16_t>& algorithm_preferences,
      mojom::SSLPrivateKeyPtr ssl_private_key) override;
  void ContinueWithoutCertificate() override;
  void CancelRequest() override;

  net::LoadState GetLoadStateForTesting() const;

  uint32_t GetRenderFrameId() const;
  uint32_t GetProcessId() const;
  uint32_t GetResourceType() const;

  // Whether this URLLoader should allow sending/setting cookies for requests
  // with |url| and |site_for_cookies|. This decision is based on the options
  // passed to URLLoaderFactory::CreateLoaderAndStart().
  bool AllowCookies(const GURL& url, const GURL& site_for_cookies) const;

  const net::HttpRequestHeaders& custom_proxy_pre_cache_headers() const {
    return custom_proxy_pre_cache_headers_;
  }

  const net::HttpRequestHeaders& custom_proxy_post_cache_headers() const {
    return custom_proxy_post_cache_headers_;
  }

  bool custom_proxy_use_alternate_proxy_list() const {
    return custom_proxy_use_alternate_proxy_list_;
  }

  const base::Optional<GURL>& new_redirect_url() const {
    return new_redirect_url_;
  }

  const base::Optional<std::string>& devtools_request_id() const {
    return devtools_request_id_;
  }

  void SetAllowReportingRawHeaders(bool allow);

  // Gets the URLLoader associated with this request.
  static URLLoader* ForRequest(const net::URLRequest& request);

  static const void* const kUserDataKey;

 private:
  // This class is used to set the URLLoader as user data on a URLRequest. This
  // is used instead of URLLoader directly because SetUserData requires a
  // std::unique_ptr. This is safe because URLLoader owns the URLRequest, so is
  // guaranteed to outlive it.
  class UnownedPointer : public base::SupportsUserData::Data {
   public:
    explicit UnownedPointer(URLLoader* pointer) : pointer_(pointer) {}

    URLLoader* get() const { return pointer_; }

   private:
    URLLoader* const pointer_;

    DISALLOW_COPY_AND_ASSIGN(UnownedPointer);
  };

  class FileOpenerForUpload;
  friend class FileOpenerForUpload;

  void OpenFilesForUpload(const ResourceRequest& request);
  void SetUpUpload(const ResourceRequest& request,
                   int error_code,
                   const std::vector<base::File> opened_files);
  void ScheduleStart();
  void ReadMore();
  void DidRead(int num_bytes, bool completed_synchronously);
  void NotifyCompleted(int error_code);
  void OnConnectionError();
  void OnResponseBodyStreamConsumerClosed(MojoResult result);
  void OnResponseBodyStreamReady(MojoResult result);
  void DeleteSelf();
  void SendResponseToClient();
  void CompletePendingWrite(bool success);
  void SetRawResponseHeaders(scoped_refptr<const net::HttpResponseHeaders>);
  void SetRawRequestHeadersAndNotify(net::HttpRawRequestHeaders);
  void SendUploadProgress(const net::UploadProgress& progress);
  void OnUploadProgressACK();
  void OnSSLCertificateErrorResponse(const net::SSLInfo& ssl_info,
                                     int net_error);
  bool HasDataPipe() const;
  void RecordBodyReadFromNetBeforePausedIfNeeded();
  void ResumeStart();
  void OnBeforeSendHeadersComplete(
      net::CompletionOnceCallback callback,
      net::HttpRequestHeaders* out_headers,
      int result,
      const base::Optional<net::HttpRequestHeaders>& headers);
  void OnHeadersReceivedComplete(
      net::CompletionOnceCallback callback,
      scoped_refptr<net::HttpResponseHeaders>* out_headers,
      GURL* out_allowed_unsafe_redirect_url,
      int result,
      const base::Optional<std::string>& headers,
      const GURL& allowed_unsafe_redirect_url);

  void CompleteBlockedResponse(int error_code,
                               bool should_report_corb_blocking);

  enum BlockResponseForCorbResult {
    // Returned when caller of BlockResponseForCorb doesn't need to continue,
    // because the request will be cancelled soon.
    kWillCancelRequest,

    // Returned when the caller of BlockResponseForCorb should continue
    // processing the request (e.g. by calling ReadMore as necessary).
    kContinueRequest,
  };
  BlockResponseForCorbResult BlockResponseForCorb();

  void ReportFlaggedResponseCookies();
  void StartReading();
  void OnOriginPolicyManagerRetrieveDone(const OriginPolicy& origin_policy);

  net::URLRequestContext* url_request_context_;
  mojom::NetworkServiceClient* network_service_client_;
  mojom::NetworkContextClient* network_context_client_;
  DeleteCallback delete_callback_;

  int32_t options_;
  bool corb_detachable_;
  int resource_type_;
  bool is_load_timing_enabled_;

  // URLLoaderFactory is guaranteed to outlive URLLoader, so it is safe to
  // store a raw pointer to mojom::URLLoaderFactoryParams.
  const mojom::URLLoaderFactoryParams* const factory_params_;

  int render_frame_id_;
  uint32_t request_id_;
  const bool keepalive_;
  const bool do_not_prompt_for_login_;
  std::unique_ptr<net::URLRequest> url_request_;
  mojo::Binding<mojom::URLLoader> binding_;
  mojo::Receiver<mojom::AuthChallengeResponder>
      auth_challenge_responder_receiver_{this};
  mojo::Receiver<mojom::ClientCertificateResponder>
      client_cert_responder_receiver_{this};
  mojom::URLLoaderClientPtr url_loader_client_;
  int64_t total_written_bytes_ = 0;

  mojo::ScopedDataPipeProducerHandle response_body_stream_;
  scoped_refptr<NetToMojoPendingBuffer> pending_write_;
  uint32_t pending_write_buffer_size_ = 0;
  uint32_t pending_write_buffer_offset_ = 0;
  mojo::SimpleWatcher writable_handle_watcher_;
  mojo::SimpleWatcher peer_closed_handle_watcher_;

  // Used when deferring sending the data to the client until mime sniffing is
  // finished.
  scoped_refptr<ResourceResponse> response_;
  mojo::ScopedDataPipeConsumerHandle consumer_handle_;

  // Sniffing state.
  std::unique_ptr<CrossOriginReadBlocking::ResponseAnalyzer> corb_analyzer_;
  bool is_more_corb_sniffing_needed_ = false;
  bool is_more_mime_sniffing_needed_ = false;

  std::unique_ptr<ResourceScheduler::ScheduledResourceRequest>
      resource_scheduler_request_handle_;

  // Whether client requested raw headers.
  const bool want_raw_headers_;
  // Whether we actually should report them.
  bool report_raw_headers_;
  net::HttpRawRequestHeaders raw_request_headers_;
  scoped_refptr<const net::HttpResponseHeaders> raw_response_headers_;

  std::unique_ptr<UploadProgressTracker> upload_progress_tracker_;

  // Holds the URL of a redirect if it's currently deferred.
  std::unique_ptr<GURL> deferred_redirect_url_;

  // If |new_url| is given to FollowRedirect() it's saved here, so that it can
  // be later referred to from NetworkContext::OnBeforeURLRequestInternal, which
  // is called from NetworkDelegate::NotifyBeforeURLRequest.
  base::Optional<GURL> new_redirect_url_;

  // The ID that DevTools uses to track network requests. It is generated in the
  // renderer process and is only present when DevTools is enabled in the
  // renderer.
  const base::Optional<std::string> devtools_request_id_;

  bool should_pause_reading_body_ = false;
  // The response body stream is open, but transferring data is paused.
  bool paused_reading_body_ = false;

  // Whether to update |body_read_before_paused_| after the pending read is
  // completed (or when the response body stream is closed).
  bool update_body_read_before_paused_ = false;
  // The number of bytes obtained by the reads initiated before the last
  // PauseReadingBodyFromNet() call. -1 means the request hasn't been paused.
  // The body may be read from cache or network. So even if this value is not
  // -1, we still need to check whether it is from network before reporting it
  // as BodyReadFromNetBeforePaused.
  int64_t body_read_before_paused_ = -1;

  // This is used to compute the delta since last time received
  // encoded body size was reported to the client.
  int64_t reported_total_encoded_bytes_ = 0;

  // Indicates whether this request was made by a CORB-excluded request type and
  // was not using CORS. Such requests are exempt from blocking, while other
  // CORB-excluded requests must be blocked if the CORS check fails.
  bool is_nocors_corb_excluded_request_ = false;

  mojom::RequestMode request_mode_;

  scoped_refptr<ResourceSchedulerClient> resource_scheduler_client_;

  mojom::SSLPrivateKeyPtr ssl_private_key_;

  base::WeakPtr<KeepaliveStatisticsRecorder> keepalive_statistics_recorder_;

  base::WeakPtr<NetworkUsageAccumulator> network_usage_accumulator_;

  bool first_auth_attempt_;

  std::unique_ptr<ScopedThrottlingToken> throttling_token_;

  net::HttpRequestHeaders custom_proxy_pre_cache_headers_;
  net::HttpRequestHeaders custom_proxy_post_cache_headers_;
  bool custom_proxy_use_alternate_proxy_list_ = false;

  // Indicates the originating frame of the request, see
  // network::ResourceRequest::fetch_window_id for details.
  base::Optional<base::UnguessableToken> fetch_window_id_;

  mojo::Remote<mojom::TrustedHeaderClient> header_client_;

  std::unique_ptr<FileOpenerForUpload> file_opener_for_upload_;

  // See detailed comment in
  // mojom::network::URLRequest::update_network_isolation_key_on_redirect.
  mojom::UpdateNetworkIsolationKeyOnRedirect
      update_network_isolation_key_on_redirect_;

  // Will only be set for requests that have |obey_origin_policy| set.
  mojom::OriginPolicyManager* origin_policy_manager_;

  base::WeakPtrFactory<URLLoader> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(URLLoader);
};

}  // namespace network

#endif  // SERVICES_NETWORK_URL_LOADER_H_
