// The dependency headers come FIRST: AnalysisController.h pulls in the whole
// application layer and must be parsed before this unit's own declarations.
#include "ui/AnalysisController.h"

#include "ui/engineering/EngineeringReadController.h"

#include <cmath>

#include "core/profile/RegisterReadiness.h"
#include "ui/profile/ProfileController.h"
#include "ui/profile/ProfileStore.h"

namespace modbuslens::ui {

namespace {

QString formatNumber(double value)
{
    // A pragmatic display precision: ten significant digits preserve every
    // exactly-representable decimal step of common scale values (0.01, 0.1)
    // without inventing digits. Never used to mask an invalid value — the
    // finiteness gates run BEFORE this.
    return QString::number(value, 'g', 10);
}

} // namespace

EngineeringReadController::EngineeringReadController(
    AnalysisController* analysis, ProfileController* profiles, QObject* parent)
    : QObject(parent)
{
    attach(analysis, profiles);
}

void EngineeringReadController::attach(AnalysisController* analysis,
                                       ProfileController* profiles)
{
    if (analysis_ != nullptr) {
        analysis_->disconnect(this);
    }
    analysis_ = analysis;
    profiles_ = profiles;
    if (analysis_ != nullptr) {
        connect(analysis_, &AnalysisController::readResultChanged, this,
                &EngineeringReadController::onReadResultChanged);
        // A serial disconnect invalidates everything derived from it: an
        // in-flight request can never land on a dead session, and a previous
        // value must not keep looking current.
        connect(analysis_, &AnalysisController::serialConnChanged, this,
                [this]() {
                    if (analysis_ != nullptr
                        && !analysis_->serialConnected()
                        && phase_ != Phase::Idle) {
                        invalidateResult();
                    }
                });
    }
    emit stateChanged();
}

QString EngineeringReadController::readState() const
{
    switch (phase_) {
    case Phase::Idle:
        return QStringLiteral("idle");
    case Phase::Reading:
        return QStringLiteral("reading");
    case Phase::Success:
        return QStringLiteral("success");
    case Phase::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("idle");
}

QString EngineeringReadController::statusLineText() const
{
    switch (phase_) {
    case Phase::Idle:
        return QStringLiteral("读取状态：尚未读取");
    case Phase::Reading:
        return QStringLiteral("读取状态：正在读取");
    case Phase::Success:
        return QStringLiteral("读取状态：成功");
    case Phase::Failed:
        return QStringLiteral("读取状态：失败");
    }
    return QStringLiteral("读取状态：尚未读取");
}

void EngineeringReadController::resetProjection()
{
    hasValue_ = false;
    valueText_.clear();
    unitText_.clear();
    decodedValueText_.clear();
    scaleText_.clear();
    offsetText_.clear();
    decoderKindText_.clear();
    failureText_.clear();
}

void EngineeringReadController::refuse(const QString& token)
{
    // A refusal means "this click sent NOTHING" — it does not touch the
    // previous result (only a selection/connection CHANGE invalidates that,
    // through invalidateResult()).
    refusalToken_ = token;
    emit stateChanged();
}

void EngineeringReadController::fail(const QString& text)
{
    pending_.reset();
    phase_ = Phase::Failed;
    hasValue_ = false;
    valueText_.clear();
    unitText_.clear();
    decodedValueText_.clear();
    scaleText_.clear();
    offsetText_.clear();
    decoderKindText_.clear();
    failureText_ = text;
    emit stateChanged();
}

void EngineeringReadController::invalidateResult()
{
    pending_.reset(); // abandon any in-flight request: its completion is dropped
    phase_ = Phase::Idle;
    resetProjection();
    refusalToken_.clear();
    emit stateChanged();
}

bool EngineeringReadController::readOnce(const QString& profileId,
                                         int functionCode, int address,
                                         int slaveId, int timeoutMs)
{
    refusalToken_.clear();
    if (analysis_ == nullptr || profiles_ == nullptr) {
        refuse(QStringLiteral("controller_unavailable"));
        return false;
    }
    // ---- (10) request parameter domains ---------------------------------
    if (profileId.isEmpty() || (functionCode != 0x03 && functionCode != 0x04)
        || address < 0 || address > 65535 || slaveId < 1 || slaveId > 247
        || timeoutMs < 1 || timeoutMs > 60000) {
        refuse(QStringLiteral("invalid_request_parameters"));
        return false;
    }

    // ---- (1..5) the SAVED-profile Readiness gate, at click time ---------
    const QVariantMap readiness =
        profiles_->savedRegisterReadiness(profileId, functionCode, address);
    if (!readiness.value(QStringLiteral("available")).toBool()) {
        refuse(QStringLiteral("profile_unavailable"));
        return false;
    }
    if (!readiness.value(QStringLiteral("engineeringReady")).toBool()) {
        refuse(QStringLiteral("not_engineering_ready"));
        return false;
    }
    if (readiness.value(QStringLiteral("decoderKind")).toString()
        != QStringLiteral("linear")) {
        // Redundant on top of EngineeringReady, but the Linear contract is
        // stated explicitly here: no other decoder may ever reach a read.
        refuse(QStringLiteral("decoder_not_linear"));
        return false;
    }

    // ---- (6..9) the serial gates ----------------------------------------
    if (analysis_->sourceKind()
            != modbuslens::core::TransactionSourceKind::ActiveSerial
        || !analysis_->serialConnected()) {
        refuse(QStringLiteral("serial_not_connected"));
        return false;
    }
    if (analysis_->serialBusy()) {
        refuse(QStringLiteral("serial_busy"));
        return false;
    }

    // ---- the send-time snapshot, taken from the DISK profile -------------
    const ProfileLoadResult loaded = ProfileStore::loadFromFile(
        ProfileStore::defaultFilePathFor(profileId));
    if (!loaded.ok()) {
        refuse(QStringLiteral("profile_unavailable"));
        return false;
    }
    const modbuslens::core::ProfileLookupResult lookup =
        modbuslens::core::findProfileEntryByStartAddress(
            loaded.profile, static_cast<std::uint8_t>(functionCode),
            static_cast<std::uint16_t>(address));
    if (!lookup.found()) {
        refuse(QStringLiteral("register_not_found"));
        return false;
    }
    const modbuslens::core::RegisterEntry& entry =
        loaded.profile
            .registers[static_cast<std::size_t>(lookup.entryIndex)];
    // Defence in depth: the exact values the computation will use must still
    // satisfy the Linear contract in the freshly loaded snapshot (C2-21).
    if (entry.decoderKind != modbuslens::core::EngineeringDecoderKind::Linear
        || !entry.scaleConfigured || !entry.offsetConfigured
        || !entry.unitConfigured || !std::isfinite(entry.scale)
        || !std::isfinite(entry.offset) || entry.unit.empty()) {
        refuse(QStringLiteral("not_engineering_ready"));
        return false;
    }

    PendingSnapshot snapshot;
    snapshot.serial = ++requestSerial_;
    snapshot.profileId = profileId;
    snapshot.functionCode = functionCode;
    snapshot.address = address;
    snapshot.quantity = entry.registerCount;
    snapshot.slaveId = slaveId;
    snapshot.dataType = entry.dataType;
    snapshot.byteOrder = entry.byteOrder;
    snapshot.wordOrder = entry.wordOrder;
    snapshot.scale = entry.scale;
    snapshot.offset = entry.offset;
    snapshot.unit = entry.unit;
    pending_ = snapshot;

    // A new read CLEARS the previous value FIRST — a stale number is never
    // presented while (or after) a new attempt is running.
    resetProjection();
    phase_ = Phase::Reading;
    emit stateChanged();

    analysis_->readHoldingRegistersOnce(slaveId, address, snapshot.quantity,
                                        timeoutMs, functionCode);

    // Synchronous refusal inside the shipped path (it did not become busy):
    // surface it as a failure instead of leaving the UI in "reading" forever.
    if (!analysis_->serialBusy()) {
        if (analysis_->hasSerialError()) {
            fail(analysis_->serialErrorMessage());
        } else {
            fail(QStringLiteral("读取请求未被接受（串口会话未进入忙碌状态）。"));
        }
        return false;
    }
    return true;
}

void EngineeringReadController::onReadResultChanged()
{
    if (phase_ != Phase::Reading || !pending_.has_value()) {
        return; // no pending request of ours: nothing to consume
    }
    if (analysis_->readResultWaiting()) {
        return; // still in flight
    }
    const PendingSnapshot snapshot = *pending_;

    // ---- request identity: a completion for ANOTHER target is discarded --
    if (analysis_->readResultUnitId() != snapshot.slaveId
        || analysis_->readResultStartAddress() != snapshot.address
        || analysis_->readResultQuantity() != snapshot.quantity) {
        fail(QStringLiteral("读取结果与请求不匹配（已丢弃，不显示数值）。"));
        return;
    }

    if (!analysis_->readResultHasValues()) {
        // Every non-success terminal class surfaces its own shipped summary.
        fail(analysis_->readResultSummaryText());
        return;
    }

    // ---- raw words from the projection's raw DEC column (never the derived
    // decode view, which belongs to a different configuration) -------------
    std::vector<std::uint16_t> words;
    const QVariantList rows = analysis_->readResultValues();
    for (const QVariant& row : rows) {
        words.push_back(
            static_cast<std::uint16_t>(row.toMap()
                                           .value(QStringLiteral("dec"))
                                           .toInt()));
    }
    if (words.size()
        < static_cast<std::size_t>(modbuslens::core::registerDecodeTypeWordCount(
            snapshot.dataType))) {
        fail(QStringLiteral("响应寄存器数量不足，无法按所选类型解码。"));
        return;
    }

    const modbuslens::core::RegisterDecodedNumeric decoded =
        modbuslens::core::decodeRegisterNumeric(words, 0, snapshot.dataType,
                                                snapshot.byteOrder,
                                                snapshot.wordOrder);
    if (decoded.status != modbuslens::core::RegisterDecodeStatus::Ok
        || !std::isfinite(decoded.value)) {
        fail(QStringLiteral("解码拒绝：数据类型或数值无效（NaN / 无穷大 / "
                            "不支持的组合）。"));
        return;
    }

    const modbuslens::core::LinearEngineeringValue engineered =
        modbuslens::core::computeLinearEngineeringValue(
            decoded.value, snapshot.scale, snapshot.offset);
    if (!engineered.ok) {
        fail(QStringLiteral("计算拒绝：工程值不是有限数值。"));
        return;
    }

    pending_.reset();
    phase_ = Phase::Success;
    hasValue_ = true;
    valueText_ = formatNumber(engineered.value);
    unitText_ = QString::fromStdString(snapshot.unit);
    decodedValueText_ = formatNumber(decoded.value);
    scaleText_ = formatNumber(snapshot.scale);
    offsetText_ = formatNumber(snapshot.offset);
    decoderKindText_ = QStringLiteral("Linear");
    failureText_.clear();
    emit stateChanged();
}

} // namespace modbuslens::ui
