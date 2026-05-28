#pragma once

/// *********************************************************************************
/// 数学機能集約ヘッダ
/// *********************************************************************************

// 行ベクトル row-vector 前提
// 行列は行優先 row-major 前提

#define NOMINMAX // Windows.h の min/max マクロを無効化

// === C++ includes ===
#include <cstdint>
#include <limits>
#include <numbers>

// === Math includes ===
#include "TimeUnit.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4.h"
#include "Quaternion.h"
