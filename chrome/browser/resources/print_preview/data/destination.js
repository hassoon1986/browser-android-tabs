// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('print_preview', function() {
  'use strict';

  /**
   * Enumeration of the types of destinations.
   * @enum {string}
   */
  const DestinationType = {
    GOOGLE: 'google',
    GOOGLE_PROMOTED: 'google_promoted',
    LOCAL: 'local',
    MOBILE: 'mobile',
  };

  /**
   * Enumeration of the origin types for cloud destinations.
   * @enum {string}
   */
  const DestinationOrigin = {
    LOCAL: 'local',
    COOKIES: 'cookies',
    // <if expr="chromeos">
    DEVICE: 'device',
    // </if>
    PRIVET: 'privet',
    EXTENSION: 'extension',
    CROS: 'chrome_os',
  };

  /**
   * Cloud Print origins.
   * @const {!Array<!print_preview.DestinationOrigin>}
   */
  const CloudOrigins = [
    DestinationOrigin.COOKIES,
    // <if expr="chromeos">
    DestinationOrigin.DEVICE,
    // </if>
  ];

  /**
   * Enumeration of the connection statuses of printer destinations.
   * @enum {string}
   */
  const DestinationConnectionStatus = {
    DORMANT: 'DORMANT',
    OFFLINE: 'OFFLINE',
    ONLINE: 'ONLINE',
    UNKNOWN: 'UNKNOWN',
    UNREGISTERED: 'UNREGISTERED',
  };

  /**
   * Enumeration specifying whether a destination is provisional and the reason
   * the destination is provisional.
   * @enum {string}
   */
  const DestinationProvisionalType = {
    // Destination is not provisional.
    NONE: 'NONE',
    // User has to grant USB access for the destination to its provider.
    // Used for destinations with extension origin.
    NEEDS_USB_PERMISSION: 'NEEDS_USB_PERMISSION',
  };

  /**
   * Enumeration specifying the status of a destination's 2018 certificate.
   * Values UNKNOWN and YES are returned directly by the GCP server.
   * @enum {string}
   */
  const DestinationCertificateStatus = {
    // Destination is not a cloud printer or no status was retrieved.
    NONE: 'NONE',
    // Printer does not have a valid 2018 certificate. Currently unused, to be
    // sent by GCP server.
    NO: 'NO',
    // Printer may or may not have a valid certificate. Sent by GCP server.
    UNKNOWN: 'UNKNOWN',
    // Printer has a valid 2018 certificate. Sent by GCP server.
    YES: 'YES',
  };

  /**
   * @typedef {{
   *   display_name: (string),
   *   type: (string | undefined),
   *   value: (number | string | boolean),
   *   is_default: (boolean | undefined),
   * }}
   */
  let VendorCapabilitySelectOption;

  /**
   * Specifies a custom vendor capability.
   * @typedef {{
   *   id: (string),
   *   display_name: (string),
   *   localized_display_name: (string | undefined),
   *   type: (string),
   *   select_cap: ({
   *     option: (Array<!print_preview.VendorCapabilitySelectOption>|undefined),
   *   }|undefined),
   *   typed_value_cap: ({
   *     default: (number | string | boolean | undefined),
   *   }|undefined),
   *   range_cap: ({
   *     default: (number),
   *   }),
   * }}
   */
  let VendorCapability;

  /**
   * Capabilities of a print destination represented in a CDD.
   * Pin capability is not a part of standard CDD description and is defined
   * only on Chrome OS.
   *
   * @typedef {{
   *   vendor_capability: !Array<!print_preview.VendorCapability>,
   *   collate: ({default: (boolean|undefined)}|undefined),
   *   color: ({
   *     option: !Array<{
   *       type: (string|undefined),
   *       vendor_id: (string|undefined),
   *       custom_display_name: (string|undefined),
   *       is_default: (boolean|undefined)
   *     }>
   *   }|undefined),
   *   copies: ({default: (number|undefined),
   *             max: (number|undefined)}|undefined),
   *   duplex: ({option: !Array<{type: (string|undefined),
   *                             is_default: (boolean|undefined)}>}|undefined),
   *   page_orientation: ({
   *     option: !Array<{type: (string|undefined),
   *                      is_default: (boolean|undefined)}>
   *   }|undefined),
   *   media_size: ({
   *     option: !Array<{
   *       type: (string|undefined),
   *       vendor_id: (string|undefined),
   *       custom_display_name: (string|undefined),
   *       is_default: (boolean|undefined)
   *     }>
   *   }|undefined),
   *   dpi: ({
   *     option: !Array<{
   *       vendor_id: (string|undefined),
   *       horizontal_dpi: number,
   *       vertical_dpi: number,
   *       is_default: (boolean|undefined)
   *     }>
   *   }|undefined),
   *   pin: ({supported: (boolean|undefined)}|undefined)
   * }}
   */
  let CddCapabilities;

  /**
   * The CDD (Cloud Device Description) describes the capabilities of a print
   * destination.
   *
   * @typedef {{
   *   version: string,
   *   printer: !print_preview.CddCapabilities,
   * }}
   */
  let Cdd;

  /**
   * Enumeration of color modes used by Chromium.
   * @enum {number}
   */
  const ColorMode = {
    GRAY: 1,
    COLOR: 2,
  };

  // <if expr="chromeos">
  /**
   * Enumeration of color mode restrictions used by Chromium.
   * This has to coincide with |printing::ColorModeRestriction| as defined in
   * printing/backend/printing_restrictions.h
   * @enum {number}
   */
  const ColorModeRestriction = {
    UNSET: 0x0,
    MONOCHROME: 0x1,
    COLOR: 0x2,
  };

  /**
   * Enumeration of duplex mode restrictions used by Chromium.
   * This has to coincide with |printing::DuplexModeRestriction| as defined in
   * printing/backend/printing_restrictions.h
   * @enum {number}
   */
  const DuplexModeRestriction = {
    UNSET: 0x0,
    SIMPLEX: 0x1,
    LONG_EDGE: 0x2,
    SHORT_EDGE: 0x4,
    DUPLEX: 0x6,
  };

  /**
   * Enumeration of PIN printing mode restrictions used by Chromium.
   * This has to coincide with |printing::PinModeRestriction| as defined in
   * printing/backend/printing_restrictions.h
   * @enum {number}
   */
  const PinModeRestriction = {
    UNSET: 0,
    PIN: 1,
    NO_PIN: 2,
  };

  /**
   * Policies affecting a destination.
   * @typedef {{
   *   allowedColorModes: ?print_preview.ColorModeRestriction,
   *   allowedDuplexModes: ?print_preview.DuplexModeRestriction,
   *   allowedPinMode: ?print_preview.PinModeRestriction,
   *   defaultColorMode: ?print_preview.ColorModeRestriction,
   *   defaultDuplexMode: ?print_preview.DuplexModeRestriction,
   *   defaultPinMode: ?print_preview.PinModeRestriction,
   * }}
   */
  let Policies;
  // </if>

  /**
   * @typedef {{id: string,
   *            origin: print_preview.DestinationOrigin,
   *            account: string,
   *            capabilities: ?print_preview.Cdd,
   *            displayName: string,
   *            extensionId: string,
   *            extensionName: string}}
   */
  let RecentDestination;

  /**
   * Creates a |RecentDestination| to represent |destination| in the app
   * state.
   * @param {!print_preview.Destination} destination The destination to store.
   * @return {!print_preview.RecentDestination}
   */
  function makeRecentDestination(destination) {
    return {
      id: destination.id,
      origin: destination.origin,
      account: destination.account || '',
      capabilities: destination.capabilities,
      displayName: destination.displayName || '',
      extensionId: destination.extensionId || '',
      extensionName: destination.extensionName || '',
    };
  }

  /**
   * @param {string} id Destination id.
   * @param {!print_preview.DestinationOrigin} origin Destination origin.
   * @param {string} account User account destination is registered for.
   * @return {string} A key that maps to a destination with the selected |id|,
   *     |origin|, and |account|.
   */
  function createDestinationKey(id, origin, account) {
    return `${id}/${origin}/${account}`;
  }

  /**
   * @param {!print_preview.RecentDestination} recentDestination
   * @return {string} A key that maps to a destination with parameters matching
   *     |recentDestination|.
   */
  function createRecentDestinationKey(recentDestination) {
    return print_preview.createDestinationKey(
        recentDestination.id, recentDestination.origin,
        recentDestination.account);
  }

  class Destination {
    /**
     * Print destination data object that holds data for both local and cloud
     * destinations.
     * @param {string} id ID of the destination.
     * @param {!print_preview.DestinationType} type Type of the destination.
     * @param {!print_preview.DestinationOrigin} origin Origin of the
     *     destination.
     * @param {string} displayName Display name of the destination.
     * @param {!print_preview.DestinationConnectionStatus} connectionStatus
     *     Connection status of the print destination.
     * @param {{tags: (Array<string>|undefined),
     *          isOwned: (boolean|undefined),
     *          isEnterprisePrinter: (boolean|undefined),
     *          account: (string|undefined),
     *          lastAccessTime: (number|undefined),
     *          cloudID: (string|undefined),
     *          provisionalType:
     *              (print_preview.DestinationProvisionalType|undefined),
     *          extensionId: (string|undefined),
     *          extensionName: (string|undefined),
     *          description: (string|undefined),
     *          certificateStatus:
     *              (print_preview.DestinationCertificateStatus|undefined),
     *          policies: (print_preview.Policies|undefined),
     *         }=} opt_params Optional
     *     parameters for the destination.
     */
    constructor(id, type, origin, displayName, connectionStatus, opt_params) {
      /**
       * ID of the destination.
       * @private {string}
       */
      this.id_ = id;

      /**
       * Type of the destination.
       * @private {!print_preview.DestinationType}
       */
      this.type_ = type;

      /**
       * Origin of the destination.
       * @private {!print_preview.DestinationOrigin}
       */
      this.origin_ = origin;

      /**
       * Display name of the destination.
       * @private {string}
       */
      this.displayName_ = displayName || '';

      /**
       * Tags associated with the destination.
       * @private {!Array<string>}
       */
      this.tags_ = (opt_params && opt_params.tags) || [];

      /**
       * Print capabilities of the destination.
       * @private {?print_preview.Cdd}
       */
      this.capabilities_ = null;

      /**
       * Policies affecting the destination.
       * @private {?print_preview.Policies}
       */
      this.policies_ = (opt_params && opt_params.policies) || null;

      /**
       * Whether the destination is owned by the user.
       * @private {boolean}
       */
      this.isOwned_ = (opt_params && opt_params.isOwned) || false;

      /**
       * Whether the destination is an enterprise policy controlled printer.
       * @private {boolean}
       */
      this.isEnterprisePrinter_ =
          (opt_params && opt_params.isEnterprisePrinter) || false;

      /**
       * Account this destination is registered for, if known.
       * @private {string}
       */
      this.account_ = (opt_params && opt_params.account) || '';

      /**
       * Cache of destination location fetched from tags.
       * @private {?string}
       */
      this.location_ = null;

      /**
       * Printer description.
       * @private {string}
       */
      this.description_ = (opt_params && opt_params.description) || '';

      /**
       * Connection status of the destination.
       * @private {!print_preview.DestinationConnectionStatus}
       */
      this.connectionStatus_ = connectionStatus;

      /**
       * Number of milliseconds since the epoch when the printer was last
       * accessed.
       * @private {number}
       */
      this.lastAccessTime_ =
          (opt_params && opt_params.lastAccessTime) || Date.now();

      /**
       * Cloud ID for Privet printers.
       * @private {string}
       */
      this.cloudID_ = (opt_params && opt_params.cloudID) || '';

      /**
       * Extension ID for extension managed printers.
       * @private {string}
       */
      this.extensionId_ = (opt_params && opt_params.extensionId) || '';

      /**
       * Extension name for extension managed printers.
       * @private {string}
       */
      this.extensionName_ = (opt_params && opt_params.extensionName) || '';

      /**
       * Different from {@code print_preview.DestinationProvisionalType.NONE} if
       * the destination is provisional. Provisional destinations cannot be
       * selected as they are, but have to be resolved first (i.e. extra steps
       * have to be taken to get actual destination properties, which should
       * replace the provisional ones). Provisional destination resolvment flow
       * will be started when the user attempts to select the destination in
       * search UI.
       * @private {print_preview.DestinationProvisionalType}
       */
      this.provisionalType_ = (opt_params && opt_params.provisionalType) ||
          DestinationProvisionalType.NONE;

      /**
       * Printer 2018 certificate status
       * @private {print_preview.DestinationCertificateStatus}
       */
      this.certificateStatus_ = opt_params && opt_params.certificateStatus ||
          print_preview.DestinationCertificateStatus.NONE;

      assert(
          this.provisionalType_ !=
                  DestinationProvisionalType.NEEDS_USB_PERMISSION ||
              this.isExtension,
          'Provisional USB destination only supprted with extension origin.');

      /**
       * @private {!Array<string>} List of capability types considered color.
       * @const
       */
      this.COLOR_TYPES_ = ['STANDARD_COLOR', 'CUSTOM_COLOR'];

      /**
       * @private {!Array<string>} List of capability types considered
       *     monochrome.
       * @const
       */
      this.MONOCHROME_TYPES_ = ['STANDARD_MONOCHROME', 'CUSTOM_MONOCHROME'];
    }

    /** @return {string} ID of the destination. */
    get id() {
      return this.id_;
    }

    /** @return {!print_preview.DestinationType} Type of the destination. */
    get type() {
      return this.type_;
    }

    /**
     * @return {!print_preview.DestinationOrigin} Origin of the destination.
     */
    get origin() {
      return this.origin_;
    }

    /** @return {string} Display name of the destination. */
    get displayName() {
      return this.displayName_;
    }

    /**
     * @return {boolean} Whether the user owns the destination. Only applies to
     *     cloud-based destinations.
     */
    get isOwned() {
      return this.isOwned_;
    }

    /**
     * @return {string} Account this destination is registered for, if known.
     */
    get account() {
      return this.account_;
    }

    /** @return {boolean} Whether the destination is local or cloud-based. */
    get isLocal() {
      return this.origin_ == DestinationOrigin.LOCAL ||
          this.origin_ == DestinationOrigin.EXTENSION ||
          this.origin_ == DestinationOrigin.CROS ||
          (this.origin_ == DestinationOrigin.PRIVET &&
           this.connectionStatus_ != DestinationConnectionStatus.UNREGISTERED);
    }

    /** @return {boolean} Whether the destination is a Privet local printer */
    get isPrivet() {
      return this.origin_ == DestinationOrigin.PRIVET;
    }

    /**
     * @return {boolean} Whether the destination is an extension managed
     *     printer.
     */
    get isExtension() {
      return this.origin_ == DestinationOrigin.EXTENSION;
    }

    /**
     * @return {string} The location of the destination, or an empty string if
     *     the location is unknown.
     */
    get location() {
      if (this.location_ == null) {
        this.location_ = '';
        this.tags_.some(tag => {
          return Destination.LOCATION_TAG_PREFIXES.some(prefix => {
            if (tag.startsWith(prefix)) {
              this.location_ = tag.substring(prefix.length) || '';
              return true;
            }
          });
        });
      }
      return this.location_;
    }

    /**
     * @return {string} The description of the destination, or an empty string,
     *     if it was not provided.
     */
    get description() {
      return this.description_;
    }

    /**
     * @return {string} Most relevant string to help user to identify this
     *     destination.
     */
    get hint() {
      if (this.id_ == Destination.GooglePromotedId.DOCS) {
        return this.account_;
      }
      return this.location || this.extensionName || this.description;
    }

    /** @return {!Array<string>} Tags associated with the destination. */
    get tags() {
      return this.tags_.slice(0);
    }

    /** @return {string} Cloud ID associated with the destination */
    get cloudID() {
      return this.cloudID_;
    }

    /**
     * @return {string} Extension ID associated with the destination. Non-empty
     *     only for extension managed printers.
     */
    get extensionId() {
      return this.extensionId_;
    }

    /**
     * @return {string} Extension name associated with the destination.
     *     Non-empty only for extension managed printers.
     */
    get extensionName() {
      return this.extensionName_;
    }

    /** @return {?print_preview.Cdd} Print capabilities of the destination. */
    get capabilities() {
      return this.capabilities_;
    }

    /**
     * @param {?print_preview.Cdd} capabilities Print capabilities of the
     *     destination.
     */
    set capabilities(capabilities) {
      if (capabilities) {
        this.capabilities_ = capabilities;
      }
    }

    // <if expr="chromeos">
    /**
     * @return {?print_preview.Policies} Print policies affecting the
     *     destination.
     */
    get policies() {
      return this.policies_;
    }

    /**
     * @param {?print_preview.Policies} policies Print policies affecting the
     *     destination.
     */
    set policies(policies) {
      this.policies_ = policies;
    }
    // </if>

    /**
     * @return {!print_preview.DestinationConnectionStatus} Connection status
     *     of the print destination.
     */
    get connectionStatus() {
      return this.connectionStatus_;
    }

    /**
     * @param {!print_preview.DestinationConnectionStatus} status Connection
     *     status of the print destination.
     */
    set connectionStatus(status) {
      this.connectionStatus_ = status;
    }

    /**
     * @return {boolean} Whether the destination has an invalid 2018
     *     certificate.
     */
    get hasInvalidCertificate() {
      return this.certificateStatus_ ==
          print_preview.DestinationCertificateStatus.NO;
    }

    /**
     * @return {boolean} Whether the destination should display an invalid
     *     certificate UI warning in the selection dialog and cause a UI
     *     warning to appear in the preview area when selected.
     */
    get shouldShowInvalidCertificateError() {
      return this.certificateStatus_ ==
          print_preview.DestinationCertificateStatus.NO &&
          !loadTimeData.getBoolean('isEnterpriseManaged');
    }

    /** @return {boolean} Whether the destination is considered offline. */
    get isOffline() {
      return [
        print_preview.DestinationConnectionStatus.OFFLINE,
        print_preview.DestinationConnectionStatus.DORMANT
      ].includes(this.connectionStatus_);
    }

    /**
     * @return {boolean} Whether the destination is offline or has an invalid
     *     certificate.
     */
    get isOfflineOrInvalid() {
      return this.isOffline || this.shouldShowInvalidCertificateError;
    }

    /** @return {boolean} Whether the destination is ready to be selected. */
    get readyForSelection() {
      return (!cr.isChromeOS || this.origin_ != DestinationOrigin.CROS ||
              this.capabilities_ != null) &&
          !this.isProvisional;
    }

    /**
     * @return {string} Human readable status for a destination that is offline
     *     or has a bad certificate.
     */
    get connectionStatusText() {
      if (!this.isOfflineOrInvalid) {
        return '';
      }
      const offlineDurationMs = Date.now() - this.lastAccessTime_;
      let statusMessageId;
      if (this.shouldShowInvalidCertificateError) {
        statusMessageId = 'noLongerSupported';
      } else if (offlineDurationMs > 31622400000.0) {  // One year.
        statusMessageId = 'offlineForYear';
      } else if (offlineDurationMs > 2678400000.0) {  // One month.
        statusMessageId = 'offlineForMonth';
      } else if (offlineDurationMs > 604800000.0) {  // One week.
        statusMessageId = 'offlineForWeek';
      } else {
        statusMessageId = 'offline';
      }
      return loadTimeData.getString(statusMessageId);
    }

    /**
     * @return {number} Number of milliseconds since the epoch when the printer
     *     was last accessed.
     */
    get lastAccessTime() {
      return this.lastAccessTime_;
    }

    /** @return {string} Path to the SVG for the destination's icon. */
    get icon() {
      if (this.id_ == Destination.GooglePromotedId.DOCS) {
        return 'print-preview:save-to-drive';
      }
      if (this.id_ == Destination.GooglePromotedId.SAVE_AS_PDF) {
        return 'cr:insert-drive-file';
      }
      if (this.isEnterprisePrinter) {
        return 'print-preview:business';
      }
      if (this.isLocal) {
        return 'print-preview:print';
      }
      if (this.type_ == print_preview.DestinationType.MOBILE && this.isOwned_) {
        return 'print-preview:smartphone';
      }
      if (this.type_ == print_preview.DestinationType.MOBILE) {
        return 'print-preview:smartphone';
      }
      if (this.isOwned_) {
        return 'print-preview:print';
      }
      return 'print-preview:printer-shared';
    }

    /**
     * @return {!Array<string>} Properties (besides display name) to match
     *     search queries against.
     */
    get extraPropertiesToMatch() {
      return [this.location, this.description];
    }

    /**
     * Matches a query against the destination.
     * @param {!RegExp} query Query to match against the destination.
     * @return {boolean} {@code true} if the query matches this destination,
     *     {@code false} otherwise.
     */
    matches(query) {
      return !!this.displayName_.match(query) ||
          !!this.extensionName_.match(query) ||
          this.extraPropertiesToMatch.some(p => p.match(query));
    }

    /**
     * Gets the destination's provisional type.
     * @return {print_preview.DestinationProvisionalType}
     */
    get provisionalType() {
      return this.provisionalType_;
    }

    /**
     * Gets the destination's certificate status.
     * @return {print_preview.DestinationCertificateStatus}
     */
    get certificateStatus() {
      return this.certificateStatus_;
    }

    /**
     * Whether the destinaion is provisional.
     * @return {boolean}
     */
    get isProvisional() {
      return this.provisionalType_ != DestinationProvisionalType.NONE;
    }

    /**
     * Whether the printer is enterprise policy controlled printer.
     * @return {boolean}
     */
    get isEnterprisePrinter() {
      return this.isEnterprisePrinter_;
    }

    /**
     * @return {Object} Color capability of this destination.
     * @private
     */
    colorCapability_() {
      return this.capabilities && this.capabilities.printer &&
              this.capabilities.printer.color ?
          this.capabilities.printer.color :
          null;
    }

    // <if expr="chromeos">
    /**
     * @return {?print_preview.ColorModeRestriction} Color mode set by policy.
     */
    get colorPolicy() {
      return this.policies && this.policies.allowedColorModes ?
          this.policies.allowedColorModes :
          null;
    }

    /**
     * @return {?print_preview.DuplexModeRestriction} Duplex modes allowed by
     *     policy.
     */
    get duplexPolicy() {
      return this.policies && this.policies.allowedDuplexModes ?
          this.policies.allowedDuplexModes :
          null;
    }

    /**
     * @return {?print_preview.PinModeRestriction} Pin mode allowed by policy.
     */
    get pinPolicy() {
      return this.policies && this.policies.allowedPinModes ?
          this.policies.allowedPinModes :
          null;
    }
    // </if>

    /**
     * @return {boolean} Whether the printer supports both black and white and
     *     color printing.
     */
    get hasColorCapability() {
      const capability = this.colorCapability_();
      if (!capability || !capability.option) {
        return false;
      }
      let hasColor = false;
      let hasMonochrome = false;
      capability.option.forEach(option => {
        const type = assert(option.type);
        hasColor = hasColor || this.COLOR_TYPES_.includes(option.type);
        hasMonochrome =
            hasMonochrome || this.MONOCHROME_TYPES_.includes(option.type);
      });
      return hasColor && hasMonochrome;
    }

    // <if expr="chromeos">
    /**
     * @return {?print_preview.ColorModeRestriction} Value of default color
     *     setting given by policy.
     */
    get defaultColorPolicy() {
      return this.policies && this.policies.defaultColorMode;
    }

    /**
     * @return {?print_preview.DuplexModeRestriction} Value of default duplex
     *     setting given by policy.
     */
    get defaultDuplexPolicy() {
      return this.policies && this.policies.defaultDuplexMode;
    }

    /**
     * @return {?print_preview.PinModeRestriction} Value of default pin setting
     *     given by policy.
     */
    get defaultPinPolicy() {
      return this.policies && this.policies.defaultPinMode;
    }
    // </if>

    /**
     * @param {boolean} isColor Whether to use a color printing mode.
     * @return {Object} Selected color option.
     */
    getSelectedColorOption(isColor) {
      const typesToLookFor =
          isColor ? this.COLOR_TYPES_ : this.MONOCHROME_TYPES_;
      const capability = this.colorCapability_();
      if (!capability || !capability.option) {
        return null;
      }
      for (let i = 0; i < typesToLookFor.length; i++) {
        const matchingOptions = capability.option.filter(option => {
          return option.type == typesToLookFor[i];
        });
        if (matchingOptions.length > 0) {
          return matchingOptions[0];
        }
      }
      return null;
    }

    /**
     * @param {boolean} isColor Whether to use a color printing mode.
     * @return {number} Native color model of the destination.
     */
    getNativeColorModel(isColor) {
      // For non-local printers or printers without capability, native color
      // model is ignored.
      const capability = this.colorCapability_();
      if (!capability || !capability.option || !this.isLocal) {
        return isColor ? print_preview.ColorMode.COLOR :
                         print_preview.ColorMode.GRAY;
      }
      const selected = this.getSelectedColorOption(isColor);
      const mode = parseInt(selected ? selected.vendor_id : null, 10);
      if (isNaN(mode)) {
        return isColor ? print_preview.ColorMode.COLOR :
                         print_preview.ColorMode.GRAY;
      }
      return mode;
    }

    /**
     * @return {Object} The default color option for the destination.
     */
    get defaultColorOption() {
      const capability = this.colorCapability_();
      if (!capability || !capability.option) {
        return null;
      }
      const defaultOptions = capability.option.filter(option => {
        return option.is_default;
      });
      return defaultOptions.length != 0 ? defaultOptions[0] : null;
    }

    /** @return {string} A unique identifier for this destination. */
    get key() {
      return `${this.id_}/${this.origin_}/${this.account_}`;
    }
  }

  /**
   * Prefix of the location destination tag.
   * @type {!Array<string>}
   * @const
   */
  Destination.LOCATION_TAG_PREFIXES =
      ['__cp__location=', '__cp__printer-location='];

  /**
   * Enumeration of Google-promoted destination IDs.
   * @enum {string}
   */
  Destination.GooglePromotedId = {
    DOCS: '__google__docs',
    SAVE_AS_PDF: 'Save as PDF'
  };

  // Export
  return {
    CddCapabilities: CddCapabilities,
    Cdd: Cdd,
    CloudOrigins: CloudOrigins,
    ColorMode: ColorMode,
    createDestinationKey: createDestinationKey,
    createRecentDestinationKey: createRecentDestinationKey,
    DestinationCertificateStatus: DestinationCertificateStatus,
    DestinationConnectionStatus: DestinationConnectionStatus,
    Destination: Destination,
    DestinationOrigin: DestinationOrigin,
    DestinationProvisionalType: DestinationProvisionalType,
    DestinationType: DestinationType,
    makeRecentDestination: makeRecentDestination,
    RecentDestination: RecentDestination,
    VendorCapabilitySelectOption: VendorCapabilitySelectOption,
    VendorCapability: VendorCapability,

    // <if expr="chromeos">
    ColorModeRestriction: ColorModeRestriction,
    DuplexModeRestriction: DuplexModeRestriction,
    PinModeRestriction: PinModeRestriction,
    Policies: Policies,
    // </if>
  };
});
