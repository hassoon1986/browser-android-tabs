// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PAGE_LOAD_METRICS_OBSERVERS_AD_METRICS_ADS_PAGE_LOAD_METRICS_OBSERVER_H_
#define CHROME_BROWSER_PAGE_LOAD_METRICS_OBSERVERS_AD_METRICS_ADS_PAGE_LOAD_METRICS_OBSERVER_H_

#include <bitset>
#include <list>
#include <map>
#include <memory>

#include "base/macros.h"
#include "base/scoped_observer.h"
#include "base/time/tick_clock.h"
#include "chrome/browser/page_load_metrics/observers/ad_metrics/frame_data.h"
#include "chrome/browser/page_load_metrics/page_load_metrics_observer.h"
#include "components/page_load_metrics/common/page_load_metrics.mojom.h"
#include "components/subresource_filter/content/browser/subresource_filter_observer.h"
#include "components/subresource_filter/content/browser/subresource_filter_observer_manager.h"
#include "components/subresource_filter/core/common/load_policy.h"
#include "net/http/http_response_info.h"
#include "services/metrics/public/cpp/ukm_source.h"

class HeavyAdBlocklist;

// This observer labels each sub-frame as an ad or not, and keeps track of
// relevant per-frame and whole-page byte statistics.
class AdsPageLoadMetricsObserver
    : public page_load_metrics::PageLoadMetricsObserver,
      public subresource_filter::SubresourceFilterObserver {
 public:

  // Returns a new AdsPageLoadMetricObserver. If the feature is disabled it
  // returns nullptr.
  static std::unique_ptr<AdsPageLoadMetricsObserver> CreateIfNeeded(
      content::WebContents* web_contents);

  // For a given subframe, returns whether or not the subframe's url would be
  // considering same origin to the main frame's url. |use_parent_origin|
  // indicates that the subframe's parent frames's origin should be used when
  // performing the comparison.
  static bool IsSubframeSameOriginToMainFrame(
      content::RenderFrameHost* sub_host,
      bool use_parent_origin);

  using ResourceMimeType = FrameData::ResourceMimeType;

  // Aggregates high level summary statistics across FrameData objects.
  struct AggregateFrameInfo {
    AggregateFrameInfo();
    size_t bytes;
    size_t network_bytes;
    size_t num_frames;
    base::TimeDelta cpu_time;

    DISALLOW_COPY_AND_ASSIGN(AggregateFrameInfo);
  };

  explicit AdsPageLoadMetricsObserver(base::TickClock* clock = nullptr,
                                      HeavyAdBlocklist* blocklist = nullptr);
  ~AdsPageLoadMetricsObserver() override;

  // page_load_metrics::PageLoadMetricsObserver
  ObservePolicy OnStart(content::NavigationHandle* navigation_handle,
                        const GURL& currently_committed_url,
                        bool started_in_foreground) override;
  ObservePolicy OnCommit(content::NavigationHandle* navigation_handle,
                         ukm::SourceId source_id) override;
  void OnTimingUpdate(
      content::RenderFrameHost* subframe_rfh,
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void OnCpuTimingUpdate(
      content::RenderFrameHost* subframe_rfh,
      const page_load_metrics::mojom::CpuTiming& timing) override;
  void RecordAdFrameData(FrameTreeNodeId ad_id,
                         bool is_adframe,
                         content::RenderFrameHost* ad_host,
                         bool frame_navigated);
  void ReadyToCommitNextNavigation(
      content::NavigationHandle* navigation_handle) override;
  void OnDidFinishSubFrameNavigation(
      content::NavigationHandle* navigation_handle) override;
  ObservePolicy FlushMetricsOnAppEnterBackground(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void OnComplete(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void OnResourceDataUseObserved(
      content::RenderFrameHost* rfh,
      const std::vector<page_load_metrics::mojom::ResourceDataUpdatePtr>&
          resources) override;
  void OnPageInteractive(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void FrameReceivedFirstUserActivation(content::RenderFrameHost* rfh) override;
  void FrameDisplayStateChanged(content::RenderFrameHost* render_frame_host,
                                bool is_display_none) override;
  void FrameSizeChanged(content::RenderFrameHost* render_frame_host,
                        const gfx::Size& frame_size) override;
  void MediaStartedPlaying(
      const content::WebContentsObserver::MediaPlayerInfo& video_type,
      content::RenderFrameHost* render_frame_host) override;
  void OnFrameDeleted(content::RenderFrameHost* render_frame_host) override;

 private:
  // subresource_filter::SubresourceFilterObserver:
  void OnAdSubframeDetected(
      content::RenderFrameHost* render_frame_host) override;
  void OnSubresourceFilterGoingAway() override;

  // Gets the number of bytes that we may have not attributed to ad
  // resources due to the resource being reported as an ad late.
  int GetUnaccountedAdBytes(
      int process_id,
      const page_load_metrics::mojom::ResourceDataUpdatePtr& resource) const;

  // Updates page level counters for resource loads.
  void ProcessResourceForPage(
      int process_id,
      const page_load_metrics::mojom::ResourceDataUpdatePtr& resource);
  void ProcessResourceForFrame(
      content::RenderFrameHost* render_frame_host,
      const page_load_metrics::mojom::ResourceDataUpdatePtr& resource);

  void RecordPageResourceTotalHistograms(ukm::SourceId source_id);
  void RecordHistograms(ukm::SourceId source_id);
  void RecordAggregateHistogramsForAdTagging(
      FrameData::FrameVisibility visibility);
  void RecordAggregateHistogramsForCpuUsage();
  void RecordPerFrameHistogramsForAdTagging(const FrameData& ad_frame_data);
  void RecordPerFrameHistogramsForCpuUsage(const FrameData& ad_frame_data);

  // Checks to see if a resource is waiting for a navigation in the given
  // RenderFrameHost to commit before it can be processed. If so, call
  // OnResourceDataUpdate for the delayed resource.
  void ProcessOngoingNavigationResource(content::RenderFrameHost* rfh);

  // Find the FrameData object associated with a given FrameTreeNodeId in
  // |ad_frames_data_storage_|.
  FrameData* FindFrameData(FrameTreeNodeId id);

  // Loads the heavy ad intervention page in the target frame if it is safe to
  // do so on this origin, and the frame meets the criteria to be considered a
  // heavy ad.
  // TODO(johnidel): Ads may only automatically be unloaded 5 times per-origin
  // per day and to prevent a side channel leak of cross-origin resource size /
  // CPU usage.
  void MaybeTriggerHeavyAdIntervention(
      content::RenderFrameHost* render_frame_host,
      FrameData* frame_data);

  bool IsBlocklisted();
  HeavyAdBlocklist* GetHeavyAdBlocklist();

  // Stores the size data of each ad frame. Pointed to by ad_frames_ so use a
  // data structure that won't move the data around. This only stores ad frames
  // that are actively on the page. When a frame is destroyed, so should its
  // FrameData.
  std::list<FrameData> ad_frames_data_storage_;

  // Maps a frame (by id) to the corresponding iterator of
  // |ad_frames_data_storage_| responsible for the frame. Multiple frame ids can
  // point to the same FrameData. The responsible frame is the top-most frame
  // labeled as an ad in the frame's ancestry, which may be itself. If no
  // responsible frame is found, the data is an iterator to the end of
  // |ad_frames_data_storage_|.
  std::map<FrameTreeNodeId, std::list<FrameData>::iterator> ad_frames_data_;

  // When the observer receives report of a document resource loading for a
  // sub-frame before the sub-frame commit occurs, hold onto the resource
  // request info (delay it) until the sub-frame commits.
  std::map<FrameTreeNodeId, page_load_metrics::mojom::ResourceDataUpdatePtr>
      ongoing_navigation_resources_;

  // Tracks byte counts only for resources loaded in the main frame.
  std::unique_ptr<FrameData> main_frame_data_;

  // Tracks aggregate counts across all frames on the page.
  std::unique_ptr<FrameData> aggregate_frame_data_;

  // Tracks aggregate counts across all ad frames on the page by visibility
  // type.
  AggregateFrameInfo aggregate_ad_info_by_visibility_
      [static_cast<size_t>(FrameData::FrameVisibility::kMaxValue) + 1];

  // Flag denoting that this observer should no longer monitor changes in
  // display state for frames. This prevents us from receiving the updates when
  // the frame elements are being destroyed in the renderer.
  bool process_display_state_updates_ = true;

  // Time the page was committed.
  base::TimeTicks time_commit_;

  // Time the page was observed to be interactive.
  base::TimeTicks time_interactive_;

  // Duration before |time_interactive_| during which the page was foregrounded.
  base::TimeDelta pre_interactive_duration_;

  // Total ad bytes loaded by the page since it was observed to be interactive.
  size_t page_ad_bytes_at_interactive_ = 0u;

  bool committed_ = false;

  ScopedObserver<subresource_filter::SubresourceFilterObserverManager,
                 subresource_filter::SubresourceFilterObserver>
      subresource_observer_;

  // The tick clock used to get the current time.  Can be replaced by tests.
  const base::TickClock* clock_;

  // Stores whether the heavy ad intervention is blocklisted or not for the user
  // on the URL of this page. Incognito Profiles will cause this to be set to
  // true. Used as a cache to avoid checking the blocklist once the page is
  // blocklisted. Once blocklisted, a page load cannot be unblocklisted.
  bool heavy_ads_blocklist_blocklisted_ = false;

  // Pointer to the blocklist used to throttle the heavy ad intervention. Can
  // be replaced by tests.
  HeavyAdBlocklist* heavy_ad_blocklist_;

  // Whether the heavy ad blocklist feature is enabled.
  const bool heavy_ad_blocklist_enabled_;

  DISALLOW_COPY_AND_ASSIGN(AdsPageLoadMetricsObserver);
};

#endif  // CHROME_BROWSER_PAGE_LOAD_METRICS_OBSERVERS_AD_METRICS_ADS_PAGE_LOAD_METRICS_OBSERVER_H_
