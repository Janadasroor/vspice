#pragma once

#include <QString>

namespace flux::plugin {

// Bump MAJOR for breaking SDK ABI/API changes.
inline constexpr int kSdkVersionMajor = 1;
// Bump MINOR for additive backward-compatible SDK changes.
inline constexpr int kSdkVersionMinor = 0;

inline QString sdkVersionString() {
    return QString::number(kSdkVersionMajor) + "." + QString::number(kSdkVersionMinor);
}

} // namespace flux::plugin
