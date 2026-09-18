#pragma once

// V1 ABI declarations only, in the upstream virtual-method order. TDM explicitly
// publishes this interface for other mods. No TDM implementation is included.
// https://github.com/ersh1/TrueDirectionalMovement/blob/57b913ae17fed857c9e281799174898cd66a3e82/src/TrueDirectionalMovementAPI.h
namespace Wayfarer::TDM
{
    enum class Result : std::uint8_t
    {
        OK,
        NotOwner,
        MustKeep,
        AlreadyGiven,
        AlreadyTaken,
        BadThread
    };
    class Interface
    {
      public:
        virtual unsigned long GetTDMThreadId() const noexcept = 0;
        virtual bool GetDirectionalMovementState() const noexcept = 0;
        virtual bool GetTargetLockState() const noexcept = 0;
        virtual RE::ActorHandle GetCurrentTarget() const noexcept = 0;
        virtual Result RequestDisableDirectionalMovement(SKSE::PluginHandle) noexcept = 0;
        virtual Result RequestDisableHeadtracking(SKSE::PluginHandle) noexcept = 0;
        virtual SKSE::PluginHandle GetDisableDirectionalMovementOwner() const noexcept = 0;
        virtual SKSE::PluginHandle GetDisableHeadtrackingOwner() const noexcept = 0;
        virtual Result ReleaseDisableDirectionalMovement(SKSE::PluginHandle) noexcept = 0;
        virtual Result ReleaseDisableHeadtracking(SKSE::PluginHandle) noexcept = 0;
    };
    inline Interface* Get()
    {
        const auto module = GetModuleHandleW(L"TrueDirectionalMovement.dll");
        if (!module) {
            return nullptr;
        }
        using Request = void* (*)(std::uint8_t);
        const auto request = reinterpret_cast<Request>(GetProcAddress(module, "RequestPluginAPI"));
        return request ? static_cast<Interface*>(request(0)) : nullptr; // V1 = 0
    }
} // namespace Wayfarer::TDM
