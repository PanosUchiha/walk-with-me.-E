#include "input_handler.h"

#include "config.h"
#include "formation_controller.h"

namespace Wayfarer
{
    InputHandler& InputHandler::GetSingleton()
    {
        static InputHandler singleton;
        return singleton;
    }

    void InputHandler::Register()
    {
        if (registered) {
            return;
        }
        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
            input->AddEventSink(static_cast<RE::BSTEventSink<RE::InputEvent*>*>(&GetSingleton()));
            registered = true;
            logger::info("[Input] Hotkeys registered.");
        }
    }

    RE::BSEventNotifyControl InputHandler::ProcessEvent(RE::InputEvent* const* a_event,
                                                        RE::BSTEventSource<RE::InputEvent*>*)
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (Menu::IsInstalled()) {
            return RE::BSEventNotifyControl::kContinue;
        }
        for (auto* event = *a_event; event; event = event->next) {
            (void)Menu::HandleInput(event);
        }
        return RE::BSEventNotifyControl::kContinue;
    }
} // namespace Wayfarer
