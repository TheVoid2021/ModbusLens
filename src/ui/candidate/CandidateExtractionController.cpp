#include "ui/candidate/CandidateExtractionController.h"

#include <QVariantMap>

#include <utility>

#include "core/candidate/CandidateExtraction.h"
#include "core/candidate/ProviderExtractionContract.h"
#include "ui/manual/ManualStore.h"

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

} // namespace modbuslens::ui