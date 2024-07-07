// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"

#include "_output.h"

#include "directio.h"
#include "handle.h"
#include "misc.h"
#include "_stream.h"

#include "../interactivity/inc/ServiceLocator.hpp"
#include "../types/inc/convert.hpp"
#include "../types/inc/GlyphWidth.hpp"
#include "../types/inc/Viewport.hpp"

using namespace Microsoft::Console::Types;
using Microsoft::Console::Interactivity::ServiceLocator;

// Routine Description:
// - This routine writes a screen buffer region to the screen.
// Arguments:
// - screenInfo - reference to screen buffer information.
// - srRegion - Region to write in screen buffer coordinates. Region is inclusive
// Return Value:
// - <none>
void WriteToScreen(SCREEN_INFORMATION& screenInfo, const Viewport& region)
{
    const auto& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
    // update to screen, if we're not iconic.
    if (!screenInfo.IsActiveScreenBuffer() || WI_IsFlagSet(gci.Flags, CONSOLE_IS_ICONIC))
    {
        return;
    }

    // clip region to fit within the viewport
    const auto clippedRegion = screenInfo.GetViewport().Clamp(region);
    if (!clippedRegion.IsValid())
    {
        return;
    }

    if (screenInfo.IsActiveScreenBuffer())
    {
        if (ServiceLocator::LocateGlobals().pRender != nullptr)
        {
            ServiceLocator::LocateGlobals().pRender->TriggerRedraw(region);
        }
    }
}

enum class FillConsoleMode
{
    WriteAttribute,
    WriteCharacter,
    FillAttribute,
    FillCharacter,
};

struct FillConsoleResult
{
    size_t lengthRead = 0;
    til::CoordType cellsModified = 0;
};

static FillConsoleResult FillConsoleImpl(SCREEN_INFORMATION& screenInfo, FillConsoleMode mode, const void* data, const size_t lengthToWrite, const til::point startingCoordinate)
{
    if (lengthToWrite == 0)
    {
        return {};
    }

    LockConsole();
    const auto unlock = wil::scope_exit([&] { UnlockConsole(); });

    auto& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
    auto& screenBuffer = screenInfo.GetActiveBuffer();
    const auto bufferSize = screenBuffer.GetBufferSize();
    FillConsoleResult result;

    // Technically we could always pass `data` as `uint16_t*`, because `wchar_t` is guaranteed to be 16 bits large.
    // However, OutputCellIterator is terrifyingly unsafe code and so we don't do that.
    //
    // Constructing an OutputCellIterator with a `wchar_t` takes the `wchar_t` by reference, so that it can reference
    // it in a `wstring_view` forever. That's of course really bad because passing a `const uint16_t&` to a
    // `const wchar_t&` argument implicitly converts the types. To do so, the implicit conversion allocates a
    // `wchar_t` value on the stack. The lifetime of that copy DOES NOT get extended beyond the constructor call.
    // The result is that OutputCellIterator would read random data from the stack.
    //
    // Don't ever assume the lifetime of implicitly convertible types given by reference.
    // Ironically that's a bug that cannot happen with C pointers. To no ones surprise, C keeps on winning.
    auto attrs = static_cast<const uint16_t*>(data);
    auto chars = static_cast<const wchar_t*>(data);

    if (!bufferSize.IsInBounds(startingCoordinate))
    {
        return {};
    }

    if (const auto io = gci.GetVtIoForBuffer(&screenInfo))
    {
        const auto corkLock = io->Cork();

        const auto h = bufferSize.Height();
        const auto w = bufferSize.Width();
        auto y = startingCoordinate.y;
        til::CoordType end = 0;
        auto remaining = lengthToWrite;

        til::small_vector<CHAR_INFO, 1024> infos;
        infos.resize(gsl::narrow_cast<size_t>(w) + 1);

        Viewport unused;

        while (y < h && remaining > 0)
        {
            const auto beg = y == startingCoordinate.y ? startingCoordinate.x : 0;
            auto len = std::min(remaining, gsl::narrow_cast<size_t>(w - beg));
            end = beg + gsl::narrow_cast<til::CoordType>(len);

            auto viewport = Viewport::FromInclusive({ beg, y, end - 1, y });
            THROW_IF_FAILED(ReadConsoleOutputWImplHelper(screenInfo, infos, viewport, unused));

            switch (mode)
            {
            case FillConsoleMode::WriteAttribute:
                for (size_t i = 0; i < len; ++i)
                {
                    infos[i].Attributes = *attrs++;
                }
                break;
            case FillConsoleMode::WriteCharacter:
                for (size_t i = 0; i < len;)
                {
                    const auto ch = *chars++;

                    auto& lead = infos[i++];
                    lead.Char.UnicodeChar = ch;
                    lead.Attributes = lead.Attributes & ~(COMMON_LVB_LEADING_BYTE | COMMON_LVB_TRAILING_BYTE);

                    if (IsGlyphFullWidth(ch))
                    {
                        lead.Attributes |= COMMON_LVB_LEADING_BYTE;

                        auto& trail = infos[i++];
                        trail.Char.UnicodeChar = ch;
                        trail.Attributes = trail.Attributes & ~(COMMON_LVB_LEADING_BYTE | COMMON_LVB_TRAILING_BYTE) | COMMON_LVB_LEADING_BYTE;
                    }
                }
                break;
            case FillConsoleMode::FillAttribute:
            {
                const auto attr = *attrs;
                for (size_t i = 0; i < len; ++i)
                {
                    infos[i].Attributes = attr;
                }
                break;
            }
            case FillConsoleMode::FillCharacter:
            {
                const auto ch = *chars;

                if (IsGlyphFullWidth(ch))
                {
                    for (size_t i = 0; i < len;)
                    {
                        auto& lead = infos[i++];
                        lead.Char.UnicodeChar = ch;
                        lead.Attributes = lead.Attributes & ~(COMMON_LVB_LEADING_BYTE | COMMON_LVB_TRAILING_BYTE) | COMMON_LVB_LEADING_BYTE;

                        auto& trail = infos[i++];
                        trail.Char.UnicodeChar = ch;
                        trail.Attributes = trail.Attributes & ~(COMMON_LVB_LEADING_BYTE | COMMON_LVB_TRAILING_BYTE) | COMMON_LVB_LEADING_BYTE;
                    }
                }
                else
                {
                    for (size_t i = 0; i < len;)
                    {
                        auto& lead = infos[i++];
                        lead.Char.UnicodeChar = ch;
                        lead.Attributes = lead.Attributes & ~(COMMON_LVB_LEADING_BYTE | COMMON_LVB_TRAILING_BYTE);
                    }
                }

                break;
            }
            }

            if (auto& last = infos[len - 1]; last.Attributes & COMMON_LVB_LEADING_BYTE)
            {
                len--;
                end--;
            }

            viewport = Viewport::FromInclusive({ beg, y, end - 1, y });
            THROW_IF_FAILED(WriteConsoleOutputWImplHelper(screenInfo, infos, viewport.Width(), viewport, unused));

            y += 1;
            remaining -= len;
        }

        result.lengthRead = lengthToWrite - remaining;
        result.cellsModified = std::max(0, (y - startingCoordinate.y - 1) * w + end - startingCoordinate.x);

        if (io && io->BufferHasContent())
        {
            io->WriteCUP(screenInfo.GetTextBuffer().GetCursor().GetPosition());
            io->WriteAttributes(screenInfo.GetAttributes().GetLegacyAttributes());
        }
    }
    else
    {
        OutputCellIterator it;

        switch (mode)
        {
        case FillConsoleMode::WriteAttribute:
            it = OutputCellIterator({ attrs, lengthToWrite });
            break;
        case FillConsoleMode::WriteCharacter:
            it = OutputCellIterator({ chars, lengthToWrite });
            break;
        case FillConsoleMode::FillAttribute:
            it = OutputCellIterator(TextAttribute(*attrs), lengthToWrite);
            break;
        case FillConsoleMode::FillCharacter:
            it = OutputCellIterator(*chars, lengthToWrite);
            break;
        default:
            __assume(false);
        }

        const auto done = screenBuffer.Write(it, startingCoordinate, false);
        result.lengthRead = done.GetInputDistance(it);
        result.cellsModified = done.GetCellDistance(it);

        // If we've overwritten image content, it needs to be erased.
        ImageSlice::EraseCells(screenInfo.GetTextBuffer(), startingCoordinate, result.cellsModified);
    }

    if (screenBuffer.HasAccessibilityEventing())
    {
        // Notify accessibility
        auto endingCoordinate = startingCoordinate;
        bufferSize.WalkInBounds(endingCoordinate, result.cellsModified);
        screenBuffer.NotifyAccessibilityEventing(startingCoordinate.x, startingCoordinate.y, endingCoordinate.x, endingCoordinate.y);
    }

    return result;
}

// Routine Description:
// - writes text attributes to the screen
// Arguments:
// - OutContext - the screen info to write to
// - attrs - the attrs to write to the screen
// - target - the starting coordinate in the screen
// - used - number of elements written
// Return Value:
// - S_OK, E_INVALIDARG or similar HRESULT error.
[[nodiscard]] HRESULT ApiRoutines::WriteConsoleOutputAttributeImpl(IConsoleOutputObject& OutContext,
                                                                   const std::span<const WORD> attrs,
                                                                   const til::point target,
                                                                   size_t& used) noexcept
{
    // Set used to 0 from the beginning in case we exit early.
    used = 0;

    if (attrs.empty())
    {
        return S_OK;
    }

    try
    {
        LockConsole();
        const auto unlock = wil::scope_exit([&] { UnlockConsole(); });

        used = FillConsoleImpl(OutContext, FillConsoleMode::WriteAttribute, attrs.data(), attrs.size(), target).cellsModified;
        return S_OK;
    }
    CATCH_RETURN();
}

// Routine Description:
// - writes text to the screen
// Arguments:
// - screenInfo - the screen info to write to
// - chars - the text to write to the screen
// - target - the starting coordinate in the screen
// - used - number of elements written
// Return Value:
// - S_OK, E_INVALIDARG or similar HRESULT error.
[[nodiscard]] HRESULT ApiRoutines::WriteConsoleOutputCharacterWImpl(IConsoleOutputObject& OutContext,
                                                                    const std::wstring_view chars,
                                                                    const til::point target,
                                                                    size_t& used) noexcept
{
    // Set used to 0 from the beginning in case we exit early.
    used = 0;

    if (chars.empty())
    {
        return S_OK;
    }

    try
    {
        LockConsole();
        const auto unlock = wil::scope_exit([&] { UnlockConsole(); });

        used = FillConsoleImpl(OutContext, FillConsoleMode::WriteCharacter, chars.data(), chars.size(), target).lengthRead;
    }
    CATCH_RETURN();

    return S_OK;
}

// Routine Description:
// - writes text to the screen
// Arguments:
// - screenInfo - the screen info to write to
// - chars - the text to write to the screen
// - target - the starting coordinate in the screen
// - used - number of elements written
// Return Value:
// - S_OK, E_INVALIDARG or similar HRESULT error.
[[nodiscard]] HRESULT ApiRoutines::WriteConsoleOutputCharacterAImpl(IConsoleOutputObject& OutContext,
                                                                    const std::string_view chars,
                                                                    const til::point target,
                                                                    size_t& used) noexcept
{
    // Set used to 0 from the beginning in case we exit early.
    used = 0;

    const auto& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
    const auto codepage = gci.OutputCP;
    try
    {
        // convert to wide chars so we can call the W version of this function
        const auto wideChars = ConvertToW(codepage, chars);

        size_t wideCharsWritten = 0;
        RETURN_IF_FAILED(WriteConsoleOutputCharacterWImpl(OutContext, wideChars, target, wideCharsWritten));

        // Create a view over the wide chars and reduce it to the amount actually written (do in two steps to enforce bounds)
        std::wstring_view writtenView(wideChars);
        writtenView = writtenView.substr(0, wideCharsWritten);

        // Look over written wide chars to find equivalent count of ascii chars so we can properly report back
        // how many elements were actually written
        used = GetALengthFromW(codepage, writtenView);
    }
    CATCH_RETURN();

    return S_OK;
}

// Routine Description:
// - fills the screen buffer with the specified text attribute
// Arguments:
// - OutContext - reference to screen buffer information.
// - attribute - the text attribute to use to fill
// - lengthToWrite - the number of elements to write
// - startingCoordinate - Screen buffer coordinate to begin writing to.
// - cellsModified - the number of elements written
// Return Value:
// - S_OK or suitable HRESULT code from failure to write (memory issues, invalid arg, etc.)
[[nodiscard]] HRESULT ApiRoutines::FillConsoleOutputAttributeImpl(IConsoleOutputObject& OutContext,
                                                                  const WORD attribute,
                                                                  const size_t lengthToWrite,
                                                                  const til::point startingCoordinate,
                                                                  size_t& cellsModified,
                                                                  const bool enablePowershellShim) noexcept
{
    try
    {
        LockConsole();
        const auto unlock = wil::scope_exit([&] { UnlockConsole(); });

        auto& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
        if (const auto io = gci.GetVtIoForBuffer(&OutContext))
        {
            // GH#3126 - This is a shim for powershell's `Clear-Host` function. In
            // the vintage console, `Clear-Host` is supposed to clear the entire
            // buffer. In conpty however, there's no difference between the viewport
            // and the entirety of the buffer. We're going to see if this API call
            // exactly matched the way we expect powershell to call it. If it does,
            // then let's manually emit a Full Reset (RIS).
            if (enablePowershellShim)
            {
                const auto currentBufferDimensions{ OutContext.GetBufferSize().Dimensions() };
                const auto wroteWholeBuffer = lengthToWrite == (currentBufferDimensions.area<size_t>());
                const auto startedAtOrigin = startingCoordinate == til::point{ 0, 0 };
                const auto wroteSpaces = attribute == (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);

                if (wroteWholeBuffer && startedAtOrigin && wroteSpaces)
                {
                    // PowerShell has previously called FillConsoleOutputCharacterW() which triggered a call to WriteClearScreen().
                    cellsModified = lengthToWrite;
                    return S_OK;
                }
            }
        }

        cellsModified = FillConsoleImpl(OutContext, FillConsoleMode::FillAttribute, &attribute, lengthToWrite, startingCoordinate).cellsModified;
        return S_OK;
    }
    CATCH_RETURN();
}

// Routine Description:
// - fills the screen buffer with the specified wchar
// Arguments:
// - OutContext - reference to screen buffer information.
// - character - wchar to fill with
// - lengthToWrite - the number of elements to write
// - startingCoordinate - Screen buffer coordinate to begin writing to.
// - cellsModified - the number of elements written
// - enablePowershellShim - true iff the client process that's calling this
//   method is "powershell.exe". Used to enable certain compatibility shims for
//   conpty mode. See GH#3126.
// Return Value:
// - S_OK or suitable HRESULT code from failure to write (memory issues, invalid arg, etc.)
[[nodiscard]] HRESULT ApiRoutines::FillConsoleOutputCharacterWImpl(IConsoleOutputObject& OutContext,
                                                                   const wchar_t character,
                                                                   const size_t lengthToWrite,
                                                                   const til::point startingCoordinate,
                                                                   size_t& cellsModified,
                                                                   const bool enablePowershellShim) noexcept
{
    try
    {
        LockConsole();
        const auto unlock = wil::scope_exit([&] { UnlockConsole(); });

        auto& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
        if (const auto io = gci.GetVtIoForBuffer(&OutContext))
        {
            // GH#3126 - This is a shim for powershell's `Clear-Host` function. In
            // the vintage console, `Clear-Host` is supposed to clear the entire
            // buffer. In conpty however, there's no difference between the viewport
            // and the entirety of the buffer. We're going to see if this API call
            // exactly matched the way we expect powershell to call it. If it does,
            // then let's manually emit a Full Reset (RIS).
            if (enablePowershellShim)
            {
                const auto currentBufferDimensions{ OutContext.GetBufferSize().Dimensions() };
                const auto wroteWholeBuffer = lengthToWrite == (currentBufferDimensions.area<size_t>());
                const auto startedAtOrigin = startingCoordinate == til::point{ 0, 0 };
                const auto wroteSpaces = character == UNICODE_SPACE;

                if (wroteWholeBuffer && startedAtOrigin && wroteSpaces)
                {
                    WriteClearScreen(OutContext);
                    cellsModified = lengthToWrite;
                    return S_OK;
                }
            }
        }

        cellsModified = FillConsoleImpl(OutContext, FillConsoleMode::FillCharacter, &character, lengthToWrite, startingCoordinate).lengthRead;
        return S_OK;
    }
    CATCH_RETURN();
}

// Routine Description:
// - fills the screen buffer with the specified char
// Arguments:
// - OutContext - reference to screen buffer information.
// - character - ascii character to fill with
// - lengthToWrite - the number of elements to write
// - startingCoordinate - Screen buffer coordinate to begin writing to.
// - cellsModified - the number of elements written
// Return Value:
// - S_OK or suitable HRESULT code from failure to write (memory issues, invalid arg, etc.)
[[nodiscard]] HRESULT ApiRoutines::FillConsoleOutputCharacterAImpl(IConsoleOutputObject& OutContext,
                                                                   const char character,
                                                                   const size_t lengthToWrite,
                                                                   const til::point startingCoordinate,
                                                                   size_t& cellsModified) noexcept
try
{
    // In case ConvertToW throws causing an early return, set modified cells to 0.
    cellsModified = 0;

    const auto& gci = ServiceLocator::LocateGlobals().getConsoleInformation();

    // convert to wide chars and call W version
    const auto wchs = ConvertToW(gci.OutputCP, { &character, 1 });

    LOG_HR_IF(E_UNEXPECTED, wchs.size() > 1);

    return FillConsoleOutputCharacterWImpl(OutContext, wchs.at(0), lengthToWrite, startingCoordinate, cellsModified);
}
CATCH_RETURN()
