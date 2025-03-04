// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/launcher_app_service_app_updater.h"

#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/services/app_service/public/cpp/app_registry_cache.h"
#include "chrome/services/app_service/public/cpp/app_update.h"

LauncherAppServiceAppUpdater::LauncherAppServiceAppUpdater(
    Delegate* delegate,
    content::BrowserContext* browser_context)
    : LauncherAppUpdater(delegate, browser_context) {
  apps::AppServiceProxy* proxy = apps::AppServiceProxyFactory::GetForProfile(
      Profile::FromBrowserContext(browser_context));
  if (proxy) {
    Observe(&proxy->AppRegistryCache());
  }
}

LauncherAppServiceAppUpdater::~LauncherAppServiceAppUpdater() = default;

void LauncherAppServiceAppUpdater::OnAppUpdate(const apps::AppUpdate& update) {
  if (!update.ReadinessChanged()) {
    return;
  }

  const std::string& app_id = update.AppId();
  std::set<std::string>::const_iterator it = installed_apps_.find(app_id);
  if (update.Readiness() == apps::mojom::Readiness::kReady) {
    if (it == installed_apps_.end()) {
      installed_apps_.insert(app_id);
      delegate()->OnAppInstalled(browser_context(), app_id);
    }
  } else {
    if (it != installed_apps_.end()) {
      installed_apps_.erase(it);
      delegate()->OnAppUninstalledPrepared(browser_context(), app_id);
      delegate()->OnAppUninstalled(browser_context(), app_id);
    }
  }
}

void LauncherAppServiceAppUpdater::OnAppRegistryCacheWillBeDestroyed(
    apps::AppRegistryCache* cache) {
  Observe(nullptr);
}
