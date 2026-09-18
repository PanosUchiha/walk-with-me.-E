#pragma once

namespace Wayfarer
{
    class InputHandler final : public RE::BSTEventSink<RE::InputEvent*>
    {
      public:
        static InputHandler& GetSingleton();
        static void Register();

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                              RE::BSTEventSource<RE::InputEvent*>* a_source) override;

      private:
        static inline bool registered{false};
    };
} // namespace Wayfarer
