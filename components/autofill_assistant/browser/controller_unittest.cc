// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/controller.h"

#include <memory>
#include <utility>

#include "base/guid.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill_assistant/browser/features.h"
#include "components/autofill_assistant/browser/mock_controller_observer.h"
#include "components/autofill_assistant/browser/mock_personal_data_manager.h"
#include "components/autofill_assistant/browser/mock_service.h"
#include "components/autofill_assistant/browser/service.h"
#include "components/autofill_assistant/browser/trigger_context.h"
#include "components/autofill_assistant/browser/web/mock_web_controller.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace autofill_assistant {

using ::base::test::RunOnceCallback;
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::Sequence;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;

namespace {

class FakeClient : public Client {
 public:
  // Implements Client
  std::string GetApiKey() override { return ""; }
  AccessTokenFetcher* GetAccessTokenFetcher() override { return nullptr; }
  MockPersonalDataManager* GetPersonalDataManager() override {
    return &mock_personal_data_manager_;
  }
  WebsiteLoginFetcher* GetWebsiteLoginFetcher() override { return nullptr; }
  std::string GetServerUrl() override { return ""; }
  std::string GetAccountEmailAddress() override { return ""; }
  std::string GetLocale() override { return ""; }
  std::string GetCountryCode() override { return ""; }
  MOCK_METHOD1(Shutdown, void(Metrics::DropOutReason reason));
  MOCK_METHOD0(AttachUI, void());
  MOCK_METHOD0(DestroyUI, void());

 private:
  MockPersonalDataManager mock_personal_data_manager_;
};

// Same as non-mock, but provides default mock callbacks.
struct MockCollectUserDataOptions : public CollectUserDataOptions {
  MockCollectUserDataOptions() {
    base::MockOnceCallback<void(std::unique_ptr<UserData>)>
        mock_confirm_callback;
    confirm_callback = std::move(mock_confirm_callback.Get());
    base::MockOnceCallback<void(int)> mock_actions_callback;
    additional_actions_callback = std::move(mock_actions_callback.Get());
    base::MockOnceCallback<void(int)> mock_terms_callback;
    terms_link_callback = std::move(mock_terms_callback.Get());
  }
};

}  // namespace

class ControllerTest : public content::RenderViewHostTestHarness {
 public:
  ControllerTest()
      : RenderViewHostTestHarness(
            base::test::TaskEnvironment::MainThreadType::UI,
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  ~ControllerTest() override {}

  void SetUp() override {
    RenderViewHostTestHarness::SetUp();

    scoped_feature_list_.InitAndEnableFeature(
        features::kAutofillAssistantChromeEntry);
    auto web_controller = std::make_unique<NiceMock<MockWebController>>();
    mock_web_controller_ = web_controller.get();
    auto service = std::make_unique<NiceMock<MockService>>();
    mock_service_ = service.get();

    controller_ = std::make_unique<Controller>(
        web_contents(), &fake_client_, task_environment()->GetMockTickClock(),
        std::move(service));
    controller_->SetWebControllerForTest(std::move(web_controller));

    // Fetching scripts succeeds for all URLs, but return nothing.
    ON_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
        .WillByDefault(RunOnceCallback<2>(true, ""));

    // Scripts run, but have no actions.
    ON_CALL(*mock_service_, OnGetActions(_, _, _, _, _, _))
        .WillByDefault(RunOnceCallback<5>(true, ""));

    ON_CALL(*mock_service_, OnGetNextActions(_, _, _, _, _))
        .WillByDefault(RunOnceCallback<4>(true, ""));

    ON_CALL(*mock_web_controller_, OnElementCheck(_, _))
        .WillByDefault(RunOnceCallback<1>(false));

    ON_CALL(mock_observer_, OnStateChanged(_))
        .WillByDefault(Invoke([this](AutofillAssistantState state) {
          states_.emplace_back(state);
        }));
    controller_->AddObserver(&mock_observer_);
  }

  void TearDown() override { controller_->RemoveObserver(&mock_observer_); }

 protected:
  static SupportedScriptProto* AddRunnableScript(
      SupportsScriptResponseProto* response,
      const std::string& name_and_path) {
    SupportedScriptProto* script = response->add_scripts();
    script->set_path(name_and_path);
    script->mutable_presentation()->mutable_chip()->set_text(name_and_path);
    return script;
  }

  static void RunOnce(SupportedScriptProto* proto) {
    auto* run_once = proto->mutable_presentation()
                         ->mutable_precondition()
                         ->add_script_status_match();
    run_once->set_script(proto->path());
    run_once->set_status(SCRIPT_STATUS_NOT_RUN);
  }

  void SetupScripts(SupportsScriptResponseProto scripts) {
    std::string scripts_str;
    scripts.SerializeToString(&scripts_str);
    EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
        .WillOnce(RunOnceCallback<2>(true, scripts_str));
  }

  void SetupActionsForScript(const std::string& path,
                             ActionsResponseProto actions_response) {
    std::string actions_response_str;
    actions_response.SerializeToString(&actions_response_str);
    EXPECT_CALL(*mock_service_, OnGetActions(StrEq(path), _, _, _, _, _))
        .WillOnce(RunOnceCallback<5>(true, actions_response_str));
  }

  void Start() { Start("http://initialurl.com"); }

  void Start(const std::string& url_string) {
    GURL url(url_string);
    SetLastCommittedUrl(url);
    controller_->Start(url, TriggerContext::CreateEmpty());
  }

  void SetLastCommittedUrl(const GURL& url) {
    content::WebContentsTester::For(web_contents())->SetLastCommittedURL(url);
  }

  void SimulateNavigateToUrl(const GURL& url) {
    content::NavigationSimulator::NavigateAndCommitFromDocument(
        url, web_contents()->GetMainFrame());
    content::WebContentsTester::For(web_contents())->TestSetIsLoading(false);
    SetLastCommittedUrl(url);
    controller_->DidFinishLoad(nullptr, GURL(""));
  }

  void SimulateWebContentsFocused() {
    controller_->OnWebContentsFocused(nullptr);
  }

  // Sets up the next call to the service for scripts to return |response|.
  void SetNextScriptResponse(const SupportsScriptResponseProto& response) {
    std::string response_str;
    response.SerializeToString(&response_str);

    EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
        .WillOnce(RunOnceCallback<2>(true, response_str));
  }

  // Sets up all calls to the service for scripts to return |response|.
  void SetRepeatedScriptResponse(const SupportsScriptResponseProto& response) {
    std::string response_str;
    response.SerializeToString(&response_str);

    EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
        .WillRepeatedly(RunOnceCallback<2>(true, response_str));
  }

  UiDelegate* GetUiDelegate() { return controller_.get(); }

  // |task_environment_| must be the first field, to make sure that everything
  // runs in the same task environment.
  base::test::ScopedFeatureList scoped_feature_list_;
  base::TimeTicks now_;
  std::vector<AutofillAssistantState> states_;
  MockService* mock_service_;
  MockWebController* mock_web_controller_;
  NiceMock<FakeClient> fake_client_;
  NiceMock<MockControllerObserver> mock_observer_;

  std::unique_ptr<Controller> controller_;
};

struct NavigationState {
  bool navigating = false;
  bool has_errors = false;

  bool operator==(const NavigationState& other) const {
    return navigating == other.navigating && has_errors == other.has_errors;
  }
};

std::ostream& operator<<(std::ostream& out, const NavigationState& state) {
  out << "{navigating=" << state.navigating << ","
      << "has_errors=" << state.has_errors << "}";
  return out;
}

// A Listener that keeps track of the reported state of the delegate captured
// from OnNavigationStateChanged.
class NavigationStateChangeListener : public ScriptExecutorDelegate::Listener {
 public:
  explicit NavigationStateChangeListener(ScriptExecutorDelegate* delegate)
      : delegate_(delegate) {}
  ~NavigationStateChangeListener() = default;
  void OnNavigationStateChanged() override;

  std::vector<NavigationState> events;

 private:
  ScriptExecutorDelegate* const delegate_;
};

void NavigationStateChangeListener::OnNavigationStateChanged() {
  NavigationState state;
  state.navigating = delegate_->IsNavigatingToNewDocument();
  state.has_errors = delegate_->HasNavigationError();
  events.emplace_back(state);
}

TEST_F(ControllerTest, FetchAndRunScriptsWithChip) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script1");
  auto* script2 = AddRunnableScript(&script_response, "script2");
  RunOnce(script2);
  SetNextScriptResponse(script_response);

  testing::InSequence seq;

  Start("http://a.example.com/path");

  // Offering the choices: script1 and script2
  EXPECT_EQ(AutofillAssistantState::AUTOSTART_FALLBACK_PROMPT,
            controller_->GetState());
  EXPECT_THAT(
      controller_->GetUserActions(),
      UnorderedElementsAre(Property(&UserAction::chip,
                                    AllOf(Field(&Chip::text, StrEq("script1")),
                                          Field(&Chip::type, SUGGESTION))),
                           Property(&UserAction::chip,
                                    AllOf(Field(&Chip::text, StrEq("script2")),
                                          Field(&Chip::type, SUGGESTION)))));

  // Choose script2 and run it successfully.
  EXPECT_CALL(*mock_service_, OnGetActions(StrEq("script2"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(true, ""));
  EXPECT_TRUE(controller_->PerformUserAction(1));

  // Offering the remaining choice: script1 as script2 can only run once.
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());
  EXPECT_THAT(controller_->GetUserActions(),
              ElementsAre(Property(&UserAction::chip,
                                   Field(&Chip::text, StrEq("script1")))));
}

TEST_F(ControllerTest, ReportDirectActions) {
  SupportsScriptResponseProto script_response;

  // script1 is available as a chip and a direct action.
  auto* script1 = AddRunnableScript(&script_response, "script1");
  script1->mutable_presentation()->mutable_direct_action()->add_names(
      "action_1");

  // script1 is available only as a direct action.
  auto* script2 = AddRunnableScript(&script_response, "script2");
  script2->mutable_presentation()->mutable_direct_action()->add_names(
      "action_2");
  script2->mutable_presentation()->clear_chip();

  SetNextScriptResponse(script_response);

  testing::InSequence seq;

  Start("http://a.example.com/path");

  // Offering the choices: script1 and script2
  EXPECT_EQ(AutofillAssistantState::AUTOSTART_FALLBACK_PROMPT,
            controller_->GetState());
  EXPECT_THAT(
      controller_->GetUserActions(),
      UnorderedElementsAre(
          AllOf(Property(&UserAction::chip, Field(&Chip::text, "script1")),
                Property(&UserAction::direct_action,
                         Field(&DirectAction::names, ElementsAre("action_1")))),
          AllOf(
              Property(&UserAction::chip, Property(&Chip::empty, true)),
              Property(&UserAction::direct_action,
                       Field(&DirectAction::names, ElementsAre("action_2"))))));
}

TEST_F(ControllerTest, RunDirectActionWithArguments) {
  SupportsScriptResponseProto script_response;

  // script is available as a chip and a direct action.
  auto* script1 = AddRunnableScript(&script_response, "script");
  auto* action = script1->mutable_presentation()->mutable_direct_action();
  action->add_names("action");
  action->add_required_arguments("required");
  action->add_optional_arguments("arg0");
  action->add_optional_arguments("arg1");

  SetNextScriptResponse(script_response);

  testing::InSequence seq;

  Start("http://a.example.com/path");

  EXPECT_EQ(AutofillAssistantState::AUTOSTART_FALLBACK_PROMPT,
            controller_->GetState());
  EXPECT_THAT(controller_->GetUserActions(),
              ElementsAre(Property(
                  &UserAction::direct_action,
                  AllOf(Field(&DirectAction::names, ElementsAre("action")),
                        Field(&DirectAction::required_arguments,
                              ElementsAre("required")),
                        Field(&DirectAction::optional_arguments,
                              ElementsAre("arg0", "arg1"))))));

  EXPECT_CALL(*mock_service_, OnGetActions("script", _, _, _, _, _))
      .WillOnce(Invoke([](const std::string& script_path, const GURL& url,
                          const TriggerContext& trigger_context,
                          const std::string& global_payload,
                          const std::string& script_payload,
                          Service::ResponseCallback& callback) {
        EXPECT_THAT("value",
                    trigger_context.GetParameter("required").value_or(""));
        EXPECT_THAT("value0",
                    trigger_context.GetParameter("arg0").value_or(""));

        std::move(callback).Run(true, "");
      }));

  std::map<std::string, std::string> parameters;
  parameters["required"] = "value";
  parameters["arg0"] = "value0";
  EXPECT_TRUE(controller_->PerformUserActionWithContext(
      0, TriggerContext::Create(parameters, "")));
}

TEST_F(ControllerTest, NoScripts) {
  SupportsScriptResponseProto empty;
  SetNextScriptResponse(empty);

  EXPECT_CALL(fake_client_,
              Shutdown(Metrics::DropOutReason::NO_INITIAL_SCRIPTS));
  Start("http://a.example.com/path");
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
}

TEST_F(ControllerTest, NoRelevantScripts) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "no_match")
      ->mutable_presentation()
      ->mutable_precondition()
      ->add_domain("http://otherdomain.com");
  SetNextScriptResponse(script_response);

  EXPECT_CALL(fake_client_,
              Shutdown(Metrics::DropOutReason::NO_INITIAL_SCRIPTS));
  Start("http://a.example.com/path");
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
}

TEST_F(ControllerTest, NoRelevantScriptYet) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "no_match_yet")
      ->mutable_presentation()
      ->mutable_precondition()
      ->add_elements_exist()
      ->add_selectors("#element");
  SetNextScriptResponse(script_response);

  Start("http://a.example.com/path");
  EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
}
TEST_F(ControllerTest, ReportPromptAndSuggestionsChanged) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script1");
  AddRunnableScript(&script_response, "script2");
  SetNextScriptResponse(script_response);

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(2)));
  Start("http://a.example.com/path");

  EXPECT_EQ(AutofillAssistantState::AUTOSTART_FALLBACK_PROMPT,
            controller_->GetState());
}

TEST_F(ControllerTest, ClearUserActionsWhenRunning) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script1");
  AddRunnableScript(&script_response, "script2");
  SetNextScriptResponse(script_response);

  // Discover 2 scripts, one is selected and run (with no chips shown), then the
  // same chips are shown.
  {
    testing::InSequence seq;
    // Discover 2 scripts, script1 and script2.
    EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(2)));
    // Set of chips is cleared while running script1.
    EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(0)));
    // This test doesn't specify what happens after that.
    EXPECT_CALL(mock_observer_, OnUserActionsChanged(_)).Times(AnyNumber());
  }
  Start("http://a.example.com/path");
  EXPECT_TRUE(controller_->PerformUserAction(0));
}

TEST_F(ControllerTest, ShowFirstInitialStatusMessage) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script1");

  SupportedScriptProto* script2 =
      AddRunnableScript(&script_response, "script2");
  script2->mutable_presentation()->set_initial_prompt("script2 prompt");
  script2->mutable_presentation()->set_priority(10);

  SupportedScriptProto* script3 =
      AddRunnableScript(&script_response, "script3");
  script3->mutable_presentation()->set_initial_prompt("script3 prompt");
  script3->mutable_presentation()->set_priority(5);

  SupportedScriptProto* script4 =
      AddRunnableScript(&script_response, "script4");
  script4->mutable_presentation()->set_initial_prompt("script4 prompt");
  script4->mutable_presentation()->set_priority(8);

  SetNextScriptResponse(script_response);

  Start("http://a.example.com/path");

  EXPECT_THAT(controller_->GetUserActions(), SizeIs(4));
  // Script3, with higher priority (lower number), wins.
  EXPECT_EQ("script3 prompt", controller_->GetStatusMessage());
}

TEST_F(ControllerTest, ScriptStartMessage) {
  SupportsScriptResponseProto script_response;
  auto* script = AddRunnableScript(&script_response, "script");
  script->mutable_presentation()->set_start_message("Starting Script...");
  SetNextScriptResponse(script_response);

  ActionsResponseProto script_actions;
  script_actions.add_actions()->mutable_tell()->set_message("Script running.");
  SetupActionsForScript("script", script_actions);

  Start("http://a.example.com/path");

  {
    testing::InSequence seq;
    EXPECT_CALL(mock_observer_, OnStatusMessageChanged("Starting Script..."));
    EXPECT_CALL(mock_observer_, OnStatusMessageChanged("Script running."));
  }
  EXPECT_TRUE(controller_->PerformUserAction(0));
}

TEST_F(ControllerTest, Stop) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "stop");
  SetNextScriptResponse(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_stop();
  std::string actions_response_str;
  actions_response.SerializeToString(&actions_response_str);
  EXPECT_CALL(*mock_service_, OnGetActions(StrEq("stop"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(true, actions_response_str));

  Start();
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));

  testing::InSequence seq;
  EXPECT_CALL(fake_client_, Shutdown(Metrics::DropOutReason::SCRIPT_SHUTDOWN));
  EXPECT_TRUE(controller_->PerformUserAction(0));
}

TEST_F(ControllerTest, Reset) {
  // 1. Fetch scripts for URL, which in contains a single "reset" script.
  SupportsScriptResponseProto script_response;
  auto* reset_script = AddRunnableScript(&script_response, "reset");
  RunOnce(reset_script);
  std::string script_response_str;
  script_response.SerializeToString(&script_response_str);
  EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
      .WillRepeatedly(RunOnceCallback<2>(true, script_response_str));

  Start("http://a.example.com/path");
  EXPECT_THAT(controller_->GetUserActions(),
              ElementsAre(Property(&UserAction::chip,
                                   Field(&Chip::text, StrEq("reset")))));

  // 2. Execute the "reset" script, which contains a reset action.
  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_reset();
  std::string actions_response_str;
  actions_response.SerializeToString(&actions_response_str);
  EXPECT_CALL(*mock_service_, OnGetActions(StrEq("reset"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(true, actions_response_str));

  controller_->GetClientMemory()->set_selected_card(
      std::make_unique<autofill::CreditCard>());
  EXPECT_TRUE(controller_->GetClientMemory()->has_selected_card());

  EXPECT_TRUE(controller_->PerformUserAction(0));

  // Resetting should have cleared the client memory
  EXPECT_FALSE(controller_->GetClientMemory()->has_selected_card());

  // The reset script should be available again, even though it's marked
  // RunOnce, as the script state should have been cleared as well.
  EXPECT_THAT(controller_->GetUserActions(),
              ElementsAre(Property(&UserAction::chip,
                                   Field(&Chip::text, StrEq("reset")))));
}

TEST_F(ControllerTest, RefreshScriptWhenDomainChanges) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script");
  std::string scripts_str;
  script_response.SerializeToString(&scripts_str);

  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(Eq(GURL("http://a.example.com/path1")), _, _))
      .WillOnce(RunOnceCallback<2>(true, scripts_str));
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(Eq(GURL("http://b.example.com/path1")), _, _))
      .WillOnce(RunOnceCallback<2>(true, scripts_str));

  Start("http://a.example.com/path1");
  SimulateNavigateToUrl(GURL("http://a.example.com/path2"));
  SimulateNavigateToUrl(GURL("http://b.example.com/path1"));
  SimulateNavigateToUrl(GURL("http://b.example.com/path2"));
}

TEST_F(ControllerTest, Autostart) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  AddRunnableScript(&script_response, "autostart")
      ->mutable_presentation()
      ->set_autostart(true);
  SetNextScriptResponse(script_response);

  EXPECT_CALL(*mock_service_, OnGetActions(StrEq("autostart"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(true, ""));

  EXPECT_CALL(fake_client_, AttachUI());
  Start("http://a.example.com/path");
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  ActionsResponseProto runnable_script;
  runnable_script.add_actions()->mutable_tell()->set_message("runnable");
  runnable_script.add_actions()->mutable_stop();
  SetupActionsForScript("runnable", runnable_script);

  // The script "runnable" stops the flow and shutdowns the controller.
  EXPECT_CALL(fake_client_, Shutdown(Metrics::DropOutReason::SCRIPT_SHUTDOWN));
  controller_->PerformUserAction(0);
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  // Full history state transitions
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::STOPPED));
}

TEST_F(ControllerTest, AutostartIsNotPassedToTheUi) {
  SupportsScriptResponseProto script_response;
  auto* autostart = AddRunnableScript(&script_response, "runnable");
  autostart->mutable_presentation()->set_autostart(true);
  RunOnce(autostart);
  SetRepeatedScriptResponse(script_response);

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(0u)))
      .Times(AnyNumber());
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(Gt(0u)))).Times(0);

  Start("http://a.example.com/path");
  EXPECT_THAT(controller_->GetUserActions(), SizeIs(0));
}

TEST_F(ControllerTest, InitialUrlLoads) {
  GURL initialUrl("http://a.example.com/path");
  EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(Eq(initialUrl), _, _))
      .WillOnce(RunOnceCallback<2>(true, ""));

  controller_->Start(initialUrl, TriggerContext::CreateEmpty());
}

TEST_F(ControllerTest, ProgressIncreasesAtStart) {
  EXPECT_EQ(0, controller_->GetProgress());
  EXPECT_CALL(mock_observer_, OnProgressChanged(5));
  Start();
  EXPECT_EQ(5, controller_->GetProgress());
}

TEST_F(ControllerTest, SetProgress) {
  Start();
  EXPECT_CALL(mock_observer_, OnProgressChanged(20));
  controller_->SetProgress(20);
  EXPECT_EQ(20, controller_->GetProgress());
}

TEST_F(ControllerTest, IgnoreProgressDecreases) {
  Start();
  EXPECT_CALL(mock_observer_, OnProgressChanged(Not(15))).Times(AnyNumber());
  controller_->SetProgress(20);
  controller_->SetProgress(15);
  EXPECT_EQ(20, controller_->GetProgress());
}

TEST_F(ControllerTest, StateChanges) {
  EXPECT_EQ(AutofillAssistantState::INACTIVE, GetUiDelegate()->GetState());

  SupportsScriptResponseProto script_response;
  auto* script1 = AddRunnableScript(&script_response, "script1");
  RunOnce(script1);
  auto* script2 = AddRunnableScript(&script_response, "script2");
  RunOnce(script2);
  SetNextScriptResponse(script_response);

  Start("http://a.example.com/path");
  EXPECT_THAT(states_,
              ElementsAre(AutofillAssistantState::STARTING,
                          AutofillAssistantState::AUTOSTART_FALLBACK_PROMPT));

  // Run script1: State should become RUNNING, as there's another script, then
  // go back to prompt to propose that script.
  states_.clear();
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(2));
  EXPECT_TRUE(controller_->PerformUserAction(0));

  EXPECT_EQ(AutofillAssistantState::PROMPT, GetUiDelegate()->GetState());
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT));

  // Run script2: State should become STOPPED, as there are no more runnable
  // scripts.
  states_.clear();
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));
  EXPECT_TRUE(controller_->PerformUserAction(0));

  EXPECT_EQ(AutofillAssistantState::STOPPED, GetUiDelegate()->GetState());
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT,
                                   AutofillAssistantState::STOPPED));

  // The cancel button is removed.
  EXPECT_TRUE(controller_->GetUserActions().empty());
}

TEST_F(ControllerTest, AttachUIWhenStarting) {
  EXPECT_CALL(fake_client_, AttachUI());
  Start();
}

TEST_F(ControllerTest, AttachUIWhenContentsFocused) {
  SimulateWebContentsFocused();  // must not call AttachUI

  testing::InSequence seq;
  EXPECT_CALL(fake_client_, AttachUI());

  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script1");
  SetNextScriptResponse(script_response);
  Start();  // must call AttachUI

  EXPECT_CALL(fake_client_, AttachUI());
  SimulateWebContentsFocused();  // must call AttachUI

  controller_->OnFatalError("test", Metrics::DropOutReason::TAB_CHANGED);
  SimulateWebContentsFocused();  // must not call AttachUI
}

TEST_F(ControllerTest, KeepCheckingForElement) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "no_match_yet")
      ->mutable_presentation()
      ->mutable_precondition()
      ->add_elements_exist()
      ->add_selectors("#element");
  SetNextScriptResponse(script_response);

  Start("http://a.example.com/path");
  // No scripts yet; the element doesn't exit.
  EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());

  for (int i = 0; i < 3; i++) {
    task_environment()->FastForwardBy(base::TimeDelta::FromSeconds(1));
    EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
  }

  EXPECT_CALL(*mock_web_controller_, OnElementCheck(_, _))
      .WillRepeatedly(RunOnceCallback<1>(true));
  task_environment()->FastForwardBy(base::TimeDelta::FromSeconds(1));

  EXPECT_EQ(AutofillAssistantState::AUTOSTART_FALLBACK_PROMPT,
            controller_->GetState());
}

TEST_F(ControllerTest, ScriptTimeoutError) {
  // Wait for #element to show up for will_never_match. After 25s, execute the
  // script on_timeout_error.
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "will_never_match")
      ->mutable_presentation()
      ->mutable_precondition()
      ->add_elements_exist()
      ->add_selectors("#element");
  script_response.mutable_script_timeout_error()->set_timeout_ms(30000);
  script_response.mutable_script_timeout_error()->set_script_path(
      "on_timeout_error");
  SetNextScriptResponse(script_response);

  // on_timeout_error stops everything with a custom error message.
  ActionsResponseProto on_timeout_error;
  on_timeout_error.add_actions()->mutable_tell()->set_message("I give up");
  on_timeout_error.add_actions()->mutable_stop();
  std::string on_timeout_error_str;
  on_timeout_error.SerializeToString(&on_timeout_error_str);
  EXPECT_CALL(*mock_service_,
              OnGetActions(StrEq("on_timeout_error"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(true, on_timeout_error_str));

  Start("http://a.example.com/path");
  for (int i = 0; i < 30; i++) {
    EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
    task_environment()->FastForwardBy(base::TimeDelta::FromSeconds(1));
  }
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
  EXPECT_EQ("I give up", controller_->GetStatusMessage());
}

TEST_F(ControllerTest, ScriptTimeoutWarning) {
  // Wait for #element to show up for will_never_match. After 10s, execute the
  // script on_timeout_error.
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "will_never_match")
      ->mutable_presentation()
      ->mutable_precondition()
      ->add_elements_exist()
      ->add_selectors("#element");
  script_response.mutable_script_timeout_error()->set_timeout_ms(4000);
  script_response.mutable_script_timeout_error()->set_script_path(
      "on_timeout_error");
  SetNextScriptResponse(script_response);

  // on_timeout_error displays an error message and terminates
  ActionsResponseProto on_timeout_error;
  on_timeout_error.add_actions()->mutable_tell()->set_message("This is slow");
  std::string on_timeout_error_str;
  on_timeout_error.SerializeToString(&on_timeout_error_str);
  EXPECT_CALL(*mock_service_,
              OnGetActions(StrEq("on_timeout_error"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(true, on_timeout_error_str));

  Start("http://a.example.com/path");

  // Warning after 4s, script succeeds and the client continues to wait.
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
    task_environment()->FastForwardBy(base::TimeDelta::FromSeconds(1));
  }
  EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
  EXPECT_EQ("This is slow", controller_->GetStatusMessage());
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
    task_environment()->FastForwardBy(base::TimeDelta::FromSeconds(1));
  }
}

TEST_F(ControllerTest, SuccessfulNavigation) {
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  NavigationStateChangeListener listener(controller_.get());
  controller_->AddListener(&listener);
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://initialurl.com"), web_contents()->GetMainFrame());
  controller_->RemoveListener(&listener);

  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  EXPECT_THAT(listener.events, ElementsAre(NavigationState{true, false},
                                           NavigationState{false, false}));
}

TEST_F(ControllerTest, FailedNavigation) {
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  NavigationStateChangeListener listener(controller_.get());
  controller_->AddListener(&listener);
  content::NavigationSimulator::NavigateAndFailFromDocument(
      GURL("http://initialurl.com"), net::ERR_CONNECTION_TIMED_OUT,
      web_contents()->GetMainFrame());
  controller_->RemoveListener(&listener);

  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_TRUE(controller_->HasNavigationError());

  EXPECT_THAT(listener.events, ElementsAre(NavigationState{true, false},
                                           NavigationState{false, true}));
}

TEST_F(ControllerTest, NavigationWithRedirects) {
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  NavigationStateChangeListener listener(controller_.get());
  controller_->AddListener(&listener);

  std::unique_ptr<content::NavigationSimulator> simulator =
      content::NavigationSimulator::CreateRendererInitiated(
          GURL("http://original.example.com/"), web_contents()->GetMainFrame());
  simulator->SetTransition(ui::PAGE_TRANSITION_LINK);
  simulator->Start();
  EXPECT_TRUE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  simulator->Redirect(GURL("http://redirect.example.com/"));
  EXPECT_TRUE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  simulator->Commit();
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  controller_->RemoveListener(&listener);

  // Redirection should not be reported as a state change.
  EXPECT_THAT(listener.events, ElementsAre(NavigationState{true, false},
                                           NavigationState{false, false}));
}

TEST_F(ControllerTest, EventuallySuccessfulNavigation) {
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  NavigationStateChangeListener listener(controller_.get());
  controller_->AddListener(&listener);
  content::NavigationSimulator::NavigateAndFailFromDocument(
      GURL("http://initialurl.com"), net::ERR_CONNECTION_TIMED_OUT,
      web_contents()->GetMainFrame());
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://initialurl.com"), web_contents()->GetMainFrame());
  controller_->RemoveListener(&listener);

  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  EXPECT_THAT(listener.events,
              ElementsAre(
                  // 1st navigation starts
                  NavigationState{true, false},
                  // 1st navigation fails
                  NavigationState{false, true},
                  // 2nd navigation starts, while in error state
                  NavigationState{true, true},
                  // 2nd navigation succeeds
                  NavigationState{false, false}));
}

TEST_F(ControllerTest, RemoveListener) {
  NavigationStateChangeListener listener(controller_.get());
  controller_->AddListener(&listener);
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://initialurl.com"), web_contents()->GetMainFrame());
  listener.events.clear();
  controller_->RemoveListener(&listener);

  content::NavigationSimulator::NavigateAndFailFromDocument(
      GURL("http://initialurl.com"), net::ERR_CONNECTION_TIMED_OUT,
      web_contents()->GetMainFrame());
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://initialurl.com"), web_contents()->GetMainFrame());

  EXPECT_THAT(listener.events, IsEmpty());
}

TEST_F(ControllerTest, WaitForNavigationActionTimesOut) {
  // A single script, with a wait_for_navigation action
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script");
  SetupScripts(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_expect_navigation();
  auto* action = actions_response.add_actions()->mutable_wait_for_navigation();
  action->set_timeout_ms(1000);
  SetupActionsForScript("script", actions_response);

  std::vector<ProcessedActionProto> processed_actions_capture;
  EXPECT_CALL(*mock_service_, OnGetNextActions(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&processed_actions_capture),
                      RunOnceCallback<4>(true, "")));

  Start("http://a.example.com/path");
  EXPECT_THAT(controller_->GetUserActions(), SizeIs(1));

  // Start script, which waits for some navigation event to happen after the
  // expect_navigation action has run..
  EXPECT_TRUE(controller_->PerformUserAction(0));

  // No navigation event happened within the action timeout and the script ends.
  EXPECT_THAT(processed_actions_capture, SizeIs(0));
  task_environment()->FastForwardBy(base::TimeDelta::FromSeconds(1));

  ASSERT_THAT(processed_actions_capture, SizeIs(2));
  EXPECT_EQ(ACTION_APPLIED, processed_actions_capture[0].status());
  EXPECT_EQ(TIMED_OUT, processed_actions_capture[1].status());
}

TEST_F(ControllerTest, WaitForNavigationActionStartWithinTimeout) {
  // A single script, with a wait_for_navigation action
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script");
  SetupScripts(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_expect_navigation();
  auto* action = actions_response.add_actions()->mutable_wait_for_navigation();
  action->set_timeout_ms(1000);
  SetupActionsForScript("script", actions_response);

  std::vector<ProcessedActionProto> processed_actions_capture;
  EXPECT_CALL(*mock_service_, OnGetNextActions(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&processed_actions_capture),
                      RunOnceCallback<4>(true, "")));

  Start("http://a.example.com/path");
  EXPECT_THAT(controller_->GetUserActions(), SizeIs(1));

  // Start script, which waits for some navigation event to happen after the
  // expect_navigation action has run..
  EXPECT_TRUE(controller_->PerformUserAction(0));

  // Navigation starts, but does not end, within the timeout.
  EXPECT_THAT(processed_actions_capture, SizeIs(0));
  std::unique_ptr<content::NavigationSimulator> simulator =
      content::NavigationSimulator::CreateRendererInitiated(
          GURL("http://a.example.com/path"), web_contents()->GetMainFrame());
  simulator->SetTransition(ui::PAGE_TRANSITION_LINK);
  simulator->Start();
  task_environment()->FastForwardBy(base::TimeDelta::FromSeconds(1));

  // Navigation finishes and the script ends.
  EXPECT_THAT(processed_actions_capture, SizeIs(0));
  simulator->Commit();

  ASSERT_THAT(processed_actions_capture, SizeIs(2));
  EXPECT_EQ(ACTION_APPLIED, processed_actions_capture[0].status());
  EXPECT_EQ(ACTION_APPLIED, processed_actions_capture[1].status());
}

TEST_F(ControllerTest, InitialDataUrlDoesNotChange) {
  const std::string deeplink_url("http://initialurl.com/path");
  Start(deeplink_url);
  EXPECT_THAT(controller_->GetDeeplinkURL(), deeplink_url);
  EXPECT_THAT(controller_->GetCurrentURL(), deeplink_url);

  const std::string navigate_url("http://navigateurl.com/path");
  SimulateNavigateToUrl(GURL(navigate_url));
  EXPECT_THAT(controller_->GetDeeplinkURL().spec(), deeplink_url);
  EXPECT_THAT(controller_->GetCurrentURL().spec(), navigate_url);
}

TEST_F(ControllerTest, Track) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  std::string response_str;
  script_response.SerializeToString(&response_str);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(true, response_str));

  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://b.example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(true, ""));

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(TriggerContext::CreateEmpty(), base::DoNothing());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));

  // Execute the script, which requires showing the UI, then go back to tracking
  // mode
  EXPECT_CALL(fake_client_, AttachUI());
  EXPECT_TRUE(controller_->PerformUserAction(0));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_THAT(controller_->GetUserActions(), SizeIs(1));

  // Move to a domain for which there are no scripts. This causes the controller
  // to stop.
  SimulateNavigateToUrl(GURL("http://b.example.com/"));
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::STOPPED));

  // Shutdown once we've moved from domain b.example.com, for which we know
  // there are no scripts, to c.example.com, which we don't want to check.
  EXPECT_CALL(fake_client_, Shutdown(Metrics::DropOutReason::NO_SCRIPTS));
  SimulateNavigateToUrl(GURL("http://c.example.com/"));
}

TEST_F(ControllerTest, TrackScriptWithNoUI) {
  // The UI is never shown during this test.
  EXPECT_CALL(fake_client_, AttachUI()).Times(0);

  SupportsScriptResponseProto script_response;
  auto* script = AddRunnableScript(&script_response, "runnable");
  script->mutable_presentation()->set_needs_ui(false);
  SetupScripts(script_response);

  // Script does nothing
  ActionsResponseProto runnable_script;
  auto* hidden_tell = runnable_script.add_actions()->mutable_tell();
  hidden_tell->set_message("optional message");
  hidden_tell->set_needs_ui(false);
  runnable_script.add_actions()->mutable_stop();
  SetupActionsForScript("runnable", runnable_script);

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(TriggerContext::CreateEmpty(), base::DoNothing());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));

  EXPECT_TRUE(controller_->PerformUserAction(0));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, TrackScriptShowUIOnTell) {
  SupportsScriptResponseProto script_response;
  auto* script = AddRunnableScript(&script_response, "runnable");
  script->mutable_presentation()->set_needs_ui(false);
  SetupScripts(script_response);

  ActionsResponseProto runnable_script;
  runnable_script.add_actions()->mutable_tell()->set_message("error");
  SetupActionsForScript("runnable", runnable_script);

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(TriggerContext::CreateEmpty(), base::DoNothing());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));

  EXPECT_FALSE(controller_->NeedsUI());
  EXPECT_CALL(fake_client_, AttachUI());
  EXPECT_TRUE(controller_->PerformUserAction(0));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  // As the controller is back in tracking mode; A UI is not needed anymore.
  EXPECT_FALSE(controller_->NeedsUI());

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, TrackScriptShowUIOnError) {
  SupportsScriptResponseProto script_response;
  auto* script = AddRunnableScript(&script_response, "runnable");
  script->mutable_presentation()->set_needs_ui(false);
  SetupScripts(script_response);

  // Running the script fails, due to a backend issue. The error message should
  // be shown.
  EXPECT_CALL(*mock_service_, OnGetActions(_, _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(false, ""));

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(TriggerContext::CreateEmpty(), base::DoNothing());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));

  EXPECT_FALSE(controller_->NeedsUI());
  EXPECT_CALL(fake_client_, AttachUI());
  EXPECT_TRUE(controller_->PerformUserAction(0));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  // As the controller is back in tracking mode; A UI is not needed anymore.
  EXPECT_FALSE(controller_->NeedsUI());

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::STOPPED,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, TrackContinuesAfterScriptError) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  std::string response_str;
  script_response.SerializeToString(&response_str);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(true, response_str));

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(TriggerContext::CreateEmpty(), base::DoNothing());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));

  EXPECT_CALL(*mock_service_, OnGetActions(StrEq("runnable"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(false, ""));

  // When the script fails, the controller transitions to STOPPED state, then
  // right away back to TRACKING state.
  EXPECT_TRUE(controller_->PerformUserAction(0));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_THAT(controller_->GetUserActions(), SizeIs(1));

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::STOPPED,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, TrackReportsFirstSetOfScripts) {
  Service::ResponseCallback get_scripts_callback;
  EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
      .WillOnce(
          Invoke([&get_scripts_callback](const GURL& url,
                                         const TriggerContext& trigger_context,
                                         Service::ResponseCallback& callback) {
            get_scripts_callback = std::move(callback);
          }));

  SetLastCommittedUrl(GURL("http://example.com/"));
  bool first_check_done = false;
  controller_->Track(TriggerContext::CreateEmpty(),
                     base::BindOnce(
                         [](Controller* controller, bool* is_done) {
                           // User actions must have been set when this is
                           // called
                           EXPECT_THAT(controller->GetUserActions(), SizeIs(1));
                           *is_done = true;
                         },
                         base::Unretained(controller_.get()),
                         base::Unretained(&first_check_done)));
  EXPECT_FALSE(first_check_done);

  ASSERT_TRUE(get_scripts_callback);

  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  std::string response_str;
  script_response.SerializeToString(&response_str);
  std::move(get_scripts_callback).Run(true, response_str);

  EXPECT_TRUE(first_check_done);
}

TEST_F(ControllerTest, TrackReportsNoScripts) {
  SetLastCommittedUrl(GURL("http://example.com/"));
  base::MockCallback<base::OnceCallback<void()>> callback;

  EXPECT_CALL(callback, Run());
  controller_->Track(TriggerContext::CreateEmpty(), callback.Get());
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
}

TEST_F(ControllerTest, TrackReportsNoScriptsForNow) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "no_match_yet")
      ->mutable_presentation()
      ->mutable_precondition()
      ->add_elements_exist()
      ->add_selectors("#element");
  SetNextScriptResponse(script_response);

  SetLastCommittedUrl(GURL("http://example.com/"));
  base::MockCallback<base::OnceCallback<void()>> callback;

  EXPECT_CALL(callback, Run());
  controller_->Track(TriggerContext::CreateEmpty(), callback.Get());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
}

TEST_F(ControllerTest, TrackReportsNoScriptsForThePage) {
  // Having scripts for the domain but not for the current page is fatal in
  // STARTING or PROMPT mode, but not in TRACKING mode.
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "no_match_yet")
      ->mutable_presentation()
      ->mutable_precondition()
      ->add_path_pattern("/otherpage.html");
  SetNextScriptResponse(script_response);

  SetLastCommittedUrl(GURL("http://example.com/"));
  base::MockCallback<base::OnceCallback<void()>> callback;

  EXPECT_CALL(callback, Run());
  controller_->Track(TriggerContext::CreateEmpty(), callback.Get());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
}

TEST_F(ControllerTest, TrackReportsAlreadyDone) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  SetNextScriptResponse(script_response);

  SetLastCommittedUrl(GURL("http://example.com/"));
  controller_->Track(TriggerContext::CreateEmpty(), base::DoNothing());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  base::MockCallback<base::OnceCallback<void()>> callback;
  EXPECT_CALL(callback, Run());
  controller_->Track(TriggerContext::CreateEmpty(), callback.Get());
}

TEST_F(ControllerTest, TrackThenAutostart) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  AddRunnableScript(&script_response, "autostart")
      ->mutable_presentation()
      ->set_autostart(true);
  SetNextScriptResponse(script_response);

  SetLastCommittedUrl(GURL("http://example.com/"));
  controller_->Track(TriggerContext::CreateEmpty(), base::DoNothing());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_THAT(controller_->GetUserActions(), SizeIs(1));

  EXPECT_CALL(*mock_service_, OnGetActions(StrEq("autostart"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(true, ""));

  ActionsResponseProto runnable_script;
  runnable_script.add_actions()->mutable_tell()->set_message("runnable");
  runnable_script.add_actions()->mutable_stop();
  SetupActionsForScript("runnable", runnable_script);

  EXPECT_CALL(fake_client_, AttachUI());
  Start("http://example.com/");
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());
  EXPECT_THAT(controller_->GetUserActions(), SizeIs(1));

  // Run "runnable", which then calls stop and ends. The controller should then
  // go back to TRACKING mode.
  controller_->PerformUserAction(0);
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, UnexpectedNavigationDuringPromptAction_Tracking) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  SetNextScriptResponse(script_response);

  ActionsResponseProto runnable_script;
  runnable_script.add_actions()
      ->mutable_prompt()
      ->add_choices()
      ->mutable_chip()
      ->set_text("continue");
  std::string never_shown = "never shown";
  runnable_script.add_actions()->mutable_tell()->set_message(never_shown);
  SetupActionsForScript("runnable", runnable_script);

  SetLastCommittedUrl(GURL("http://example.com/"));
  controller_->Track(TriggerContext::CreateEmpty(), base::DoNothing());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));
  EXPECT_EQ(controller_->GetUserActions()[0].chip().text, "runnable");

  // Start the script, which should show a prompt with the continue chip.
  controller_->PerformUserAction(0);
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));
  EXPECT_EQ(controller_->GetUserActions()[0].chip().text, "continue");

  // Browser (not document) initiated navigation while in prompt mode (such as
  // go back): The controller stops the scripts, shows an error, then goes back
  // to tracking mode.
  //
  // The tell never_shown which follows the prompt action should never be
  // executed.
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged(never_shown)).Times(0);
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged(testing::Not(never_shown)))
      .Times(testing::AnyNumber());

  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://example.com/otherpage"));

  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));
  EXPECT_EQ(controller_->GetUserActions()[0].chip().text, "runnable");

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT,
                                   AutofillAssistantState::STOPPED,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, UnexpectedNavigationDuringPromptAction) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "autostart")
      ->mutable_presentation()
      ->set_autostart(true);
  SetNextScriptResponse(script_response);

  ActionsResponseProto autostart_script;
  autostart_script.add_actions()
      ->mutable_prompt()
      ->add_choices()
      ->mutable_chip()
      ->set_text("continue");
  std::string never_shown = "never shown";
  autostart_script.add_actions()->mutable_tell()->set_message(never_shown);
  SetupActionsForScript("autostart", autostart_script);

  Start();
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));
  EXPECT_EQ(controller_->GetUserActions()[0].chip().text, "continue");

  // Browser (not document) initiated navigation while in prompt mode (such as
  // go back): The controller stops the scripts, shows an error and shuts down.
  //
  // The tell never_shown which follows the prompt action should never be
  // executed.
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged(never_shown)).Times(0);
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged(testing::Not(never_shown)))
      .Times(testing::AnyNumber());

  EXPECT_CALL(fake_client_, Shutdown(Metrics::DropOutReason::NAVIGATION));
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://example.com/otherpage"));

  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT,
                                   AutofillAssistantState::STOPPED));
}

TEST_F(ControllerTest, UserDataFormEmpty) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  auto user_data = std::make_unique<UserData>();

  // Request nothing, expect continue button to be enabled.
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  EXPECT_CALL(mock_observer_, OnCollectUserDataOptionsChanged(Not(nullptr)))
      .Times(1);
  EXPECT_CALL(mock_observer_, OnUserDataChanged(Not(nullptr))).Times(1);
  controller_->SetCollectUserDataOptions(std::move(options),
                                         std::move(user_data));
}

TEST_F(ControllerTest, UserDataFormContactInfo) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  auto user_data = std::make_unique<UserData>();

  options->request_payer_name = true;
  options->request_payer_email = true;
  options->request_payer_phone = true;

  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(std::move(options),
                                         std::move(user_data));

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);

  autofill::AutofillProfile contact_profile;
  contact_profile.SetRawInfo(autofill::ServerFieldType::EMAIL_ADDRESS,
                             base::UTF8ToUTF16("joedoe@example.com"));
  contact_profile.SetRawInfo(autofill::ServerFieldType::NAME_FULL,
                             base::UTF8ToUTF16("Joe Doe"));
  contact_profile.SetRawInfo(autofill::ServerFieldType::PHONE_HOME_WHOLE_NUMBER,
                             base::UTF8ToUTF16("+1 23 456 789 01"));
  controller_->SetContactInfo(
      std::make_unique<autofill::AutofillProfile>(contact_profile));
  EXPECT_THAT(
      controller_->GetUserData()->contact_profile->Compare(contact_profile),
      Eq(0));
}

TEST_F(ControllerTest, UserDataFormCreditCard) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  auto user_data = std::make_unique<UserData>();

  options->request_payment_method = true;
  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(std::move(options),
                                         std::move(user_data));

  // Credit card without billing address is invalid.
  auto credit_card = std::make_unique<autofill::CreditCard>(
      base::GenerateGUID(), "https://www.example.com");
  autofill::test::SetCreditCardInfo(credit_card.get(), "Marion Mitchell",
                                    "4111 1111 1111 1111", "01", "2020",
                                    /* billing_address_id = */ "");
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCreditCard(
      std::make_unique<autofill::CreditCard>(*credit_card));

  // Credit card with valid billing address is ok.
  auto billing_address = std::make_unique<autofill::AutofillProfile>(
      base::GenerateGUID(), "https://www.example.com");
  autofill::test::SetProfileInfo(billing_address.get(), "Marion", "Mitchell",
                                 "Morrison", "marion@me.xyz", "Fox",
                                 "123 Zoo St.", "unit 5", "Hollywood", "CA",
                                 "91601", "US", "16505678910");
  credit_card->set_billing_address_id(billing_address->guid());
  ON_CALL(*fake_client_.GetPersonalDataManager(),
          GetProfileByGUID(billing_address->guid()))
      .WillByDefault(Return(billing_address.get()));
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  controller_->SetCreditCard(
      std::make_unique<autofill::CreditCard>(*credit_card));
  EXPECT_THAT(controller_->GetUserData()->card->Compare(*credit_card), Eq(0));
}

TEST_F(ControllerTest, SetTermsAndConditions) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  auto user_data = std::make_unique<UserData>();

  options->accept_terms_and_conditions_text.assign("Accept T&C");
  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(std::move(options),
                                         std::move(user_data));

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  controller_->SetTermsAndConditions(TermsAndConditionsState::ACCEPTED);
  EXPECT_THAT(controller_->GetUserData()->terms_and_conditions,
              Eq(TermsAndConditionsState::ACCEPTED));
}

TEST_F(ControllerTest, SetLoginOption) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  auto user_data = std::make_unique<UserData>();

  options->request_login_choice = true;
  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(std::move(options),
                                         std::move(user_data));

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  controller_->SetLoginOption("1");
  EXPECT_THAT(controller_->GetUserData()->login_choice_identifier, Eq("1"));
}

TEST_F(ControllerTest, SetShippingAddress) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  auto user_data = std::make_unique<UserData>();

  options->request_shipping = true;
  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(std::move(options),
                                         std::move(user_data));

  auto shipping_address = std::make_unique<autofill::AutofillProfile>(
      base::GenerateGUID(), "https://www.example.com");
  autofill::test::SetProfileInfo(shipping_address.get(), "Marion", "Mitchell",
                                 "Morrison", "marion@me.xyz", "Fox",
                                 "123 Zoo St.", "unit 5", "Hollywood", "CA",
                                 "91601", "US", "16505678910");
  ON_CALL(*fake_client_.GetPersonalDataManager(),
          GetProfileByGUID(shipping_address->guid()))
      .WillByDefault(Return(shipping_address.get()));
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  controller_->SetShippingAddress(
      std::make_unique<autofill::AutofillProfile>(*shipping_address));
  EXPECT_THAT(
      controller_->GetUserData()->shipping_address->Compare(*shipping_address),
      Eq(0));
}

}  // namespace autofill_assistant
