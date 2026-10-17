#include "ui/candidate/CandidateExtractionController.h"

#include "ui/ai/AiRuntimeConfiguration.h"

#include <QVariantMap>

#include <QDateTime>
#include <QFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

#include "core/candidate/CandidateExtraction.h"
#include "core/candidate/ProviderExtractionContract.h"
#include "core/profile/DeviceProfile.h"
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
// T033: a PDF whose extraction state is no_extractable_text must fail with
// the ACCURATE classification, not the generic no_canonical_text.
constexpr char kFailureNoExtractableText[] = "no_extractable_text";
constexpr char kFailureNotConfigured[] = "not_configured";
constexpr char kFailureNoCandidate[] = "no_candidate";

// ---------------------------------------------------------------------------
// T034-B1 live RCA — SANITIZED, EXPLICITLY GATED evidence-validation forensics
// (MODBUSLENS_PROVIDER_FORENSICS=1; unset => ZERO output). Records structural
// metadata + the REAL refusal branches of the frozen generation-time validator
// (unsupported_target / empty_value / evidence excerpt not found / not unique)
// by re-running the SAME deterministic lookup read-only. NEVER logged: value
// text, evidence text, canonical text, provider response, credentials.
// Human decision pending: delete before the B1 commit, or converge into a
// default-off controlled diagnostic.
// ---------------------------------------------------------------------------
[[nodiscard]] bool evidenceForensicsEnabled()
{
    return qEnvironmentVariable("MODBUSLENS_PROVIDER_FORENSICS")
           == QLatin1String("1");
}

// EXACT-ONLY read-only counter for the PROFILE-FIELD forensics block. The
// manufacturer validator is exact-only (Human §5/§10 freeze it), so its
// forensics must stay exact-only too — using the shared rule here would make
// the log claim a line-ending hit for an excerpt the manufacturer validator
// actually REFUSED, i.e. forensics lying in the opposite direction (§6).
[[nodiscard]] int excerptExactMatchCount(std::string_view canonicalText,
                                         std::string_view excerpt)
{
    if (excerpt.empty()) {
        return -1;
    }
    const std::size_t first = canonicalText.find(excerpt);
    if (first == std::string_view::npos) {
        return 0;
    }
    if (canonicalText.find(excerpt, first + 1) != std::string_view::npos) {
        return 2;
    }
    return 1;
}

// Read-only forensics counter. T035-A.3.2.3: it now delegates to the SAME
// deterministic locate rule the judgment path uses, so forensics can never
// report `evidence_not_found` for an excerpt the validator actually ACCEPTED
// (Human §6). It remains a pure read — it classifies, it never decides.
//   1  = uniquely locatable under the shared rule (exact or line-ending)
//   0  = not found
//   2  = ambiguous (two or more)
//  -1  = empty excerpt (its own refusal class)
[[nodiscard]] int excerptMatchCount(std::string_view canonicalText,
                                    std::string_view excerpt)
{
    if (excerpt.empty()) {
        return -1; // empty excerpt is its own refusal class
    }
    std::int64_t start = -1;
    std::int64_t end = -1;
    switch (core::locateEvidenceExcerpt(canonicalText, excerpt, start, end)) {
    case core::EvidenceLocateCode::Located:
        return 1;
    case core::EvidenceLocateCode::NotFound:
        return 0;
    case core::EvidenceLocateCode::Ambiguous:
        return 2;
    }
    return 0;
}

// Which rule actually produced a successful location — reported by forensics as
// a sanitized token so a live RCA can tell an exact hit from a line-ending
// fallback hit. It never carries any text, only the rule name.
[[nodiscard]] QString evidenceMatchMode(std::string_view canonicalText,
                                        std::string_view excerpt)
{
    if (excerpt.empty()) {
        return QStringLiteral("none");
    }
    // Exact first, mirroring the shared rule: an exact hit is never labelled a
    // fallback, so the token cannot overstate the normalization's effect.
    if (canonicalText.find(excerpt) != std::string_view::npos) {
        return QStringLiteral("exact");
    }
    std::int64_t start = -1;
    std::int64_t end = -1;
    if (core::locateEvidenceExcerpt(canonicalText, excerpt, start, end)
        == core::EvidenceLocateCode::Located) {
        return QStringLiteral("line_ending_normalized");
    }
    return QStringLiteral("none");
}

void writeEvidenceForensics(
    const std::vector<core::CandidateProposal> &proposals,
    std::string_view canonicalText,
    const core::CandidateExtractionResult &result)
{
    if (!evidenceForensicsEnabled()) {
        return;
    }
    QString block = QStringLiteral(
                        "[evidence forensics] %1 (sanitized: enums/lengths/counts only "
                        "— no value/evidence/canonical text, no provider response)")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    block += QStringLiteral("\n  proposals_count=%1 refused_total=%2 candidates=%3")
                 .arg(proposals.size())
                 .arg(result.refusedProposalCount)
                 .arg(result.candidates.size());
    for (std::size_t index = 0; index < proposals.size(); ++index) {
        const core::CandidateProposal &proposal = proposals[index];
        const QString targetToken = QString::fromUtf8(
            core::profileFieldTargetToken(proposal.target));
        const bool unsupported =
            !core::isC2FirstSliceProfileField(proposal.target);
        const bool emptyValue = proposal.proposedValue.empty();
        const int matches =
            excerptExactMatchCount(canonicalText, proposal.evidenceExcerpt);
        QString stage = QStringLiteral("accepted");
        if (unsupported) {
            stage = QStringLiteral("schema:unsupported_target");
        } else if (emptyValue) {
            stage = QStringLiteral("schema:empty_value");
        } else if (matches == -1) {
            stage = QStringLiteral("evidence:evidence_excerpt_missing");
        } else if (matches == 0) {
            stage = QStringLiteral("evidence:evidence_not_found");
        } else if (matches >= 2) {
            stage = QStringLiteral("evidence:evidence_not_unique");
        }
        block += QStringLiteral(
                     "\n  item[%1] target=%2 value_len=%3 evidence_len=%4 "
                     "match_count=%5 stage=%6")
                     .arg(index)
                     .arg(targetToken)
                     .arg(static_cast<int>(proposal.proposedValue.size()))
                     .arg(static_cast<int>(proposal.evidenceExcerpt.size()))
                     .arg(matches)
                     .arg(stage);
    }
    qWarning().noquote() << block;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + QStringLiteral("/modbuslens_provider_forensics.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(block.toUtf8());
        file.write("\n");
    }
}

// ---------------------------------------------------------------------------
// T035-A.3.2.1 — REGISTER VALIDATION forensics for the controlled apply
// path. Sanitized exactly like the evidence forensics: enums/lengths/flags
// only — no register name/unit/description values, no canonical text, no
// provider response. stage = validation:<profile validation token> (e.g.
// duplicate_address) so a live RCA can tell the APPLY stage apart from the
// extraction stages. The existing profile-field evidence forensics above is
// untouched.
// ---------------------------------------------------------------------------
void writeRegisterValidationForensics(const QVariantMap &fields,
                                      const QString &errorToken)
{
    if (!evidenceForensicsEnabled()) {
        return;
    }
    QString block = QStringLiteral(
                        "[register validation forensics] %1 (sanitized: "
                        "enums/lengths/flags only — no name/unit/description "
                        "values, no canonical text, no provider response)")
                        .arg(QDateTime::currentDateTime().toString(
                            Qt::ISODateWithMs));
    block += QStringLiteral("\n  stage=validation:%1").arg(errorToken);
    block += QStringLiteral(
                 "\n  function_code=%1 address=%2 data_type=%3 quantity=%4")
                 .arg(fields.value(QStringLiteral("functionCode")).toInt())
                 .arg(fields.value(QStringLiteral("address")).toInt())
                 .arg(fields.value(QStringLiteral("dataType")).toString())
                 .arg(fields.value(QStringLiteral("quantity")).toInt());
    block += QStringLiteral(
                 "\n  name_len=%1 description_len=%2 unit_len=%3 "
                 "evidence_len=%4 has_scale=%5 has_offset=%6")
                 .arg(fields.value(QStringLiteral("name")).toString().size())
                 .arg(fields.value(QStringLiteral("description"))
                          .toString()
                          .size())
                 .arg(fields.value(QStringLiteral("unit")).toString().size())
                 .arg(fields.value(QStringLiteral("evidenceExcerpt"))
                          .toString()
                          .size())
                 .arg(fields.contains(QStringLiteral("scale"))
                          ? QStringLiteral("1")
                          : QStringLiteral("0"))
                 .arg(fields.contains(QStringLiteral("offset"))
                          ? QStringLiteral("1")
                          : QStringLiteral("0"));
    qWarning().noquote() << block;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + QStringLiteral("/modbuslens_provider_forensics.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(block.toUtf8());
        file.write("\n");
    }
}

// ---------------------------------------------------------------------------
// T035-A.3.2.2 — REGISTER EVIDENCE forensics for the production register
// completion. Same gate/sanitization discipline as the profile-field block:
// enums/lengths/counts only. The stage vocabulary covers the §6 families:
//   schema:name_missing / schema:invalid_function_code /
//   schema:unsupported_data_type  (domain shape, recomputed locally)
//   validation:quantity_mismatch / validation:non_finite_scale /
//   validation:non_finite_offset  (deterministic field rules)
//   evidence:evidence_excerpt_missing / evidence:evidence_not_found /
//   evidence:evidence_not_unique  (H6, shared locate rule — T035-A.3.2.3)
//   accepted
// T035-A.3.2.3: `match_mode` records WHICH rule located the excerpt —
// `exact` | `line_ending_normalized` | `none`. Forensics shares the validator's
// rule, so a stage other than `accepted` can never disagree with the judgment
// path (Human §6). No text is ever logged.
// ---------------------------------------------------------------------------
void writeRegisterEvidenceForensics(
    const std::vector<core::RegisterCandidateProposal> &proposals,
    std::string_view canonicalText,
    const core::RegisterCandidateExtractionResult &result)
{
    if (!evidenceForensicsEnabled()) {
        return;
    }
    QString block = QStringLiteral(
                        "[register evidence forensics] %1 (sanitized: "
                        "enums/lengths/counts only — no name/unit values, no "
                        "canonical text, no provider response)")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    block += QStringLiteral("\n  proposals_count=%1 refused_total=%2 candidates=%3")
                 .arg(proposals.size())
                 .arg(result.refusedProposalCount)
                 .arg(result.candidates.size());
    for (std::size_t index = 0; index < proposals.size(); ++index) {
        const core::RegisterCandidateProposal &proposal = proposals[index];
        const int wordCount =
            core::registerDecodeTypeWordCount(proposal.dataType);
        const int matches =
            excerptMatchCount(canonicalText, proposal.evidenceExcerpt);
        QString stage = QStringLiteral("accepted");
        if (proposal.name.empty()) {
            stage = QStringLiteral("schema:name_missing");
        } else if (proposal.functionCode < 0x01
                   || proposal.functionCode > 0x7F) {
            stage = QStringLiteral("schema:invalid_function_code");
        } else if (wordCount == 0) {
            stage = QStringLiteral("schema:unsupported_data_type");
        } else if (proposal.quantity != wordCount) {
            stage = QStringLiteral("validation:quantity_mismatch");
        } else if (!std::isfinite(proposal.scale)) {
            stage = QStringLiteral("validation:non_finite_scale");
        } else if (!std::isfinite(proposal.offset)) {
            stage = QStringLiteral("validation:non_finite_offset");
        } else if (matches == -1) {
            stage = QStringLiteral("evidence:evidence_excerpt_missing");
        } else if (matches == 0) {
            stage = QStringLiteral("evidence:evidence_not_found");
        } else if (matches >= 2) {
            stage = QStringLiteral("evidence:evidence_not_unique");
        }
        block += QStringLiteral(
                     "\n  item[%1] function_code=%2 address=%3 data_type=%4 "
                     "quantity=%5 name_len=%6 evidence_len=%7 match_count=%8 "
                     "match_mode=%9 stage=%10")
                     .arg(index)
                     .arg(static_cast<int>(proposal.functionCode))
                     .arg(static_cast<int>(proposal.address))
                     .arg(QString::fromUtf8(
                         core::profileDataTypeToken(proposal.dataType)))
                     .arg(proposal.quantity)
                     .arg(static_cast<int>(proposal.name.size()))
                     .arg(static_cast<int>(proposal.evidenceExcerpt.size()))
                     .arg(matches)
                     .arg(evidenceMatchMode(canonicalText,
                                            proposal.evidenceExcerpt))
                     .arg(stage);
    }
    qWarning().noquote() << block;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + QStringLiteral("/modbuslens_provider_forensics.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(block.toUtf8());
        file.write("\n");
    }
}

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
    ownedRunner_ = std::make_unique<CandidateProviderRunner>();
    runner_ = ownedRunner_.get();
}

CandidateExtractionController::CandidateExtractionController(
    ICandidateExtractionRunner &runner, QObject *parent)
    : QObject(parent), runner_(&runner)
{
}

void CandidateExtractionController::setRunnerForAutomation(
    ICandidateExtractionRunner *runner)
{
    // nullptr restores the production owned runner (see the header contract).
    runner_ = runner != nullptr ? runner : ownedRunner_.get();
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

QObject *CandidateExtractionController::runtimeConfiguration() const
{
    return runtimeConfiguration_; // AiRuntimeConfiguration IS-A QObject
}

void CandidateExtractionController::setRuntimeConfiguration(QObject *configuration)
{
    auto *casted = qobject_cast<AiRuntimeConfiguration *>(configuration);
    if (runtimeConfiguration_ == casted) {
        return; // same owner re-assigned: no state change, no connection churn
    }
    // T034-B2.1 slice C1.2 — Human-frozen PROVENANCE RULE: an in-flight
    // request belongs to the runtime configuration owner that existed when
    // the request started. An owner identity change (A -> B or A -> nullptr)
    // while an attempt is Running makes that attempt obsolete IMMEDIATELY;
    // its epoch is NEVER rebased to the new owner's revision. The old
    // callback stays permanently stale through the invalidated attempt
    // identity (generation 0) and the idle state — NEVER through the
    // revision guard, which is conditional on having an owner.
    const bool attemptWasRunning = state_ == State::Running;
    if (configurationConnection_) {
        QObject::disconnect(configurationConnection_);
        configurationConnection_ = {};
    }
    runtimeConfiguration_ = casted;
    if (attemptWasRunning) {
        invalidateActiveAttempt();
    }
    if (runtimeConfiguration_ != nullptr) {
        configurationConnection_ = connect(
            runtimeConfiguration_, &AiRuntimeConfiguration::configurationChanged,
            this, &CandidateExtractionController::handleConfigurationChanged);
    }
    emit runtimeConfigurationChanged();
}

void CandidateExtractionController::invalidateActiveAttempt()
{
    // Explicitly invalidate the attempt identity: a late completion for the
    // discarded attempt is rejected by BOTH existing guards (generation no
    // longer matches activeAttempt_; state is no longer Running) — the
    // completion can never publish. The physical provider request may still
    // finish on its own: NETWORK CANCEL remains NOT IMPLEMENTED by design.
    activeAttempt_.generation = 0;
    activeAttempt_.documentId.clear();
    activeAttempt_.contentHash.clear();
    setState(State::Idle, QString(), QString());
}

void CandidateExtractionController::handleConfigurationChanged()
{
    // T034-B2.1 slice C1 — Human-frozen convergence semantics: when the
    // configuration epoch of the IN-FLIGHT attempt is successfully replaced
    // (provider/model save, credential save/replace/delete), the attempt is
    // obsolete IMMEDIATELY. The controller rests at idle — never failed,
    // never succeeded, nothing published, nothing auto-restarted, prior
    // Candidate results / Profile / Manual state untouched. States outside
    // Running are left alone: an unrelated settings change must not rewrite
    // them for bookkeeping.
    if (state_ != State::Running || runtimeConfiguration_ == nullptr) {
        return;
    }
    if (runtimeConfiguration_->configRevision() == pendingConfigRevision_) {
        return;
    }
    invalidateActiveAttempt();
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
    // T035-A.3.1: edited-confirm writes the draft — the register apply path
    // is a separately authorized slice, so a register candidate refuses
    // closed here exactly like acceptCandidate.
    if (candidateMap.value(QStringLiteral("kind")).toString()
        == QLatin1String("register")) {
        setReviewError(QString::fromLatin1(kReviewRegisterApplyNotAvailable),
                       tr("寄存器候选的接受写入尚未开放（等待受控应用路径）。"));
        return deleteResult(false,
                            QString::fromLatin1(kReviewRegisterApplyNotAvailable),
                            lastReviewError());
    }

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
    return static_cast<int>(candidates_.size()
                            + registerCandidates_.size());
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
    // T035-A.3.1 NOTE: the profile-field projection keeps its FROZEN key set
    // (o16 enforces exactly 8 keys as a no-leak guard) — the kind key exists
    // ONLY on the register variant (a new surface, no frozen contract). The
    // review actions route on kind=="register"; an absent kind means the
    // legacy profile-field candidate.
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
    list.reserve(static_cast<qsizetype>(candidates_.size()
                                        + registerCandidates_.size()));
    for (const core::ProfileFieldCandidate &candidate : candidates_) {
        list.push_back(candidateToVariant(candidate));
    }
    // T035-A.3.1: register candidates ride the SAME review projection, after
    // the profile-field entries (deterministic order, one combined list).
    for (const core::RegisterEntryCandidate &candidate : registerCandidates_) {
        list.push_back(registerCandidateToVariant(candidate));
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

bool CandidateExtractionController::prepareSelectedExtraction(
    core::ManualDocument &document, std::string &canonicalText)
{
    if (manualController_ == nullptr || manualController_->selectedIndex() < 0) {
        setState(State::Failed, QString::fromLatin1(kFailureNoSelection),
                 tr("未选择说明书"));
        return false;
    }
    // The selection is owned by ManualImportController; the orchestrator reads
    // it and never mutates Manual state (§9).
    const QVariantMap selected = manualController_->selectedDocument();
    const QString contentHash = selected.value(QStringLiteral("contentHash")).toString();
    // T033: a no_extractable_text document (e.g. a scanned PDF) must fail with
    // the accurate classification BEFORE any cache read — the import succeeded
    // but there is provably no extracted text to send (never OCR'd, never
    // guessed; the frozen C1b semantics).
    if (selected.value(QStringLiteral("extractionStateToken")).toString()
        == QLatin1String("no_extractable_text")) {
        setState(State::Failed,
                 QString::fromLatin1(kFailureNoExtractableText),
                 tr("该说明书不包含可提取的文本（OCR 暂不支持）。"));
        return false;
    }
    bool ok = false;
    // C1b canonical extracted text, straight from the managed cache. NEVER the
    // original file, and never previewText() (which carries PDF display-only
    // page headers that are not cache truth). For a PDF the store aggregates
    // the per-page cache truth deterministically (T033).
    const QString canonical = ManualStore::loadText(contentHash, &ok);
    if (!ok || canonical.isEmpty()) {
        setState(State::Failed, QString::fromLatin1(kFailureNoCanonicalText),
                 tr("无法读取已抽取的说明书文本"));
        return false;
    }
    document.documentId =
        selected.value(QStringLiteral("documentId")).toString().toStdString();
    document.contentHash = contentHash.toStdString();
    document.originalFileName =
        selected.value(QStringLiteral("originalFileName")).toString().toStdString();
    document.originalPath =
        selected.value(QStringLiteral("originalPath")).toString().toStdString();
    // T033: the document identity carries the TRUTHFUL type (derived from the
    // original file name through the frozen extension rule) instead of a
    // hardcoded Txt; the coarse type token is provider format context.
    std::ignore =
        core::manualDocumentTypeFromExtension(document.originalFileName,
                                              document.documentType);
    canonicalText = canonical.toStdString();
    return true;
}

void CandidateExtractionController::requestExtraction()
{
    core::ManualDocument document;
    std::string canonicalText;
    if (!prepareSelectedExtraction(document, canonicalText)) {
        return;
    }
    requestExtractionFor(document, canonicalText);
}

void CandidateExtractionController::requestRegisterExtraction()
{
    // T035-A.3.2.2: the INDEPENDENT register-map Human trigger. Identical
    // preamble and consent semantics; only the request token / prompt /
    // strict parser differ. The manufacturer button's semantics are unchanged.
    core::ManualDocument document;
    std::string canonicalText;
    if (!prepareSelectedExtraction(document, canonicalText)) {
        return;
    }
    requestRegisterExtractionFor(document, canonicalText);
}

void CandidateExtractionController::requestExtractionFor(
    const core::ManualDocument &document, const std::string &canonicalExtractedText)
{
    requestExtractionFor(document, canonicalExtractedText,
                         Attempt::Mode::ProfileField);
}

void CandidateExtractionController::requestRegisterExtractionFor(
    const core::ManualDocument &document, const std::string &canonicalExtractedText)
{
    requestExtractionFor(document, canonicalExtractedText,
                         Attempt::Mode::RegisterMap);
}

void CandidateExtractionController::requestExtractionFor(
    const core::ManualDocument &document, const std::string &canonicalExtractedText,
    Attempt::Mode mode)
{
    if (state_ == State::Running
        && activeAttempt_.documentId == document.documentId
        && activeAttempt_.contentHash == document.contentHash
        && activeAttempt_.mode == mode) {
        // single-flight for the SAME document/content AND mode: a duplicate
        // trigger for the attempt already in flight is ignored. The SAME
        // document in the OTHER mode supersedes (a new generation), because
        // the Human explicitly asked for a different extraction kind.
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
        pendingMode_ = mode;
        setState(State::ConsentRequired, QString(), QString());
        return;
    }
    beginAttempt(document, canonicalExtractedText, mode);
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
    beginAttempt(pendingDocument_, pendingCanonicalText_, pendingMode_);
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
    const core::ManualDocument &document, const std::string &canonicalExtractedText,
    Attempt::Mode mode)
{
    // One generation per attempt. A completion carrying an older generation is
    // dropped, so a late result can never contaminate a new selection (§9).
    ++generation_;
    activeAttempt_.generation = generation_;
    activeAttempt_.documentId = document.documentId;
    activeAttempt_.contentHash = document.contentHash;
    activeAttempt_.mode = mode;
    // The canonical text of THIS attempt is what the local Evidence validation
    // must run against at completion time.
    lastCanonicalText_ = canonicalExtractedText;

    // T035-A.3.2.2: the ONLY routing key is the attempt mode — register_map
    // builds the register request (register prompt + strict register parser
    // downstream); every other mode keeps the byte-for-byte manufacturer
    // request.
    const core::ExtractionRequest request =
        mode == Attempt::Mode::RegisterMap
            ? core::buildRegisterMapExtractionRequest(document,
                                                      canonicalExtractedText)
            : core::buildC2FirstSliceExtractionRequest(document,
                                                       canonicalExtractedText);

    // ---- T034-B2.1 Global AI Configuration (Human §10/§11) --------------
    // Precedence: saved UI configuration (+ Credential Manager) wins; the B1
    // environment fallback applies ONLY when nothing is saved; the snapshot
    // revision is what makes a post-switch completion stale.
    if (runtimeConfiguration_ != nullptr) {
        const std::optional<AiRuntimeConfiguration::ResolvedRequestConfig>
            resolved = runtimeConfiguration_->resolveRequestConfig();
        pendingConfigRevision_ = runtimeConfiguration_->configRevision();
        if (resolved.has_value()) {
            runner_->applyExplicitConfig(resolved->kind, resolved->model,
                                         resolved->secret);
        } else {
            runner_->clearExplicitConfig();
        }
    }

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
    // ---- T034-B2.1 config-revision stale guard (Human §12) -------------
    // A completion produced under an older configuration (the user switched
    // provider/model/credential while this attempt was in flight) must never
    // be presented as the result of the NEW configuration. Slice C1 added the
    // immediate Running->idle convergence on configurationChanged; this guard
    // remains as defense in depth — a late obsolete completion is now
    // typically already rejected by the generation/state guards above, and
    // this check also covers any path that could bypass them.
    if (runtimeConfiguration_ != nullptr
        && runtimeConfiguration_->configRevision() != pendingConfigRevision_) {
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

    // T035-A.3.2.2: the attempt mode routes the completion to the matching
    // deterministic validator. The profile-field path below is UNTOUCHED.
    if (activeAttempt_.mode == Attempt::Mode::RegisterMap) {
        completeRegisterAttempt(done);
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
    writeEvidenceForensics(done.proposals, lastCanonicalText_, validated);

    if (validated.candidates.empty()) {
        // Evidence rejection counts as a failure (§8): a run that yields
        // nothing must NOT clear the old successful Candidate set.
        setState(State::Failed, QString::fromLatin1(kFailureNoCandidate),
                 tr("AI 结果未通过本地证据验证"));
        return;
    }

    // ---- ATOMIC REPLACEMENT (§8) ---------------------------------------
    // Built first, swapped only when complete: no partial mixture is possible.
    // T035-A.3.2.2: the review surface is replaced WHOLESALE — a successful
    // profile-field extraction also clears the register queue (one
    // document's review set = the latest successful extraction, never a
    // mixture of two runs).
    std::vector<core::ProfileFieldCandidate> replacement = validated.candidates;
    candidates_ = std::move(replacement);
    registerCandidates_.clear();
    candidatesDocumentId_ = activeAttempt_.documentId;
    candidatesContentHash_ = activeAttempt_.contentHash;
    setState(State::Succeeded, QString(), QString());
    emit candidatesChanged();
}

void CandidateExtractionController::completeRegisterAttempt(
    const ICandidateExtractionRunner::Completion &done)
{
    // T035-A.3.2.2: the REGISTER completion path. The provider proposals are
    // untrusted until the REAL deterministic register validator runs: an
    // excerpt that cannot be located uniquely in the canonical text (or any
    // v1 field violation) never becomes a Candidate.
    core::ManualDocument document;
    document.documentId = activeAttempt_.documentId;
    document.contentHash = activeAttempt_.contentHash;

    const core::RegisterCandidateExtractionResult validated =
        core::extractRegisterCandidates(document, lastCanonicalText_,
                                        done.registerProposals);
    writeRegisterEvidenceForensics(done.registerProposals, lastCanonicalText_,
                                   validated);

    if (validated.candidates.empty()) {
        // Evidence rejection counts as a failure (§8): a run that yields
        // nothing must NOT clear the old successful Candidate set.
        setState(State::Failed, QString::fromLatin1(kFailureNoCandidate),
                 tr("AI 结果未通过本地证据验证"));
        return;
    }

    // ---- ATOMIC REPLACEMENT (§8) ---------------------------------------
    // The attempt mode is authoritative for the WHOLE review surface: a
    // register extraction replaces the register queue AND clears the
    // profile-field queue (one document's review set = the latest
    // successful extraction, never a mixture of two runs).
    std::vector<core::RegisterEntryCandidate> replacement = validated.candidates;
    registerCandidates_ = std::move(replacement);
    candidates_.clear();
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

bool CandidateExtractionController::revalidateRegisterEvidence(
    const core::RegisterEntryCandidate &candidate)
{
    // T035-A.3.2.1: the C3-H3 freshness gate for REGISTER candidates — the
    // same four deterministic checks as the profile-field gate, typed for
    // the register candidate. No provider input, nothing guessed.
    const std::vector<core::ManualDocument> documents = ManualStore::loadAll();
    const auto record = std::find_if(
        documents.begin(), documents.end(),
        [&](const core::ManualDocument &d) {
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
    bool textOk = false;
    const QString canonicalText =
        ManualStore::loadText(QString::fromStdString(candidate.evidence.contentHash),
                              &textOk);
    if (!textOk) {
        setReviewError(QString::fromLatin1(kReviewEvidenceSourceMissing),
                       tr("候选引用的已抽取文本无法读取，无法接受"));
        return false;
    }
    // T035-A.3.2.3: the REGISTER revalidation uses the SAME deterministic
    // evidence rule as register GENERATION time (exact first, then the
    // line-ending-normalized fallback). Using the shared profile-field
    // revalidation here would have been WRONG in the other direction: a
    // candidate that legitimately passed extraction would fail Accept purely
    // on a CRLF/LF difference (Human §5: extraction and Accept must agree).
    const auto revalidation = core::revalidateRegisterCandidateEvidence(
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
    // T035-A.3.2.1: the register APPLY path is now OPEN — but ONLY through
    // the controlled staged-draft write (ProfileController::
    // applyRegisterCandidate: whole-draft copy -> full validateDeviceProfile
    // -> commit-to-draft once; never a Save / any persistence). A validation
    // failure is fail-closed: the candidate stays PendingReview and the
    // draft is untouched.
    if (candidateMap.value(QStringLiteral("kind")).toString()
        == QLatin1String("register")) {
        core::RegisterEntryCandidate candidate;
        if (!registerCandidateFromVariant(candidateMap, candidate)) {
            setReviewError(QString::fromLatin1(kReviewCandidateNotPending),
                           tr("该候选不在待审核集合中"));
            return false;
        }
        const int index = findPendingRegisterCandidate(candidate);
        if (index < 0) {
            // Already consumed (C3-H5/H6) or never pending: never act again.
            setReviewError(QString::fromLatin1(kReviewCandidateNotPending),
                           tr("该候选不在待审核集合中"));
            return false;
        }
        // C3-H4: the authoritative target is the CURRENTLY shown selected
        // Profile + draft. Without a valid target there is nothing to accept
        // onto.
        if (profileController_ == nullptr
            || !profileController_->hasOpenProfile()) {
            setReviewError(QString::fromLatin1(kReviewProfileTargetMissing),
                           tr("没有当前有效的设备档案目标，无法接受"));
            return false;
        }
        // C3-H3: deterministic evidence freshness gate BEFORE any draft
        // change (register mirror of the profile-field gate).
        if (!revalidateRegisterEvidence(
                registerCandidates_[static_cast<std::size_t>(index)])) {
            return false;
        }
        // C3-H2: the controlled staged-draft write. The authoritative value
        // source is the in-store candidate projection (never the UI echo).
        const bool applied = profileController_->applyRegisterCandidate(
            registerCandidateToVariant(
                registerCandidates_[static_cast<std::size_t>(index)]));
        if (!applied) {
            // T035-A.3.2.1 forensics: the validation stage is traceable as
            // validation:<profile token> (sanitized — no register values).
            writeRegisterValidationForensics(
                registerCandidateToVariant(
                    registerCandidates_[static_cast<std::size_t>(index)]),
                profileController_->lastActionErrorToken());
            setReviewError(QString::fromLatin1(kReviewApplyFailed),
                           profileController_->lastActionError());
            return false;
        }
        consumeRegisterCandidate(
            registerCandidates_[static_cast<std::size_t>(index)],
            core::CandidateLifecycleState::Accepted);
        return true;
    }
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
    // T035-A.3.1: a register candidate is rejected through its OWN value
    // path — Reject mutates NOTHING (C3-H6), it only consumes the pending
    // copy into the session-only rejected list.
    if (candidateMap.value(QStringLiteral("kind")).toString()
        == QLatin1String("register")) {
        core::RegisterEntryCandidate candidate;
        if (!registerCandidateFromVariant(candidateMap, candidate)) {
            setReviewError(QString::fromLatin1(kReviewCandidateNotPending),
                           tr("该候选不在待审核集合中"));
            return false;
        }
        const int index = findPendingRegisterCandidate(candidate);
        if (index < 0) {
            setReviewError(QString::fromLatin1(kReviewCandidateNotPending),
                           tr("该候选不在待审核集合中"));
            return false;
        }
        consumeRegisterCandidate(
            registerCandidates_[static_cast<std::size_t>(index)],
            core::CandidateLifecycleState::Rejected);
        return true;
    }
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

QVariantMap CandidateExtractionController::registerCandidateToVariant(
    const core::RegisterEntryCandidate &candidate)
{
    QVariantMap map;
    // T035-A.3.1: only locally validated fields are exposed — same discipline
    // as the profile-field projection (no confidence, no raw provider data).
    map.insert(QStringLiteral("kind"), QStringLiteral("register"));
    map.insert(QStringLiteral("functionCode"),
               static_cast<int>(candidate.functionCode));
    map.insert(QStringLiteral("address"),
               static_cast<int>(candidate.address));
    map.insert(QStringLiteral("name"),
               QString::fromStdString(candidate.name));
    map.insert(QStringLiteral("description"),
               QString::fromStdString(candidate.description));
    map.insert(QStringLiteral("dataType"),
               QString::fromUtf8(
                   core::profileDataTypeToken(candidate.dataType)));
    map.insert(QStringLiteral("quantity"), candidate.quantity);
    map.insert(QStringLiteral("scale"), candidate.scale);
    map.insert(QStringLiteral("offset"), candidate.offset);
    map.insert(QStringLiteral("unit"), QString::fromStdString(candidate.unit));
    // T035-A.3.2.2: optional-member presence flags — the review surface must
    // show HONESTLY whether the manual stated each optional member.
    map.insert(QStringLiteral("hasDescription"), candidate.hasDescription);
    map.insert(QStringLiteral("hasScale"), candidate.hasScale);
    map.insert(QStringLiteral("hasOffset"), candidate.hasOffset);
    map.insert(QStringLiteral("hasUnit"), candidate.hasUnit);
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

bool CandidateExtractionController::registerCandidateFromVariant(
    const QVariantMap &map, core::RegisterEntryCandidate &out)
{
    // Exact inverse of registerCandidateToVariant: the kind must be
    // "register", the dataType token must round-trip against the frozen M11
    // matrix, and the evidence span must be present. Anything else means this
    // map was never a pending register Candidate of this session — refuse,
    // never guess.
    if (map.value(QStringLiteral("kind")).toString()
        != QLatin1String("register")) {
        return false;
    }
    const QVariant functionCode = map.value(QStringLiteral("functionCode"));
    const QVariant address = map.value(QStringLiteral("address"));
    const QVariant quantity = map.value(QStringLiteral("quantity"));
    const QVariant scale = map.value(QStringLiteral("scale"));
    const QVariant offset = map.value(QStringLiteral("offset"));
    const QVariant textStart =
        map.value(QStringLiteral("textStart"), qlonglong{-1});
    const QVariant textEnd =
        map.value(QStringLiteral("textEnd"), qlonglong{-1});
    if (!functionCode.canConvert<int>() || !address.canConvert<int>()
        || !quantity.canConvert<int>() || !scale.canConvert<double>()
        || !offset.canConvert<double>() || !textStart.canConvert<qlonglong>()
        || !textEnd.canConvert<qlonglong>()) {
        return false;
    }
    core::RegisterDecodeType dataType{};
    if (!core::profileDataTypeFromToken(
            map.value(QStringLiteral("dataType")).toString().toStdString(),
            dataType)) {
        return false;
    }

    core::RegisterEntryCandidate candidate;
    candidate.functionCode =
        static_cast<std::uint8_t>(functionCode.value<int>());
    candidate.address = static_cast<std::uint16_t>(address.value<int>());
    candidate.name =
        map.value(QStringLiteral("name")).toString().toStdString();
    candidate.description =
        map.value(QStringLiteral("description")).toString().toStdString();
    candidate.dataType = dataType;
    candidate.quantity = quantity.value<int>();
    candidate.scale = scale.value<double>();
    candidate.offset = offset.value<double>();
    candidate.unit = map.value(QStringLiteral("unit")).toString().toStdString();
    candidate.hasDescription =
        map.value(QStringLiteral("hasDescription")).toBool();
    candidate.hasScale = map.value(QStringLiteral("hasScale")).toBool();
    candidate.hasOffset = map.value(QStringLiteral("hasOffset")).toBool();
    candidate.hasUnit = map.value(QStringLiteral("hasUnit")).toBool();
    candidate.evidence.documentId =
        map.value(QStringLiteral("documentId")).toString().toStdString();
    candidate.evidence.contentHash =
        map.value(QStringLiteral("contentHash")).toString().toStdString();
    candidate.evidence.textStart = textStart.value<qlonglong>();
    candidate.evidence.textEnd = textEnd.value<qlonglong>();
    candidate.evidence.excerpt =
        map.value(QStringLiteral("evidenceExcerpt")).toString().toStdString();
    candidate.lifecycle = core::CandidateLifecycleState::PendingReview;
    out = std::move(candidate);
    return true;
}

int CandidateExtractionController::findPendingRegisterCandidate(
    const core::RegisterEntryCandidate &candidate)
{
    for (std::size_t i = 0; i < registerCandidates_.size(); ++i) {
        if (registerCandidates_[i] == candidate) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void CandidateExtractionController::consumeRegisterCandidate(
    core::RegisterEntryCandidate &pending,
    core::CandidateLifecycleState consumedState)
{
    // C3-H5/H6 for the register set: mark + remove into the session-only
    // lists, never persisted anywhere. Since A.3.2.1 an Accepted state is
    // reachable — it records a successful CONTROLLED staged-draft write.
    pending.lifecycle = consumedState;
    if (consumedState == core::CandidateLifecycleState::Accepted) {
        registerConsumedAccepted_.push_back(pending);
    } else {
        registerConsumedRejected_.push_back(pending);
    }
    registerCandidates_.erase(
        registerCandidates_.begin()
        + static_cast<std::ptrdiff_t>(&pending - registerCandidates_.data()));
    emit candidatesChanged();
}

bool CandidateExtractionController::seedRegisterReviewCandidatesForAutomation(
    const QVariantMap &documentMap, const QString &canonicalExtractedText,
    const QVariantList &proposals)
{
    // TEST/AUTOMATION seam (mirror of seedReviewCandidatesForAutomation):
    // the proposals flow through the REAL deterministic register validator
    // (core::extractRegisterCandidates — the same H6 evidence rules as a
    // live register extraction), so the review set holds exactly what
    // production could produce, without any provider, credential or socket.
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

    std::vector<core::RegisterCandidateProposal> parsed;
    parsed.reserve(static_cast<std::size_t>(proposals.size()));
    for (const QVariant &entry : proposals) {
        const QVariantMap map = entry.toMap();
        core::RegisterDecodeType dataType{};
        if (!core::profileDataTypeFromToken(
                map.value(QStringLiteral("dataType")).toString().toStdString(),
                dataType)) {
            // Same skip rule as the profile-field seed's unknown target: an
            // unrepresentable proposal is not silently forced into the set.
            continue;
        }
        core::RegisterCandidateProposal proposal;
        proposal.functionCode = static_cast<std::uint8_t>(
            map.value(QStringLiteral("functionCode")).toInt());
        proposal.address = static_cast<std::uint16_t>(
            map.value(QStringLiteral("address")).toInt());
        proposal.name =
            map.value(QStringLiteral("name")).toString().toStdString();
        proposal.description =
            map.value(QStringLiteral("description")).toString().toStdString();
        proposal.dataType = dataType;
        proposal.quantity = map.value(QStringLiteral("quantity")).toInt();
        proposal.scale = map.value(QStringLiteral("scale")).toDouble();
        proposal.offset = map.value(QStringLiteral("offset")).toDouble();
        proposal.unit =
            map.value(QStringLiteral("unit")).toString().toStdString();
        proposal.hasDescription =
            map.contains(QStringLiteral("description"));
        proposal.hasScale = map.contains(QStringLiteral("scale"));
        proposal.hasOffset = map.contains(QStringLiteral("offset"));
        proposal.hasUnit = map.contains(QStringLiteral("unit"));
        proposal.evidenceExcerpt =
            map.value(QStringLiteral("evidenceExcerpt"))
                .toString()
                .toStdString();
        parsed.push_back(std::move(proposal));
    }

    // The REAL deterministic register validator: refused proposals never
    // become candidates (H6 evidence + v1 field rules, zero normalization —
    // the T035-A.1 foundation).
    const core::RegisterCandidateExtractionResult validated =
        core::extractRegisterCandidates(document,
                                         canonicalExtractedText.toStdString(),
                                         parsed);

    // Atomic replacement WITHIN the register set; the profile-field pending
    // set is a separate queue and is left untouched.
    registerCandidates_ = validated.candidates;
    setState(State::Succeeded, QString(), QString());
    emit candidatesChanged();
    return true;
}

} // namespace modbuslens::ui