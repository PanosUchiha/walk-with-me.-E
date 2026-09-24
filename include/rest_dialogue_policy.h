#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Wayfarer::RestDialogue
{
    enum class SpokenContent
    {
        None,
        Acknowledgement,
        Conversation
    };

    enum class ResponseAction
    {
        Ignore,
        Notify,
        FollowLinks
    };

    inline ResponseAction NextResponseAction(bool known, bool heard, bool eligible, bool linksAllowed)
    {
        // "New conversation" is based on the currently offered response. A
        // heard/grey topic must not act as a gateway to unused alternative
        // replies. Newly selected INFOs and newly offered roots remain eligible.
        if (!known || heard) {
            return ResponseAction::Ignore;
        }
        if (eligible) {
            return ResponseAction::Notify;
        }
        return linksAllowed ? ResponseAction::FollowLinks : ResponseAction::Ignore;
    }

    inline std::string Lower(std::string_view text)
    {
        std::string result(text);
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return result;
    }

    inline SpokenContent ResponseContent(std::string_view response)
    {
        auto text = Lower(response);
        // Empty/menu-only records and punctuation are not spoken conversations.
        // UTF-8 typography must not count as letters just because its bytes are >127.
        for (const std::string_view punctuation :
             {"\xE2\x80\xA6", "\xE2\x80\x94", "\xE2\x80\x93", "\xE2\x80\x98", "\xE2\x80\x99", "\xE2\x80\x9C",
              "\xE2\x80\x9D", "\xC2\xA0"}) {
            for (auto at = text.find(punctuation); at != std::string::npos; at = text.find(punctuation)) {
                text.replace(at, punctuation.size(), " ");
            }
        }
        std::string words;
        for (unsigned char ch : text) {
            if (std::isalnum(ch) || ch >= 128) {
                words += static_cast<char>(ch);
            }
            else if (!words.empty() && words.back() != ' ') {
                words += ' ';
            }
        }
        while (!words.empty() && words.back() == ' ') {
            words.pop_back();
        }
        if (words.empty() || words == "silent" || words == "silence") {
            return SpokenContent::None;
        }
        for (auto acknowledgement : {"okay", "ok", "yes", "no", "sure", "fine", "all right", "alright",
                                     "very well", "of course", "as you wish", "certainly"}) {
            if (words == acknowledgement) {
                return SpokenContent::Acknowledgement;
            }
        }
        return SpokenContent::Conversation;
    }

    inline bool ConversationResponse(SpokenContent content, bool scripted)
    {
        // An attached script plus only "Okay"/"Yes" is an action confirmation,
        // not new conversational content. Scripts with real speech stay eligible:
        // quest updates routinely advance stages as part of a conversation.
        return content == SpokenContent::Conversation ||
               (content == SpokenContent::Acknowledgement && !scripted);
    }

    inline bool ManagementSource(std::string_view plugin, std::string_view quest)
    {
        const auto file = Lower(plugin), id = Lower(quest);
        // Check the originating quest as well as the topic: patches may replace a
        // framework's records without changing their purpose. Custom follower mods
        // are deliberately NOT an allowlist.
        for (auto name : {"nwsfollowerframework.esp", "wayfarer.esp", "amazingfollowertweaks.esp",
                          "effcore.esm", "effdialogue.esp", "followerlivingspace.esp"}) {
            if (file == name) {
                return true;
            }
        }
        return id == "dialoguefollower" || id.starts_with("nwsfollower") || id.starts_with("nwsdialogue") ||
               id.starts_with("nwsff") || id.starts_with("wayfarer") || id.starts_with("tweakfollower") ||
               id.starts_with("effdialogue");
    }

    inline bool ManagementPrompt(std::string_view prompt, bool allowLabel = true)
    {
        const auto start = prompt.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) {
            return false;
        }
        prompt.remove_prefix(start);
        if (prompt.front() == '"' || prompt.front() == '\'') {
            return false; // Quoted/reported instructions are not player commands.
        }
        auto text = Lower(prompt);
        for (const std::string_view apostrophe : {"\xE2\x80\x98", "\xE2\x80\x99"}) {
            for (auto at = text.find(apostrophe); at != std::string::npos;
                 at = text.find(apostrophe, at + 1)) {
                text.replace(at, apostrophe.size(), "'");
            }
        }
        // Punctuation-insensitive, whole phrases (never a bare 'trade' or 'wait').
        std::string words;
        for (unsigned char ch : text) {
            if (ch == '\'' || ch == 0x92) {
                continue;
            }
            if (std::isalnum(ch) || ch >= 128) {
                words += static_cast<char>(ch);
            }
            else if (!words.empty() && words.back() != ' ') {
                words += ' ';
            }
        }
        while (!words.empty() && words.back() == ' ') {
            words.pop_back();
        }
        // Followers often personalize commands with a short preamble. Strip
        // only these discourse words, not arbitrary text before a keyword.
        for (auto preamble : {"alas ", "well ", "okay ", "alright ", "all right ", "please "}) {
            if (words.starts_with(preamble)) {
                words.erase(0, std::string_view(preamble).size());
                break;
            }
        }
        // Equipment directives can live in an otherwise personal quest (for
        // example Rumarin's bound-weapon choices). Classify the request and its
        // object together so a story about bows or swords is still eligible.
        for (const std::string_view request : {"i want you to switch to ", "id like you to switch to ",
                                               "switch to ", "i want you to use ", "id like you to use "}) {
            if (!words.starts_with(request)) {
                continue;
            }
            auto target = std::string_view(words).substr(request.size());
            for (const std::string_view article : {"a ", "an ", "the ", "your "}) {
                if (target.starts_with(article)) {
                    target.remove_prefix(article.size());
                    break;
                }
            }
            for (const std::string_view equipment :
                 {"bow", "sword", "axe", "mace", "dagger", "staff", "crossbow", "shield", "bound", "melee",
                  "ranged", "magic", "spells", "unarmed", "dual wielding"}) {
                if (target == equipment ||
                    (target.starts_with(equipment) && target.size() > equipment.size() &&
                     target[equipment.size()] == ' ')) {
                    return true;
                }
            }
        }
        for (auto phrase : {"i need to trade",
                            "id like to trade",
                            "lets trade",
                            "trade with me",
                            "show me your inventory",
                            "let me see your inventory",
                            "open your inventory",
                            "i need you to carry",
                            "what have you got for sale",
                            "what do you have for sale",
                            "follow me",
                            "wait here",
                            "wait for me here",
                            "wait for me there",
                            "wait for me in",
                            "wait for me at",
                            "wait for me outside",
                            "wait in",
                            "wait at",
                            "meet me in",
                            "meet me at",
                            "head home",
                            "go home",
                            "go back to",
                            "head back to",
                            "return home",
                            "return to your home",
                            "stay here",
                            "come on lets go",
                            "lets get going",
                            "its time for us to part ways",
                            "i need you to do something",
                            "time for you to go",
                            "change your outfit",
                            "lets change your outfit",
                            "id like to create an outfit for you",
                            "id like to change your outfit",
                            "let me create an outfit for you",
                            "lets create an outfit for you",
                            "set your home",
                            "this is your new home",
                            "change your combat style",
                            "change formation",
                            "relax here",
                            "stop following me",
                            "dismiss follower",
                            "could you follow closer",
                            "could you follow further",
                            "follow closer",
                            "follow further",
                            "about how closely you follow",
                            "lets talk about your combat style",
                            "lets talk about your tactics",
                            "lets talk about your equipment",
                            "do you want to relax here",
                            "lets go ahead and relax",
                            "feel free to relax",
                            "put your torch away",
                            "draw your weapon",
                            "sheathe your weapon",
                            "id like to see your additional follower inventory"}) {
            const std::string_view value(phrase);
            if (words == value ||
                (words.starts_with(value) && words.size() > value.size() && words[value.size()] == ' ')) {
                return true;
            }
        }
        for (auto exact : {"trade", "inventory", "equipment", "outfit", "outfits", "formation", "formations",
                           "settings", "commands", "wait", "dismiss", "lets go", "time to go", "never mind",
                           "nevermind", "goodbye", "nothing", "back"}) {
            if (allowLabel && words == exact) {
                return true;
            }
        }
        return text.find("[nff]") != std::string::npos || text.find("(nff)") != std::string::npos ||
               text.starts_with("$ff_") || text.find("[trade]") != std::string::npos ||
               text.find("[formation]") != std::string::npos;
    }

    inline bool ManagementTopic(std::string_view editorID, std::string_view prompt)
    {
        const auto id = Lower(editorID);
        // Compound identifiers catch translated commands without matching a
        // story merely because its ID mentions waiting or trading.
        for (auto fragment :
             {"followertrade",   "followerdismiss",     "followerwait",       "followerfollow",
              "followerrecruit", "followerdosomething", "followerdoingfavor", "followerfavour",
              "followerfavor",   "combatstyle",         "setoutfit",          "changeoutfit",
              "sethome",         "setformation",        "formationcommand",   "inventorycommand",
              "sandboxcommand",  "followertactics",     "followerrelax",      "controllertrade",
              "controllerwait",  "controllerfollow",    "controllerdismiss"}) {
            if (id.find(fragment) != std::string::npos) {
                return true;
            }
        }
        std::string text(prompt);
        // Normalize quotation marks before finding sentence boundaries so
        // quoted speech stays intact, including punctuation inside the quote.
        for (const std::string_view quote : {"\xE2\x80\x98", "\xE2\x80\x99", "\x91", "\x92"}) {
            for (auto at = text.find(quote); at != std::string::npos; at = text.find(quote)) {
                text.replace(at, quote.size(), "'");
            }
        }
        for (const std::string_view quote : {"\xE2\x80\x9C", "\xE2\x80\x9D", "\x93", "\x94"}) {
            for (auto at = text.find(quote); at != std::string::npos; at = text.find(quote)) {
                text.replace(at, quote.size(), "\"");
            }
        }
        if (ManagementPrompt(text)) {
            return true;
        }
        // Personal quests may embed a real instruction after conversational
        // setup: "I have errands to do. Wait for me in <town>." Match directive
        // phrases at sentence starts, never arbitrary mentions inside a story.
        std::size_t start{};
        char quote{};
        auto word = [](unsigned char ch) { return std::isalnum(ch) || ch >= 128; };
        for (std::size_t i = 0; i <= text.size(); ++i) {
            const char ch = i < text.size() ? text[i] : '\0';
            if (quote) {
                if (ch == quote && (quote == '"' || i + 1 == text.size() || !word(text[i + 1]))) {
                    quote = 0;
                }
                continue;
            }
            if (ch == '"' || (ch == '\'' && (i == 0 || !word(text[i - 1])))) {
                quote = ch;
                continue;
            }
            if (i == text.size() || std::string_view(".!?;\r\n").find(ch) != std::string_view::npos) {
                // A lone "Wait." or "Nothing." inside a longer prompt is not
                // evidence of follower management (e.g. "Wait. Do you know me?").
                if (ManagementPrompt(std::string_view(text).substr(start, i - start), false)) {
                    return true;
                }
                start = i + 1;
            }
        }
        return false;
    }

    // Creation Kit condition lists are ANDs of OR groups. The final item in an
    // OR group has isOR=false. Share the grouping code with standalone tests.
    template <class Item, class Evaluate> bool ConditionsPass(const Item* item, Evaluate evaluate)
    {
        bool group = false;
        for (; item; item = item->next) {
            group = group || evaluate(*item);
            if (!item->data.flags.isOR) {
                if (!group) {
                    return false;
                }
                group = false;
            }
            else if (!item->next) {
                return group;
            }
        }
        return true;
    }
} // namespace Wayfarer::RestDialogue
