#pragma once

// A shader-property controller's animation, evaluated by the view itself: the controller's timing (frequency, phase,
// loop, reverse or clamp over its key range) and its interpolator's keys, copied out of the engine at the collect, and
// the material lanes each controlled variable drives. The actor view runs these while the game is paused, when the
// engine advances no controller. Kept engine-free. Tested by ControllerKeysCoreTest.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Phantasm::ControllerKeysCore
{
    // NiTimeController::Flag bits (MannequinRenderer static_asserts them against CommonLib's).
    inline constexpr std::uint16_t kFlagCycleMask     = 0x06;
    inline constexpr std::uint16_t kFlagCycleReverse  = 0x02;
    inline constexpr std::uint16_t kFlagCycleClamp    = 0x04;
    inline constexpr std::uint16_t kFlagActive        = 0x08;
    inline constexpr std::uint16_t kFlagPlayBackwards = 0x10;
    inline constexpr std::uint16_t kFlagComputeScaled = 0x40;

    struct Timing
    {
        std::uint16_t flags     = 0;
        float         frequency = 1.0f;
        float         phase     = 0.0f;
        float         lo        = 0.0f;   // loKeyTime
        float         hi        = 0.0f;   // hiKeyTime
    };

    /// A controller's running time cycled over its key range [lo, hi]: looped, ping-ponged (reverse) or clamped. An
    /// empty range holds its low key.
    inline float CycleTime(std::uint16_t flags, float lo, float hi, float s) noexcept
    {
        const float span = hi - lo;
        switch (flags & kFlagCycleMask) {
        case kFlagCycleClamp:
            return std::clamp(s, (std::min)(lo, hi), (std::max)(lo, hi));
        case kFlagCycleReverse: {
            if (!(span > 0.0f)) return lo;
            float m = std::fmod(s - lo, 2.0f * span);
            if (m < 0.0f) m += 2.0f * span;
            return m > span ? hi - (m - span) : lo + m;
        }
        default: {   // loop
            if (!(span > 0.0f)) return lo;
            float m = std::fmod(s - lo, span);
            if (m < 0.0f) m += span;
            return lo + m;
        }
        }
    }

    /// The time a controller hands its interpolator, `elapsed` seconds of app time after its last update. Gamebryo's
    /// ComputeScaledTime keeps a clock of its own, weightedLastTime, advanced at each update by the app time since the
    /// last one times the frequency (back for a controller that plays backwards), and hands on that clock plus the
    /// phase, cycled; `weighted` is that clock at the last update. A controller without kFlagComputeScaled hands on
    /// the app time itself (`appTime`).
    inline float ScaledTime(const Timing& t, float weighted, float elapsed, float appTime) noexcept
    {
        if (!(t.flags & kFlagComputeScaled)) return appTime;
        const float dir = (t.flags & kFlagPlayBackwards) ? -1.0f : 1.0f;
        return CycleTime(t.flags, t.lo, t.hi, weighted + dir * elapsed * t.frequency + t.phase);
    }

    /// NiAnimationKey::KeyType.
    enum class KeyType : std::uint32_t
    {
        kNoInterp  = 0,
        kLinear    = 1,
        kQuadratic = 2,   // Hermite with stored in and out tangents
        kTCB       = 3,   // Kochanek-Bartels: tension, continuity, bias
        kEuler     = 4,
        kStep      = 5,
    };

    struct Key
    {
        float time = 0.0f;
        float value[4]{};
        float inTan[4]{};    // the tangent arriving at this key
        float outTan[4]{};   // the tangent leaving it
    };

    /// A channel of 1 (float), 3 (point / RGB) or 4 (RGBA) values: its keys, or the constant an interpolator without
    /// keys holds.
    struct Curve
    {
        KeyType          type = KeyType::kLinear;
        int              dims = 1;
        std::vector<Key> keys;
        float            constant[4]{};
    };

    /// The bytes one key takes at least, by type: the time, the value, and for quadratic keys both tangents, for TCB
    /// keys tension, continuity and bias. The engine's own stride can be larger (derived values follow).
    inline std::uint32_t MinKeyBytes(KeyType type, int dims) noexcept
    {
        const std::uint32_t base = 4u + 4u * static_cast<std::uint32_t>(dims);
        switch (type) {
        case KeyType::kQuadratic: return base + 8u * static_cast<std::uint32_t>(dims);
        case KeyType::kTCB:       return base + 12u;
        default:                  return base;
        }
    }

    // Kochanek-Bartels tangents for the TCB keys of `c`, adjusted for unequal key spacing; an end key mirrors its
    // one neighbouring segment.
    inline void FillTcbTangents(Curve& c, const std::vector<std::array<float, 3>>& tcb)
    {
        const std::size_t n = c.keys.size();
        for (std::size_t i = 0; i < n; ++i) {
            const auto& k  = c.keys[i];
            const float tn = tcb[i][0], cn = tcb[i][1], bs = tcb[i][2];
            const bool  hasPrev = i > 0, hasNext = i + 1 < n;
            const float dtPrev  = hasPrev ? k.time - c.keys[i - 1].time : 0.0f;
            const float dtNext  = hasNext ? c.keys[i + 1].time - k.time : 0.0f;
            for (int d = 0; d < c.dims; ++d) {
                float d1 = hasPrev ? k.value[d] - c.keys[i - 1].value[d] : 0.0f;
                float d2 = hasNext ? c.keys[i + 1].value[d] - k.value[d] : 0.0f;
                if (!hasPrev) d1 = d2;
                if (!hasNext) d2 = d1;
                float out = 0.5f * ((1 - tn) * (1 + bs) * (1 + cn) * d1 + (1 - tn) * (1 - bs) * (1 - cn) * d2);
                float in  = 0.5f * ((1 - tn) * (1 + bs) * (1 - cn) * d1 + (1 - tn) * (1 - bs) * (1 + cn) * d2);
                if (hasPrev && hasNext && dtPrev + dtNext > 0.0f) {
                    out *= 2.0f * dtNext / (dtPrev + dtNext);
                    in *= 2.0f * dtPrev / (dtPrev + dtNext);
                }
                c.keys[i].outTan[d] = out;
                c.keys[i].inTan[d]  = in;
            }
        }
    }

    /// Reads `count` keys of `stride` bytes each from `raw` (a copy of the engine's key array) into `out`. False when
    /// the type is unknown, the stride too small for it, or a time or value is not a number or the times go back.
    inline bool ParseKeys(const std::uint8_t* raw, std::uint32_t count, std::uint32_t stride, std::uint32_t rawType,
                          int dims, Curve& out)
    {
        if (dims < 1 || dims > 4 || rawType > static_cast<std::uint32_t>(KeyType::kStep)) return false;
        const auto type = static_cast<KeyType>(rawType);
        if (count == 0 || !raw || stride < MinKeyBytes(type, dims)) return false;
        out.type = type;
        out.dims = dims;
        out.keys.assign(count, Key{});
        std::vector<std::array<float, 3>> tcb(type == KeyType::kTCB ? count : 0);
        const auto at = [&](std::uint32_t k, std::uint32_t floatIndex) {
            float f;
            std::memcpy(&f, raw + static_cast<std::size_t>(k) * stride + 4u * floatIndex, sizeof f);
            return f;
        };
        for (std::uint32_t k = 0; k < count; ++k) {
            auto& key = out.keys[k];
            key.time  = at(k, 0);
            if (!std::isfinite(key.time) || (k > 0 && key.time < out.keys[k - 1].time)) return false;
            for (int d = 0; d < dims; ++d) {
                key.value[d] = at(k, 1 + d);
                if (!std::isfinite(key.value[d])) return false;
                if (type == KeyType::kQuadratic) {
                    key.inTan[d]  = at(k, 1 + dims + d);
                    key.outTan[d] = at(k, 1 + 2 * dims + d);
                    if (!std::isfinite(key.inTan[d]) || !std::isfinite(key.outTan[d])) return false;
                }
            }
            if (type == KeyType::kTCB) {
                for (int j = 0; j < 3; ++j) {
                    tcb[k][j] = at(k, 1 + dims + j);
                    if (!std::isfinite(tcb[k][j])) return false;
                }
            }
        }
        if (type == KeyType::kTCB) FillTcbTangents(out, tcb);
        return true;
    }

    /// The curve's value at `time`: held at the first and last key outside them, stepped for no-interp, step and
    /// Euler keys, linear, or Hermite over the two keys' tangents with the time normalised to the segment.
    inline void Evaluate(const Curve& c, float time, float out[4]) noexcept
    {
        const int dims = std::clamp(c.dims, 1, 4);
        if (c.keys.empty()) {
            for (int d = 0; d < dims; ++d) out[d] = c.constant[d];
            return;
        }
        const auto& first = c.keys.front();
        const auto& last  = c.keys.back();
        if (!(time > first.time)) {
            for (int d = 0; d < dims; ++d) out[d] = first.value[d];
            return;
        }
        if (!(time < last.time)) {
            for (int d = 0; d < dims; ++d) out[d] = last.value[d];
            return;
        }
        const auto  it = std::upper_bound(c.keys.begin(), c.keys.end(), time,
                                          [](float t, const Key& k) { return t < k.time; });
        const auto& k1 = *it;
        const auto& k0 = *(it - 1);
        const float dt = k1.time - k0.time;
        if (!(dt > 0.0f)) {
            for (int d = 0; d < dims; ++d) out[d] = k1.value[d];
            return;
        }
        const float u = (time - k0.time) / dt;
        switch (c.type) {
        case KeyType::kLinear:
            for (int d = 0; d < dims; ++d) out[d] = k0.value[d] + (k1.value[d] - k0.value[d]) * u;
            return;
        case KeyType::kQuadratic:
        case KeyType::kTCB: {
            const float u2 = u * u, u3 = u2 * u;
            const float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
            for (int d = 0; d < dims; ++d) {
                out[d] = h00 * k0.value[d] + h10 * k0.outTan[d] + h01 * k1.value[d] + h11 * k1.inTan[d];
            }
            return;
        }
        default:
            for (int d = 0; d < dims; ++d) out[d] = k0.value[d];
            return;
        }
    }

    /// Whether a check of `c` at `time` proves anything: a curve whose value changes, checked strictly inside its keys.
    /// A constant curve, or a time held at an end key, agrees whatever the timing or the key layout.
    inline bool CheckIsTelling(const Curve& c, float time) noexcept
    {
        if (c.keys.size() < 2 || c.type == KeyType::kNoInterp || c.type == KeyType::kStep || c.type == KeyType::kEuler) {
            return false;
        }
        if (!(time > c.keys.front().time && time < c.keys.back().time)) return false;
        for (const auto& k : c.keys) {
            for (int d = 0; d < c.dims; ++d) {
                if (k.value[d] != c.keys.front().value[d]) return true;
            }
        }
        return false;
    }

    /// Whether the view's value agrees with the engine's (a self-check): within 1e-3, relative above 1.
    inline bool Agrees(float ours, float engine) noexcept
    {
        return std::fabs(ours - engine) <= 1.0e-3f * (std::max)(1.0f, std::fabs(engine));
    }

    /// How far behind a controller's last update the engine's drawn lane may be: it shows the value from a frame or
    /// two before the time the controller records (every scrolling effect lane of the staff, about 35 ms).
    inline constexpr float kMaxEngineLag  = 0.1f;
    inline constexpr int   kEngineLagSteps = 48;   // the window searched in steps of about 2 ms

    inline constexpr float kDegToRad = 0.017453292519943295f;

    /// Whether the engine's lane value `engine` is the curve's at the controller's last update or up to kMaxEngineLag
    /// seconds of app time before it; `lag` is the lag of the closest match. `weighted` and `appTime` as ScaledTime's.
    /// `angle`: the lane holds a falloff angle's cosine (IsAngleLane), so the curve's degrees are compared as their
    /// cosine; converting the engine's cosine to degrees instead loses more than the tolerance just above 0 degrees.
    inline bool AgreesWithLag(const Curve& c, const Timing& t, float weighted, float appTime, const float* engine,
                              int dims, float& lag, bool angle = false) noexcept
    {
        bool  found = false;
        float best  = 0.0f;
        for (int i = 0; i <= kEngineLagSteps; ++i) {
            const float back = kMaxEngineLag * static_cast<float>(i) / static_cast<float>(kEngineLagSteps);
            float       v[4]{};
            Evaluate(c, ScaledTime(t, weighted, -back, appTime - back), v);
            if (angle) v[0] = std::cos(v[0] * kDegToRad);
            bool  ok  = true;
            float err = 0.0f;
            for (int d = 0; d < dims; ++d) {
                ok  = ok && Agrees(v[d], engine[d]);
                err = (std::max)(err, std::fabs(v[d] - engine[d]));
            }
            if (ok && (!found || err < best)) {
                found = true;
                best  = err;
                lag   = back;
            }
        }
        return found;
    }

    /// The material lane a controller drives, from its class and controlled variable (the NIF enums
    /// EffectShaderControlledVariable / EffectShaderControlledColor / LightingShaderControlledFloat /
    /// LightingShaderControlledColor). kUndrawn: a real variable the views do not draw (specular, glossiness...).
    enum class Lane : std::uint8_t
    {
        kNone,
        kUndrawn,
        kEffectEmissiveMult,
        kEffectFalloffStartAngle,
        kEffectFalloffStopAngle,
        kEffectFalloffStartOpacity,
        kEffectFalloffStopOpacity,
        kEffectAlpha,
        kEffectUOffset,
        kEffectUScale,
        kEffectVOffset,
        kEffectVScale,
        kEffectColor,
        kLitEmissiveMult,
        kLitAlpha,
        kLitUOffset,
        kLitUScale,
        kLitVOffset,
        kLitVScale,
        kLitEmissiveColor,
    };

    enum class ControllerClass : std::uint8_t
    {
        kOther,
        kEffectFloat,
        kEffectColor,
        kLitFloat,
        kLitColor,
    };

    inline ControllerClass ClassOf(const char* rttiName) noexcept
    {
        if (!rttiName) return ControllerClass::kOther;
        if (std::strcmp(rttiName, "BSEffectShaderPropertyFloatController") == 0) return ControllerClass::kEffectFloat;
        if (std::strcmp(rttiName, "BSEffectShaderPropertyColorController") == 0) return ControllerClass::kEffectColor;
        if (std::strcmp(rttiName, "BSLightingShaderPropertyFloatController") == 0) return ControllerClass::kLitFloat;
        if (std::strcmp(rttiName, "BSLightingShaderPropertyColorController") == 0) return ControllerClass::kLitColor;
        return ControllerClass::kOther;
    }

    inline Lane LaneOf(ControllerClass cls, std::uint32_t variable) noexcept
    {
        switch (cls) {
        case ControllerClass::kEffectFloat:
            return variable <= 9 ? static_cast<Lane>(static_cast<std::uint8_t>(Lane::kEffectEmissiveMult) + variable)
                                 : Lane::kNone;
        case ControllerClass::kEffectColor:
            return variable == 0 ? Lane::kEffectColor : Lane::kNone;
        case ControllerClass::kLitFloat:
            switch (variable) {
            case 0: case 8: case 9: case 10: return Lane::kUndrawn;
            case 11: return Lane::kLitEmissiveMult;
            case 12: return Lane::kLitAlpha;
            case 20: return Lane::kLitUOffset;
            case 21: return Lane::kLitUScale;
            case 22: return Lane::kLitVOffset;
            case 23: return Lane::kLitVScale;
            default: return Lane::kNone;
            }
        case ControllerClass::kLitColor:
            return variable == 0 ? Lane::kUndrawn : variable == 1 ? Lane::kLitEmissiveColor : Lane::kNone;
        default:
            return Lane::kNone;
        }
    }

    /// A lane's name for the log.
    inline const char* LaneName(Lane l) noexcept
    {
        switch (l) {
        case Lane::kEffectEmissiveMult:        return "the emissive multiple";
        case Lane::kEffectFalloffStartAngle:   return "the falloff start angle";
        case Lane::kEffectFalloffStopAngle:    return "the falloff stop angle";
        case Lane::kEffectFalloffStartOpacity: return "the falloff start opacity";
        case Lane::kEffectFalloffStopOpacity:  return "the falloff stop opacity";
        case Lane::kEffectAlpha:               return "the alpha";
        case Lane::kEffectUOffset:             return "the U offset";
        case Lane::kEffectUScale:              return "the U scale";
        case Lane::kEffectVOffset:             return "the V offset";
        case Lane::kEffectVScale:              return "the V scale";
        case Lane::kEffectColor:               return "the colour";
        case Lane::kLitEmissiveMult:           return "the emissive multiple";
        case Lane::kLitAlpha:                  return "the alpha";
        case Lane::kLitUOffset:                return "the U offset";
        case Lane::kLitUScale:                 return "the U scale";
        case Lane::kLitVOffset:                return "the V offset";
        case Lane::kLitVScale:                 return "the V scale";
        case Lane::kLitEmissiveColor:          return "the emissive colour";
        case Lane::kUndrawn:                   return "an undrawn lane";
        default:                               return "no lane";
        }
    }

    inline const char* KeyTypeName(KeyType t) noexcept
    {
        switch (t) {
        case KeyType::kLinear:    return "linear";
        case KeyType::kQuadratic: return "quadratic";
        case KeyType::kTCB:       return "TCB";
        case KeyType::kStep:      return "step";
        default:                  return "constant";
        }
    }

    /// The values a colour lane takes (3: RGB) or a float lane (1).
    inline int LaneDims(Lane l) noexcept { return l == Lane::kEffectColor || l == Lane::kLitEmissiveColor ? 3 : 1; }

    /// An effect-shader material's lanes, as the engine holds them (BSEffectShaderMaterial and the property's alpha).
    struct EffectLanes
    {
        float uOffset = 0.0f, vOffset = 0.0f, uScale = 1.0f, vScale = 1.0f;
        float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };   // baseColor; [3] is the alpha lane
        float colorScale = 1.0f;                    // baseColorScale (emissive multiple)
        float falloff[4]{};                         // start angle, stop angle, start opacity, stop opacity
        float propertyAlpha = 1.0f;
    };

    /// A lighting material's drawn lanes (BSLightingShaderMaterialBase UVs and alpha, the property's emissive).
    struct LitLanes
    {
        float uOffset = 0.0f, vOffset = 0.0f, uScale = 1.0f, vScale = 1.0f;
        float emissive[3]{};
        float emissiveMult = 0.0f;
        float alpha        = 1.0f;
        bool  hasEmissive  = false;   // the property has an emissive colour at all
    };

    /// A lane's value read from the lanes (the self-check's engine side). False for a lane neither struct holds.
    inline bool LaneValue(const EffectLanes& e, const LitLanes& l, Lane lane, float out[3]) noexcept
    {
        switch (lane) {
        case Lane::kEffectEmissiveMult:        out[0] = e.colorScale; return true;
        case Lane::kEffectFalloffStartAngle:   out[0] = e.falloff[0]; return true;
        case Lane::kEffectFalloffStopAngle:    out[0] = e.falloff[1]; return true;
        case Lane::kEffectFalloffStartOpacity: out[0] = e.falloff[2]; return true;
        case Lane::kEffectFalloffStopOpacity:  out[0] = e.falloff[3]; return true;
        case Lane::kEffectAlpha:               out[0] = e.color[3]; return true;
        case Lane::kEffectUOffset:             out[0] = e.uOffset; return true;
        case Lane::kEffectUScale:              out[0] = e.uScale; return true;
        case Lane::kEffectVOffset:             out[0] = e.vOffset; return true;
        case Lane::kEffectVScale:              out[0] = e.vScale; return true;
        case Lane::kEffectColor:
            out[0] = e.color[0], out[1] = e.color[1], out[2] = e.color[2];
            return true;
        case Lane::kLitEmissiveMult:  out[0] = l.emissiveMult; return true;
        case Lane::kLitAlpha:         out[0] = l.alpha; return true;
        case Lane::kLitUOffset:       out[0] = l.uOffset; return true;
        case Lane::kLitUScale:        out[0] = l.uScale; return true;
        case Lane::kLitVOffset:       out[0] = l.vOffset; return true;
        case Lane::kLitVScale:        out[0] = l.vScale; return true;
        case Lane::kLitEmissiveColor:
            if (!l.hasEmissive) return false;
            out[0] = l.emissive[0], out[1] = l.emissive[1], out[2] = l.emissive[2];
            return true;
        default:
            return false;
        }
    }

    /// Whether a lane holds a falloff angle: the curve's keys are degrees, and the engine's controller stores
    /// cos(degrees * pi / 180) in the material (BSEffectShaderPropertyFloatController::Update, variables 1 and 2).
    inline bool IsAngleLane(Lane l) noexcept
    {
        return l == Lane::kEffectFalloffStartAngle || l == Lane::kEffectFalloffStopAngle;
    }

    /// An evaluated curve value in its lane's units (degrees to the cosine an angle lane holds); others unchanged.
    inline void ToLaneUnits(Lane l, float v[3]) noexcept
    {
        if (IsAngleLane(l)) v[0] = std::cos(v[0] * kDegToRad);
    }

    /// A lane value in its curve's units (an angle lane's cosine back to degrees, 0-180), for the log; others unchanged.
    inline void FromLaneUnits(Lane l, float v[3]) noexcept
    {
        if (IsAngleLane(l)) v[0] = std::acos((std::clamp)(v[0], -1.0f, 1.0f)) * 57.29577951308232f;
    }

    /// Writes an evaluated value into its lane (the inverse of LaneValue). A lane neither struct holds is ignored.
    inline void SetLane(EffectLanes& e, LitLanes& l, Lane lane, const float v[3]) noexcept
    {
        switch (lane) {
        case Lane::kEffectEmissiveMult:        e.colorScale = v[0]; break;
        case Lane::kEffectFalloffStartAngle:   e.falloff[0] = v[0]; break;
        case Lane::kEffectFalloffStopAngle:    e.falloff[1] = v[0]; break;
        case Lane::kEffectFalloffStartOpacity: e.falloff[2] = v[0]; break;
        case Lane::kEffectFalloffStopOpacity:  e.falloff[3] = v[0]; break;
        case Lane::kEffectAlpha:               e.color[3] = v[0]; break;
        case Lane::kEffectUOffset:             e.uOffset = v[0]; break;
        case Lane::kEffectUScale:              e.uScale = v[0]; break;
        case Lane::kEffectVOffset:             e.vOffset = v[0]; break;
        case Lane::kEffectVScale:              e.vScale = v[0]; break;
        case Lane::kEffectColor:
            e.color[0] = v[0], e.color[1] = v[1], e.color[2] = v[2];
            break;
        case Lane::kLitEmissiveMult: l.emissiveMult = v[0]; break;
        case Lane::kLitAlpha:        l.alpha = v[0]; break;
        case Lane::kLitUOffset:      l.uOffset = v[0]; break;
        case Lane::kLitUScale:       l.uScale = v[0]; break;
        case Lane::kLitVOffset:      l.vOffset = v[0]; break;
        case Lane::kLitVScale:       l.vScale = v[0]; break;
        case Lane::kLitEmissiveColor:
            if (l.hasEmissive) l.emissive[0] = v[0], l.emissive[1] = v[1], l.emissive[2] = v[2];
            break;
        default:
            break;
        }
    }

    /// The emissive colour a lit piece draws with: its colour x multiple, none at a multiple of 0.001 or less, none
    /// without an emissive colour or when the piece cannot glow (a glow-map material whose map did not load).
    inline void LitEmissive(const LitLanes& l, bool emits, float out[3]) noexcept
    {
        const bool on = emits && l.hasEmissive && l.emissiveMult > 0.001f;
        for (int d = 0; d < 3; ++d) out[d] = on ? l.emissive[d] * l.emissiveMult : 0.0f;
    }
}
