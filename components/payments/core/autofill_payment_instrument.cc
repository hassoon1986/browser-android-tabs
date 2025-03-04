// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/payments/core/autofill_payment_instrument.h"

#include <algorithm>
#include <memory>

#include "base/bind.h"
#include "base/json/json_writer.h"
#include "base/metrics/histogram_functions.h"
#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "components/autofill/core/browser/autofill_data_util.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/geo/autofill_country.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/payments/core/autofill_card_validation.h"
#include "components/payments/core/basic_card_response.h"
#include "components/payments/core/features.h"
#include "components/payments/core/payment_request_base_delegate.h"
#include "components/payments/core/payment_request_data_util.h"
#include "components/payments/core/payments_experimental_features.h"

namespace payments {

AutofillPaymentInstrument::AutofillPaymentInstrument(
    const std::string& method_name,
    const autofill::CreditCard& card,
    bool matches_merchant_card_type_exactly,
    const std::vector<autofill::AutofillProfile*>& billing_profiles,
    const std::string& app_locale,
    PaymentRequestBaseDelegate* payment_request_delegate)
    : PaymentInstrument(
          autofill::data_util::GetPaymentRequestData(card.network())
              .icon_resource_id,
          PaymentInstrument::Type::AUTOFILL),
      method_name_(method_name),
      credit_card_(card),
      matches_merchant_card_type_exactly_(matches_merchant_card_type_exactly),
      billing_profiles_(billing_profiles),
      app_locale_(app_locale),
      delegate_(nullptr),
      payment_request_delegate_(payment_request_delegate) {}
AutofillPaymentInstrument::~AutofillPaymentInstrument() {}

void AutofillPaymentInstrument::InvokePaymentApp(
    PaymentInstrument::Delegate* delegate) {
  DCHECK(delegate);
  // There can be only one FullCardRequest going on at a time. If |delegate_| is
  // not null, there's already an active request, which shouldn't happen.
  // |delegate_| is reset to nullptr when the request succeeds or fails.
  DCHECK(!delegate_);
  delegate_ = delegate;

  // Get the billing address.
  if (!credit_card_.billing_address_id().empty()) {
    autofill::AutofillProfile* billing_address =
        autofill::PersonalDataManager::GetProfileFromProfilesByGUID(
            credit_card_.billing_address_id(), billing_profiles_);
    if (billing_address)
      billing_address_ = *billing_address;
  }

  is_waiting_for_billing_address_normalization_ = true;
  is_waiting_for_card_unmask_ = true;

  // Start the normalization of the billing address.
  payment_request_delegate_->GetAddressNormalizer()->NormalizeAddressAsync(
      billing_address_, /*timeout_seconds=*/5,
      base::BindOnce(&AutofillPaymentInstrument::OnAddressNormalized,
                     weak_ptr_factory_.GetWeakPtr()));

  payment_request_delegate_->DoFullCardRequest(credit_card_,
                                               weak_ptr_factory_.GetWeakPtr());
}

bool AutofillPaymentInstrument::IsCompleteForPayment() const {
  // COMPLETE or EXPIRED cards are considered valid for payment. The user will
  // be prompted to enter the new expiration at the CVC step.
  return GetCompletionStatusForCard(credit_card_, app_locale_,
                                    billing_profiles_) <= CREDIT_CARD_EXPIRED;
}

uint32_t AutofillPaymentInstrument::GetCompletenessScore() const {
  return ::payments::GetCompletenessScore(credit_card_, app_locale_,
                                          billing_profiles_);
}

bool AutofillPaymentInstrument::CanPreselect() const {
  return IsCompleteForPayment() && matches_merchant_card_type_exactly_;
}

void AutofillPaymentInstrument::RecordMissingFieldsForInstrument() const {
  CreditCardCompletionStatus completion_status =
      GetCompletionStatusForCard(credit_card_, app_locale_, billing_profiles_);
  if (completion_status == CREDIT_CARD_COMPLETE &&
      matches_merchant_card_type_exactly_) {
    return;
  }

  // Record cases that the card type does not match the requested type(s) in
  // addititon to missing fields from card completion status.
  base::UmaHistogramSparse(
      "PaymentRequest.MissingPaymentFields",
      completion_status |
          (matches_merchant_card_type_exactly_ ? 0
                                               : CREDIT_CARD_TYPE_MISMATCH));
}

bool AutofillPaymentInstrument::IsExactlyMatchingMerchantRequest() const {
  return matches_merchant_card_type_exactly_;
}

base::string16 AutofillPaymentInstrument::GetMissingInfoLabel() const {
  return GetCompletionMessageForCard(
      GetCompletionStatusForCard(credit_card_, app_locale_, billing_profiles_));
}

bool AutofillPaymentInstrument::IsValidForCanMakePayment() const {
  CreditCardCompletionStatus status =
      GetCompletionStatusForCard(credit_card_, app_locale_, billing_profiles_);
  if (PaymentsExperimentalFeatures::IsEnabled(
          features::kStrictHasEnrolledAutofillInstrument)) {
    return status == CREDIT_CARD_COMPLETE &&
           is_requested_autofill_data_available_;
  }

  // Card has to have a cardholder name and number for the purposes of
  // CanMakePayment. An expired card is still valid at this stage.
  return !(status & CREDIT_CARD_NO_CARDHOLDER ||
           status & CREDIT_CARD_NO_NUMBER);
}

void AutofillPaymentInstrument::RecordUse() {
  // Record the use of the credit card.
  payment_request_delegate_->GetPersonalDataManager()->RecordUseOf(
      credit_card_);
}

base::string16 AutofillPaymentInstrument::GetLabel() const {
  return credit_card_.NetworkAndLastFourDigits();
}

base::string16 AutofillPaymentInstrument::GetSublabel() const {
  return credit_card_.GetInfo(
      autofill::AutofillType(autofill::CREDIT_CARD_NAME_FULL), app_locale_);
}

bool AutofillPaymentInstrument::IsValidForModifier(
    const std::string& method,
    bool supported_networks_specified,
    const std::set<std::string>& supported_networks,
    bool supported_types_specified,
    const std::set<autofill::CreditCard::CardType>& supported_types) const {
  bool is_valid = false;
  IsValidForPaymentMethodIdentifier(method, &is_valid);
  if (!is_valid)
    return false;

  // If supported_types is not specified and this instrument matches the method,
  // the modifier is applicable. If supported_types is populated, it must
  // contain this card's type to be applicable. The same is true for
  // supported_networks.
  if (supported_types_specified) {
    // supported_types may contain CARD_TYPE_UNKNOWN because of the parsing
    // function so the local card only matches if it's because the website
    // didn't specify types (meaning they don't care).
    if (credit_card_.card_type() ==
        autofill::CreditCard::CardType::CARD_TYPE_UNKNOWN) {
      return false;
    }

    if (supported_types.find(credit_card_.card_type()) == supported_types.end())
      return false;
  }

  if (supported_networks_specified) {
    std::string basic_card_network =
        autofill::data_util::GetPaymentRequestData(credit_card_.network())
            .basic_card_issuer_network;
    if (supported_networks.find(basic_card_network) == supported_networks.end())
      return false;
  }

  return true;
}

void AutofillPaymentInstrument::IsValidForPaymentMethodIdentifier(
    const std::string& payment_method_identifier,
    bool* is_valid) const {
  // This instrument only matches basic-card.
  *is_valid = payment_method_identifier == "basic-card";
}

base::WeakPtr<PaymentInstrument> AutofillPaymentInstrument::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void AutofillPaymentInstrument::OnFullCardRequestSucceeded(
    const autofill::payments::FullCardRequest& /* full_card_request */,
    const autofill::CreditCard& card,
    const base::string16& cvc) {
  DCHECK(delegate_);
  credit_card_ = card;
  cvc_ = cvc;
  is_waiting_for_card_unmask_ = false;

  if (!is_waiting_for_billing_address_normalization_)
    GenerateBasicCardResponse();
}

void AutofillPaymentInstrument::OnFullCardRequestFailed() {
  // The user may have cancelled the unmask or something has gone wrong (e.g.,
  // the network request failed). In all cases, reset the |delegate_| so another
  // request can start.
  delegate_ = nullptr;
}

void AutofillPaymentInstrument::GenerateBasicCardResponse() {
  DCHECK(!is_waiting_for_billing_address_normalization_);
  DCHECK(!is_waiting_for_card_unmask_);
  DCHECK(delegate_);

  std::unique_ptr<base::DictionaryValue> response_value =
      payments::data_util::GetBasicCardResponseFromAutofillCreditCard(
          credit_card_, cvc_, billing_address_, app_locale_)
          ->ToDictionaryValue();
  std::string stringified_details;
  base::JSONWriter::Write(*response_value, &stringified_details);
  delegate_->OnInstrumentDetailsReady(method_name_, stringified_details);

  delegate_ = nullptr;
  cvc_ = base::UTF8ToUTF16("");
}

void AutofillPaymentInstrument::OnAddressNormalized(
    bool success,
    const autofill::AutofillProfile& normalized_profile) {
  DCHECK(is_waiting_for_billing_address_normalization_);

  billing_address_ = normalized_profile;
  is_waiting_for_billing_address_normalization_ = false;

  if (!is_waiting_for_card_unmask_)
    GenerateBasicCardResponse();
}

}  // namespace payments
