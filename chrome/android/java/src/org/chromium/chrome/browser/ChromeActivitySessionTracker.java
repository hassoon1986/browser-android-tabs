// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.SharedPreferences;
import android.os.SystemClock;
import android.provider.Settings;
import android.support.annotation.Nullable;
import android.text.TextUtils;

import org.chromium.base.ApplicationState;
import org.chromium.base.ApplicationStatus;
import org.chromium.base.ApplicationStatus.ApplicationStateListener;
import org.chromium.base.Callback;
import org.chromium.base.ContextUtils;
import org.chromium.base.LocaleUtils;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.chrome.browser.accessibility.FontSizePrefs;
import org.chromium.chrome.browser.browsing_data.BrowsingDataType;
import org.chromium.chrome.browser.browsing_data.TimePeriod;
import org.chromium.chrome.browser.metrics.UmaUtils;
import org.chromium.chrome.browser.metrics.VariationsSession;
import org.chromium.chrome.browser.notifications.NotificationPlatformBridge;
import org.chromium.chrome.browser.partnercustomizations.PartnerBrowserCustomizations;
import org.chromium.chrome.browser.preferences.PrefServiceBridge;
import org.chromium.chrome.browser.preferences.privacy.BrowsingDataBridge;
import org.chromium.chrome.browser.profiles.ProfileManagerUtils;
import org.chromium.chrome.browser.share.ShareHelper;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.util.FeatureUtilities;
import org.chromium.ui.base.ResourceBundle;

import java.util.Locale;

/**
 * Tracks the foreground session state for the Chrome activities.
 */
public class ChromeActivitySessionTracker {

    private static final String PREF_LOCALE = "locale";
    public static final String SESSION_START_TIME = "session_start_time";
    public static final String TOTAL_LIFETIME_USAGE = "total_lifetime_usage";

    @SuppressLint("StaticFieldLeak")
    private static ChromeActivitySessionTracker sInstance;

    private final PowerBroadcastReceiver mPowerBroadcastReceiver = new PowerBroadcastReceiver();

    // Used to trigger variation changes (such as seed fetches) upon application foregrounding.
    private VariationsSession mVariationsSession;

    private boolean mIsInitialized;
    private boolean mIsStarted;
    private boolean mIsFinishedCachingNativeFlags;

    /**
     * @return The activity session tracker for Chrome.
     */
    public static ChromeActivitySessionTracker getInstance() {
        ThreadUtils.assertOnUiThread();

        if (sInstance == null) sInstance = new ChromeActivitySessionTracker();
        return sInstance;
    }

    /**
     * Constructor exposed for extensibility only.
     * @see #getInstance()
     */
    protected ChromeActivitySessionTracker() {
        mVariationsSession = AppHooks.get().createVariationsSession();
    }

    /**
     * Asynchronously returns the value of the "restrict" URL param that the variations service
     * should use for variation seed requests.
     * @param callback Callback that will be called with the param value when available.
     */
    public void getVariationsRestrictModeValue(Callback<String> callback) {
        mVariationsSession.getRestrictModeValue(callback);
    }

    /**
     * @return The latest country according to the current variations state. Null if not available.
     */
    @Nullable
    public String getVariationsLatestCountry() {
        return mVariationsSession.getLatestCountry();
    }

    /**
     * Handle any initialization that occurs once native has been loaded.
     */
    public void initializeWithNative() {
        ThreadUtils.assertOnUiThread();

        if (mIsInitialized) return;
        mIsInitialized = true;
        assert !mIsStarted;

        ApplicationStatus.registerApplicationStateListener(createApplicationStateListener());
    }

    /**
     * Each top-level activity (those extending {@link ChromeActivity}) should call this during
     * its onStart phase. When called for the first time, this marks the beginning of a foreground
     * session and calls onForegroundSessionStart(). Subsequent calls are noops until
     * onForegroundSessionEnd() is called, to handle changing top-level Chrome activities in one
     * foreground session.
     */
    public void onStartWithNative() {
        ThreadUtils.assertOnUiThread();

        if (mIsStarted) return;
        mIsStarted = true;

        assert mIsInitialized;

        onForegroundSessionStart();
        cacheNativeFlags();
    }

    /**
     * Called when a top-level Chrome activity (ChromeTabbedActivity, CustomTabActivity) is
     * started in foreground. It will not be called again when other Chrome activities take over
     * (see onStart()), that is, when correct activity calls startActivity() for another Chrome
     * activity.
     */
    private void onForegroundSessionStart() {
        UmaUtils.recordForegroundStartTime();
        recordSessionStart();
        updatePasswordEchoState();
        FontSizePrefs.getInstance().onSystemFontScaleChanged();
        recordWhetherSystemAndAppLanguagesDiffer();
        updateAcceptLanguages();
        mVariationsSession.start();
        mPowerBroadcastReceiver.onForegroundSessionStart();

        // Track the ratio of Chrome startups that are caused by notification clicks.
        // TODO(johnme): Add other reasons (and switch to recordEnumeratedHistogram).
        RecordHistogram.recordBooleanHistogram(
                "Startup.BringToForegroundReason",
                NotificationPlatformBridge.wasNotificationRecentlyClicked());
    }

    /**
     * Called when last of Chrome activities is stopped, ending the foreground session. This will
     * not be called when a Chrome activity is stopped because another Chrome activity takes over.
     * This is ensured by ActivityStatus, which switches to track new activity when its started and
     * will not report the old one being stopped (see createStateListener() below).
     */
    private void onForegroundSessionEnd() {
        if (!mIsStarted) return;
        UmaUtils.recordBackgroundTime();
        updateLifetimeSessionLength();
        ProfileManagerUtils.flushPersistentDataForAllProfiles();
        mIsStarted = false;
        mPowerBroadcastReceiver.onForegroundSessionEnd();

        IntentHandler.clearPendingReferrer();
        IntentHandler.clearPendingIncognitoUrl();

        int totalTabCount = 0;
        for (Activity activity : ApplicationStatus.getRunningActivities()) {
            if (activity instanceof ChromeActivity
                    && ((ChromeActivity) activity).areTabModelsInitialized()) {
                TabModelSelector tabModelSelector =
                        ((ChromeActivity) activity).getTabModelSelector();
                if (tabModelSelector != null) {
                    totalTabCount += tabModelSelector.getTotalTabCount();
                }
            }
        }
        RecordHistogram.recordCountHistogram(
                "Tab.TotalTabCount.BeforeLeavingApp", totalTabCount);
    }

    private void recordSessionStart() {
        SharedPreferences prefs = ContextUtils.getAppSharedPreferences();
        SharedPreferences.Editor editor = prefs.edit();
        editor.putLong(SESSION_START_TIME, SystemClock.uptimeMillis());
        editor.apply();
    }

    private void updateLifetimeSessionLength() {
        SharedPreferences prefs = ContextUtils.getAppSharedPreferences();
        SharedPreferences.Editor editor = prefs.edit();
        editor.putLong(TOTAL_LIFETIME_USAGE, prefs.getLong(TOTAL_LIFETIME_USAGE, 0) + SystemClock.uptimeMillis() - prefs.getLong(SESSION_START_TIME, SystemClock.uptimeMillis()));
        editor.apply();
    }

    private void onForegroundActivityDestroyed() {
        if (ApplicationStatus.isEveryActivityDestroyed()) {
            // These will all be re-initialized when a new Activity starts / upon next use.
            PartnerBrowserCustomizations.destroy();
            ShareHelper.clearSharedImages();
        }
    }

    private ApplicationStateListener createApplicationStateListener() {
        return newState -> {
            if (newState == ApplicationState.HAS_STOPPED_ACTIVITIES) {
                onForegroundSessionEnd();
            } else if (newState == ApplicationState.HAS_DESTROYED_ACTIVITIES) {
                onForegroundActivityDestroyed();
            }
        };
    }

    /**
     * Update the accept languages after changing Android locale setting. Doing so kills the
     * Activities but it doesn't kill the Application, so this should be called in
     * {@link #onStart} instead of {@link #initialize}.
     */
    private void updateAcceptLanguages() {
        String localeString = LocaleUtils.getDefaultLocaleListString();
        if (hasLocaleChanged(localeString)) {
            // Clear cache so that accept-languages change can be applied immediately.
            // TODO(changwan): The underlying BrowsingDataRemover::Remove() is an asynchronous call.
            // So cache-clearing may not be effective if URL rendering can happen before
            // OnBrowsingDataRemoverDone() is called, in which case we may have to reload as well.
            // Check if it can happen.
            BrowsingDataBridge.getInstance().clearBrowsingData(
                    null, new int[] {BrowsingDataType.CACHE}, TimePeriod.ALL_TIME);
        }
    }

    private boolean hasLocaleChanged(String newLocale) {
        String previousLocale = ContextUtils.getAppSharedPreferences().getString(PREF_LOCALE, null);
        if (!TextUtils.equals(previousLocale, newLocale)) {
            SharedPreferences prefs = ContextUtils.getAppSharedPreferences();
            SharedPreferences.Editor editor = prefs.edit();
            editor.putString(PREF_LOCALE, newLocale);
            editor.apply();
            PrefServiceBridge.getInstance().resetAcceptLanguages(newLocale);
            // We consider writing the initial value to prefs as _not_ changing the locale.
            return previousLocale != null;
        }
        return false;
    }

    /**
     * Honor the Android system setting about showing the last character of a password for a short
     * period of time.
     */
    private void updatePasswordEchoState() {
        boolean systemEnabled =
                Settings.System.getInt(ContextUtils.getApplicationContext().getContentResolver(),
                        Settings.System.TEXT_SHOW_PASSWORD, 1)
                == 1;
        if (PrefServiceBridge.getInstance().getPasswordEchoEnabled() == systemEnabled) return;

        PrefServiceBridge.getInstance().setPasswordEchoEnabled(systemEnabled);
    }

    /**
     * Caches flags that are needed by Activities that launch before the native library is loaded
     * and stores them in SharedPreferences. Because this function is called during launch after the
     * library has loaded, they won't affect the next launch until Chrome is restarted.
     */
    private void cacheNativeFlags() {
        if (mIsFinishedCachingNativeFlags) return;
        FeatureUtilities.cacheNativeFlags();
        mIsFinishedCachingNativeFlags = true;
    }

    /**
     * Records whether Chrome was started in a language other than the system language but we
     * support the system language. That can happen if the user changes the system language and the
     * required language split cannot be installed in time.
     */
    private void recordWhetherSystemAndAppLanguagesDiffer() {
        String uiLanguage =
                LocaleUtils.toLanguage(ChromeLocalizationUtils.getUiLocaleStringForCompressedPak());
        String systemLanguage =
                LocaleUtils.toLanguage(LocaleUtils.toLanguageTag(Locale.getDefault()));
        boolean isWrongLanguage = !systemLanguage.equals(uiLanguage)
                && isLanguageSupported(
                        systemLanguage, ResourceBundle.getAvailableCompressedPakLocales());
        RecordHistogram.recordBooleanHistogram(
                "Android.Language.WrongLanguageAfterResume", isWrongLanguage);
    }

    private static boolean isLanguageSupported(String language, String[] compressedLocales) {
        for (String languageTag : compressedLocales) {
            if (LocaleUtils.toLanguage(languageTag).equals(language)) {
                return true;
            }
        }
        return false;
    }

    /**
     * @return The PowerBroadcastReceiver for the browser process.
     */
    @VisibleForTesting
    public PowerBroadcastReceiver getPowerBroadcastReceiverForTesting() {
        return mPowerBroadcastReceiver;
    }
}
