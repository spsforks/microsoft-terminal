// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "AppLogic.g.h"
#include "FindTargetWindowResult.g.h"

#include "Jumplist.h"
#include "LanguageProfileNotifier.h"
#include "AppCommandlineArgs.h"
#include "TerminalWindow.h"
#include "ContentManager.h"

#include <inc/cppwinrt_utils.h>
#include <ThrottledFunc.h>

#ifdef UNIT_TESTING
// fwdecl unittest classes
namespace TerminalAppLocalTests
{
    class CommandlineTest;
};
#endif

namespace winrt::TerminalApp::implementation
{
    struct FindTargetWindowResult : FindTargetWindowResultT<FindTargetWindowResult>
    {
        WINRT_PROPERTY(int32_t, WindowId, -1);
        WINRT_PROPERTY(winrt::hstring, WindowName, L"");

    public:
        FindTargetWindowResult(const int32_t id, const winrt::hstring& name) :
            _WindowId{ id }, _WindowName{ name } {};

        FindTargetWindowResult(const int32_t id) :
            FindTargetWindowResult(id, L""){};
    };

    struct AppLogic : AppLogicT<AppLogic>
    {
    public:
        static AppLogic* Current() noexcept;
        static const Microsoft::Terminal::Settings::Model::CascadiaSettings CurrentAppSettings();

        AppLogic();
        ~AppLogic() = default;

        void Create();
        bool IsUwp() const noexcept;
        void RunAsUwp();
        bool IsElevated() const noexcept;
        void ReloadSettings();

        bool HasSettingsStartupActions() const noexcept;

        [[nodiscard]] Microsoft::Terminal::Settings::Model::CascadiaSettings GetSettings() const noexcept;

        TerminalApp::FindTargetWindowResult FindTargetWindow(array_view<const winrt::hstring> actions);
        bool ShouldImmediatelyHandoffToElevated();
        void HandoffToElevated();

        void SetInboundListener();

        Windows::Foundation::Collections::IMapView<Microsoft::Terminal::Control::KeyChord, Microsoft::Terminal::Settings::Model::Command> GlobalHotkeys();

        Microsoft::Terminal::Settings::Model::Theme Theme();

        TerminalApp::TerminalWindow CreateNewWindow();

        winrt::TerminalApp::ContentManager ContentManager();
        TYPED_EVENT(SettingsChanged, winrt::Windows::Foundation::IInspectable, winrt::Windows::Foundation::IInspectable);

    private:
        bool _isUwp{ false };
        bool _isElevated{ false };

        Microsoft::Terminal::Settings::Model::CascadiaSettings _settings{ nullptr };

        winrt::hstring _settingsLoadExceptionText;
        HRESULT _settingsLoadedResult = S_OK;
        bool _loadedInitialSettings = false;

        bool _hasSettingsStartupActions{ false };
        ::TerminalApp::AppCommandlineArgs _settingsAppArgs;

        std::shared_ptr<ThrottledFuncTrailing<>> _reloadSettings;
        til::throttled_func_trailing<> _reloadState;

        std::vector<Microsoft::Terminal::Settings::Model::SettingsLoadWarnings> _warnings;

        // These fields invoke _reloadSettings and must be destroyed before _reloadSettings.
        // (C++ destroys members in reverse-declaration-order.)
        winrt::com_ptr<LanguageProfileNotifier> _languageProfileNotifier;
        wil::unique_folder_change_reader_nothrow _reader;

        TerminalApp::ContentManager _contentManager{ *winrt::make_self<implementation::ContentManager>() };

        static TerminalApp::FindTargetWindowResult _doFindTargetWindow(winrt::array_view<const hstring> args,
                                                                       const Microsoft::Terminal::Settings::Model::WindowingMode& windowingBehavior);

        void _ApplyLanguageSettingChange() noexcept;
        fire_and_forget _ApplyStartupTaskStateChange();

        [[nodiscard]] HRESULT _TryLoadSettings() noexcept;
        void _ProcessLazySettingsChanges();
        void _RegisterSettingsChange();
        fire_and_forget _DispatchReloadSettings();

#ifdef UNIT_TESTING
        friend class TerminalAppLocalTests::CommandlineTest;
#endif
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(AppLogic);
}
