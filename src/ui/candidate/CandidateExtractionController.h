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
#include "ui/profile/ProfileController.h"

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
    // M12-C C3 (T027 §81, C3-H10): the CONTROLLED draft path. The review
    // controller is the ONLY object allowed to hand a Candidate value to the
    // editor draft; the QML never touches a Profile model or a store.
    Q_PROPERTY(ProfileController *profileController READ profileController
                   WRITE setProfileController NOTIFY profileControllerChanged)
    // Machine token + human text of the last FAILED review action (Accept /
    // Reject). Empty whenever the last review action succeeded. Consumed or
    // unresolvable Candidates are refusals, never silent drops (C3-H3/H5/H6).
    Q_PROPERTY(QString lastReviewError READ lastReviewError NOTIFY reviewChanged)
    Q_PROPERTY(QString lastReviewErrorToken READ lastReviewErrorToken NOTIFY
                   reviewChanged)
    // M12-C ML-2 (T027 §91): Human-visible surface of the LAST delete
    // request - either a P0-ML-B/C blocker (the delete did NOT happen) or a
    // P0-ML-D/F outcome (success / cleanup warning). Empty while none.
    Q_PROPERTY(QString lastDeleteNotice READ lastDeleteNotice NOTIFY
                   deleteNoticeChanged)
    Q_PROPERTY(QString lastDeleteNoticeToken READ lastDeleteNoticeToken NOTIFY
                   deleteNoticeChanged)
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

    [[nodiscard]] ProfileController *profileController() const
    {
        return profileController_;
    }
    void setProfileController(ProfileController *controller);

    [[nodiscard]] QString lastReviewError() const { return lastReviewError_; }
    [[nodiscard]] QString lastReviewErrorToken() const
    {
        return lastReviewErrorToken_;
    }

    [[nodiscard]] QString lastDeleteNotice() const { return lastDeleteNotice_; }
    [[nodiscard]] QString lastDeleteNoticeToken() const
    {
        return lastDeleteNoticeToken_;
    }

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

    // --- M12-C C3 Human review (T027 §81) --------------------------------
    // The candidate map is the SAME QVariantMap the UI received through the
    // `candidates` property: review actions address a Candidate by value, so a
    // stale or repeated action can never act on a different Candidate (C3-H10,
    // no index shifting). Accept revalidates the evidence against the CURRENT
    // canonical Manual truth (C3-H3) and — only on success — hands the value
    // to the controlled ProfileController draft path (C3-H2). Reject mutates
    // nothing (C3-H6). A failed action leaves the Candidate PendingReview.
    // NO persistence anywhere; consumed Candidates live in session lists that
    // die with the process (C3-H5/H6).
    Q_INVOKABLE bool acceptCandidate(const QVariantMap &candidate);
    Q_INVOKABLE bool rejectCandidate(const QVariantMap &candidate);

    // ------------------------------------------------------------------
    // M12-C ML-2 (T027 §91): the ONE authoritative application-level Manual
    // delete path. checkManualDeleteAllowed runs the frozen P0-ML-B/C guards
    // as a pure read (no mutation) so the UI can show a blocker BEFORE any
    // destructive confirmation; deleteManualDocument RE-RUNS the same guards
    // (an enabled button is never the authority), then dispatches the delete
    // through the wired Manual controller and reports the P0-ML-D/F outcome.
    // The result map is { ok: bool, token: QString, text: QString }.
    // ------------------------------------------------------------------
    Q_INVOKABLE QVariantMap checkManualDeleteAllowed(
        const QVariantMap &document);
    Q_INVOKABLE QVariantMap deleteManualDocument(const QVariantMap &document);

    // ------------------------------------------------------------------
    // M12-C C3-R3B (C3-H8): edited-confirm - ONE explicit Human authority
    // action. The edited value is HUMAN-AUTHORED / HUMAN-CONFIRMED (never
    // an AI proposal); the ORIGINAL pending Candidate is re-resolved by
    // value (stale UI objects fail safely), the SAME evidence freshness
    // gate as Accept re-runs, and the SAME controlled staged-copy write
    // applies the value with FULL profile validation. Atomic: any failure
    // leaves the Candidate PendingReview and the draft untouched. Success
    // consumes the Candidate (Accepted) and writes the draft only - never
    // persisted storage. No provenance is recorded (C3-H7).
    // ------------------------------------------------------------------
    Q_INVOKABLE QVariantMap confirmEditedCandidate(
        const QVariantMap &candidate, const QString &editedValue);

    // TEST/AUTOMATION seam (same discipline as
    // ProfileStore::setManagedRootOverride): installs proposals as PendingReview
    // Candidates through the REAL deterministic C2 validator, without any
    // provider call. It never runs in production: nothing in the UI reaches
    // this, and production extraction always flows through the consent gate +
    // runner seam above.
    Q_INVOKABLE bool seedReviewCandidatesForAutomation(
        const QVariantMap &document, const QString &canonicalExtractedText,
        const QVariantList &proposals);

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
    void profileControllerChanged();
    void reviewChanged();
    void deleteNoticeChanged();

private:
    enum class State { Idle, ConsentRequired, Running, Failed, Succeeded };

    // Stable machine tokens of the review surface (C3-H2/H3/H4/H5/H6).
    // ML-2 delete tokens (P0-ML-B/C blockers and P0-ML-D/F outcomes).
    static constexpr char kDeleteBlockedPending[] =
        "manual_delete_blocked_pending";
    static constexpr char kDeleteBlockedRunning[] =
        "manual_delete_blocked_running";
    static constexpr char kDeleteNotAvailable[] = "manual_delete_unavailable";
    static constexpr char kDeleteNotFound[] = "manual_delete_not_found";
    static constexpr char kDeleteMetadataFailed[] =
        "manual_delete_metadata_failed";
    static constexpr char kDeleteCleanupWarning[] =
        "manual_delete_cleanup_warning";
    static constexpr char kReviewCandidateNotPending[] = "candidate_not_pending";
    static constexpr char kReviewProfileTargetMissing[] = "profile_target_missing";
    static constexpr char kReviewEvidenceDocumentMissing[] =
        "evidence_document_missing";
    static constexpr char kReviewEvidenceContentMismatch[] =
        "evidence_content_mismatch";
    static constexpr char kReviewEvidenceSourceMissing[] = "evidence_source_missing";
    static constexpr char kReviewEvidenceInvalid[] = "evidence_invalid";
    static constexpr char kReviewApplyFailed[] = "candidate_apply_failed";

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
    [[nodiscard]] static bool candidateFromVariant(const QVariantMap &map,
                                                   core::ProfileFieldCandidate &out);
    // Locates the EXACT pending Candidate a review action refers to (value
    // equality): consumed/stale actions can never address another Candidate.
    [[nodiscard]] int findPendingCandidate(
        const core::ProfileFieldCandidate &candidate);
    // The C3-H3 freshness gate: document identity resolves, content identity
    // matches, the canonical text loads, and the stored evidence round-trips.
    [[nodiscard]] bool revalidateEvidence(
        const core::ProfileFieldCandidate &candidate);
    void setReviewError(QString token, QString text);
    void clearReviewError();
    // ML-2 (T027 §91): the frozen P0-ML-B/C guards as a pure read. An empty
    // map means allowed; otherwise it carries { allowed:false, token, text }.
    [[nodiscard]] QVariantMap deleteBlockerFor(const QString &documentId) const;
    void setDeleteNotice(QString token, QString text);
    void clearDeleteNotice();
    [[nodiscard]] static QVariantMap deleteResult(bool ok, const QString &token,
                                                  const QString &text);
    // Moves a Candidate out of the pending set into its session-only consumed
    // list (C3-H5/H6) and refreshes the UI projection.
    void consumeCandidate(core::ProfileFieldCandidate &pending,
                          core::CandidateLifecycleState consumedState);

    ManualImportController *manualController_{nullptr};
    ProfileController *profileController_{nullptr};
    ICandidateExtractionRunner *runner_{nullptr};
    // Owned only in the production construction; empty when injected.
    std::unique_ptr<ModelScopeCandidateRunner> ownedRunner_;

    State state_{State::Idle};
    QString failureToken_;
    QString failureText_;

    // Session-only review surface (C3-H5/H6): consumed Candidates are marked
    // and REMOVED from the pending set; they die with the process and are
    // never persisted anywhere.
    std::vector<core::ProfileFieldCandidate> candidates_;
    std::vector<core::ProfileFieldCandidate> consumedAccepted_;
    std::vector<core::ProfileFieldCandidate> consumedRejected_;
    QString lastReviewError_;
    QString lastReviewErrorToken_;

    // ML-2 delete notice (P0-ML-B/C blockers and P0-ML-D/F outcomes) -
    // session-only, like every other surface on this controller.
    QString lastDeleteNotice_;
    QString lastDeleteNoticeToken_;

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
    std::string candidatesDocumentId_;
    std::string candidatesContentHash_;
};

} // namespace modbuslens::ui