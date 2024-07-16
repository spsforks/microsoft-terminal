// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "GithubCopilotLLMProvider.g.h"
#include "GithubCopilotResponse.g.h"

namespace winrt::Microsoft::Terminal::Query::Extension::implementation
{
    struct GithubCopilotLLMProvider : GithubCopilotLLMProviderT<GithubCopilotLLMProvider>
    {
        GithubCopilotLLMProvider() = default;
        GithubCopilotLLMProvider(const winrt::hstring& endpoint, const winrt::hstring& key);

        void ClearMessageHistory();
        void SetSystemPrompt(const winrt::hstring& systemPrompt);
        void SetContext(const Extension::IContext context);

        winrt::Windows::Foundation::IAsyncOperation<Extension::IResponse> GetResponseAsync(const winrt::hstring& userPrompt);

        winrt::fire_and_forget CompleteAuthWithUrl(const Windows::Foundation::Uri url);
        TYPED_EVENT(AuthChanged, winrt::Microsoft::Terminal::Query::Extension::ILMProvider, winrt::hstring);

    private:
        winrt::hstring _authToken;
        winrt::hstring _refreshToken;
        winrt::Windows::Web::Http::HttpClient _httpClient{ nullptr };

        Extension::IContext _context;

        winrt::Windows::Data::Json::JsonArray _jsonMessages;

        void _refreshAuthTokens();
    };

    struct GithubCopilotResponse : GithubCopilotResponseT<GithubCopilotResponse>
    {
        GithubCopilotResponse(const winrt::hstring& message, const bool isError) :
            _message{ message },
            _isError{ isError } {}
        winrt::hstring Message() { return _message; };
        bool IsError() { return _isError; };

    private:
        winrt::hstring _message;
        bool _isError;
    };
}

namespace winrt::Microsoft::Terminal::Query::Extension::factory_implementation
{
    BASIC_FACTORY(GithubCopilotLLMProvider);
    BASIC_FACTORY(GithubCopilotResponse);
}
