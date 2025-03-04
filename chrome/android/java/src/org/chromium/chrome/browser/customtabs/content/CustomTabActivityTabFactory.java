// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.customtabs.content;

import android.content.Intent;
import android.support.annotation.Nullable;
import android.util.Pair;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeApplication;
import org.chromium.chrome.browser.IntentHandler;
import org.chromium.chrome.browser.customtabs.CustomTabDelegateFactory;
import org.chromium.chrome.browser.customtabs.CustomTabIntentDataProvider;
import org.chromium.chrome.browser.customtabs.CustomTabTabPersistencePolicy;
import org.chromium.chrome.browser.dependency_injection.ActivityScope;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabBuilder;
import org.chromium.chrome.browser.tab.TabDelegateFactory;
import org.chromium.chrome.browser.tabmodel.ChromeTabCreator;
import org.chromium.chrome.browser.tabmodel.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorImpl;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver;
import org.chromium.chrome.browser.util.IntentUtils;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.ui.base.ActivityWindowAndroid;

import java.net.URL;

import javax.inject.Inject;

import dagger.Lazy;

/**
 * Creates {@link Tab}, {@link TabModelSelector}, and {@link ChromeTabCreator}s in the context of a
 * Custom Tab activity.
 */
@ActivityScope
public class CustomTabActivityTabFactory {
    private final ChromeActivity mActivity;
    private final CustomTabTabPersistencePolicy mPersistencePolicy;
    private final Lazy<ActivityWindowAndroid> mActivityWindowAndroid;
    private final Lazy<CustomTabDelegateFactory> mCustomTabDelegateFactory;
    private final CustomTabIntentDataProvider mIntentDataProvider;

    private TabModelSelectorTabObserver mTabModelSelectorTabObserver;

    @Nullable
    private TabModelSelectorImpl mTabModelSelector;

    @Inject
    public CustomTabActivityTabFactory(ChromeActivity activity,
            CustomTabTabPersistencePolicy persistencePolicy,
            Lazy<ActivityWindowAndroid> activityWindowAndroid,
            Lazy<CustomTabDelegateFactory> customTabDelegateFactory,
            CustomTabIntentDataProvider intentDataProvider) {
        mActivity = activity;
        mPersistencePolicy = persistencePolicy;
        mActivityWindowAndroid = activityWindowAndroid;
        mCustomTabDelegateFactory = customTabDelegateFactory;
        mIntentDataProvider = intentDataProvider;
    }

    /** Creates a {@link TabModelSelector} for the custom tab. */
    public TabModelSelectorImpl createTabModelSelector() {
        mTabModelSelector = new TabModelSelectorImpl(
                mActivity, mActivity, mPersistencePolicy, false, false, false);

        mTabModelSelectorTabObserver = new TabModelSelectorTabObserver(mTabModelSelector) {
            @Override
            public void onLoadStarted(Tab tab, boolean toDifferentDocument) {
                ChromeApplication app = (ChromeApplication)ContextUtils.getBaseApplicationContext();
                if ((null != app) && (null != app.getShieldsConfig())) {
                    app.getShieldsConfig().setTabModelSelectorTabObserver(mTabModelSelectorTabObserver);
                }
                if (mActivity.getActivityTab() == tab) {
                    try {
                        URL urlCheck = new URL(tab.getUrl());
                        mActivity.setBraveShieldsColor(tab.isIncognito(), urlCheck.getHost());
                    } catch (Exception e) {
                        mActivity.setBraveShieldsBlackAndWhite();
                    }
                }
                tab.clearBraveShieldsCount();
            }

            @Override
            public void onPageLoadFinished(Tab tab, String url) {
                if (mActivity.getActivityTab() == tab) {
                    try {
                        URL urlCheck = new URL(url);
                        mActivity.setBraveShieldsColor(tab.isIncognito(), urlCheck.getHost());
                    } catch (Exception e) {
                        mActivity.setBraveShieldsBlackAndWhite();
                    }
                }
            }

            @Override
            public void onBraveShieldsCountUpdate(String url, int adsAndTrackers, int httpsUpgrades,
                    int scriptsBlocked, int fingerprintsBlocked) {
                mActivity.braveShieldsCountUpdate(url, adsAndTrackers, httpsUpgrades, scriptsBlocked, fingerprintsBlocked);
            }
        };

        return mTabModelSelector;
    }

    /** Returns the previously created {@link TabModelSelector}. */
    public TabModelSelectorImpl getTabModelSelector() {
        if (mTabModelSelector == null) {
            assert false;
            return createTabModelSelector();
        }
        return mTabModelSelector;
    }

    /** Creates a {@link ChromeTabCreator}s for the custom tab. */
    public Pair<ChromeTabCreator, ChromeTabCreator> createTabCreators() {
        return Pair.create(createTabCreator(false), createTabCreator(true));
    }

    private ChromeTabCreator createTabCreator(boolean incognito) {
        return new ChromeTabCreator(mActivity, mActivityWindowAndroid.get(), incognito) {
            @Override
            public TabDelegateFactory createDefaultTabDelegateFactory() {
                return mCustomTabDelegateFactory.get();
            }
        };
    }

    /** Creates a new tab for a Custom Tab activity */
    public Tab createTab() {
        Intent intent = mIntentDataProvider.getIntent();
        int assignedTabId =
                IntentUtils.safeGetIntExtra(intent, IntentHandler.EXTRA_TAB_ID, Tab.INVALID_TAB_ID);
        int parentTabId = IntentUtils.safeGetIntExtra(
                intent, IntentHandler.EXTRA_PARENT_TAB_ID, Tab.INVALID_TAB_ID);
        Tab parent = mTabModelSelector != null ? mTabModelSelector.getTabById(parentTabId) : null;
        return new TabBuilder()
                .setId(assignedTabId)
                .setParent(parent)
                .setIncognito(mIntentDataProvider.isIncognito())
                .setWindow(mActivityWindowAndroid.get())
                .setLaunchType(TabLaunchType.FROM_EXTERNAL_APP)
                .build();
    }

    /**
     * This method is to circumvent calling final {@link ChromeActivity#initializeTabModels} in unit
     * tests for {@link CustomTabActivityTabController}.
     * TODO(pshmakov): remove once mock-maker-inline is introduced.
     */
    public void initializeTabModels() {
        mActivity.initializeTabModels();
    }
}
