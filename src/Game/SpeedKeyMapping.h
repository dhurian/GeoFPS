#pragma once

namespace GeoFPS
{
// Which typed characters adjust camera move speed.  Kept as a single source of
// truth (and unit-tested) because the +/- speed keys have been a recurring
// cross-keyboard-layout pain point:
//   * '+' is the canonical increase character, but on a US layout it needs
//     Shift — many users tap the unshifted '=' on the same physical key while
//     flying, so '=' also increases.
//   * '-' is decrease; '_' (its shifted form) decreases too, symmetrically.
// Detection is by typed CHARACTER (layout-aware), never by physical key code,
// so the right key works on Danish/German/etc. layouts where '+'/'-' live in
// different positions.  Numpad +/- are handled separately by key code since
// they are layout-independent.
[[nodiscard]] inline bool IsSpeedIncreaseChar(unsigned int codepoint)
{
    return codepoint == '+' || codepoint == '=';
}

[[nodiscard]] inline bool IsSpeedDecreaseChar(unsigned int codepoint)
{
    return codepoint == '-' || codepoint == '_';
}
} // namespace GeoFPS
