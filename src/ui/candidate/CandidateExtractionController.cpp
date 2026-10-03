#include "ui/candidate/CandidateExtractionController.h"

#include <QVariantMap>

#include <algorithm>
#include <utility>

#include "core/candidate/CandidateExtraction.h"
#include "core/candidate/ProviderExtractionContract.h"
#include "ui/manual/ManualStore.h"
#include "ui/profile/ProfileController.h"

namespace modbuslens::ui {
namespace {

constexpr char kStateIdle[] = "idle";
constexpr char kStateConsentRequired[] = "consent_required";
constexpr char kStateRunning[] = "running";
constexpr char kStateFailed[] = "failed";
constexpr char kStateSucceeded[] = "succeeded";

constexpr char kFailureNone[] = "";
constexpr char kFailureNoSelection[] = "no_selection";
constexpr char kFailureNoCanonicalText[] = "no_canonical_text";
constexpr char kFailureNotConfigured[] = "not_configured";
constexpr char kFailureNoCandidate[] = "no_candidate";

// The provider-neutral proposal replay used to feed the ALREADY ACCEPTED
// SESSION M local Evidence validation. It invents nothing: it hands the
// provider's untrusted proposals to the same seam the frozen test double uses,
// so validation authority stays exactly where SESSION M/N put it.
class ProposalReplayProvider : public core::ICandidateProposalProvider
{
public:
    explicit ProposalReplayProvider(std::vector<core::CandidateProposal> proposals)
        : proposals_(std::move(proposals))
    {
    }

    std::vector<core::CandidateProposal> propose(
        const core::ManualDocument & /*document*/,
        std::string_view /*canonicalExtractedText*/) override
    {
        return proposals_;
    }

private:
    std::vector<core::CandidateProposal> proposals_;
};

} // namespace

CandidateExtractionController::CandidateExtractionController(QObject *parent)
    : QObject(parent)
{
    ownedRunner_ = std::make_unique<ModelScopeCandidateRunner>();
    runner_ = ownedRunner_.get();
}

CandidateExtractionController::CandidateExtractionController(
    ICandidateExtractionRunner &runner, QObject *parent)
    : QObject(parent), runner_(&runner)
{
}

void CandidateExtractionController::setManualController(
    ManualImportController *controller)
{
    if (manualController_ == controller) {
        return;
    }
    manualController_ = controller;
    emit manualControllerChanged();
}

void CandidateExtractionController::setProfileController(
    ProfileController *controller)
{
    if (profileController_ == controller) {
        return;
    }
    profileController_ = controller;
    emit profileControllerChanged();
}

void CandidateExtractionController::setReviewError(QString token, QString text)
{
    lastReviewErrorToken_ = std::move(token);
    lastReviewError_ = std::move(text);
    emit reviewChanged();
}

void CandidateExtractionController::setDeleteNotice(QString token,
                                                    QString text)
{
    lastDeleteNoticeToken_ = std::move(token);
    lastDeleteNotice_ = std::move(text);
    emit deleteNoticeChanged();
}

void CandidateExtractionController::clearDeleteNotice()
{
    if (lastDeleteNoticeToken_.isEmpty() && lastDeleteNotice_.isEmpty()) {
        return;
    }
    lastDeleteNoticeToken_.clear();
    lastDeleteNotice_.clear();
    emit deleteNoticeChanged();
}

QVariantMap CandidateExtractionController::deleteResult(bool ok,
                                                        const QString &token,
                                                        const QString &text)
{
    QVariantMap map;
    map.insert(QStringLiteral("ok"), ok);
    // The pre-confirmation CHECK reads `allowed`; the delete command reads
    // `ok`. Both mean "the guard chain let this request through".
    map.insert(QStringLiteral("allowed"), ok);
    map.insert(QStringLiteral("token"), token);
    map.insert(QStringLiteral("text"), text);
    return map;
}

QVariantMap CandidateExtractionController::deleteBlockerFor(
    const QString &documentId) const
{
    // P0-ML-C: a RUNNING extraction for THIS document blocks deletion first
    // (frozen product policy even though the canonical text is captured
    // in-memory - the Human must resolve the attempt before deleting).
    if (state_ == State::Running
        && activeAttempt_.documentId == documentId.toStdString()) {
        return deleteResult(
            false, QString::fromLatin1(kDeleteBlockedRunning),
            tr("该说明书正在进行 AI 提取，请等待提取完成后再删除。"));
    }
    // P0-ML-B: any PendingReview Candidate referencing this document blocks.
    // Consumed (Accepted/Rejected) Candidates are in their own session lists
    // and can never block (ML2-20).
    for (const core::ProfileFieldCandidate &candidate : candidates_) {
        if (candidate.evidence.documentId == documentId.toStdString()) {
            return deleteResult(
                false, QString::fromLatin1(kDeleteBlockedPending),
                tr("该说明书仍有待审核 AI 候选，请先接受或拒绝候选，"
                   "再删除说明书。"));
        }
    }
    return {};
}

QVariantMap CandidateExtractionController::checkManualDeleteAllowed(
    const QVariantMap &document)
{
    // Pure read: the frozen guards with ZERO mutation, so the UI can show a
    // blocker BEFORE opening any destructive confirmation (P0-ML-B/C). An
    // empty blocker set yields the explicit allowed envelope.
    const QVariantMap blocker = deleteBlockerFor(
        document.value(QStringLiteral("documentId")).toString());
    if (blocker.isEmpty()) {
        return deleteResult(true, QString(), QString());
    }
    return blocker;
}

QVariantMap CandidateExtractionController::deleteManualDocument(
    const QVariantMap &document)
{
    // ML-2 (T027 §91): the ONE authoritative application-level delete path.
    // The guards are RE-RUN here - an enabled button is never the authority -
    // and the store-level deletion carries its own P0-ML-D/F semantics.
    const QString documentId =
        document.value(QStringLiteral("documentId")).toString();

    const QVariantMap blocker = deleteBlockerFor(documentId);
    if (!blocker.isEmpty()) {
        setDeleteNotice(blocker.value("token").toString(),
                        blocker.value("text").toString());
        return blocker;
    }

    if (manualController_ == nullptr) {
        setDeleteNotice(QString::fromLatin1(kDeleteNotAvailable),
                        tr("说明书控制器不可用。"));
        return deleteResult(false, QString::fromLatin1(kDeleteNotAvailable),
                            tr("说明书控制器不可用。"));
    }

    const auto result = manualController_->deleteDocumentById(documentId);
    switch (result.outcome) {
    case ManualStore::ManualDeleteOutcome::Success: {
        clearDeleteNotice();
        return deleteResult(true, QStringLiteral("manual_delete_success"),
                            QString());
    }
    case ManualStore::ManualDeleteOutcome::SuccessWithCleanupWarning: {
        // P0-ML-F: the record is deleted; never claim full cleanup success.
        setDeleteNotice(QString::fromLatin1(kDeleteCleanupWarning),
                        tr("说明书已从 ModbusLens 移除，但本地缓存清理失败。"));
        return deleteResult(
            true, QString::fromLatin1(kDeleteCleanupWarning),
            tr("说明书已从 ModbusLens 移除，但本地缓存清理失败。"));
    }
    case ManualStore::ManualDeleteOutcome::FailureDocumentNotFound: {
        setDeleteNotice(QString::fromLatin1(kDeleteNotFound),
                        tr("该说明书不存在或已被删除。"));
        return deleteResult(false, QString::fromLatin1(kDeleteNotFound),
                            tr("该说明书不存在或已被删除。"));
    }
    case ManualStore::ManualDeleteOutcome::FailureMetadataRemove:
        break;
    }
    setDeleteNotice(QString::fromLatin1(kDeleteMetadataFailed),
                    tr("删除说明书失败，请重试。"));
    return deleteResult(false, QString::fromLatin1(kDeleteMetadataFailed),
                        tr("删除说明书失败，请重试。"));
}

QVariantMap CandidateExtractionController::confirmEditedCandidate(
    const QVariantMap &candidateMap, const QString &editedValue)
{
    // C3-H8: edited-confirm = ONE explicit Human authority action. The Human
    // value is authoritative; the evidence is revalidated as SOURCE CONTEXT
    // only (C3-H3) and never claimed to prove the edited value. Failure is
    // atomic: Candidate stays PendingReview, draft untouched (C3-H8/4.8).
    clearReviewError();

    core::ProfileFieldCandidate candidate;
    if (!candidateFromVariant(candidateMap, candidate)) {
        return deleteResult(false,
                            QStringLiteral("edit_candidate_not_pending"),
                            tr("该候选不在待审核集合中。"));
    }
    const int index = findPendingCandidate(candidate);
    if (index < 0) {
        // EDIT-23: the Candidate was consumed/replaced while the dialog was
        // open - a stale UI object must never mutate the Profile.
        setReviewError(QStringLiteral("edit_candidate_not_pending"),
                       tr("该候选不在待审核集合中。"));
        return deleteResult(false,
                            QStringLiteral("edit_candidate_not_pending"),
                            tr("该候选不在待审核集合中。"));
    }

    // C3-H4: the authoritative target is the CURRENTLY shown selected
    // Profile + draft. Without a valid target the confirm must fail.
    if (profileController_ == nullptr || !profileController_->hasOpenProfile()) {
        setReviewError(QStringLiteral("edit_profile_target_missing"),
                       tr("没有当前有效的设备档案目标，无法确认修改。"));
        return deleteResult(false,
                            QStringLiteral("edit_profile_target_missing"),
                            tr("没有当前有效的设备档案目标，无法确认修改。"));
    }

    // C3-H8/4.5: the SAME evidence freshness gate as Accept.
    if (!revalidateEvidence(candidates_[static_cast<std::size_t>(index)])) {
        setReviewError(QStringLiteral("edit_evidence_freshness_failed"),
                       tr("候选证据未通过重新验证，无法确认修改。"));
        return deleteResult(false,
                            QStringLiteral("edit_evidence_freshness_failed"),
                            tr("候选证据未通过重新验证，无法确认修改。"));
    }

    // C3-H8/4.6: the Human-edited value goes through the SAME controlled
    // staged-copy write as Accept - full profile validation, commit-once.
    const bool applied = profileController_->applyCandidateField(
        QString::fromUtf8(
            core::profileFieldTargetToken(candidates_[index].target)),
        editedValue);
    if (!applied) {
        setReviewError(profileController_->lastActionErrorToken(),
                       profileController_->lastActionError());
        return deleteResult(false,
                            profileController_->lastActionErrorToken(),
                            profileController_->lastActionError());
    }

    // C3-H5: consumed as Accepted under the existing lifecycle semantics.
    consumeCandidate(candidates_[static_cast<std::size_t>(index)],
                     core::CandidateLifecycleState::Accepted);
    clearReviewError();
    return deleteResult(true, QStringLiteral("edit_confirm_success"),
                        QString());
}

void CandidateExtractionController::clearReviewError()
{
    if (lastReviewErrorToken_.isEmpty() && lastReviewError_.isEmpty()) {
        return;
    }
    lastReviewErrorToken_.clear();
    lastReviewError_.clear();
    emit reviewChanged();
}

QString CandidateExtractionController::stateToken() const
{
    switch (state_) {
    case State::Idle:
        return QString::fromLatin1(kStateIdle);
    case State::ConsentRequired:
        return QString::fromLatin1(kStateConsentRequired);
    case State::Running:
        return QString::fromLatin1(kStateRunning);
    case State::Failed:
        return QString::fromLatin1(kStateFailed);
    case State::Succeeded:
        return QString::fromLatin1(kStateSucceeded);
    }
    return QString::fromLatin1(kStateIdle);
}

QString CandidateExtractionController::failureText() const
{
    return failureText_;
}

QString CandidateExtractionController::failureToken() const
{
    return failureToken_;
}

bool CandidateExtractionController::hasSelection() const
{
    return manualController_ != nullptr
        && manualController_->selectedIndex() >= 0;
}

int CandidateExtractionController::candidateCount() const
{
    return static_cast<int>(candidates_.size());
}

QString CandidateExtractionController::consentScopeText() const
{
    // The SEMANTICS are the contract (§6); the wording is a UI detail. Stated
    // explicitly because a vague "may upload data" would not be consent.
    return tr("将把该说明书已抽取的文本发送给 ModelScope；"
              "不会上传原始 PDF/DOCX 文件；"
              "AI 结果只作为待审核候选，不会自动修改已验证的设备档案。");
}

QVariantMap CandidateExtractionController::candidateToVariant(
    const core::ProfileFieldCandidate &candidate)
{
    QVariantMap map;
    // Only locally validated fields are exposed. There is deliberately no
    // confidence, no raw provider response and no provider-authored offset.
    map.insert(QStringLiteral("targetField"),
               QString::fromUtf8(core::profileFieldTargetToken(candidate.target)));
    map.insert(QStringLiteral("proposedValue"),
               QString::fromStdString(candidate.proposedValue));
    map.insert(QStringLiteral("evidenceExcerpt"),
               QString::fromStdString(candidate.evidence.excerpt));
    map.insert(QStringLiteral("lifecycle"),
               QString::fromUtf8(
                   core::candidateLifecycleStateToken(candidate.lifecycle)));
    map.insert(QStringLiteral("documentId"),
               QString::fromStdString(candidate.evidence.documentId));
    map.insert(QStringLiteral("contentHash"),
               QString::fromStdString(candidate.evidence.contentHash));
    map.insert(QStringLiteral("textStart"),
               static_cast<qlonglong>(candidate.evidence.textStart));
    map.insert(QStringLiteral("textEnd"),
               static_cast<qlonglong>(candidate.evidence.textEnd));
    return map;
}

QVariantList CandidateExtractionController::candidates() const
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(candidates_.size()));
    for (const core::ProfileFieldCandidate &candidate : candidates_) {
        list.push_back(candidateToVariant(candidate));
    }
    return list;
}

void CandidateExtractionController::setState(State state, QString failureToken,
                                             QString failureText)
{
    state_ = state;
    failureToken_ = std::move(failureToken);
    failureText_ = std::move(failureText);
    emit stateChanged();
}

bool CandidateExtractionController::isConsentGranted(
    const std::string &documentId, const std::string &contentHash) const
{
    // Consent scope = document identity + content identity, session-only.
    for (const auto &grant : grantedConsents_) {
        if (grant.first == documentId && grant.second == contentHash) {
            return true;
        }
    }
    return false;
}

void CandidateExtractionController::requestExtraction()
{
    if (manualController_ == nullptr || manualController_->selectedIndex() < 0) {
        setState(State::Failed, QString::fromLatin1(kFailureNoSelection),
                 tr("未选择说明书"));
        return;
    }
    // The selection is owned by ManualImportController; the orchestrator reads
    // it and never mutates Manual state (§9).
    const QVariantMap selected = manualController_->selectedDocument();
    const QString contentHash = selected.value(QStringLiteral("contentHash")).toString();
    bool ok = false;
    // C1b canonical extracted text, straight from the managed cache. NEVER the
    // original file, and never previewText() (which carries PDF display-only
    // page headers that are not cache truth).
    const QString canonicalText = ManualStore::loadText(contentHash, &ok);
    if (!ok) {
        setState(State::Failed, QString::fromLatin1(kFailureNoCanonicalText),
                 tr("无法读取已抽取的说明书文本"));
        return;
    }
    core::ManualDocument document;
    document.documentId =
        selected.value(QStringLiteral("documentId")).toString().toStdString();
    document.contentHash = contentHash.toStdString();
    document.originalFileName =
        selected.value(QStringLiteral("originalFileName")).toString().toStdString();
    document.originalPath =
        selected.value(QStringLiteral("originalPath")).toString().toStdString();
    document.documentType = core::ManualDocumentType::Txt;
    requestExtractionFor(document, canonicalText.toStdString());
}

void CandidateExtractionController::requestExtractionFor(
    const core::ManualDocument &document, const std::string &canonicalExtractedText)
{
    if (state_ == State::Running
        && activeAttempt_.documentId == document.documentId
        && activeAttempt_.contentHash == document.contentHash) {
        // single-flight for the SAME document/content: a duplicate trigger for
        // the attempt already in flight is ignored.
        return;
    }
    // A request for a DIFFERENT document/content SUPERSEDES the in-flight
    // attempt: the new attempt gets a new generation, so the older completion
    // is dropped by the stale guard instead of contaminating the new
    // selection (§9). Nothing about Manual state is cancelled or rewritten.
    // ---- THE SAFETY GATE (§6) -------------------------------------------
    // Without a grant for THIS document/content the attempt stops here and
    // calls NOTHING. This is the most important gate in the slice.
    if (!isConsentGranted(document.documentId, document.contentHash)) {
        pendingDocument_ = document;
        pendingCanonicalText_ = canonicalExtractedText;
        setState(State::ConsentRequired, QString(), QString());
        return;
    }
    beginAttempt(document, canonicalExtractedText);
}

void CandidateExtractionController::grantConsent()
{
    if (state_ != State::ConsentRequired) {
        return;
    }
    // Recorded for (documentId, contentHash) in THIS session only. A different
    // document, or the same document with a different content identity, needs a
    // new grant (O05 / O06).
    bool known = false;
    for (const auto &grant : grantedConsents_) {
        if (grant.first == pendingDocument_.documentId
            && grant.second == pendingDocument_.contentHash) {
            known = true;
            break;
        }
    }
    if (!known) {
        grantedConsents_.emplace_back(pendingDocument_.documentId,
                                      pendingDocument_.contentHash);
    }
    beginAttempt(pendingDocument_, pendingCanonicalText_);
}

void CandidateExtractionController::rejectConsent()
{
    if (state_ != State::ConsentRequired) {
        return;
    }
    // Reject/Cancel is NOT Candidate Reject (§13): nothing about the Candidate
    // set, the verified Profile or the manuals changes. It is also not
    // persisted, so a later explicit attempt asks again.
    pendingDocument_ = core::ManualDocument{};
    pendingCanonicalText_.clear();
    setState(State::Idle, QString(), QString());
}

void CandidateExtractionController::beginAttempt(
    const core::ManualDocument &document, const std::string &canonicalExtractedText)
{
    // One generation per attempt. A completion carrying an older generation is
    // dropped, so a late result can never contaminate a new selection (§9).
    ++generation_;
    activeAttempt_.generation = generation_;
    activeAttempt_.documentId = document.documentId;
    activeAttempt_.contentHash = document.contentHash;
    // The canonical text of THIS attempt is what the local Evidence validation
    // must run against at completion time.
    lastCanonicalText_ = canonicalExtractedText;

    const core::ExtractionRequest request =
        core::buildC2FirstSliceExtractionRequest(document, canonicalExtractedText);

    setState(State::Running, QString(), QString());

    if (runner_ == nullptr) {
        setState(State::Failed, QString::fromLatin1(kFailureNotConfigured),
                 tr("AI 提取未配置"));
        return;
    }

    const bool started = runner_->begin(
        request, activeAttempt_.generation,
        [this](const ICandidateExtractionRunner::Completion &done) {
            completeAttempt(done);
        });
    if (!started) {
        // Nothing was sent. The previous successful Candidate set is preserved.
        setState(State::Failed, QString::fromLatin1(kFailureNotConfigured),
                 tr("AI 提取未配置"));
    }
}

void CandidateExtractionController::completeAttempt(
    const ICandidateExtractionRunner::Completion &done)
{
    // ---- STALE GUARD (§9) ----------------------------------------------
    // A completion for a superseded generation, or for a document/content that
    // is no longer the one the attempt was started for, is discarded.
    if (done.generation != activeAttempt_.generation) {
        return;
    }
    if (state_ != State::Running) {
        return;
    }

    if (!done.ok) {
        // Failure: the OLD successful Candidate set stays exactly as it was
        // (§8). Nothing is cleared, partially replaced or mutated.
        QString token = QString::fromUtf8(
            core::providerExtractionFailureToken(done.failure));
        setState(State::Failed, token, tr("AI 提取失败"));
        return;
    }

    // ---- SESSION M/N local Evidence validation --------------------------
    // The provider proposals are untrusted until this runs: an excerpt that
    // cannot be located uniquely in the canonical text never becomes a
    // Candidate (the accepted M/N invariant).
    core::ManualDocument document;
    document.documentId = activeAttempt_.documentId;
    document.contentHash = activeAttempt_.contentHash;

    ProposalReplayProvider provider(done.proposals);
    const core::CandidateExtractionResult validated =
        core::extractProfileFieldCandidates(document, lastCanonicalText_, provider);

    if (validated.candidates.empty()) {
        // Evidence rejection counts as a failure (§8): a run that yields
        // nothing must NOT clear the old successful Candidate set.
        setState(State::Failed, QString::fromLatin1(kFailureNoCandidate),
                 tr("AI 结果未通过本地证据验证"));
        return;
    }

    // ---- ATOMIC REPLACEMENT (§8) ---------------------------------------
    // Built first, swapped only when complete: no partial mixture is possible.
    std::vector<core::ProfileFieldCandidate> replacement = validated.candidates;
    candidates_ = std::move(replacement);
    candidatesDocumentId_ = activeAttempt_.documentId;
    candidatesContentHash_ = activeAttempt_.contentHash;
    setState(State::Succeeded, QString(), QString());
    emit candidatesChanged();
}

// ---------------------------------------------------------------------------
// M12-C C3 Human review (T027 §81). The ONLY bridge between the session-only
// PendingReview set and the verified DeviceProfile draft. No persistence, no
// provider call, no second pipeline: consumption is session-only (C3-H5/H6)
// and the draft write goes through the single controlled ProfileController
// staged-copy API (C3-H2/H10).
// ---------------------------------------------------------------------------

bool CandidateExtractionController::candidateFromVariant(
    const QVariantMap &map, core::ProfileFieldCandidate &out)
{
    // Exact inverse of candidateToVariant: an unknown target token, a missing
    // field or a malformed offset means this map was never a pending Candidate
    // of this session — refuse, never guess.
    const QString targetToken = map.value(QStringLiteral("targetField")).toString();
    core::ProfileFieldTarget target{};
    if (!core::profileFieldTargetFromToken(targetToken.toStdString(), target)) {
        return false;
    }
    const QVariant evidenceStart =
        map.value(QStringLiteral("textStart"), qlonglong{-1});
    const QVariant evidenceEnd = map.value(QStringLiteral("textEnd"), qlonglong{-1});
    if (!evidenceStart.canConvert<qlonglong>()
        || !evidenceEnd.canConvert<qlonglong>()) {
        return false;
    }

    core::ProfileFieldCandidate candidate;
    candidate.target = target;
    candidate.proposedValue =
        map.value(QStringLiteral("proposedValue")).toString().toStdString();
    candidate.evidence.documentId =
        map.value(QStringLiteral("documentId")).toString().toStdString();
    candidate.evidence.contentHash =
        map.value(QStringLiteral("contentHash")).toString().toStdString();
    candidate.evidence.textStart = evidenceStart.value<qlonglong>();
    candidate.evidence.textEnd = evidenceEnd.value<qlonglong>();
    candidate.evidence.excerpt =
        map.value(QStringLiteral("evidenceExcerpt")).toString().toStdString();
    candidate.lifecycle = core::CandidateLifecycleState::PendingReview;
    out = std::move(candidate);
    return true;
}

int CandidateExtractionController::findPendingCandidate(
    const core::ProfileFieldCandidate &candidate)
{
    for (std::size_t i = 0; i < candidates_.size(); ++i) {
        if (candidates_[i] == candidate) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool CandidateExtractionController::revalidateEvidence(
    const core::ProfileFieldCandidate &candidate)
{
    // C3-H3 (1)/(2): the referenced document must still resolve, and its
    // content identity must still be the one the evidence was bound to.
    const std::vector<core::ManualDocument> documents = ManualStore::loadAll();
    const auto record = std::find_if(
        documents.begin(), documents.end(), [&](const core::ManualDocument &d) {
            return d.documentId == candidate.evidence.documentId;
        });
    if (record == documents.end()) {
        setReviewError(QString::fromLatin1(kReviewEvidenceDocumentMissing),
                       tr("候选引用的说明书已不存在，无法接受"));
        return false;
    }
    if (record->contentHash != candidate.evidence.contentHash) {
        setReviewError(QString::fromLatin1(kReviewEvidenceContentMismatch),
                       tr("候选引用的说明书内容已变化，无法接受"));
        return false;
    }
    // C3-H3 (3): the canonical extracted text must still resolve.
    bool textOk = false;
    const QString canonicalText =
        ManualStore::loadText(QString::fromStdString(candidate.evidence.contentHash),
                              &textOk);
    if (!textOk) {
        setReviewError(QString::fromLatin1(kReviewEvidenceSourceMissing),
                       tr("候选引用的已抽取文本无法读取，无法接受"));
        return false;
    }
    // C3-H3 (4): the exact excerpt + location must still round-trip against
    // the canonical truth, under the same deterministic rules as generation.
    const auto revalidation = core::revalidateCandidateEvidence(
        candidate.evidence, canonicalText.toStdString());
    if (revalidation != core::CandidateEvidenceRevalidationCode::Ok) {
        setReviewError(QString::fromLatin1(kReviewEvidenceInvalid),
                       tr("候选证据未通过重新验证（%1）")
                           .arg(QString::fromUtf8(
                               core::candidateEvidenceRevalidationCodeName(
                                   revalidation))));
        return false;
    }
    return true;
}

void CandidateExtractionController::consumeCandidate(
    core::ProfileFieldCandidate &pending,
    core::CandidateLifecycleState consumedState)
{
    // C3-H5/H6: mark + remove. The consumed copy lives only in this session's
    // list and is never persisted anywhere.
    pending.lifecycle = consumedState;
    if (consumedState == core::CandidateLifecycleState::Accepted) {
        consumedAccepted_.push_back(pending);
    } else {
        consumedRejected_.push_back(pending);
    }
    candidates_.erase(candidates_.begin()
                      + static_cast<std::ptrdiff_t>(
                          &pending - candidates_.data()));
    emit candidatesChanged();
}

bool CandidateExtractionController::acceptCandidate(const QVariantMap &candidateMap)
{
    clearReviewError();
    core::ProfileFieldCandidate candidate;
    if (!candidateFromVariant(candidateMap, candidate)) {
        setReviewError(QString::fromLatin1(kReviewCandidateNotPending),
                       tr("该候选不在待审核集合中"));
        return false;
    }
    const int index = findPendingCandidate(candidate);
    if (index < 0) {
        // Already consumed (C3-H5/H6) or never pending: never act again.
        setReviewError(QString::fromLatin1(kReviewCandidateNotPending),
                       tr("该候选不在待审核集合中"));
        return false;
    }

    // C3-H4: the authoritative target is the CURRENTLY shown selected Profile
    // + draft. Without a valid target there is nothing to accept onto.
    if (profileController_ == nullptr || !profileController_->hasOpenProfile()) {
        setReviewError(QString::fromLatin1(kReviewProfileTargetMissing),
                       tr("没有当前有效的设备档案目标，无法接受"));
        return false;
    }

    // C3-H3: deterministic evidence freshness gate BEFORE any draft change.
    if (!revalidateEvidence(candidates_[static_cast<std::size_t>(index)])) {
        return false;
    }

    // C3-H2: the controlled draft write. Only the first-slice target is
    // accepted there; anything else is an explicit refusal with the draft
    // untouched (the Candidate stays pending for Human handling).
    const bool applied = profileController_->applyCandidateField(
        QString::fromUtf8(core::profileFieldTargetToken(candidates_[index].target)),
        QString::fromStdString(candidates_[index].proposedValue));
    if (!applied) {
        setReviewError(QString::fromLatin1(kReviewApplyFailed),
                       profileController_->lastActionError());
        return false;
    }

    consumeCandidate(candidates_[static_cast<std::size_t>(index)],
                     core::CandidateLifecycleState::Accepted);
    return true;
}

bool CandidateExtractionController::rejectCandidate(const QVariantMap &candidateMap)
{
    clearReviewError();
    core::ProfileFieldCandidate candidate;
    if (!candidateFromVariant(candidateMap, candidate)) {
        setReviewError(QString::fromLatin1(kReviewCandidateNotPending),
                       tr("该候选不在待审核集合中"));
        return false;
    }
    const int index = findPendingCandidate(candidate);
    if (index < 0) {
        setReviewError(QString::fromLatin1(kReviewCandidateNotPending),
                       tr("该候选不在待审核集合中"));
        return false;
    }
    // C3-H6: Reject mutates NOTHING — no draft, no persisted profile, no
    // consent state, no future extraction behavior.
    consumeCandidate(candidates_[static_cast<std::size_t>(index)],
                     core::CandidateLifecycleState::Rejected);
    return true;
}

bool CandidateExtractionController::seedReviewCandidatesForAutomation(
    const QVariantMap &documentMap, const QString &canonicalExtractedText,
    const QVariantList &proposals)
{
    // TEST/AUTOMATION seam: the proposals flow through the REAL deterministic
    // C2 validator (same function, same H6/H8 rules as a live extraction), so
    // what lands in the review set is exactly what production could produce —
    // without any provider, credential or socket.
    core::ManualDocument document;
    document.documentId =
        documentMap.value(QStringLiteral("documentId")).toString().toStdString();
    document.contentHash =
        documentMap.value(QStringLiteral("contentHash")).toString().toStdString();
    document.originalFileName =
        documentMap.value(QStringLiteral("originalFileName"))
            .toString()
            .toStdString();
    document.originalPath =
        documentMap.value(QStringLiteral("originalPath")).toString().toStdString();
    document.documentType = core::ManualDocumentType::Txt;

    std::vector<core::CandidateProposal> parsed;
    parsed.reserve(static_cast<std::size_t>(proposals.size()));
    for (const QVariant &entry : proposals) {
        const QVariantMap map = entry.toMap();
        core::ProfileFieldTarget target{};
        if (!core::profileFieldTargetFromToken(
                map.value(QStringLiteral("target")).toString().toStdString(),
                target)) {
            continue;
        }
        core::CandidateProposal proposal;
        proposal.target = target;
        proposal.proposedValue =
            map.value(QStringLiteral("proposedValue")).toString().toStdString();
        proposal.evidenceExcerpt =
            map.value(QStringLiteral("evidenceExcerpt")).toString().toStdString();
        parsed.push_back(std::move(proposal));
    }

    ProposalReplayProvider provider(std::move(parsed));
    const core::CandidateExtractionResult validated = core::extractProfileFieldCandidates(
        document, canonicalExtractedText.toStdString(), provider);

    // Same atomic-replacement discipline as a successful extraction.
    candidates_ = validated.candidates;
    candidatesDocumentId_ = document.documentId;
    candidatesContentHash_ = document.contentHash;
    setState(State::Succeeded, QString(), QString());
    emit candidatesChanged();
    return true;
}

} // namespace modbuslens::ui