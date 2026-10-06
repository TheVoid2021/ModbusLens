#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqml.h>

#include <memory>

#include "core/manualqa/ManualQaContract.h"
#include "ui/manualqa/IManualQaRunner.h"

namespace modbuslens::ui {

class ManualImportController;

// ---------------------------------------------------------------------------
// M12-D FIRST SLICE (T027 §100, D1–D5): the independent Manual Q&A
// orchestration owner. Deliberately NOT CandidateExtractionController (no
// shared consent, no shared generation, no second C3-H10 path) and NOT
// ProfileController (answers are INFORMATIONAL ONLY — D3: no Accept/Edit/Save
// for an answer, zero mutation authority).
//
// Frozen semantics implemented here:
//   · D1: the authoritative Manual selection stays owned by
//     ManualImportController; this controller only OBSERVES it. Switching the
//     selected Manual invalidates the in-flight generation and clears the Q&A
//     context/answer/citations.
//   · D4: Q&A consent is a SEPARATE session-scoped grant — extraction consent
//     never grants it and granting it never touches extraction consent.
//   · D5: session-only. Nothing here persists anything. A successful Manual
//     delete (observed through the manual list change) invalidates the Q&A
//     generation, best-effort cancels, and clears everything; late
//     completions for an invalidated generation are dropped.
//   · D2/D3: FOUND is displayed only after local deterministic validation of
//     every citation against the selected Manual's CURRENT canonical text;
//     any invalid citation (or malformed provider output) is ERROR — never a
//     semantic state.
// ---------------------------------------------------------------------------

class ManualQaController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString stateToken READ stateToken NOTIFY stateChanged)
    Q_PROPERTY(QString resultStatusToken READ resultStatusToken NOTIFY
                   resultChanged)
    Q_PROPERTY(QString answerText READ answerText NOTIFY resultChanged)
    Q_PROPERTY(QVariantList citations READ citations NOTIFY resultChanged)
    Q_PROPERTY(QString failureText READ failureText NOTIFY resultChanged)
    Q_PROPERTY(QString lastErrorToken READ lastErrorToken NOTIFY resultChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelectedManual READ hasSelectedManual NOTIFY
                   manualContextChanged)
    Q_PROPERTY(QString selectedManualLabel READ selectedManualLabel NOTIFY
                   manualContextChanged)
    Q_PROPERTY(QString consentScopeText READ consentScopeText CONSTANT)
    Q_PROPERTY(modbuslens::ui::ManualImportController* manualController READ
                   manualController WRITE setManualController NOTIFY
                       manualControllerChanged)

public:
    explicit ManualQaController(QObject* parent = nullptr);

    [[nodiscard]] QString stateToken() const;
    [[nodiscard]] QString resultStatusToken() const;
    [[nodiscard]] QString answerText() const;
    [[nodiscard]] QVariantList citations() const;
    [[nodiscard]] QString failureText() const;
    [[nodiscard]] QString lastErrorToken() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool hasSelectedManual() const;
    [[nodiscard]] QString selectedManualLabel() const;
    [[nodiscard]] QString consentScopeText() const;
    [[nodiscard]] ManualImportController* manualController() const
    {
        return manualController_;
    }

    void setManualController(ManualImportController* controller);

    // The Human trigger (D1/D4). Locally rejected (no dispatch) when no
    // Manual is selected, when the question is empty/whitespace, or while an
    // attempt is already running (single-flight). Stops at
    // consent_required when the session Q&A consent has not been granted.
    Q_INVOKABLE void ask(const QString& question);
    Q_INVOKABLE void grantConsent();
    Q_INVOKABLE void rejectConsent();

    // TEST-ONLY harness seam (same class of minimal automation hook as
    // CandidateExtractionController::setRunnerForAutomation): temporarily
    // routes dispatches through a caller-owned deterministic runner. Passing
    // nullptr restores the production owned runner. Never used by the product
    // UI; a harness that installs one must restore nullptr before it exits.
    void setRunnerForAutomation(IManualQaRunner* runner);

signals:
    void manualControllerChanged();
    void stateChanged();
    void resultChanged();
    void manualContextChanged();

private:
    void handleManualContextChanged();
    void invalidateGeneration();
    void clearResult();
    void localReject(const char* token, const QString& text);
    void setState(QString token);
    void beginAttempt(const QString& question);
    void completeAttempt(std::uint64_t generation, bool ok,
                         const QString& failureToken,
                         const std::string& rawJson);

    ManualImportController* manualController_{nullptr};
    std::unique_ptr<IManualQaRunner> ownedRunner_;
    IManualQaRunner* runner_{nullptr};

    QString state_{"idle"};
    bool consentGranted_{false}; // Q&A session consent (D4) — never shared
    std::uint64_t generation_{0};
    // Identity of the Manual the current attempt/context is bound to.
    QString boundDocumentId_;
    QString boundContentHash_;
    // M12-D-R2E: the exact request of the in-flight/last attempt — provides
    // the deterministic block table the citationId resolution maps against.
    core::ManualQaRequest activeRequest_;

    QString resultStatus_{"none"}; // none|found|not_found|insufficient_evidence|error
    QString answerText_;
    QVariantList citations_;
    QString failureText_;
    QString lastErrorToken_;
};

} // namespace modbuslens::ui
