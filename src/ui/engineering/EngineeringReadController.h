#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <optional>

#include "core/analysis/RegisterDecode.h"
#include "core/profile/DeviceProfile.h"

// NOTE (C1.2/C2 naming map): AnalysisController is a GLOBAL-scope class
// (src/ui/AnalysisController.h has no namespace), while ProfileController
// lives in modbuslens::ui. The forward declarations below follow that map —
// a forward declaration in the wrong namespace would shadow the real type
// with an incomplete one (the exact bug this file once had).
class AnalysisController;

namespace modbuslens::ui {

class ProfileController;

// ---------------------------------------------------------------------------
// T035-C Round C2 — the Engineering single-read controller.
//
// ONE user action ("读取一次") produces ONE Modbus RTU read transaction,
// decoded and computed with the SAVED Device Profile's own parameters, and
// displayed as a large engineering value. Nothing here polls, retries,
// schedules, writes, or reads a PDF / the AI / an unsaved draft.
//
// Trusted inputs (in this order, all re-checked AT THE MOMENT of the click):
//   1. the SAVED profile on disk — never the editor draft, never a candidate;
//   2. the Readiness verdict (savedRegisterReadiness, decoderKind must be the
//      explicitly declared Linear);
//   3. the shipped serial session state (ActiveSerial && connected && !busy).
//
// The request identity is a send-time snapshot: a completion whose
// unitId/startAddress/quantity does not match the pending snapshot is
// DISCARDED, and invalidateResult() (called on every selection/connection
// change) abandons any in-flight request so a late response can never land on
// a new target.
//
// State machine: Idle -> Reading -> Success | Failed. A new read CLEARS the
// previous value first (a stale number is never presented as current), and a
// failure NEVER resurrects an older value.
// ---------------------------------------------------------------------------
class EngineeringReadController : public QObject
{
    Q_OBJECT

public:
    EngineeringReadController(AnalysisController* analysis,
                             ProfileController* profiles,
                             QObject* parent = nullptr);

    // Late dependency injection: the QML-side AnalysisController /
    // ProfileController instances only exist AFTER the engine has loaded, so
    // this controller is created BEFORE the load (as a stable context
    // property the QML binds to by reference) and attached right after. Until
    // attached, every read refuses with `controller_unavailable`.
    void attach(AnalysisController* analysis, ProfileController* profiles);

    // State tokens: idle / reading / success / failed.
    Q_PROPERTY(QString readState READ readState NOTIFY stateChanged)
    Q_PROPERTY(bool hasValue READ hasValue NOTIFY stateChanged)
    Q_PROPERTY(QString valueText READ valueText NOTIFY stateChanged)
    Q_PROPERTY(QString unitText READ unitText NOTIFY stateChanged)
    Q_PROPERTY(QString decodedValueText READ decodedValueText NOTIFY stateChanged)
    Q_PROPERTY(QString scaleText READ scaleText NOTIFY stateChanged)
    Q_PROPERTY(QString offsetText READ offsetText NOTIFY stateChanged)
    Q_PROPERTY(QString decoderKindText READ decoderKindText NOTIFY stateChanged)
    Q_PROPERTY(QString failureText READ failureText NOTIFY stateChanged)
    Q_PROPERTY(QString refusalToken READ refusalToken NOTIFY stateChanged)
    Q_PROPERTY(QString statusLineText READ statusLineText NOTIFY stateChanged)
    Q_PROPERTY(int requestSerial READ requestSerial NOTIFY stateChanged)

    [[nodiscard]] QString readState() const;
    [[nodiscard]] bool hasValue() const { return hasValue_; }
    [[nodiscard]] QString valueText() const { return valueText_; }
    [[nodiscard]] QString unitText() const { return unitText_; }
    [[nodiscard]] QString decodedValueText() const { return decodedValueText_; }
    [[nodiscard]] QString scaleText() const { return scaleText_; }
    [[nodiscard]] QString offsetText() const { return offsetText_; }
    [[nodiscard]] QString decoderKindText() const { return decoderKindText_; }
    [[nodiscard]] QString failureText() const { return failureText_; }
    [[nodiscard]] QString refusalToken() const { return refusalToken_; }
    [[nodiscard]] QString statusLineText() const;
    [[nodiscard]] int requestSerial() const
    {
        return static_cast<int>(requestSerial_);
    }

    // The ONE read entry point. Returns false (with refusalToken set) when any
    // gate refuses — in that case NOTHING was sent. Returns true once the
    // request was handed to the shipped serial path (the outcome arrives
    // asynchronously through the read-result projection).
    Q_INVOKABLE bool readOnce(const QString& profileId, int functionCode,
                              int address, int slaveId, int timeoutMs);

    // Called whenever the selection or the connection state changed: the
    // previous result is dropped and any in-flight request is abandoned
    // (its eventual completion is discarded).
    Q_INVOKABLE void invalidateResult();

signals:
    void stateChanged();

private slots:
    void onReadResultChanged();

private:
    enum class Phase { Idle, Reading, Success, Failed };

    // The send-time snapshot of everything the completion is judged against.
    struct PendingSnapshot {
        quint64 serial{0};
        QString profileId;
        int functionCode{0};
        int address{0};
        int quantity{1};
        int slaveId{0};
        modbuslens::core::RegisterDecodeType dataType{
            modbuslens::core::RegisterDecodeType::UInt16};
        modbuslens::core::RegisterByteOrder byteOrder{
            modbuslens::core::RegisterByteOrder::Normal};
        modbuslens::core::RegisterWordOrder wordOrder{
            modbuslens::core::RegisterWordOrder::HighWordFirst};
        double scale{1.0};
        double offset{0.0};
        std::string unit;
    };

    void resetProjection();
    void refuse(const QString& token);
    void fail(const QString& text);

    AnalysisController* analysis_{nullptr};
    ProfileController* profiles_{nullptr};

    Phase phase_{Phase::Idle};
    std::optional<PendingSnapshot> pending_;
    quint64 requestSerial_{0};

    bool hasValue_{false};
    QString valueText_;
    QString unitText_;
    QString decodedValueText_;
    QString scaleText_;
    QString offsetText_;
    QString decoderKindText_;
    QString failureText_;
    QString refusalToken_;
};

} // namespace modbuslens::ui
