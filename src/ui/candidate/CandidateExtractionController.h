#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/candidate/CandidateExtraction.h"
#include "core/candidate/ProviderExtractionContract.h"
#include "ui/candidate/CandidateExtractionRunner.h"
#include "ui/candidate/ModelScopeCandidateRunner.h"
#include "ui/manual/ManualImportController.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C2 THIRD SLICE — production extraction ORCHESTRATION (T027 §70).
//
// This is the smallest layer that owns what nothing else owned before:
//   · the CLOUD CONSENT gate (§6) — default is NO UPLOAD;
//   · the orchestration state machine (§7);
//   · the SESSION-ONLY Candidate set (§8) and its atomic replacement;
//   · the stale-result guard (§9).
//
// Authority boundaries that are structural here, not merely documented:
//   · it NEVER writes a DeviceProfile and never touches ProfileStore;
//   · it NEVER persists anything — consent and Candidates live in memory only;
//   · it offers NO Accept / Edit / Reject (C3, §13). Consent Reject/Cancel is a
//     different concept and never mutates a Candidate;
//   · it consumes C1b canonical text through ManualStore::loadText, never the
//     original PDF/DOCX file and never a second manual-text source.
// ---------------------------------------------------------------------------
class CandidateExtractionController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(ManualImportController *manualController READ manualController
                   WRITE setManualController NOTIFY manualControllerChanged)
    // Machine tokens for the UI. Wording is a UI detail; the token set is the
    // contract: idle | consent_required | running | failed | succeeded
    Q_PROPERTY(QString stateToken READ stateToken NOTIFY stateChanged)
    // Human-facing failure text (never provider prose, never a secret).
    Q_PROPERTY(QString failureText READ failureText NOTIFY stateChanged)
    Q_PROPERTY(QString failureToken READ failureToken NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY stateChanged)
    // The locally validated PendingReview Candidates for the CURRENT set.
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY candidatesChanged)
    Q_PROPERTY(int candidateCount READ candidateCount NOTIFY candidatesChanged)
    // Consent semantics for the consent dialog (§6): what will be sent and
    // what will never be sent.
    Q_PROPERTY(QString consentScopeText READ consentScopeText CONSTANT)

public:
    // PRODUCTION: owns the real ModelScope runner.
    explicit CandidateExtractionController(QObject *parent = nullptr);
    // INJECTION for the automated suite: a deterministic fake runner. This is a
    // construction-time dependency, not a "fake AI mode" the UI can switch on.
    explicit CandidateExtractionController(ICandidateExtractionRunner &runner,
                                           QObject *parent = nullptr);

    [[nodiscard]] ManualImportController *manualController() const
    {
        return manualController_;
    }
    void setManualController(ManualImportController *controller);

    [[nodiscard]] QString stateToken() const;
    [[nodiscard]] QString failureText() const;
    [[nodiscard]] QString failureToken() const;
    [[nodiscard]] bool busy() const { return state_ == State::Running; }
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] QVariantList candidates() const;
    [[nodiscard]] int candidateCount() const;
    [[nodiscard]] QString consentScopeText() const;

    // --- the Human trigger (§7) ------------------------------------------
    // Without a grant for the selected document/content this stops at
    // ConsentRequired and calls NOTHING (O01).
    Q_INVOKABLE void requestExtraction();

    // --- the consent gate (§6) -------------------------------------------
    // Grant applies to (documentId, contentHash) for THIS application session
    // only; it is never written to disk and there is no "remember forever".
    Q_INVOKABLE void grantConsent();
    // Reject/Cancel: candidate set, verified Profile and manuals untouched;
    // a later explicit attempt may ask again.
    Q_INVOKABLE void rejectConsent();

    // The orchestration core, callable directly by the automated suite with an
    // explicit document + canonical text (O05 / O06 / O17 need crafted inputs).
    void requestExtractionFor(const core::ManualDocument &document,
                              const std::string &canonicalExtractedText);

    // --- read-only accessors for tests -----------------------------------
    [[nodiscard]] const std::vector<core::ProfileFieldCandidate>
        &validatedCandidates() const
    {
        return candidates_;
    }
    [[nodiscard]] std::uint64_t currentGeneration() const
    {
        return generation_;
    }

signals:
    void stateChanged();
    void candidatesChanged();
    void manualControllerChanged();

private:
    enum class State { Idle, ConsentRequired, Running, Failed, Succeeded };

    struct Attempt {
        std::uint64_t generation{0};
        std::string documentId;
        std::string contentHash;
    };

    void setState(State state, QString failureToken, QString failureText);
    void beginAttempt(const core::ManualDocument &document,
                      const std::string &canonicalExtractedText);
    void completeAttempt(const ICandidateExtractionRunner::Completion &done);
    [[nodiscard]] bool isConsentGranted(const std::string &documentId,
                                        const std::string &contentHash) const;
    static QVariantMap candidateToVariant(
        const core::ProfileFieldCandidate &candidate);

    ManualImportController *manualController_{nullptr};
    ICandidateExtractionRunner *runner_{nullptr};
    // Owned only in the production construction; empty when injected.
    std::unique_ptr<ModelScopeCandidateRunner> ownedRunner_;

    State state_{State::Idle};
    QString failureToken_;
    QString failureText_;

    std::uint64_t generation_{0};
    Attempt activeAttempt_;

    // The identity/text captured while consent is pending, and the canonical
    // text of the attempt currently in flight (needed by the local Evidence
    // validation at completion time).
    core::ManualDocument pendingDocument_;
    std::string pendingCanonicalText_;
    std::string lastCanonicalText_;

    // SESSION-ONLY, in-memory. No disk, no QSettings, no cache.
    std::vector<std::pair<std::string, std::string>> grantedConsents_;
    std::vector<core::ProfileFieldCandidate> candidates_;
    std::string candidatesDocumentId_;
    std::string candidatesContentHash_;
};

} // namespace modbuslens::ui