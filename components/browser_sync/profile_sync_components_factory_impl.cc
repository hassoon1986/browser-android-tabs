// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/browser_sync/profile_sync_components_factory_impl.h"

#include <utility>

#include "base/bind.h"
#include "base/feature_list.h"
#include "base/memory/ref_counted.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/payments/autofill_wallet_model_type_controller.h"
#include "components/autofill/core/browser/webdata/autocomplete_sync_bridge.h"
#include "components/autofill/core/browser/webdata/autofill_profile_model_type_controller.h"
#include "components/autofill/core/browser/webdata/autofill_profile_sync_bridge.h"
#include "components/autofill/core/browser/webdata/autofill_wallet_metadata_sync_bridge.h"
#include "components/autofill/core/browser/webdata/autofill_wallet_sync_bridge.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "components/browser_sync/browser_sync_client.h"
#include "components/history/core/browser/sync/history_delete_directives_model_type_controller.h"
#include "components/history/core/browser/sync/typed_url_model_type_controller.h"
#include "components/password_manager/core/browser/password_store.h"
#include "components/password_manager/core/browser/sync/password_data_type_controller.h"
#include "components/password_manager/core/browser/sync/password_model_type_controller.h"
#include "components/prefs/pref_service.h"
#include "components/reading_list/features/reading_list_switches.h"
#include "components/send_tab_to_self/send_tab_to_self_model_type_controller.h"
#include "components/send_tab_to_self/send_tab_to_self_sync_service.h"
#include "components/sync/base/report_unrecoverable_error.h"
#include "components/sync/base/sync_base_switches.h"
#include "components/sync/driver/data_type_manager_impl.h"
#include "components/sync/driver/glue/sync_engine_impl.h"
#include "components/sync/driver/model_type_controller.h"
#include "components/sync/driver/sync_driver_switches.h"
#include "components/sync/driver/syncable_service_based_model_type_controller.h"
#include "components/sync/engine/sync_engine.h"
#include "components/sync/model/model_type_store_service.h"
#include "components/sync/model_impl/forwarding_model_type_controller_delegate.h"
#include "components/sync/model_impl/proxy_model_type_controller_delegate.h"
#include "components/sync_bookmarks/bookmark_change_processor.h"
#include "components/sync_bookmarks/bookmark_data_type_controller.h"
#include "components/sync_bookmarks/bookmark_model_associator.h"
#include "components/sync_bookmarks/bookmark_sync_service.h"
#include "components/sync_device_info/device_info_sync_service.h"
#include "components/sync_sessions/proxy_tabs_data_type_controller.h"
#include "components/sync_sessions/session_model_type_controller.h"
#include "components/sync_sessions/session_sync_service.h"
#include "components/sync_user_events/user_event_model_type_controller.h"

using base::FeatureList;
using bookmarks::BookmarkModel;
using sync_bookmarks::BookmarkChangeProcessor;
using sync_bookmarks::BookmarkDataTypeController;
using sync_bookmarks::BookmarkModelAssociator;
using syncer::DataTypeController;
using syncer::DataTypeManager;
using syncer::DataTypeManagerImpl;
using syncer::DataTypeManagerObserver;
using syncer::ModelTypeController;
using syncer::SyncableServiceBasedModelTypeController;

namespace browser_sync {

namespace {

// These helper functions only wrap the factory functions of the bridges. This
// way, it simplifies life for the compiler which cannot directly cast
// "WeakPtr<ModelTypeSyncBridge> (AutofillWebDataService*)" to
// "WeakPtr<ModelTypeControllerDelegate> (AutofillWebDataService*)".
base::WeakPtr<syncer::ModelTypeControllerDelegate>
AutocompleteDelegateFromDataService(autofill::AutofillWebDataService* service) {
  return autofill::AutocompleteSyncBridge::FromWebDataService(service)
      ->change_processor()
      ->GetControllerDelegate();
}

base::WeakPtr<syncer::ModelTypeControllerDelegate>
AutofillProfileDelegateFromDataService(
    autofill::AutofillWebDataService* service) {
  return autofill::AutofillProfileSyncBridge::FromWebDataService(service)
      ->change_processor()
      ->GetControllerDelegate();
}

base::WeakPtr<syncer::ModelTypeControllerDelegate>
AutofillWalletDelegateFromDataService(
    autofill::AutofillWebDataService* service) {
  return autofill::AutofillWalletSyncBridge::FromWebDataService(service)
      ->change_processor()
      ->GetControllerDelegate();
}

base::WeakPtr<syncer::ModelTypeControllerDelegate>
AutofillWalletMetadataDelegateFromDataService(
    autofill::AutofillWebDataService* service) {
  return autofill::AutofillWalletMetadataSyncBridge::FromWebDataService(service)
      ->change_processor()
      ->GetControllerDelegate();
}

}  // namespace

ProfileSyncComponentsFactoryImpl::ProfileSyncComponentsFactoryImpl(
    browser_sync::BrowserSyncClient* sync_client,
    version_info::Channel channel,
    const char* history_disabled_pref,
    const scoped_refptr<base::SequencedTaskRunner>& ui_thread,
    const scoped_refptr<base::SequencedTaskRunner>& db_thread,
    const scoped_refptr<autofill::AutofillWebDataService>&
        web_data_service_on_disk,
    const scoped_refptr<autofill::AutofillWebDataService>&
        web_data_service_in_memory,
    const scoped_refptr<password_manager::PasswordStore>&
        profile_password_store,
    const scoped_refptr<password_manager::PasswordStore>&
        account_password_store,
    sync_bookmarks::BookmarkSyncService* bookmark_sync_service)
    : sync_client_(sync_client),
      channel_(channel),
      history_disabled_pref_(history_disabled_pref),
      ui_thread_(ui_thread),
      db_thread_(db_thread),
      web_data_service_on_disk_(web_data_service_on_disk),
      web_data_service_in_memory_(web_data_service_in_memory),
      profile_password_store_(profile_password_store),
      account_password_store_(account_password_store),
      bookmark_sync_service_(bookmark_sync_service) {
  DCHECK(sync_client_);
}

ProfileSyncComponentsFactoryImpl::~ProfileSyncComponentsFactoryImpl() {}

syncer::DataTypeController::TypeVector
ProfileSyncComponentsFactoryImpl::CreateCommonDataTypeControllers(
    syncer::ModelTypeSet disabled_types,
    syncer::SyncService* sync_service) {
  syncer::DataTypeController::TypeVector controllers;

  const base::RepeatingClosure dump_stack =
      base::BindRepeating(&syncer::ReportUnrecoverableError, channel_);

  syncer::ModelTypeStoreService* model_type_store_service =
      sync_client_->GetModelTypeStoreService();
  DCHECK(model_type_store_service);
  syncer::RepeatingModelTypeStoreFactory model_type_store_factory =
      model_type_store_service->GetStoreFactory();

  controllers.push_back(std::make_unique<ModelTypeController>(
      syncer::DEVICE_INFO,
      std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
          sync_client_->GetDeviceInfoSyncService()
              ->GetControllerDelegate()
              .get())));

  // These features are enabled only if there's a DB thread to post tasks to.
  if (db_thread_) {
    // Autocomplete sync is enabled by default.  Register unless explicitly
    // disabled.
    if (!disabled_types.Has(syncer::AUTOFILL)) {
      controllers.push_back(std::make_unique<ModelTypeController>(
          syncer::AUTOFILL,
          std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
              db_thread_, base::BindRepeating(
                              &AutocompleteDelegateFromDataService,
                              base::RetainedRef(web_data_service_on_disk_)))));
    }

    // Autofill sync is enabled by default.  Register unless explicitly
    // disabled.
    if (!disabled_types.Has(syncer::AUTOFILL_PROFILE)) {
      controllers.push_back(
          std::make_unique<AutofillProfileModelTypeController>(
              std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
                  db_thread_,
                  base::BindRepeating(
                      &AutofillProfileDelegateFromDataService,
                      base::RetainedRef(web_data_service_on_disk_))),
              sync_client_->GetPrefService(), sync_service));
    }

    // Wallet data sync is enabled by default. Register unless explicitly
    // disabled.
    if (!disabled_types.Has(syncer::AUTOFILL_WALLET_DATA)) {
      controllers.push_back(CreateWalletModelTypeControllerWithInMemorySupport(
          syncer::AUTOFILL_WALLET_DATA,
          base::BindRepeating(&AutofillWalletDelegateFromDataService),
          sync_service));
    }

    // Wallet metadata sync depends on Wallet data sync. Register if neither
    // Wallet data nor Wallet metadata sync is explicitly disabled.
    if (!disabled_types.Has(syncer::AUTOFILL_WALLET_DATA) &&
        !disabled_types.Has(syncer::AUTOFILL_WALLET_METADATA)) {
      controllers.push_back(CreateWalletModelTypeController(
          syncer::AUTOFILL_WALLET_METADATA,
          base::BindRepeating(&AutofillWalletMetadataDelegateFromDataService),
          sync_service));
    }
  }

  // Bookmark sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!disabled_types.Has(syncer::BOOKMARKS)) {
    if (FeatureList::IsEnabled(switches::kSyncUSSBookmarks)) {
      controllers.push_back(std::make_unique<ModelTypeController>(
          syncer::BOOKMARKS,
          std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
              ui_thread_,
              base::BindRepeating(&sync_bookmarks::BookmarkSyncService::
                                      GetBookmarkSyncControllerDelegate,
                                  base::Unretained(bookmark_sync_service_),
                                  sync_client_->GetFaviconService()))));
    } else {
      controllers.push_back(std::make_unique<BookmarkDataTypeController>(
          dump_stack, sync_service, sync_client_->GetBookmarkModel(),
          sync_client_->GetHistoryService(), this));
    }
  }

  // These features are enabled only if history is not disabled.
  if (!sync_client_->GetPrefService()->GetBoolean(history_disabled_pref_)) {
    // TypedUrl sync is enabled by default.  Register unless explicitly
    // disabled.
    if (!disabled_types.Has(syncer::TYPED_URLS)) {
      // TypedURLModelTypeController uses a proxy delegate internally, as
      // provided by HistoryService.
      controllers.push_back(
          std::make_unique<history::TypedURLModelTypeController>(
              sync_client_->GetHistoryService(), sync_client_->GetPrefService(),
              history_disabled_pref_));
    }

    // Delete directive sync is enabled by default.
    if (!disabled_types.Has(syncer::HISTORY_DELETE_DIRECTIVES)) {
      controllers.push_back(
          std::make_unique<HistoryDeleteDirectivesModelTypeController>(
              dump_stack, sync_service,
              sync_client_->GetModelTypeStoreService(), sync_client_));
    }

    // Session sync is enabled by default.  This is disabled if history is
    // disabled because the tab sync data is added to the web history on the
    // server.
    if (!disabled_types.Has(syncer::PROXY_TABS)) {
      controllers.push_back(
          std::make_unique<sync_sessions::ProxyTabsDataTypeController>(
              base::BindRepeating(
                  &sync_sessions::SessionSyncService::ProxyTabsStateChanged,
                  base::Unretained(sync_client_->GetSessionSyncService()))));
      controllers.push_back(
          std::make_unique<sync_sessions::SessionModelTypeController>(
              sync_service, sync_client_->GetPrefService(),
              std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
                  sync_client_->GetSessionSyncService()
                      ->GetControllerDelegate()
                      .get()),
              history_disabled_pref_));
    }

    // If |kDoNotSyncFaviconDataTypes| feature is enabled, never register
    // controllers for favicon sync. Otherwise, it is enabled by default and we
    // should register unless explicitly disabled.
    if (!base::FeatureList::IsEnabled(switches::kDoNotSyncFaviconDataTypes) &&
        !disabled_types.Has(syncer::FAVICON_IMAGES) &&
        !disabled_types.Has(syncer::FAVICON_TRACKING)) {
      controllers.push_back(
          std::make_unique<SyncableServiceBasedModelTypeController>(
              syncer::FAVICON_IMAGES,
              sync_client_->GetModelTypeStoreService()->GetStoreFactory(),
              sync_client_->GetSyncableServiceForType(syncer::FAVICON_IMAGES),
              dump_stack));
      controllers.push_back(
          std::make_unique<SyncableServiceBasedModelTypeController>(
              syncer::FAVICON_TRACKING,
              sync_client_->GetModelTypeStoreService()->GetStoreFactory(),
              sync_client_->GetSyncableServiceForType(syncer::FAVICON_TRACKING),
              dump_stack));
    }
  }

  // Password sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!disabled_types.Has(syncer::PASSWORDS)) {
    if (base::FeatureList::IsEnabled(switches::kSyncUSSPasswords)) {
      if (profile_password_store_) {
        // |profile_password_store_| can be null in tests.
        controllers.push_back(
            std::make_unique<password_manager::PasswordModelTypeController>(
                profile_password_store_->CreateSyncControllerDelegate(),
                account_password_store_
                    ? account_password_store_->CreateSyncControllerDelegate()
                    : nullptr,
                sync_service, sync_client_->GetPasswordStateChangedCallback()));
      }
    } else {
      controllers.push_back(std::make_unique<PasswordDataTypeController>(
          dump_stack, sync_service, sync_client_,
          sync_client_->GetPasswordStateChangedCallback(),
          profile_password_store_));
    }
  }

  if (!disabled_types.Has(syncer::PREFERENCES)) {
    controllers.push_back(
        std::make_unique<SyncableServiceBasedModelTypeController>(
            syncer::PREFERENCES,
            sync_client_->GetModelTypeStoreService()->GetStoreFactory(),
            sync_client_->GetSyncableServiceForType(syncer::PREFERENCES),
            dump_stack));
  }

  if (!disabled_types.Has(syncer::PRIORITY_PREFERENCES)) {
    controllers.push_back(
        std::make_unique<SyncableServiceBasedModelTypeController>(
            syncer::PRIORITY_PREFERENCES,
            sync_client_->GetModelTypeStoreService()->GetStoreFactory(),
            sync_client_->GetSyncableServiceForType(
                syncer::PRIORITY_PREFERENCES),
            dump_stack));
  }

#if defined(OS_CHROMEOS)
  if (!disabled_types.Has(syncer::PRINTERS)) {
    // TODO(crbug.com/867801) This still uses a proxy delegate even though the
    // model lives on the UI thread. Switching to forwarding delegate causes
    // cryptic test failures on chromeos. Fix that and switch to the forwarding
    // delegate.
    controllers.push_back(std::make_unique<ModelTypeController>(
        syncer::PRINTERS,
        std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
            ui_thread_,
            base::BindRepeating(&browser_sync::BrowserSyncClient::
                                    GetControllerDelegateForModelType,
                                base::Unretained(sync_client_),
                                syncer::PRINTERS))));
  }
  if (!disabled_types.Has(syncer::WIFI_CONFIGURATIONS) &&
      base::FeatureList::IsEnabled(switches::kSyncWifiConfigurations)) {
    controllers.push_back(std::make_unique<ModelTypeController>(
        syncer::WIFI_CONFIGURATIONS,
        std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
            sync_client_
                ->GetControllerDelegateForModelType(syncer::WIFI_CONFIGURATIONS)
                .get())));
  }
#endif

  // Reading list sync is enabled by default only on iOS. Register unless
  // Reading List or Reading List Sync is explicitly disabled.
  if (!disabled_types.Has(syncer::READING_LIST) &&
      reading_list::switches::IsReadingListEnabled()) {
    controllers.push_back(CreateModelTypeControllerForModelRunningOnUIThread(
        syncer::READING_LIST));
  }

  if (!disabled_types.Has(syncer::USER_EVENTS)) {
    controllers.push_back(
        std::make_unique<syncer::UserEventModelTypeController>(
            sync_service,
            CreateForwardingControllerDelegate(syncer::USER_EVENTS)));
  }

  if (!disabled_types.Has(syncer::SEND_TAB_TO_SELF) &&
      base::FeatureList::IsEnabled(switches::kSyncSendTabToSelf)) {
    controllers.push_back(
        std::make_unique<send_tab_to_self::SendTabToSelfModelTypeController>(
            sync_service,
            std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
                sync_client_->GetSendTabToSelfSyncService()
                    ->GetControllerDelegate()
                    .get())));
  }

  // Forward both on-disk and in-memory storage modes to the same delegate,
  // since behavior for USER_CONSENTS does not differ (they are always
  // persisted).
  controllers.push_back(std::make_unique<ModelTypeController>(
      syncer::USER_CONSENTS,
      /*delegate_on_disk=*/
      CreateForwardingControllerDelegate(syncer::USER_CONSENTS),
      /*delegate_in_memory=*/
      CreateForwardingControllerDelegate(syncer::USER_CONSENTS)));

  return controllers;
}

std::unique_ptr<DataTypeManager>
ProfileSyncComponentsFactoryImpl::CreateDataTypeManager(
    syncer::ModelTypeSet initial_types,
    const syncer::WeakHandle<syncer::DataTypeDebugInfoListener>&
        debug_info_listener,
    const DataTypeController::TypeMap* controllers,
    const syncer::DataTypeEncryptionHandler* encryption_handler,
    syncer::ModelTypeConfigurer* configurer,
    DataTypeManagerObserver* observer) {
  return std::make_unique<DataTypeManagerImpl>(
      initial_types, debug_info_listener, controllers, encryption_handler,
      configurer, observer);
}

std::unique_ptr<syncer::SyncEngine>
ProfileSyncComponentsFactoryImpl::CreateSyncEngine(
    const std::string& name,
    invalidation::InvalidationService* invalidator,
    const base::WeakPtr<syncer::SyncPrefs>& sync_prefs) {
  return std::make_unique<syncer::SyncEngineImpl>(
      name, invalidator, sync_prefs,
      sync_client_->GetModelTypeStoreService()->GetSyncDataPath());
}

syncer::SyncApiComponentFactory::SyncComponents
ProfileSyncComponentsFactoryImpl::CreateBookmarkSyncComponents(
    std::unique_ptr<syncer::DataTypeErrorHandler> error_handler,
    syncer::UserShare* user_share) {
  BookmarkModel* bookmark_model = sync_client_->GetBookmarkModel();
// TODO(akalin): We may want to propagate this switch up eventually.
#if defined(OS_ANDROID) || defined(OS_IOS)
  const bool kExpectMobileBookmarksFolder = true;
#else
  const bool kExpectMobileBookmarksFolder = false;
#endif

  auto model_associator = std::make_unique<BookmarkModelAssociator>(
      bookmark_model, sync_client_->GetBookmarkUndoService(),
      sync_client_->GetFaviconService(), user_share, error_handler->Copy(),
      kExpectMobileBookmarksFolder);

  SyncComponents components;
  components.change_processor = std::make_unique<BookmarkChangeProcessor>(
      model_associator.get(), std::move(error_handler));
  components.model_associator = std::move(model_associator);
  return components;
}

std::unique_ptr<syncer::ModelTypeControllerDelegate>
ProfileSyncComponentsFactoryImpl::CreateForwardingControllerDelegate(
    syncer::ModelType type) {
  return std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
      sync_client_->GetControllerDelegateForModelType(type).get());
}

std::unique_ptr<ModelTypeController> ProfileSyncComponentsFactoryImpl::
    CreateModelTypeControllerForModelRunningOnUIThread(syncer::ModelType type) {
  return std::make_unique<ModelTypeController>(
      type, CreateForwardingControllerDelegate(type));
}

std::unique_ptr<ModelTypeController>
ProfileSyncComponentsFactoryImpl::CreateWalletModelTypeController(
    syncer::ModelType type,
    const base::RepeatingCallback<
        base::WeakPtr<syncer::ModelTypeControllerDelegate>(
            autofill::AutofillWebDataService*)>& delegate_from_web_data,
    syncer::SyncService* sync_service) {
  return std::make_unique<AutofillWalletModelTypeController>(
      type,
      std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
          db_thread_,
          base::BindRepeating(delegate_from_web_data,
                              base::RetainedRef(web_data_service_on_disk_))),
      sync_client_->GetPrefService(), sync_service);
}

std::unique_ptr<ModelTypeController> ProfileSyncComponentsFactoryImpl::
    CreateWalletModelTypeControllerWithInMemorySupport(
        syncer::ModelType type,
        const base::RepeatingCallback<
            base::WeakPtr<syncer::ModelTypeControllerDelegate>(
                autofill::AutofillWebDataService*)>& delegate_from_web_data,
        syncer::SyncService* sync_service) {
  return std::make_unique<AutofillWalletModelTypeController>(
      type, /*delegate_on_disk=*/
      std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
          db_thread_,
          base::BindRepeating(delegate_from_web_data,
                              base::RetainedRef(web_data_service_on_disk_))),
      /*delegate_in_memory=*/
      std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
          db_thread_,
          base::BindRepeating(delegate_from_web_data,
                              base::RetainedRef(web_data_service_in_memory_))),
      sync_client_->GetPrefService(), sync_service);
}

}  // namespace browser_sync
