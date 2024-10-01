// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "Profiles_Advanced.h"
#include "Profiles_Advanced.g.cpp"
#include "ProfileViewModel.h"

#include "EnumEntry.h"
#include <LibraryResources.h>
#include "..\WinRTUtils\inc\Utils.h"

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Navigation;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    Profiles_Advanced::Profiles_Advanced()
    {
        InitializeComponent();
    }

    void Profiles_Advanced::OnNavigatedTo(const NavigationEventArgs& e)
    {
        const auto args = e.Parameter().as<Editor::NavigateToProfileArgs>();
        _Profile = args.Profile();
        _windowRoot = args.WindowRoot();
    }

    void Profiles_Advanced::OnNavigatedFrom(const NavigationEventArgs& /*e*/)
    {
        _ViewModelChangedRevoker.revoke();
    }

    safe_void_coroutine Profiles_Advanced::BellSoundAudioPreview_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    try
    {
        const auto path = sender.as<Button>().Tag().as<Editor::BellSoundViewModel>().Path();
        if (path.empty())
        {
            co_return;
        }
        winrt::hstring soundPath{ wil::ExpandEnvironmentStringsW<std::wstring>(path.c_str()) };
        winrt::Windows::Foundation::Uri uri{ soundPath };

        auto weakThis{ get_weak() };
        co_await wil::resume_foreground(Dispatcher());
        if (auto strongThis{ weakThis.get() })
        {
            if (!strongThis->_bellPlayerCreated)
            {
                // The MediaPlayer might not exist on Windows N SKU.
                try
                {
                    strongThis->_bellPlayerCreated = true;
                    strongThis->_bellPlayer = winrt::Windows::Media::Playback::MediaPlayer();
                    // GH#12258: The media keys (like play/pause) should have no effect on our bell sound.
                    strongThis->_bellPlayer.CommandManager().IsEnabled(false);
                }
                CATCH_LOG();
            }
            if (strongThis->_bellPlayer)
            {
                const auto source{ winrt::Windows::Media::Core::MediaSource::CreateFromUri(uri) };
                const auto item{ winrt::Windows::Media::Playback::MediaPlaybackItem(source) };
                strongThis->_bellPlayer.Source(item);
                strongThis->_bellPlayer.Play();
            }
        }
    }
    CATCH_LOG();

    safe_void_coroutine Profiles_Advanced::BellSoundBrowse_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        static constexpr COMDLG_FILTERSPEC supportedFileTypes[] = {
            { L"Sound Files (*.wav;*.mp3;*.flac)", L"*.wav;*.mp3;*.flac" },
            { L"All Files (*.*)", L"*.*" }
        };

        const auto parentHwnd{ reinterpret_cast<HWND>(WindowRoot().GetHostingWindow()) };
        auto file = co_await OpenFilePicker(parentHwnd, [](auto&& dialog) {
            try
            {
                auto folderShellItem{ winrt::capture<IShellItem>(&SHGetKnownFolderItem, FOLDERID_Music, KF_FLAG_DEFAULT, nullptr) };
                dialog->SetDefaultFolder(folderShellItem.get());
            }
            CATCH_LOG(); // non-fatal
            THROW_IF_FAILED(dialog->SetFileTypes(ARRAYSIZE(supportedFileTypes), supportedFileTypes));
            THROW_IF_FAILED(dialog->SetFileTypeIndex(1)); // the array is 1-indexed
            THROW_IF_FAILED(dialog->SetDefaultExtension(L"wav;mp3;flac"));
        });
        if (!file.empty())
        {
            sender.as<Button>().Tag().as<Editor::BellSoundViewModel>().Path(file);
        }
    }

    void Profiles_Advanced::BellSoundDelete_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        auto bellSoundEntry = sender.as<Button>().Tag().as<Editor::BellSoundViewModel>();
        _Profile.RequestDeleteBellSound(bellSoundEntry);
    }
}
