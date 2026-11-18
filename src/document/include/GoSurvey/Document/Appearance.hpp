#pragma once

/// The linetypes an entity can be given (\ref EntityAttributes::linetype storage) — the Properties
/// panel's list and the values CHPROP accepts (REQ-356), one list so the two cannot disagree.
inline constexpr const char* kEntityLinetypeStorage[] = {"ByLayer", "ByBlock", "Continuous", "DASHED", "HIDDEN", "CENTER",
                                                         "PHANTOM", "DIVIDE", "BORDER"};
/// The lineweights an entity can be given, in millimetres; \c -1 = ByLayer. Same sharing as above.
inline constexpr float kEntityLineweightMmPresets[] = {
    -1.f,  0.f,   0.05f, 0.09f, 0.13f, 0.15f, 0.18f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f,
    0.50f, 0.53f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f, 1.06f, 1.20f, 1.40f, 1.58f, 2.00f, 2.11f};
