#pragma once

// === Base includes ===
#include <Result.h>

// === Windows API includes ===
#include "stdafx.h"

namespace Cue::PAL::Win
{
    [[nodiscard]] inline Code convert_hresult_code(HRESULT a_hresult)
    {
        // 成功なら OK を返す
        if (SUCCEEDED(a_hresult))
        {
            return Code::OK;
        }

        // HRESULT を Result に変換する
        switch (a_hresult)
        {
        case E_INVALIDARG:
        {
            return Code::InvalidArgument;
        }
        case E_NOTIMPL:
        {
            return Code::Unsupported;
        }
        case E_UNEXPECTED:
        {
            return Code::InternalError;
        }
        case HRESULT_FROM_WIN32(ERROR_NOT_FOUND):
        {
            return Code::NotFound;
        }
        default:
        {
            return Code::UnknownError;
        }
        }
    }
}
