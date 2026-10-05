#include "ui/manualqa/ManualQaController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/manualqa/ManualQaContract.h"
#include "ui/manual/ManualImportController.h"
#include "ui/manual/ManualStore.h"
#include "ui/manualqa/ModelScopeManualQaRunner.h"

namespace modbuslens::ui {
namespace {

constexpr auto kStateIdle = "idle";
constexpr auto kStateConsentRequired = "consent_required";
constexpr auto kStateRunning = "running";
constexpr auto kStateCompleted = "completed";
constexpr auto kStateFailed = "failed";

constexpr auto kTokenNoSelection = "qa_no_selection";
constexpr auto kTokenEmptyQuestion = "qa_empty_question";
constexpr auto kTokenNoCanonicalText = "qa_no_canonical_text";
constexpr auto kTokenNotConfigured = "qa_not_configured";
constexpr auto kTokenProviderFailure = "qa_provider_failure";
constexpr auto kTokenMalformedOutput = "qa_malformed_output";
constexpr auto kTokenSourceMissing = "qa_source_missing";

QString citationCodeToErrorToken(
    core::ManualQaCitationCode code)
{
    return QString::fromUtf8(core::manualQaCitationCodeName(code));
}

QVariantMap citationToVariant(const core::ManualQaCitation& citation)
{
    QVariantMap map;
    map.insert(QStringLiteral("documentId"),
               QString::fromStdString(citation.documentId));
    map.insert(QStringLiteral("contentHash"),
               QString::fromStdString(citation.contentHash));
    map.insert(QStringLiteral("pageNumber"),
               static_cast<qlonglong>(citation.pageNumber));
    map.insert(QStringLiteral("textStart"),
               static_cast<qlonglong>(citation.textStart));
    map.insert(QStringLiteral("textEnd"),
               static_cast<qlonglong>(citation.textEnd));
    map.insert(QStringLiteral("excerpt"),
               QString::fromStdString(citation.excerpt));
    return map;
}

} // namespace

ManualQaController::ManualQaController(QObject* parent)
    : QObject(parent)
{
    ownedRunner_ = std::make_unique<ModelScopeManualQaRunner>();
    runner_ = ownedRunner_.get();
}

QString ManualQaController::stateToken() const
{
    return state_;
}

QString ManualQaController::resultStatusToken() const
{
    return resultStatus_;
}

QString ManualQaController::answerText() const
{
    return answerText_;
}

QVariantList ManualQaController::citations() const
{
    return citations_;
}

QString ManualQaController::failureText() const
{
    return failureText_;
}

QString ManualQaController::lastErrorToken() const
{
    return lastErrorToken_;
}

bool ManualQaController::busy() const
{
    return state_ == QLatin1String(kStateRunning);
}

bool ManualQaController::hasSelectedManual() const
{
    return manualController_ != nullptr && manualController_->selectedIndex()
        >= 0;
}

QString ManualQaController::selectedManualLabel() const
{
    if (!hasSelectedManual()) {
        return QString();
    }
    const QVariantMap selected = manualController_->selectedDocument();
    return selected.value(QStringLiteral("originalFileName")).toString();
}

QString ManualQaController::consentScopeText() const
{
    // D4 disclosure, frozen wording boundary: the Human question AND bounded
    // excerpts/context from the currently selected Manual are sent to cloud
    // AI. It does NOT claim the whole Manual is uploaded.
    return tr("云端 AI 将接收你的提问，以及当前所选说明书的必要摘录/上下文，"
              "用于生成基于说明书证据的回答。未选中所选说明书以外的内容。");
}

void ManualQaController::setManualController(
    ManualImportController* controller)
{
    if (manualController_ == controller) {
        return;
    }
    if (manualController_ != nullptr) {
        disconnect(manualController_, nullptr, this, nullptr);
    }
    manualController_ = controller;
    emit manualControllerChanged();
    if (manualController_ != nullptr) {
        // D1: the authoritative selection stays owned by
        // ManualImportController; this controller only observes it. A switch
        // invalidates the in-flight generation and clears the Q&A surface.
        connect(manualController_, &ManualImportController::selectionChanged,
                this, &ManualQaController::handleManualContextChanged);
        // D5: narrow successful-delete observation path — the manual list
        // changed; if the Q&A-bound Manual disappeared, invalidate + clear.
        connect(manualController_, &ManualImportController::documentsChanged,
                this, &ManualQaController::handleManualContextChanged);
    }
    emit manualContextChanged();
}

void ManualQaController::handleManualContextChanged()
{
    // hasSelectedManual / selectedManualLabel always re-evaluate on ANY
    // selection/list change: without this notify, the first import would
    // leave the QML enabled bindings stale (gate-verified defect).
    emit manualContextChanged();
    if (manualController_ == nullptr) {
        return;
    }
    // D5: narrow check — only act when the Q&A-bound Manual disappeared
    // (successful delete) or the selection identity moved (switch). A brand
    // new import changes the list too, but the bound identity is untouched,
    // so the Q&A surface survives it.
    if (boundDocumentId_.isEmpty()) {
        return;
    }
    bool boundStillListed = false;
    const QVariantList documents = manualController_->manualDocuments();
    for (const QVariant& entry : documents) {
        if (entry.toMap().value(QStringLiteral("documentId")).toString()
            == boundDocumentId_) {
            boundStillListed = true;
            break;
        }
    }
    const QVariantMap selected = manualController_->selectedDocument();
    const QString selectedId =
        selected.value(QStringLiteral("documentId")).toString();
    const bool selectionMoved = selectedId != boundDocumentId_;

    if (!boundStillListed || selectionMoved) {
        // Invalidate + clear (switch OR successful delete of the bound
        // Manual). The in-flight generation is dropped regardless of what the
        // runner does; cancel is best-effort.
        invalidateGeneration();
        clearResult();
        setState(kStateIdle);
        emit manualContextChanged();
    }
}

void ManualQaController::invalidateGeneration()
{
    ++generation_;
    if (runner_ != nullptr) {
        runner_->cancel(); // best-effort; stale completions are dropped anyway
    }
}

void ManualQaController::clearResult()
{
    resultStatus_ = QStringLiteral("none");
    answerText_.clear();
    citations_.clear();
    failureText_.clear();
    lastErrorToken_.clear();
    emit resultChanged();
}

void ManualQaController::localReject(const char* token, const QString& text)
{
    // D3: every LOCAL technical failure surfaces as ERROR (result status
    // "error") — it is never converted into a semantic state and never
    // silent.
    clearResult();
    resultStatus_ = QStringLiteral("error");
    lastErrorToken_ = QLatin1String(token);
    failureText_ = text;
    setState(kStateFailed);
    emit resultChanged();
}

void ManualQaController::setState(QString token)
{
    if (state_ == token) {
        return;
    }
    state_ = std::move(token);
    emit stateChanged();
}

void ManualQaController::ask(const QString& question)
{
    // D1: no selected Manual -> local rejection, zero dispatch.
    if (!hasSelectedManual()) {
        localReject(kTokenNoSelection, tr("未选择说明书，无法提问。"));
        return;
    }
    // Defensive local rejection (implementation validation, not a new product
    // policy): whitespace/empty questions never dispatch.
    if (question.trimmed().isEmpty()) {
        localReject(kTokenEmptyQuestion, tr("请输入问题后再提问。"));
        return;
    }
    // Single-flight: an Ask while running cannot cause a duplicate dispatch.
    if (busy()) {
        return;
    }
    // D4: Q&A consent is separate from extraction consent and owned here.
    if (!consentGranted_) {
        clearResult();
        setState(kStateConsentRequired);
        return;
    }
    beginAttempt(question);
}

void ManualQaController::grantConsent()
{
    // D4: grants the Q&A session consent ONLY. It never reads or writes the
    // extraction consent and is never granted by it.
    consentGranted_ = true;
}

void ManualQaController::rejectConsent()
{
    // Zero mutation: no dispatch, no grant, result untouched.
}

void ManualQaController::beginAttempt(const QString& question)
{
    const QVariantMap selected = manualController_->selectedDocument();
    boundDocumentId_ =
        selected.value(QStringLiteral("documentId")).toString();
    boundContentHash_ =
        selected.value(QStringLiteral("contentHash")).toString();

    bool textOk = false;
    const QString canonicalText = ManualStore::loadText(boundContentHash_,
                                                        &textOk);
    if (!textOk) {
        localReject(kTokenNoCanonicalText,
                    tr("无法读取所选说明书的已抽取文本。"));
        return;
    }

    core::ManualQaRequest request;
    request.question = question.toStdString();
    request.documentId = boundDocumentId_.toStdString();
    request.contentHash = boundContentHash_.toStdString();
    request.blocks = core::buildManualQaContextBlocks(
        canonicalText.toStdString(), request.question);

    ++generation_;
    const std::uint64_t generation = generation_;
    clearResult();
    setState(kStateRunning);
    const bool started = runner_->begin(
        request, generation,
        [this](const IManualQaRunner::Completion& done) {
            completeAttempt(done.generation, done.ok,
                            QString::fromStdString(done.failureToken),
                            done.rawJson);
        });
    if (!started) {
        // Nothing was sent (fail-closed, e.g. credential absent).
        localReject(kTokenNotConfigured, tr("云端问答未配置，无法发送。"));
    }
}

void ManualQaController::completeAttempt(std::uint64_t generation, bool ok,
                                         const QString& failureToken,
                                         const std::string& rawJson)
{
    // Late/stale response for an invalidated generation: DROP (D5).
    if (generation != generation_ || state_ != QLatin1String(kStateRunning)) {
        return;
    }
    if (!ok) {
        localReject(!failureToken.isEmpty() ? failureToken.toUtf8().constData()
                                            : kTokenProviderFailure,
                    tr("问答未成功，请稍后重试。"));
        return;
    }

    // Strict, fail-closed parse (§100.6). Malformed output is ERROR — never a
    // semantic state.
    const std::optional<core::ManualQaParsedResult> parsed =
        ModelScopeManualQaRunner::parseProviderResult(rawJson);
    if (!parsed.has_value()) {
        localReject(kTokenMalformedOutput,
                    tr("云端返回的问答结果无法解析。"));
        return;
    }

    // D2/D3: FOUND is displayed only after local deterministic validation
    // against the CURRENT canonical text of the bound Manual.
    if (parsed->status == core::ManualQaStatus::Found) {
        bool textOk = false;
        const QString canonicalText =
            ManualStore::loadText(boundContentHash_, &textOk);
        if (!textOk) {
            localReject(kTokenSourceMissing,
                        tr("所选说明书内容已不可读，无法确认回答依据。"));
            return;
        }
        const core::ManualQaFoundValidation validation =
            core::validateManualQaFoundResult(
                *parsed, boundDocumentId_.toStdString(),
                boundContentHash_.toStdString(), canonicalText.toStdString());
        if (!validation.ok) {
            localReject(
                citationCodeToErrorToken(validation.citationCode)
                    .toUtf8()
                    .constData(),
                tr("回答的说明书依据未通过本地校验。"));
            return;
        }
    }

    clearResult();
    switch (parsed->status) {
    case core::ManualQaStatus::Found:
        resultStatus_ = QStringLiteral("found");
        answerText_ = QString::fromStdString(parsed->answer);
        for (const auto& citation : parsed->citations) {
            citations_.append(citationToVariant(citation));
        }
        break;
    case core::ManualQaStatus::NotFound:
        // Deterministic local UI text; the provider answer is NOT displayed.
        resultStatus_ = QStringLiteral("not_found");
        answerText_ = tr("在当前所选说明书中未找到与该问题相关的证据。");
        break;
    case core::ManualQaStatus::InsufficientEvidence:
        resultStatus_ = QStringLiteral("insufficient_evidence");
        answerText_ = tr("当前说明书中的证据不足以可靠回答该问题。");
        break;
    }
    setState(kStateCompleted);
    emit resultChanged();
}

void ManualQaController::setRunnerForAutomation(IManualQaRunner* runner)
{
    // nullptr restores the production owned runner (see the header contract).
    runner_ = runner != nullptr ? runner : ownedRunner_.get();
}

} // namespace modbuslens::ui
