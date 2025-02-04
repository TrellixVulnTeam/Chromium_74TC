// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/remote/ntp_snippets_fetcher.h"

#include <cstdlib>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_writer.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/default_tick_clock.h"
#include "base/values.h"
#include "components/data_use_measurement/core/data_use_user_data.h"
#include "components/ntp_snippets/category_factory.h"
#include "components/ntp_snippets/features.h"
#include "components/ntp_snippets/ntp_snippets_constants.h"
#include "components/ntp_snippets/user_classifier.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/signin/core/browser/signin_manager_base.h"
#include "components/variations/net/variations_http_headers.h"
#include "components/variations/variations_associated_data.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/url_request/url_fetcher.h"
#include "third_party/icu/source/common/unicode/uloc.h"
#include "third_party/icu/source/common/unicode/utypes.h"

using net::URLFetcher;
using net::URLRequestContextGetter;
using net::HttpRequestHeaders;
using net::URLRequestStatus;
using translate::LanguageModel;

namespace ntp_snippets {

namespace {

const char kChromeReaderApiScope[] =
    "https://www.googleapis.com/auth/webhistory";
const char kContentSuggestionsApiScope[] =
    "https://www.googleapis.com/auth/chrome-content-suggestions";
const char kSnippetsServerNonAuthorizedFormat[] = "%s?key=%s";
const char kAuthorizationRequestHeaderFormat[] = "Bearer %s";

// Variation parameter for personalizing fetching of snippets.
const char kPersonalizationName[] = "fetching_personalization";

// Variation parameter for chrome-content-suggestions backend.
const char kContentSuggestionsBackend[] = "content_suggestions_backend";

// Constants for possible values of the "fetching_personalization" parameter.
const char kPersonalizationPersonalString[] = "personal";
const char kPersonalizationNonPersonalString[] = "non_personal";
const char kPersonalizationBothString[] = "both";  // the default value

const int kMaxExcludedIds = 100;

// Variation parameter for sending LanguageModel info to the server.
const char kSendTopLanguagesName[] = "send_top_languages";

// Variation parameter for sending UserClassifier info to the server.
const char kSendUserClassName[] = "send_user_class";

const char kBooleanParameterEnabled[] = "true";
const char kBooleanParameterDisabled[] = "false";

std::string FetchResultToString(NTPSnippetsFetcher::FetchResult result) {
  switch (result) {
    case NTPSnippetsFetcher::FetchResult::SUCCESS:
      return "OK";
    case NTPSnippetsFetcher::FetchResult::DEPRECATED_EMPTY_HOSTS:
      return "Cannot fetch for empty hosts list.";
    case NTPSnippetsFetcher::FetchResult::URL_REQUEST_STATUS_ERROR:
      return "URLRequestStatus error";
    case NTPSnippetsFetcher::FetchResult::HTTP_ERROR:
      return "HTTP error";
    case NTPSnippetsFetcher::FetchResult::JSON_PARSE_ERROR:
      return "Received invalid JSON";
    case NTPSnippetsFetcher::FetchResult::INVALID_SNIPPET_CONTENT_ERROR:
      return "Invalid / empty list.";
    case NTPSnippetsFetcher::FetchResult::OAUTH_TOKEN_ERROR:
      return "Error in obtaining an OAuth2 access token.";
    case NTPSnippetsFetcher::FetchResult::INTERACTIVE_QUOTA_ERROR:
      return "Out of interactive quota.";
    case NTPSnippetsFetcher::FetchResult::NON_INTERACTIVE_QUOTA_ERROR:
      return "Out of non-interactive quota.";
    case NTPSnippetsFetcher::FetchResult::RESULT_MAX:
      break;
  }
  NOTREACHED();
  return "Unknown error";
}

bool IsFetchPreconditionFailed(NTPSnippetsFetcher::FetchResult result) {
  switch (result) {
    case NTPSnippetsFetcher::FetchResult::DEPRECATED_EMPTY_HOSTS:
    case NTPSnippetsFetcher::FetchResult::OAUTH_TOKEN_ERROR:
    case NTPSnippetsFetcher::FetchResult::INTERACTIVE_QUOTA_ERROR:
    case NTPSnippetsFetcher::FetchResult::NON_INTERACTIVE_QUOTA_ERROR:
      return true;
    case NTPSnippetsFetcher::FetchResult::SUCCESS:
    case NTPSnippetsFetcher::FetchResult::URL_REQUEST_STATUS_ERROR:
    case NTPSnippetsFetcher::FetchResult::HTTP_ERROR:
    case NTPSnippetsFetcher::FetchResult::JSON_PARSE_ERROR:
    case NTPSnippetsFetcher::FetchResult::INVALID_SNIPPET_CONTENT_ERROR:
    case NTPSnippetsFetcher::FetchResult::RESULT_MAX:
      return false;
  }
  NOTREACHED();
  return true;
}

std::string GetFetchEndpoint() {
  std::string endpoint = variations::GetVariationParamValue(
      ntp_snippets::kStudyName, kContentSuggestionsBackend);
  return endpoint.empty() ? kChromeReaderServer : endpoint;
}

bool IsBooleanParameterEnabled(const std::string& param_name,
                               bool default_value) {
  std::string param_value = variations::GetVariationParamValueByFeature(
        ntp_snippets::kArticleSuggestionsFeature, param_name);
  if (param_value == kBooleanParameterEnabled)
    return true;
  if (param_value == kBooleanParameterDisabled)
    return false;
  if (!param_value.empty()) {
    LOG(WARNING) << "Invalid value \"" << param_value
                 << "\" for variation parameter " << param_name;
  }
  return default_value;
}

bool IsSendingTopLanguagesEnabled() {
  return IsBooleanParameterEnabled(kSendTopLanguagesName, false);
}

bool IsSendingUserClassEnabled() {
  return IsBooleanParameterEnabled(kSendUserClassName, false);
}

bool UsesChromeContentSuggestionsAPI(const GURL& endpoint) {
  if (endpoint == GURL(kChromeReaderServer))
    return false;

  if (endpoint != GURL(kContentSuggestionsServer) &&
      endpoint != GURL(kContentSuggestionsDevServer) &&
      endpoint != GURL(kContentSuggestionsAlphaServer)) {
    LOG(WARNING) << "Unknown value for " << kContentSuggestionsBackend << ": "
                 << "assuming chromecontentsuggestions-style API";
  }
  return true;
}

// Creates snippets from dictionary values in |list| and adds them to
// |snippets|. Returns true on success, false if anything went wrong.
// |remote_category_id| is only used if |content_suggestions_api| is true.
bool AddSnippetsFromListValue(bool content_suggestions_api,
                              int remote_category_id,
                              const base::ListValue& list,
                              NTPSnippet::PtrVector* snippets) {
  for (const auto& value : list) {
    const base::DictionaryValue* dict = nullptr;
    if (!value->GetAsDictionary(&dict)) {
      return false;
    }

    std::unique_ptr<NTPSnippet> snippet;
    if (content_suggestions_api) {
      snippet = NTPSnippet::CreateFromContentSuggestionsDictionary(
          *dict, remote_category_id);
    } else {
      snippet = NTPSnippet::CreateFromChromeReaderDictionary(*dict);
    }
    if (!snippet) {
      return false;
    }

    snippets->push_back(std::move(snippet));
  }
  return true;
}

// Translate the BCP 47 |language_code| into a posix locale string.
std::string PosixLocaleFromBCP47Language(const std::string& language_code) {
  char locale[ULOC_FULLNAME_CAPACITY];
  UErrorCode error = U_ZERO_ERROR;
  // Translate the input to a posix locale.
  uloc_forLanguageTag(language_code.c_str(), locale, ULOC_FULLNAME_CAPACITY,
                      nullptr, &error);
  if (error != U_ZERO_ERROR) {
    DLOG(WARNING) << "Error in translating language code to a locale string: "
                  << error;
    return std::string();
  }
  return locale;
}

std::string ISO639FromPosixLocale(const std::string& locale) {
  char language[ULOC_LANG_CAPACITY];
  UErrorCode error = U_ZERO_ERROR;
  uloc_getLanguage(locale.c_str(), language, ULOC_LANG_CAPACITY, &error);
  if (error != U_ZERO_ERROR) {
    DLOG(WARNING)
        << "Error in translating locale string to a ISO639 language code: "
        << error;
    return std::string();
  }
  return language;
}

void AppendLanguageInfoToList(base::ListValue* list,
                              const LanguageModel::LanguageInfo& info) {
  auto lang = base::MakeUnique<base::DictionaryValue>();
  lang->SetString("language", info.language_code);
  lang->SetDouble("frequency", info.frequency);
  list->Append(std::move(lang));
}

std::string GetUserClassString(UserClassifier::UserClass user_class) {
  switch (user_class) {
    case UserClassifier::UserClass::RARE_NTP_USER:
      return "RARE_NTP_USER";
    case UserClassifier::UserClass::ACTIVE_NTP_USER:
      return "ACTIVE_NTP_USER";
    case UserClassifier::UserClass::ACTIVE_SUGGESTIONS_CONSUMER:
      return "ACTIVE_SUGGESTIONS_CONSUMER";
  }
  NOTREACHED();
  return std::string();
}

}  // namespace

NTPSnippetsFetcher::FetchedCategory::FetchedCategory(Category c)
    : category(c) {}

NTPSnippetsFetcher::FetchedCategory::FetchedCategory(FetchedCategory&&) =
    default;
NTPSnippetsFetcher::FetchedCategory::~FetchedCategory() = default;
NTPSnippetsFetcher::FetchedCategory& NTPSnippetsFetcher::FetchedCategory::
operator=(FetchedCategory&&) = default;

NTPSnippetsFetcher::NTPSnippetsFetcher(
    SigninManagerBase* signin_manager,
    OAuth2TokenService* token_service,
    scoped_refptr<URLRequestContextGetter> url_request_context_getter,
    PrefService* pref_service,
    CategoryFactory* category_factory,
    LanguageModel* language_model,
    const ParseJSONCallback& parse_json_callback,
    const std::string& api_key,
    const UserClassifier* user_classifier)
    : OAuth2TokenService::Consumer("ntp_snippets"),
      signin_manager_(signin_manager),
      token_service_(token_service),
      waiting_for_refresh_token_(false),
      url_request_context_getter_(std::move(url_request_context_getter)),
      category_factory_(category_factory),
      language_model_(language_model),
      parse_json_callback_(parse_json_callback),
      count_to_fetch_(0),
      fetch_url_(GetFetchEndpoint()),
      fetch_api_(UsesChromeContentSuggestionsAPI(fetch_url_)
                     ? CHROME_CONTENT_SUGGESTIONS_API
                     : CHROME_READER_API),
      api_key_(api_key),
      interactive_request_(false),
      tick_clock_(new base::DefaultTickClock()),
      user_classifier_(user_classifier),
      request_throttler_rare_ntp_user_(
          pref_service,
          RequestThrottler::RequestType::
              CONTENT_SUGGESTION_FETCHER_RARE_NTP_USER),
      request_throttler_active_ntp_user_(
          pref_service,
          RequestThrottler::RequestType::
              CONTENT_SUGGESTION_FETCHER_ACTIVE_NTP_USER),
      request_throttler_active_suggestions_consumer_(
          pref_service,
          RequestThrottler::RequestType::
              CONTENT_SUGGESTION_FETCHER_ACTIVE_SUGGESTIONS_CONSUMER),
      oauth_token_retried_(false),
      weak_ptr_factory_(this) {
  // Parse the variation parameters and set the defaults if missing.
  std::string personalization = variations::GetVariationParamValue(
      ntp_snippets::kStudyName, kPersonalizationName);
  if (personalization == kPersonalizationNonPersonalString) {
    personalization_ = Personalization::kNonPersonal;
  } else if (personalization == kPersonalizationPersonalString) {
    personalization_ = Personalization::kPersonal;
  } else {
    personalization_ = Personalization::kBoth;
    LOG_IF(WARNING, !personalization.empty() &&
                        personalization != kPersonalizationBothString)
        << "Unknown value for " << kPersonalizationName << ": "
        << personalization;
  }
}

NTPSnippetsFetcher::~NTPSnippetsFetcher() {
  if (waiting_for_refresh_token_)
    token_service_->RemoveObserver(this);
}

void NTPSnippetsFetcher::SetCallback(
    const SnippetsAvailableCallback& callback) {
  snippets_available_callback_ = callback;
}

void NTPSnippetsFetcher::FetchSnippetsFromHosts(
    const std::set<std::string>& hosts,
    const std::string& language_code,
    const std::set<std::string>& excluded_ids,
    int count,
    bool interactive_request) {
  if (!DemandQuotaForRequest(interactive_request)) {
    FetchFinished(OptionalFetchedCategories(),
                  interactive_request
                      ? FetchResult::INTERACTIVE_QUOTA_ERROR
                      : FetchResult::NON_INTERACTIVE_QUOTA_ERROR,
                  /*extra_message=*/std::string());
    return;
  }

  hosts_ = hosts;
  fetch_start_time_ = tick_clock_->NowTicks();
  excluded_ids_ = excluded_ids;

  locale_ = PosixLocaleFromBCP47Language(language_code);
  count_to_fetch_ = count;

  bool use_authentication = UsesAuthentication();
  interactive_request_ = interactive_request;

  if (use_authentication && signin_manager_->IsAuthenticated()) {
    // Signed-in: get OAuth token --> fetch snippets.
    oauth_token_retried_ = false;
    StartTokenRequest();
  } else if (use_authentication && signin_manager_->AuthInProgress()) {
    // Currently signing in: wait for auth to finish (the refresh token) -->
    //     get OAuth token --> fetch snippets.
    if (!waiting_for_refresh_token_) {
      // Wait until we get a refresh token.
      waiting_for_refresh_token_ = true;
      token_service_->AddObserver(this);
    }
  } else {
    // Not signed in: fetch snippets (without authentication).
    FetchSnippetsNonAuthenticated();
  }
}

NTPSnippetsFetcher::RequestParams::RequestParams()
    : fetch_api(),
      obfuscated_gaia_id(),
      only_return_personalized_results(),
      user_locale(),
      host_restricts(),
      count_to_fetch(),
      interactive_request(),
      user_class(),
      ui_language{"", 0.0f},
      other_top_language{"", 0.0f} {}

NTPSnippetsFetcher::RequestParams::~RequestParams() = default;

std::string NTPSnippetsFetcher::RequestParams::BuildRequest() {
  auto request = base::MakeUnique<base::DictionaryValue>();
  switch (fetch_api) {
    case CHROME_READER_API: {
      auto content_params = base::MakeUnique<base::DictionaryValue>();
      content_params->SetBoolean("only_return_personalized_results",
                                 only_return_personalized_results);

      auto content_restricts = base::MakeUnique<base::ListValue>();
      for (const auto* metadata : {"TITLE", "SNIPPET", "THUMBNAIL"}) {
        auto entry = base::MakeUnique<base::DictionaryValue>();
        entry->SetString("type", "METADATA");
        entry->SetString("value", metadata);
        content_restricts->Append(std::move(entry));
      }

      auto content_selectors = base::MakeUnique<base::ListValue>();
      for (const auto& host : host_restricts) {
        auto entry = base::MakeUnique<base::DictionaryValue>();
        entry->SetString("type", "HOST_RESTRICT");
        entry->SetString("value", host);
        content_selectors->Append(std::move(entry));
      }

      auto local_scoring_params = base::MakeUnique<base::DictionaryValue>();
      local_scoring_params->Set("content_params", std::move(content_params));
      local_scoring_params->Set("content_restricts",
                                std::move(content_restricts));
      local_scoring_params->Set("content_selectors",
                                std::move(content_selectors));

      auto global_scoring_params = base::MakeUnique<base::DictionaryValue>();
      global_scoring_params->SetInteger("num_to_return", count_to_fetch);
      global_scoring_params->SetInteger("sort_type", 1);

      auto advanced = base::MakeUnique<base::DictionaryValue>();
      advanced->Set("local_scoring_params", std::move(local_scoring_params));
      advanced->Set("global_scoring_params", std::move(global_scoring_params));

      request->SetString("response_detail_level", "STANDARD");
      request->Set("advanced_options", std::move(advanced));
      if (!obfuscated_gaia_id.empty()) {
        request->SetString("obfuscated_gaia_id", obfuscated_gaia_id);
      }
      if (!user_locale.empty()) {
        request->SetString("user_locale", user_locale);
      }
      break;
    }

    case CHROME_CONTENT_SUGGESTIONS_API: {
      if (!user_locale.empty()) {
        request->SetString("uiLanguage", user_locale);
      }

      auto regular_hosts = base::MakeUnique<base::ListValue>();
      for (const auto& host : host_restricts) {
        regular_hosts->AppendString(host);
      }
      request->Set("regularlyVisitedHostNames", std::move(regular_hosts));
      request->SetString("priority", interactive_request
                                         ? "USER_ACTION"
                                         : "BACKGROUND_PREFETCH");

      auto excluded = base::MakeUnique<base::ListValue>();
      for (const auto& id : excluded_ids) {
        excluded->AppendString(id);
        if (excluded->GetSize() >= kMaxExcludedIds)
          break;
      }
      request->Set("excludedSuggestionIds", std::move(excluded));

      if (!user_class.empty())
        request->SetString("userActivenessClass", user_class);

      if (ui_language.frequency == 0 && other_top_language.frequency == 0)
        break;

      auto language_list = base::MakeUnique<base::ListValue>();
      if (ui_language.frequency > 0)
        AppendLanguageInfoToList(language_list.get(), ui_language);
      if (other_top_language.frequency > 0)
        AppendLanguageInfoToList(language_list.get(), other_top_language);
      request->Set("topLanguages", std::move(language_list));

      // TODO(sfiera): support authentication and personalization
      // TODO(sfiera): support count_to_fetch
      break;
    }
  }

  std::string request_json;
  bool success = base::JSONWriter::WriteWithOptions(
      *request, base::JSONWriter::OPTIONS_PRETTY_PRINT, &request_json);
  DCHECK(success);
  return request_json;
}

void NTPSnippetsFetcher::FetchSnippetsImpl(const GURL& url,
                                           const std::string& auth_header,
                                           const std::string& request) {
  url_fetcher_ = URLFetcher::Create(url, URLFetcher::POST, this);

  url_fetcher_->SetRequestContext(url_request_context_getter_.get());
  url_fetcher_->SetLoadFlags(net::LOAD_DO_NOT_SEND_COOKIES |
                             net::LOAD_DO_NOT_SAVE_COOKIES);

  data_use_measurement::DataUseUserData::AttachToFetcher(
      url_fetcher_.get(), data_use_measurement::DataUseUserData::NTP_SNIPPETS);

  HttpRequestHeaders headers;
  if (!auth_header.empty())
    headers.SetHeader("Authorization", auth_header);
  headers.SetHeader("Content-Type", "application/json; charset=UTF-8");
  // Add X-Client-Data header with experiment IDs from field trials.
  variations::AppendVariationHeaders(url,
                                     false,  // incognito
                                     false,  // uma_enabled
                                     &headers);
  url_fetcher_->SetExtraRequestHeaders(headers.ToString());
  url_fetcher_->SetUploadData("application/json", request);
  // Log the request for debugging network issues.
  VLOG(1) << "Sending a NTP snippets request to " << url << ":" << std::endl
          << headers.ToString() << std::endl
          << request;
  // Fetchers are sometimes cancelled because a network change was detected.
  url_fetcher_->SetAutomaticallyRetryOnNetworkChanges(3);
  // Try to make fetching the files bit more robust even with poor connection.
  url_fetcher_->SetMaxRetriesOn5xx(3);
  url_fetcher_->Start();
}

bool NTPSnippetsFetcher::UsesAuthentication() const {
  return (personalization_ == Personalization::kPersonal ||
          personalization_ == Personalization::kBoth);
}

void NTPSnippetsFetcher::SetUpCommonFetchingParameters(
    RequestParams* params) const {
  params->fetch_api = fetch_api_;
  params->host_restricts = hosts_;
  params->user_locale = locale_;
  params->excluded_ids = excluded_ids_;
  params->count_to_fetch = count_to_fetch_;
  params->interactive_request = interactive_request_;

  if (IsSendingUserClassEnabled())
    params->user_class = GetUserClassString(user_classifier_->GetUserClass());

  // TODO(jkrcal): Add language model factory for iOS and add fakes to tests so
  // that |language_model_| is never nullptr. Remove this check and add a DCHECK
  // into the constructor.
  if (!language_model_ || !IsSendingTopLanguagesEnabled())
    return;

  params->ui_language.language_code = ISO639FromPosixLocale(locale_);
  params->ui_language.frequency =
      language_model_->GetLanguageFrequency(params->ui_language.language_code);

  std::vector<LanguageModel::LanguageInfo> top_languages =
      language_model_->GetTopLanguages();
  for (const LanguageModel::LanguageInfo& info : top_languages) {
    if (info.language_code != params->ui_language.language_code) {
      params->other_top_language = info;

      // Report to UMA how important the UI language is.
      DCHECK_GT(params->other_top_language.frequency, 0)
          << "GetTopLanguages() should not return languages with 0 frequency";
      float ratio_ui_in_both_languages = params->ui_language.frequency /
                                         (params->ui_language.frequency +
                                          params->other_top_language.frequency);
      UMA_HISTOGRAM_PERCENTAGE(
          "NewTabPage.Languages.UILanguageRatioInTwoTopLanguages",
          ratio_ui_in_both_languages * 100);
      break;
    }
  }
}

void NTPSnippetsFetcher::FetchSnippetsNonAuthenticated() {
  // When not providing OAuth token, we need to pass the Google API key.
  GURL url(base::StringPrintf(kSnippetsServerNonAuthorizedFormat,
                              fetch_url_.spec().c_str(), api_key_.c_str()));

  RequestParams params;
  SetUpCommonFetchingParameters(&params);
  FetchSnippetsImpl(url, std::string(), params.BuildRequest());
}

void NTPSnippetsFetcher::FetchSnippetsAuthenticated(
    const std::string& account_id,
    const std::string& oauth_access_token) {
  RequestParams params;
  SetUpCommonFetchingParameters(&params);
  params.obfuscated_gaia_id = account_id;
  params.only_return_personalized_results =
      personalization_ == Personalization::kPersonal;
  // TODO(jkrcal, treib): Add unit-tests for authenticated fetches.
  FetchSnippetsImpl(fetch_url_,
                    base::StringPrintf(kAuthorizationRequestHeaderFormat,
                                       oauth_access_token.c_str()),
                    params.BuildRequest());
}

void NTPSnippetsFetcher::StartTokenRequest() {
  OAuth2TokenService::ScopeSet scopes;
  scopes.insert(fetch_api_ == CHROME_CONTENT_SUGGESTIONS_API
                    ? kContentSuggestionsApiScope
                    : kChromeReaderApiScope);
  oauth_request_ = token_service_->StartRequest(
      signin_manager_->GetAuthenticatedAccountId(), scopes, this);
}

////////////////////////////////////////////////////////////////////////////////
// OAuth2TokenService::Consumer overrides
void NTPSnippetsFetcher::OnGetTokenSuccess(
    const OAuth2TokenService::Request* request,
    const std::string& access_token,
    const base::Time& expiration_time) {
  // Delete the request after we leave this method.
  std::unique_ptr<OAuth2TokenService::Request> oauth_request(
      std::move(oauth_request_));
  DCHECK_EQ(oauth_request.get(), request)
      << "Got tokens from some previous request";

  FetchSnippetsAuthenticated(oauth_request->GetAccountId(), access_token);
}

void NTPSnippetsFetcher::OnGetTokenFailure(
    const OAuth2TokenService::Request* request,
    const GoogleServiceAuthError& error) {
  oauth_request_.reset();

  if (!oauth_token_retried_ &&
      error.state() == GoogleServiceAuthError::State::REQUEST_CANCELED) {
    // The request (especially on startup) can get reset by loading the refresh
    // token - do it one more time.
    oauth_token_retried_ = true;
    StartTokenRequest();
    return;
  }

  DLOG(ERROR) << "Unable to get token: " << error.ToString();
  FetchFinished(
      OptionalFetchedCategories(), FetchResult::OAUTH_TOKEN_ERROR,
      /*extra_message=*/base::StringPrintf(" (%s)", error.ToString().c_str()));
}

////////////////////////////////////////////////////////////////////////////////
// OAuth2TokenService::Observer overrides
void NTPSnippetsFetcher::OnRefreshTokenAvailable(
    const std::string& account_id) {
  // Only react on tokens for the account the user has signed in with.
  if (account_id != signin_manager_->GetAuthenticatedAccountId())
    return;

  token_service_->RemoveObserver(this);
  waiting_for_refresh_token_ = false;
  oauth_token_retried_ = false;
  StartTokenRequest();
}

////////////////////////////////////////////////////////////////////////////////
// URLFetcherDelegate overrides
void NTPSnippetsFetcher::OnURLFetchComplete(const URLFetcher* source) {
  DCHECK_EQ(url_fetcher_.get(), source);

  const URLRequestStatus& status = source->GetStatus();

  UMA_HISTOGRAM_SPARSE_SLOWLY(
      "NewTabPage.Snippets.FetchHttpResponseOrErrorCode",
      status.is_success() ? source->GetResponseCode() : status.error());

  if (!status.is_success()) {
    FetchFinished(OptionalFetchedCategories(),
                  FetchResult::URL_REQUEST_STATUS_ERROR,
                  /*extra_message=*/base::StringPrintf(" %d", status.error()));
  } else if (source->GetResponseCode() != net::HTTP_OK) {
    // TODO(jkrcal): https://crbug.com/609084
    // We need to deal with the edge case again where the auth
    // token expires just before we send the request (in which case we need to
    // fetch a new auth token). We should extract that into a common class
    // instead of adding it to every single class that uses auth tokens.
    FetchFinished(
        OptionalFetchedCategories(), FetchResult::HTTP_ERROR,
        /*extra_message=*/base::StringPrintf(" %d", source->GetResponseCode()));
  } else {
    bool stores_result_to_string =
        source->GetResponseAsString(&last_fetch_json_);
    DCHECK(stores_result_to_string);

    parse_json_callback_.Run(last_fetch_json_,
                             base::Bind(&NTPSnippetsFetcher::OnJsonParsed,
                                        weak_ptr_factory_.GetWeakPtr()),
                             base::Bind(&NTPSnippetsFetcher::OnJsonError,
                                        weak_ptr_factory_.GetWeakPtr()));
  }
}

bool NTPSnippetsFetcher::JsonToSnippets(const base::Value& parsed,
                                        FetchedCategoriesVector* categories) {
  const base::DictionaryValue* top_dict = nullptr;
  if (!parsed.GetAsDictionary(&top_dict)) {
    return false;
  }

  switch (fetch_api_) {
    case CHROME_READER_API: {
      const int kUnusedRemoteCategoryId = -1;
      categories->push_back(FetchedCategory(
          category_factory_->FromKnownCategory(KnownCategories::ARTICLES)));
      const base::ListValue* recos = nullptr;
      return top_dict->GetList("recos", &recos) &&
             AddSnippetsFromListValue(/*content_suggestions_api=*/false,
                                      kUnusedRemoteCategoryId,
                                      *recos, &categories->back().snippets);
    }

    case CHROME_CONTENT_SUGGESTIONS_API: {
      const base::ListValue* categories_value = nullptr;
      if (!top_dict->GetList("categories", &categories_value)) {
        return false;
      }

      for (const auto& v : *categories_value) {
        std::string utf8_title;
        int remote_category_id = -1;
        const base::DictionaryValue* category_value = nullptr;
        if (!(v->GetAsDictionary(&category_value) &&
              category_value->GetString("localizedTitle", &utf8_title) &&
              category_value->GetInteger("id", &remote_category_id) &&
              (remote_category_id > 0))) {
          return false;
        }

        categories->push_back(FetchedCategory(
            category_factory_->FromRemoteCategory(remote_category_id)));
        categories->back().localized_title = base::UTF8ToUTF16(utf8_title);

        const base::ListValue* suggestions = nullptr;
        if (!category_value->GetList("suggestions", &suggestions)) {
          // Absence of a list of suggestions is treated as an empty list, which
          // is permissible.
          continue;
        }
        if (!AddSnippetsFromListValue(
                /*content_suggestions_api=*/true, remote_category_id,
                *suggestions, &categories->back().snippets)) {
          return false;
        }
      }
      return true;
    }
  }
  NOTREACHED();
  return false;
}

void NTPSnippetsFetcher::OnJsonParsed(std::unique_ptr<base::Value> parsed) {
  FetchedCategoriesVector categories;
  if (JsonToSnippets(*parsed, &categories)) {
    FetchFinished(OptionalFetchedCategories(std::move(categories)),
                  FetchResult::SUCCESS,
                  /*extra_message=*/std::string());
  } else {
    LOG(WARNING) << "Received invalid snippets: " << last_fetch_json_;
    FetchFinished(OptionalFetchedCategories(),
                  FetchResult::INVALID_SNIPPET_CONTENT_ERROR,
                  /*extra_message=*/std::string());
  }
}

void NTPSnippetsFetcher::OnJsonError(const std::string& error) {
  LOG(WARNING) << "Received invalid JSON (" << error
               << "): " << last_fetch_json_;
  FetchFinished(
      OptionalFetchedCategories(), FetchResult::JSON_PARSE_ERROR,
      /*extra_message=*/base::StringPrintf(" (error %s)", error.c_str()));
}

void NTPSnippetsFetcher::FetchFinished(
    OptionalFetchedCategories fetched_categories,
    FetchResult result,
    const std::string& extra_message) {
  DCHECK(result == FetchResult::SUCCESS || !fetched_categories);
  last_status_ = FetchResultToString(result) + extra_message;

  // Don't record FetchTimes if the result indicates that a precondition
  // failed and we never actually sent a network request
  if (!IsFetchPreconditionFailed(result)) {
    UMA_HISTOGRAM_TIMES("NewTabPage.Snippets.FetchTime",
                        tick_clock_->NowTicks() - fetch_start_time_);
  }
  UMA_HISTOGRAM_ENUMERATION("NewTabPage.Snippets.FetchResult",
                            static_cast<int>(result),
                            static_cast<int>(FetchResult::RESULT_MAX));

  DVLOG(1) << "Fetch finished: " << last_status_;
  if (!snippets_available_callback_.is_null())
    snippets_available_callback_.Run(std::move(fetched_categories));
}

bool NTPSnippetsFetcher::DemandQuotaForRequest(bool interactive_request) {
  switch (user_classifier_->GetUserClass()) {
    case UserClassifier::UserClass::RARE_NTP_USER:
      return request_throttler_rare_ntp_user_.DemandQuotaForRequest(
          interactive_request);
    case UserClassifier::UserClass::ACTIVE_NTP_USER:
      return request_throttler_active_ntp_user_.DemandQuotaForRequest(
          interactive_request);
    case UserClassifier::UserClass::ACTIVE_SUGGESTIONS_CONSUMER:
      return request_throttler_active_suggestions_consumer_
          .DemandQuotaForRequest(interactive_request);
  }
  NOTREACHED();
  return false;
}

}  // namespace ntp_snippets
