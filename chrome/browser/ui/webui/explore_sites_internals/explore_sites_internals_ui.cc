// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/explore_sites_internals/explore_sites_internals_ui.h"

#include "base/bind.h"
#include "build/build_config.h"
#include "chrome/browser/android/chrome_feature_list.h"
#include "chrome/browser/android/explore_sites/explore_sites_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/explore_sites_internals/explore_sites_internals_page_handler.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/browser_resources.h"
#include "content/public/browser/web_ui_data_source.h"

namespace explore_sites {

ExploreSitesInternalsUI::ExploreSitesInternalsUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::Create(
      chrome::kChromeUIExploreSitesInternalsHost);

  source->AddResourcePath("explore_sites_internals.css",
                          IDR_EXPLORE_SITES_INTERNALS_CSS);
  source->AddResourcePath("explore_sites_internals.js",
                          IDR_EXPLORE_SITES_INTERNALS_JS);
  source->AddResourcePath("explore_sites_internals.mojom-lite.js",
                          IDR_EXPLORE_SITES_INTERNALS_MOJO_JS);
  source->SetDefaultResource(IDR_EXPLORE_SITES_INTERNALS_HTML);

  Profile* profile = Profile::FromWebUI(web_ui);
  explore_sites_service_ =
      ExploreSitesServiceFactory::GetForBrowserContext(profile);
  content::WebUIDataSource::Add(profile, source);
  // "BindExploreSitesInternalsPageHandler" will be invoked by
  // explore_sites_internals.js, which is only possible while this object is
  // alive, so this base::Unretained is safe.
  AddHandlerToRegistry(base::BindRepeating(
      &ExploreSitesInternalsUI::BindExploreSitesInternalsPageHandler,
      base::Unretained(this)));
}

ExploreSitesInternalsUI::~ExploreSitesInternalsUI() {}

void ExploreSitesInternalsUI::BindExploreSitesInternalsPageHandler(
    explore_sites_internals::mojom::PageHandlerRequest request) {
  page_handler_ = std::make_unique<ExploreSitesInternalsPageHandler>(
      std::move(request), explore_sites_service_, Profile::FromWebUI(web_ui()));
}

}  // namespace explore_sites
