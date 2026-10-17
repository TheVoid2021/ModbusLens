#include "core/profile/RegisterReadiness.h"

#include <cmath>

namespace modbuslens::core {

namespace {

// The read paths this application actually supports today. A function code
// inside the legal 0x01..0x7F domain is NOT automatically readable — only
// the paths below are (T035-C C1: FC03/FC04).
[[nodiscard]] bool isSupportedReadFunctionCode(std::uint8_t fc) noexcept
{
    return fc == 0x03 || fc == 0x04;
}

void addMissing(RegisterReadinessResult &result, const char *token,
                std::string reason)
{
    result.missingFields.emplace_back(token);
    result.reasons.push_back(std::move(reason));
}

void addInvalid(RegisterReadinessResult &result, const char *token,
                std::string reason)
{
    result.invalidFields.emplace_back(token);
    result.reasons.push_back(std::move(reason));
}

} // namespace

RegisterReadinessResult validateRegisterForEngineeringDisplay(
    [[maybe_unused]] const DeviceProfile &profile, const RegisterEntry &entry)
{
    RegisterReadinessResult result;
    result.decoderKind = entry.decoderKind;

    // ---------------------------------------------------------- readable --
    const int wordCount = registerDecodeTypeWordCount(entry.dataType);

    if (!isSupportedReadFunctionCode(entry.readFunctionCode)) {
        addMissing(result, "supported_read_function_code",
                   "当前运行时读取仅支持 FC03/FC04：该条目为 FC"
                       + std::to_string(int(entry.readFunctionCode))
                       + "，无法进入读取流程。");
    } else {
        // The entry span must stay inside the 16-bit PDU address space.
        if (int(entry.address) + wordCount - 1 > 65535) {
            addInvalid(result, "address_span_out_of_range",
                       "寄存器跨度越界：" + std::to_string(int(entry.address))
                           + " 起 " + std::to_string(wordCount)
                           + " 个字超过 0xFFFF。");
        }
        // The declared registerCount must match the M11 word count (defence
        // in depth — the profile validator already enforces this shape).
        if (entry.registerCount != wordCount) {
            addInvalid(result, "register_count_mismatch",
                       "寄存器数量与数据类型不一致：" + std::to_string(
                           entry.registerCount) + " vs "
                           + std::to_string(wordCount) + " 个字。");
        }
        // Byte/word order values must live inside their M11 enums.
        if (entry.byteOrder != RegisterByteOrder::Normal
            && entry.byteOrder != RegisterByteOrder::ByteSwapped) {
            addInvalid(result, "byte_order_out_of_enum",
                       "字节序取值不在 M11 枚举内。");
        }
        if (entry.wordOrder != RegisterWordOrder::HighWordFirst
            && entry.wordOrder != RegisterWordOrder::LowWordFirst) {
            addInvalid(result, "word_order_out_of_enum",
                       "字序取值不在 M11 枚举内。");
        }
    }

    result.readable =
        result.missingFields.empty() && result.invalidFields.empty();
    if (result.readable) {
        result.level = RegisterReadinessLevel::Readable;
    } else {
        result.level = RegisterReadinessLevel::NotReadable;
    }

    // -------------------------------------------------------- engineering --
    switch (entry.decoderKind) {
    case EngineeringDecoderKind::Unknown:
        // C1.2: an UNDECLARED decoder is never EngineeringReady — the fields
        // being configured proves nothing about which algorithm applies.
        addMissing(result, "decoder_kind",
                   "工程解码方式尚未确认：请在设备档案中为该寄存器明确选择解码方式"
                   "（字段齐全不代表所选解析方式一定正确）。");
        break;

    case EngineeringDecoderKind::Raw:
        // Raw is a diagnostic representation, never an engineering value.
        result.reasons.emplace_back(
            "Raw 为原始/诊断表示，不构成工程值显示（不自动等同于物理量）。");
        break;

    case EngineeringDecoderKind::VariableDecimal:
        // STRUCTURAL REFUSAL: a fixed scale must never impersonate the
        // manual's dynamic decimal-point rule.
        addMissing(result, "decoder_support_variable_decimal",
                   "VariableDecimal（动态小数点）本轮不支持：不得以固定 "
                   "Scale 替代动态解析规则；原始读取能力可另行判定。");
        break;

    case EngineeringDecoderKind::Enum:
        addMissing(result, "decoder_support_enum",
                   "Enum（枚举映射）本轮不支持：不得把枚举原始代码显示为"
                   "具有工程含义的物理值。");
        break;

    case EngineeringDecoderKind::Linear:
        // The Linear contract needs the Round O provenance flags — the
        // frozen values 1 / 0 / "" never count as confirmation.
        if (!entry.scaleConfigured) {
            addMissing(result, "scale_configured", "Scale（倍率）尚未明确配置。");
        }
        if (!entry.offsetConfigured) {
            addMissing(result, "offset_configured",
                       "Offset（偏移量）尚未明确配置。");
        }
        if (!entry.unitConfigured) {
            addMissing(result, "unit_configured", "Unit（单位）尚未明确配置。");
        }
        if (entry.scaleConfigured && !std::isfinite(entry.scale)) {
            addInvalid(result, "scale_not_finite",
                       "Scale 不是有限数值（NaN/无穷大无效）。");
        }
        if (entry.offsetConfigured && !std::isfinite(entry.offset)) {
            addInvalid(result, "offset_not_finite",
                       "Offset 不是有限数值（NaN/无穷大无效）。");
        }
        if (entry.unitConfigured && entry.unit.empty()) {
            addInvalid(result, "unit_empty",
                       "Unit 标记为已配置但内容为空（结构矛盾）。");
        }
        if (result.readable && result.missingFields.empty()
            && result.invalidFields.empty()) {
            result.engineeringReady = true;
            result.level = RegisterReadinessLevel::EngineeringReady;
            result.reasons.emplace_back(
                "静态线性解码条件完备（engineeringValue = decoded × scale + offset）。");
        }
        break;
    }

    // EngineeringReady requires readability first, and can never be true
    // under a refused decoder — keep the level honest even when readable
    // checks had collected nothing (defensive).
    if (!result.readable || !result.engineeringReady) {
        result.engineeringReady = false;
        if (result.readable) {
            result.level = RegisterReadinessLevel::Readable;
        } else {
            result.level = RegisterReadinessLevel::NotReadable;
        }
    }
    return result;
}

LinearEngineeringValue computeLinearEngineeringValue(
    double decodedValue, double scale, double offset) noexcept
{
    LinearEngineeringValue result;
    if (!std::isfinite(decodedValue) || !std::isfinite(scale)
        || !std::isfinite(offset)) {
        return result; // refusal: never a shown number
    }
    const double value = decodedValue * scale + offset;
    if (!std::isfinite(value)) {
        return result; // refusal: overflow / invalid composition
    }
    result.ok = true;
    result.value = value;
    return result;
}

std::string_view
registerReadinessLevelToken(RegisterReadinessLevel level) noexcept
{
    switch (level) {
    case RegisterReadinessLevel::NotReadable:
        return "not_readable";
    case RegisterReadinessLevel::Readable:
        return "readable";
    case RegisterReadinessLevel::EngineeringReady:
        return "engineering_ready";
    }
    return "unknown";
}

} // namespace modbuslens::core
