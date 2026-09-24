#include "rest_dialogue_policy.h"
#include "rest_dialogue_records.h"
#include <array>
#include <chrono>
#include <iostream>
#include <unordered_set>

int main(int argc, char** argv)
{
    using namespace Wayfarer::RestDialogue;
    int failures{};
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            std::cerr << message << '\n';
            ++failures;
        }
    };
    check(ManagementSource("nwsFollowerFramework.esp", ""), "NFF origin is excluded");
    check(ManagementSource("CompatibilityPatch.esp", "nwsFollowerDialogue"), "NFF patches stay excluded");
    check(ManagementSource("Skyrim.esm", "DialogueFollower"), "Vanilla follower commands excluded");
    check(!ManagementSource("UnknownNewCompanion.esp", "MyFollowerQuest"), "No follower allowlist");
    check(!ManagementSource("Lucien.esp", "JRLucienFollower"), "Follower quest is not all commands");
    const std::array commands{"I need to trade some things with you.",
                              "Follow me. I need your help.",
                              "Wait here.",
                              "It's time for us to part ways.",
                              "I need you to do something.",
                              "Come on. Let's go.",
                              "Show me your inventory.",
                              "Change your outfit.",
                              "[NFF] Let's talk.",
                              "$FF_SayImport",
                              "[Trade]",
                              "Formation",
                              "Never mind.",
                              "Open your inventory!",
                              "Could you follow closer please?",
                              "About how closely you follow me...",
                              "Do you want to relax here for a bit?",
                              "Let's talk about your combat style.",
                              "I want you to switch to a bow.",
                              "I want you to switch to a sword.",
                              "Please, switch to your bound sword.",
                              "I'd like you to use a crossbow.",
                              "Alas, it's time for us to part ways.",
                              "Alas, it\xE2\x80\x99s time for us to part ways.",
                              "Alas, it\x92s time for us to part ways.",
                              "Wait here for a moment.",
                              "I have some errands to do. Wait for me in Riften.",
                              "I need to visit the market. Wait for me in Whiterun.",
                              "I have business here; wait for me at the inn.",
                              "I'll be a while... Please, wait for me outside the gate.",
                              "We can talk later. Head home, I'll catch up.",
                              "There's something I must do. Go back to the camp.",
                              "Let's split up. Meet me at the inn.",
                              "Before we leave. Open your inventory.",
                              "I'd like to create an outfit for you.",
                              "I need a moment of your time. (NFF)"};
    for (const auto* prompt : commands) {
        check(ManagementTopic("CustomTopic01", prompt), prompt);
    }
    const std::array conversations{"Can you tell me about yourself?",
                                   "How did you learn your trade?",
                                   "Wait. Do you know me?",
                                   "Why were you waiting for me?",
                                   "What happened to your sister?",
                                   "What do you think of this place?",
                                   "Do you know much about the College of Winterhold?",
                                   "I found the equipment you lost.",
                                   "What happened to your old formation?",
                                   "How are you feeling?",
                                   "Any news?",
                                   "Why do you use bound weapons?",
                                   "Who taught you to use a bow?",
                                   "Alas, what happened to your family?",
                                   "I'd like to hear about your old outfit.",
                                   "When did you become an adventurer?",
                                   "As a High Elf, you must have an opinion on the Thalmor.",
                                   "Why do they call you Skjel the Gray?",
                                   "A court wizard?",
                                   "What exactly are you doing in here?",
                                   "I have errands in Riften. What do you think of the city?",
                                   "I will wait for you in Riften. What happened to your family?",
                                   "Why did you wait for me in Riften?",
                                   "Wait for me to explain what happened.",
                                   "Nothing. What were you saying?",
                                   "He said, 'Listen. Wait for me in Riften.' Why?",
                                   "What did he say? \"Wait here,\" you said. Why?",
                                   "He told me \xE2\x80\x9CListen. Wait for me in Riften.\xE2\x80\x9D Why?",
                                   "He told me \xE2\x80\x98Listen. Wait for me in Riften.\xE2\x80\x99 Why?",
                                   "I want you to switch to another subject."};
    for (const auto* prompt : conversations) {
        check(!ManagementTopic("FollowerPersonalTopic01", prompt), prompt);
    }
    check(ManagementTopic("HLIORemiFollowerTradea1", "Objekte tauschen"), "Translated trade uses record ID");
    check(ManagementTopic("JRLucienFollowerDismissGoodbye", "Au revoir"),
          "Translated dismissal uses record ID");
    check(!ManagementTopic("InigofollowerDialogueWaitDoyouknowme", "Wait. Do you know me?"),
          "Inigo introduction is not a wait command");
    // Observed runtime regression: Rumarin topic 25327CE2 / INFO 25327CE3
    // belongs to his personal quest, with the command in the INFO's RNAM.
    check(ManagementTopic("DialogueRumarinBoundBowTopic", "I want you to switch to a bow."),
          "Rumarin bound-bow command must not produce a rest chat icon");
    // Skjel's action lives in a personal quest, has a neutral editor ID, and
    // gets a longer voiced response. None of those makes it a new conversation.
    check(ManagementTopic("Skjel1MeetTopic", "I have some errands to do. Wait for me in Riften."),
          "Skjel's second-sentence wait instruction must not trigger an icon");
    check(ConversationResponse(ResponseContent("All right. I'll stand guard until you find the motivation."),
                               true),
          "Long replies alone do not identify commands; the prompt classifier must veto this topic");
    // M'rissi's screenshot: these are commands even with follower-specific phrasing.
    check(ManagementTopic("MRTDialogueFollowerHomeSetHomeTopic", "This is our new home."),
          "Home relocation is not a conversation");
    check(ManagementTopic("MRTDialogueFollowerDismissTopic", "Head home, I'll catch up."),
          "Going home is not a conversation");
    check(ManagementTopic("MRTDialogueFollowerWaitTopic", "Wait for me here. I'll be back in a moment."),
          "Waiting is not a conversation");
    check(ManagementTopic("MRTDialogueFollowerTradeDirectTopic", "Open your satchel."),
          "Opening a satchel is not a conversation");

    for (auto placeholder : {"", "...", " \t\r\n", "\xE2\x80\xA6", "[Silence]"}) {
        check(ResponseContent(placeholder) == SpokenContent::None, "Placeholder is not spoken content");
    }
    check(!ConversationResponse(SpokenContent::None, false), "Empty unscripted INFO cannot alert");
    check(!ConversationResponse(SpokenContent::None, true), "Silent scripted action cannot alert");
    check(!ConversationResponse(ResponseContent("Okay."), true),
          "Scripted action acknowledgement cannot alert, regardless of command wording");
    check(!ConversationResponse(ResponseContent("As you wish!"), true),
          "Scripted confirmation is not new conversation content");
    check(ConversationResponse(ResponseContent("Yes."), false), "Ordinary short dialogue is preserved");
    check(ConversationResponse(ResponseContent("I discovered where your sister went."), true),
          "Real quest dialogue stays eligible even when a script advances the quest");
    check(ConversationResponse(ResponseContent("My parents were travelling performers."), false),
          "Personal conversation remains eligible");

    // Exercise traversal using the same decision as the incremental runtime
    // scanner. Hearing a parent must not turn an unchosen nested reply into
    // a new top-level conversation; other newly offered roots still count.
    struct Candidate
    {
        InfoMetadata definition;
        bool heard{};
    };
    auto findNewConversation = [](const std::unordered_map<std::uint32_t, Candidate>& graph,
                                  std::vector<std::uint32_t> roots) {
        std::unordered_set<std::uint32_t> visited;
        while (!roots.empty()) {
            const auto id = roots.back();
            roots.pop_back();
            const auto found = graph.find(id);
            if (found == graph.end() || !visited.insert(id).second) {
                continue;
            }
            const auto& [info, heard] = found->second;
            const bool eligible =
                !ManagementTopic("", info.prompt) && ConversationResponse(info.content, info.scripted);
            switch (NextResponseAction(info.valid, heard, eligible, true)) {
            case ResponseAction::Notify:
                return id;
            case ResponseAction::FollowLinks:
                roots.insert(roots.end(), info.links.begin(), info.links.end());
                break;
            case ResponseAction::Ignore:
                break;
            }
        }
        return std::uint32_t{};
    };
    InfoMetadata heardParent, unusedReply;
    heardParent.valid = unusedReply.valid = true;
    heardParent.content = unusedReply.content = SpokenContent::Conversation;
    heardParent.prompt = "What about in general?";
    unusedReply.prompt = "Yours look like whiskers.";
    heardParent.links = {2};
    std::unordered_map<std::uint32_t, Candidate> choices{{1, {heardParent, true}}, {2, {unusedReply, false}}};
    check(findNewConversation(choices, {1}) == 0, "A grey/heard root cannot alert for an unused follow-up");
    choices.at(1).heard = false;
    check(findNewConversation(choices, {1}) == 1, "The same conversation still alerts before it is heard");
    choices.at(1).heard = true;
    check(findNewConversation(choices, {2, 1}) == 2,
          "A newly offered independent root remains eligible beside an exhausted topic");
    choices.at(1).heard = false;
    choices.at(1).definition.content = SpokenContent::None;
    check(findNewConversation(choices, {1}) == 2, "An unheard silent hub can still lead to real speech");
    choices.at(1).heard = true;
    check(findNewConversation(choices, {1}) == 0, "A heard hub stops traversal too");
    choices.at(1).heard = false;
    choices.at(1).definition.valid = false;
    check(findNewConversation(choices, {1}) == 0, "Missing parent metadata cannot expose child topics");

    struct Item
    {
        Item* next{};
        struct
        {
            struct
            {
                bool isOR{};
            } flags;
        } data;
        bool value{};
    };
    auto evaluate = [](const Item& item) { return item.value; };
    Item a, b, c;
    a.next = &b;
    b.next = &c;
    check(ConditionsPass<Item>(nullptr, evaluate), "Empty conditions pass");
    a.data.flags.isOR = true;
    a.value = false;
    b.value = true;
    c.value = true;
    check(ConditionsPass(&a, evaluate), "(false OR true) AND true");
    c.value = false;
    check(!ConditionsPass(&a, evaluate), "OR group does not swallow following AND");
    a.value = b.value = false;
    c.value = true;
    check(!ConditionsPass(&a, evaluate), "Failed OR group rejects");
    a.value = true;
    b.value = false;
    check(ConditionsPass(&a, evaluate), "True first OR alternative short circuits");
    a.data.flags.isOR = false;
    check(!ConditionsPass(&a, evaluate), "Separate AND conditions all required");

    std::vector<std::uint8_t> record;
    auto subrecord = [&](std::string_view type, const std::vector<std::uint8_t>& bytes) {
        record.insert(record.end(), type.begin(), type.end());
        record.push_back(static_cast<std::uint8_t>(bytes.size()));
        record.push_back(static_cast<std::uint8_t>(bytes.size() >> 8));
        record.insert(record.end(), bytes.begin(), bytes.end());
    };
    subrecord("TCLT", {0x34, 0x12, 0, 1});
    subrecord("TCLT", {0x78, 0x56, 0, 1});
    subrecord("ANAM", {0x12, 0, 0, 2});
    subrecord("RNAM", {'T', 'r', 'a', 'd', 'e', 0});
    const auto result = ParseInfoMetadata(record, false);
    check(result.valid && result.links == std::vector<std::uint32_t>{0x01001234, 0x01005678},
          "Only explicit outgoing links are traversed");
    check(result.speaker == 0x02000012 && result.hasPrompt && result.prompt == "Trade",
          "Speaker restriction and INFO prompt override preserved");
    check(!ParseInfoMetadata(record, true).hasPrompt, "Localized string IDs are never treated as words");
    record.pop_back();
    check(!ParseInfoMetadata(record, false).valid, "Truncated record fails closed");
    record.clear();
    subrecord("XXXX", {4, 0, 0, 0});
    subrecord("TCLT", {1, 2, 3, 4});
    check(ParseInfoMetadata(record, false).valid, "Extended subrecord size is supported");
    record.push_back(0);
    check(!ParseInfoMetadata(record, false).valid, "Partial subrecord header fails closed");

    // BSAssets.esm aaaTopic / INFO 016025E9 was the actual source of M'rissi's
    // false alert. No NAM1, shared response or links: the engine's "..." prompt
    // must not turn this empty internal record into conversation content.
    record.clear();
    subrecord("ENAM", {0, 0, 0, 0});
    subrecord("PNAM", {0, 0, 0, 0});
    subrecord("CNAM", {0});
    auto emptyInfo = ParseInfoMetadata(record, false);
    check(emptyInfo.valid && emptyInfo.content == SpokenContent::None &&
              !ConversationResponse(emptyInfo.content, emptyInfo.scripted),
          "BSAssets empty INFO regression: no chat icon");
    // Minimal attached script; a command prompt/identifier is deliberately absent.
    subrecord("VMAD", {5, 0, 2, 0, 1, 0, 1, 0, 'S', 0, 0, 0});
    subrecord("NAM1", {'O', 'k', 'a', 'y', '.', 0});
    auto actionInfo = ParseInfoMetadata(record, false);
    check(actionInfo.valid && actionInfo.scripted &&
              !ConversationResponse(actionInfo.content, actionInfo.scripted),
          "Response-based action exclusion works independently of prompt and editor ID");
    subrecord("NAM1", {'I', ' ', 'f', 'o', 'u', 'n', 'd', ' ', 'h', 'e', 'r', '.', 0});
    auto storyInfo = ParseInfoMetadata(record, false);
    check(storyInfo.valid && ConversationResponse(storyInfo.content, storyInfo.scripted),
          "Every response line is considered; an initial Okay does not hide real dialogue");
    record.clear();
    subrecord("NAM1", {0x12, 0x34, 0, 0});
    check(ParseInfoMetadata(record, true).content == SpokenContent::Conversation,
          "Localized response IDs preserve dialogue without reading ID bytes as text");
    record.clear();
    subrecord("NAM1", {0, 0, 0, 0});
    check(ParseInfoMetadata(record, true).content == SpokenContent::None,
          "Null localized response IDs are empty");
    record.pop_back();
    check(!ParseInfoMetadata(record, true).valid, "Truncated localized response fails closed");
    record.clear();
    subrecord("VMAD", {5, 0, 2, 0, 0, 0, 2, 0, 0, 0});
    check(ParseInfoMetadata(record, false).valid && !ParseInfoMetadata(record, false).scripted,
          "Empty VMAD fragment scaffolding does not imply an action script");
    record.clear();
    subrecord("VMAD", {5, 0, 2});
    check(!ParseInfoMetadata(record, false).valid, "Truncated VMAD header fails closed");

    std::vector<std::uint8_t> compressed{22, 0,  0,  0,   120, 156, 11,  113, 246, 9,  97, 97,
                                         48, 17, 98, 96,  12,  242, 115, 244, 101, 99, 8,  41,
                                         74, 76, 73, 101, 0,   0,   49,  233, 4,   167};
    auto inflated = InflateInfoRecord(compressed);
    check(inflated.size() == 22 && ParseInfoMetadata(inflated, false).prompt == "Trade",
          "Compressed INFO records preserve links and prompt overrides");
    compressed.back() ^= 1;
    check(InflateInfoRecord(compressed).empty(), "Bad zlib checksum rejected");
    compressed.back() ^= 1;
    compressed[0] = 23;
    check(InflateInfoRecord(compressed).empty(), "Wrong decompressed length rejected");
    compressed[0] = 22;

    std::vector<std::uint8_t> file;
    auto u32 = [&](std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) {
            file.push_back(static_cast<std::uint8_t>(value >> (8 * byte)));
        }
    };
    auto signature = [&](std::string_view value) { file.insert(file.end(), value.begin(), value.end()); };
    signature("GRUP");
    u32(48 + static_cast<std::uint32_t>(compressed.size()));
    signature("DIAL");
    u32(0);
    u32(0);
    u32(0);
    signature("INFO");
    u32(static_cast<std::uint32_t>(compressed.size()));
    u32(0x40000);
    u32(0x01000800);
    u32(0);
    u32(0);
    file.insert(file.end(), compressed.begin(), compressed.end());
    const auto path = std::filesystem::temp_directory_path() /
                      ("wwm-dialogue-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".esp");
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(file.data()), file.size());
    }
    const auto records = ReadInfoRecords(path, false);
    check(records.size() == 1 && records.at(0x01000800).prompt == "Trade",
          "Real group/record traversal decodes compressed INFO");
    std::filesystem::remove(path);
    for (int arg = 1; arg < argc; ++arg) {
        const auto actual = ReadInfoRecords(argv[arg], false);
        std::size_t links{}, empty{}, confirmations{};
        for (const auto& [id, info] : actual) {
            static_cast<void>(id);
            links += info.links.size();
            empty += info.content == SpokenContent::None;
            confirmations += info.scripted && info.content == SpokenContent::Acknowledgement;
        }
        check(!actual.empty(), "Installed follower records parsed");
        std::cout << std::filesystem::path(argv[arg]).filename().string() << ": " << actual.size()
                  << " INFO records, " << links << " outgoing links, " << empty
                  << " without local speech (may use shared responses), " << confirmations
                  << " scripted acknowledgements\n";
        if (std::filesystem::path(argv[arg]).filename() == "BSAssets.esm") {
            const auto& info = actual.at(0x016025E9);
            check(!ConversationResponse(info.content, info.scripted),
                  "Installed BSAssets empty topic must not trigger M'rissi's icon");
        }
        if (std::filesystem::path(argv[arg]).filename() == "3DNPC.esp") {
            const auto& command = actual.at(0x05327CE3);
            check(!ConversationResponse(command.content, command.scripted),
                  "Installed Rumarin bow action filtered without using its prompt");
            const auto& personal = actual.at(0x05219624);
            check(ConversationResponse(personal.content, personal.scripted),
                  "Installed Rumarin story about his parents is still conversation");
            const auto& skjelWait = actual.at(0x052BD12E);
            check(skjelWait.valid && skjelWait.scripted &&
                      ManagementTopic("Skjel1MeetTopic", skjelWait.prompt),
                  "Installed Skjel wait-in-Riften response cannot trigger a rest icon");
            const auto& skjelJoke = actual.at(0x052C40AC);
            check(skjelJoke.valid && !ManagementTopic("", skjelJoke.prompt) &&
                      ConversationResponse(skjelJoke.content, skjelJoke.scripted),
                  "Installed Skjel knock-knock conversation stays eligible");
            const auto& warPaint = actual.at(0x05219623);
            const auto& whiskers = actual.at(0x050AC1FA);
            check(std::find(warPaint.links.begin(), warPaint.links.end(), 0x050AC1F9) != warPaint.links.end(),
                  "Installed Rumarin war-paint topic links to the observed whiskers reply");
            std::unordered_map<std::uint32_t, Candidate> rumarin{{0x050ABC81, {warPaint, true}},
                                                                 {0x050AC1F9, {whiskers, false}}};
            check(findNewConversation(rumarin, {0x050ABC81}) == 0,
                  "Installed Rumarin graph: heard war-paint topic cannot keep the icon alive");
            rumarin.at(0x050ABC81).heard = false;
            check(findNewConversation(rumarin, {0x050ABC81}) == 0x050ABC81,
                  "Installed Rumarin graph: new war-paint conversation remains eligible");
        }
    }
    return failures ? 1 : 0;
}
