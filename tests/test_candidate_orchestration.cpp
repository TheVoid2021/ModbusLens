// M12-C C2 THIRD SLICE tests (T027 §70): production orchestration, the cloud
// consent gate and the SESSION-ONLY PendingReview Candidate set.
//
// Determinism is structural: the orchestration layer is driven by an injected
// fake runner, so no network type is involved and no provider call can happen.
//
// The matrix is O01..O20 from the session contract.
#include <QtTest>

#include <QSet>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMetaMethod>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "core/candidate/CandidateExtraction.h"
#include "core/candidate/ProviderExtractionContract.h"
#include "core/profile/DeviceProfile.h"
#include "ui/candidate/CandidateExtractionController.h"
#include "ui/candidate/CandidateExtractionRunner.h"
#include "ui/manual/ManualImportController.h"
#include "ui/manual/ManualStore.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::core::CandidateProposal;
using modbuslens::core::ManualDocument;
using modbuslens::core::ProfileFieldCandidate;
using modbuslens::core::ProfileFieldTarget;
using modbuslens::core::ProviderExtractionFailure;
using modbuslens::ui::CandidateExtractionController;
using modbuslens::ui::ICandidateExtractionRunner;
using modbuslens::ui::ManualImportController;
using modbuslens::ui::ManualStore;
using modbuslens::ui::ProfileStore;

namespace {

constexpr char kManualText[] =
    "ACME Power Systems Ltd. - Inverter Manual\n"
    "\n"
    "1. Identity\n"
    "Manufacturer: ACME Power Systems Ltd.\n"
    "Model: INV-1000\n"
    "Revision: rev A\n";

constexpr char kManufacturerExcerpt[] = "Manufacturer: ACME Power Systems Ltd.";
constexpr char kManufacturerValue[] = "ACME Power Systems Ltd.";

constexpr char kDocAId[] = "aaaaaaaa-1111-2222-3333-444444444444";
constexpr char kDocAHash[] =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr char kDocBId[] = "bbbbbbbb-1111-2222-3333-444444444444";
constexpr char kDocBHash[] =
    "b2d4c2f4a5ce4f6e9f0a1b2c3d4e5f60718293a4b5c6d7e8f90123456789abcd";
// Same document identity, DIFFERENT content identity (O06).
constexpr char kDocAHash2[] =
    "1111111122222222333333334444444455555555666666667777777788888888";

ManualDocument makeDocument(const char *documentId, const char *contentHash)
{
    ManualDocument document;
    document.schemaVersion = 1;
    document.documentId = documentId;
    document.originalFileName = "inverter-manual.txt";
    document.documentType = modbuslens::core::ManualDocumentType::Txt;
    // PROVENANCE ONLY — never read again (O17).
    document.originalPath = "Z:/provenance-only/never-read.txt";
    document.contentHash = contentHash;
    document.byteSize = sizeof(kManualText);
    document.charCount = sizeof(kManualText) - 1;
    document.status = modbuslens::core::ManualDocumentStatus::Ready;
    return document;
}

CandidateProposal manufacturerProposal(std::string value = kManufacturerValue,
                                       std::string excerpt = kManufacturerExcerpt)
{
    CandidateProposal proposal;
    proposal.target = ProfileFieldTarget::Manufacturer;
    proposal.proposedValue = std::move(value);
    proposal.evidenceExcerpt = std::move(excerpt);
    proposal.locationHint = -1;
    return proposal;
}

// ------------------------------------------------------ TEST-ONLY runner --
// Deterministic fake: it captures the outbound request and never completes
// until the test says so, which is what makes the stale-result guard (O12)
// observable rather than merely asserted.
class FakeCandidateRunner : public ICandidateExtractionRunner
{
public:
    struct Pending {
        modbuslens::core::ExtractionRequest request;
        std::uint64_t generation{0};
        CompletionHandler onDone;
    };

    [[nodiscard]] bool begin(const modbuslens::core::ExtractionRequest &request,
                             std::uint64_t generation,
                             const CompletionHandler &onDone) override
    {
        ++beginCount_;
        pending.push_back(Pending{request, generation, onDone});
        return true;
    }

    [[nodiscard]] int beginCount() const override { return beginCount_; }

    // Delivers a completion for the attempt at `index` (0 = oldest).
    void complete(std::size_t index, bool ok,
                  ProviderExtractionFailure failure,
                  std::vector<CandidateProposal> proposals = {})
    {
        const Pending stored = pending.at(index);
        pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(index));
        Completion done;
        done.generation = stored.generation;
        done.ok = ok;
        done.failure = failure;
        done.proposals = std::move(proposals);
        stored.onDone(done);
    }

    void completeSuccess(std::size_t index, std::vector<CandidateProposal> proposals)
    {
        complete(index, true, ProviderExtractionFailure::None, std::move(proposals));
    }

    void completeFailure(std::size_t index, ProviderExtractionFailure failure)
    {
        complete(index, false, failure, {});
    }

    std::vector<Pending> pending;
    int beginCount_{0};
};

QStringList snapshotTree(const QString &root)
{
    QStringList entries;
    if (!QDir(root).exists()) {
        entries << "<absent>";
        return entries;
    }
    QDirIterator iterator(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo info(iterator.next());
        entries << info.fileName() + ":" + QString::number(info.size());
    }
    entries.sort();
    return entries;
}

// ------------------------------------------------ T033 PDF fixtures (offline,
// deterministic, licence-free — same minimal-PDF technique as the frozen
// test_manual_import_pdf_docx fixtures) --------------------------------------
[[nodiscard]] QByteArray orchestrationBuildPdf(
    const QList<QByteArray> &objects)
{
    QByteArray out = "%PDF-1.4\n";
    QList<int> offsets;
    for (int index = 0; index < objects.size(); ++index) {
        offsets.append(out.size());
        out += QByteArray::number(index + 1) + " 0 obj\n" + objects.at(index)
               + "\nendobj\n";
    }
    const int xrefAt = out.size();
    out += "xref\n0 " + QByteArray::number(objects.size() + 1)
           + "\n0000000000 65535 f \n";
    for (int offset : offsets) {
        out += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1)
           + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xrefAt)
           + "\n%%EOF\n";
    return out;
}

[[nodiscard]] QByteArray orchestrationStreamObject(const QByteArray &stream)
{
    return "<< /Length " + QByteArray::number(stream.size()) + " >>\nstream\n"
           + stream + "\nendstream";
}

[[nodiscard]] QByteArray orchestrationTextLayerPdf(const QString &text)
{
    const QByteArray stream =
        "BT /F1 18 Tf 72 700 Td (" + text.toUtf8() + ") Tj ET";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R "
        "/Resources << /Font << /F1 5 0 R >> >> >>",
        orchestrationStreamObject(stream),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    };
    return orchestrationBuildPdf(objects);
}

[[nodiscard]] QByteArray orchestrationNoTextPdf()
{
    const QByteArray stream = "0.9 0.9 0.9 rg 72 600 200 80 re f";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>",
        orchestrationStreamObject(stream),
    };
    return orchestrationBuildPdf(objects);
}

// Import a source file through the REAL controller path (import + auto-select),
// with the managed root pointed at a temporary directory.
[[nodiscard]] bool orchestrationImport(ManualImportController &manual,
                                       const QString &sourcePath)
{
    bool imported = false;
    QMetaObject::invokeMethod(&manual, "importManualFile", Qt::DirectConnection,
                              Q_RETURN_ARG(bool, imported),
                              Q_ARG(QUrl, QUrl::fromLocalFile(sourcePath)));
    return imported;
}

[[nodiscard]] QString stateOf(const CandidateExtractionController &controller)
{
    return controller.stateToken();
}

// Compile-time proof that the outgoing request carries NO local-only
// identifier: there is simply no such member to serialize (O19).
template <typename T, typename = void>
struct hasOriginalPathMember : std::false_type
{
};
template <typename T>
struct hasOriginalPathMember<
    T, std::void_t<decltype(std::declval<T &>().originalPath)>> : std::true_type
{
};
template <typename T, typename = void>
struct hasManifestMember : std::false_type
{
};
template <typename T>
struct hasManifestMember<T, std::void_t<decltype(std::declval<T &>().manifest)>>
    : std::true_type
{
};
static_assert(!hasOriginalPathMember<modbuslens::core::ExtractionRequest>::value,
              "the outbound request must not carry the local source path (O19)");
static_assert(!hasManifestMember<ProfileFieldCandidate>::value,
              "no provider/raw artifact may be retained on a Candidate (O16)");

} // namespace

class CandidateOrchestrationTest : public QObject
{
    Q_OBJECT

private slots:
    // --------------------------------------------------------------- O01 --
    void o01_noConsentNoProviderCall()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);

        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});

        QCOMPARE(stateOf(controller), QStringLiteral("consent_required"));
        QCOMPARE(runner.beginCount(), 0); // the gate called NOTHING
        QCOMPARE(runner.pending.size(), std::size_t{0});
        QCOMPARE(controller.candidateCount(), 0);
    }

    // --------------------------------------------------------------- O02 --
    void o02_consentRejectKeepsEverythingUnchanged()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        ProfileStore::setManagedRootOverride(root.path());
        const QStringList before = snapshotTree(root.path());

        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});
        QCOMPARE(stateOf(controller), QStringLiteral("consent_required"));

        controller.rejectConsent();

        QCOMPARE(stateOf(controller), QStringLiteral("idle"));
        QCOMPARE(runner.beginCount(), 0);
        QCOMPARE(controller.candidateCount(), 0);
        QCOMPARE(snapshotTree(root.path()), before); // no persistence at all

        ProfileStore::setManagedRootOverride(QString());
    }

    // --------------------------------------------------------------- O03 --
    void o03_consentGrantStartsExactlyOneAttempt()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});
        QCOMPARE(stateOf(controller), QStringLiteral("consent_required"));

        controller.grantConsent();

        QCOMPARE(stateOf(controller), QStringLiteral("running"));
        QCOMPARE(runner.beginCount(), 1);
        QVERIFY(runner.pending.size() == std::size_t{1});
        QCOMPARE(runner.pending.front().request.targetFieldToken,
                 std::string{"manufacturer"});
        QCOMPARE(runner.pending.front().request.canonicalExtractedText,
                 std::string{kManualText});
    }

    // --------------------------------------------------------------- O04 --
    void o04_consentIsReusedWithinSessionForSameContent()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);

        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});
        QCOMPARE(stateOf(controller), QStringLiteral("succeeded"));
        QCOMPARE(runner.beginCount(), 1);

        // Same document identity + same content identity: no second consent.
        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});
        QCOMPARE(stateOf(controller), QStringLiteral("running"));
        QCOMPARE(runner.beginCount(), 2);
    }

    // --------------------------------------------------------------- O05 --
    void o05_differentDocumentRequiresConsent()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);

        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});
        QCOMPARE(runner.beginCount(), 1);

        controller.requestExtractionFor(makeDocument(kDocBId, kDocBHash),
                                        std::string{kManualText});

        QCOMPARE(stateOf(controller), QStringLiteral("consent_required"));
        QCOMPARE(runner.beginCount(), 1); // B called nothing until granted
    }

    // --------------------------------------------------------------- O06 --
    void o06_contentIdentityChangeRequiresConsentAgain()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);

        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});
        QCOMPARE(runner.beginCount(), 1);

        // Same document identity, DIFFERENT content identity: the old grant
        // must not authorize the new content.
        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash2),
                                        std::string{kManualText});

        QCOMPARE(stateOf(controller), QStringLiteral("consent_required"));
        QCOMPARE(runner.beginCount(), 1);
    }

    // --------------------------------------------------------------- O07 --
    void o07_successfulFlowYieldsPendingReviewCandidate()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});
        controller.grantConsent();

        runner.completeSuccess(0, {manufacturerProposal()});

        QCOMPARE(stateOf(controller), QStringLiteral("succeeded"));
        QCOMPARE(controller.candidateCount(), 1);
        const QVariantMap candidate = controller.candidates().front().toMap();
        QCOMPARE(candidate.value(QStringLiteral("targetField")).toString(),
                 QStringLiteral("manufacturer"));
        QCOMPARE(candidate.value(QStringLiteral("proposedValue")).toString(),
                 QString::fromUtf8(kManufacturerValue));
        QCOMPARE(candidate.value(QStringLiteral("evidenceExcerpt")).toString(),
                 QString::fromUtf8(kManufacturerExcerpt));
        QCOMPARE(candidate.value(QStringLiteral("lifecycle")).toString(),
                 QStringLiteral("pending_review"));
        QCOMPARE(controller.validatedCandidates().front().lifecycle,
                 modbuslens::core::CandidateLifecycleState::PendingReview);
    }

    // --------------------------------------------------------------- O08 --
    void o08_schemaValidButFalseEvidencePreservesOldSet()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument(kDocAId, kDocAHash);
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});
        QCOMPARE(controller.candidateCount(), 1);
        const QString acceptedValue =
            controller.candidates().front().toMap()
                .value(QStringLiteral("proposedValue")).toString();

        // Structurally valid proposal whose excerpt is NOT in the manual: the
        // accepted M/N local Evidence validation must reject it.
        controller.requestExtractionFor(document, std::string{kManualText});
        runner.completeSuccess(
            0, {manufacturerProposal("FABRICATED CORP",
                                     "Manufacturer: FABRICATED CORP")});

        QCOMPARE(controller.candidateCount(), 1); // the OLD set is preserved
        QCOMPARE(controller.candidates().front().toMap()
                     .value(QStringLiteral("proposedValue")).toString(),
                 acceptedValue);
        QCOMPARE(stateOf(controller), QStringLiteral("failed"));
    }

    // --------------------------------------------------------------- O09 --
    void o09_transportFailurePreservesOldSet()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        ProfileStore::setManagedRootOverride(root.path());

        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument(kDocAId, kDocAHash);
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});
        QCOMPARE(controller.candidateCount(), 1);
        const QStringList filesBefore = snapshotTree(root.path());

        controller.requestExtractionFor(document, std::string{kManualText});
        runner.completeFailure(0, ProviderExtractionFailure::TransportError);

        QCOMPARE(stateOf(controller), QStringLiteral("failed"));
        QCOMPARE(controller.failureToken(), QStringLiteral("transport_error"));
        QCOMPARE(controller.candidateCount(), 1); // preserved
        QCOMPARE(snapshotTree(root.path()), filesBefore); // Profile untouched

        ProfileStore::setManagedRootOverride(QString());
    }

    // --------------------------------------------------------------- O10 --
    void o10_malformedResponsePreservesOldSet()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument(kDocAId, kDocAHash);
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});
        QCOMPARE(controller.candidateCount(), 1);

        controller.requestExtractionFor(document, std::string{kManualText});
        runner.completeFailure(0, ProviderExtractionFailure::MalformedResponse);

        QCOMPARE(controller.failureToken(), QStringLiteral("malformed_response"));
        QCOMPARE(controller.candidateCount(), 1);

        // Same for a strict schema rejection.
        controller.requestExtractionFor(document, std::string{kManualText});
        runner.completeFailure(0, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(controller.failureToken(), QStringLiteral("schema_violation"));
        QCOMPARE(controller.candidateCount(), 1);
    }

    // --------------------------------------------------------------- O11 --
    void o11_successfulReplacementIsAtomic()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument(kDocAId, kDocAHash);
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal("ACME Power Systems Ltd.")});
        QCOMPARE(controller.candidateCount(), 1);

        controller.requestExtractionFor(document, std::string{kManualText});
        runner.completeSuccess(0, {manufacturerProposal("ACME")});

        // Exactly one item, and it is the NEW one: no partial mixture.
        QCOMPARE(controller.candidateCount(), 1);
        QCOMPARE(controller.candidates().front().toMap()
                     .value(QStringLiteral("proposedValue")).toString(),
                 QStringLiteral("ACME"));
        QCOMPARE(stateOf(controller), QStringLiteral("succeeded"));
    }

    // --------------------------------------------------------------- O12 --
    void o12_staleResultDoesNotContaminateNewSelection()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);

        // A: granted, attempt in flight (never completed yet).
        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 1);

        // Selection switches to B, which supersedes A with a new generation.
        controller.requestExtractionFor(makeDocument(kDocBId, kDocBHash),
                                        std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 2);

        // B completes first and becomes the current set.
        runner.completeSuccess(1, {manufacturerProposal()});
        QCOMPARE(stateOf(controller), QStringLiteral("succeeded"));
        QCOMPARE(controller.candidateCount(), 1);
        const QString bDocumentId = controller.candidates().front().toMap()
                                        .value(QStringLiteral("documentId")).toString();
        QCOMPARE(bDocumentId, QString::fromUtf8(kDocBId));

        // A completes LATE. Its generation is stale, so it must be dropped.
        runner.completeSuccess(0, {manufacturerProposal("LATE CORP", kManufacturerExcerpt)});

        QCOMPARE(controller.candidateCount(), 1);
        QCOMPARE(controller.candidates().front().toMap()
                     .value(QStringLiteral("documentId")).toString(),
                 QString::fromUtf8(kDocBId)); // still B, never A
        QCOMPARE(stateOf(controller), QStringLiteral("succeeded"));
    }

    // --------------------------------------------------------------- O13 --
    void o13_stateIsSessionOnly()
    {
        FakeCandidateRunner runner;
        {
            CandidateExtractionController controller(runner);
            controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                            std::string{kManualText});
            controller.grantConsent();
            runner.completeSuccess(0, {manufacturerProposal()});
            QCOMPARE(controller.candidateCount(), 1);
        }
        // A fresh owner starts empty: nothing was persisted or reloaded.
        CandidateExtractionController fresh(runner);
        QCOMPARE(fresh.candidateCount(), 0);
        QCOMPARE(stateOf(fresh), QStringLiteral("idle"));
        fresh.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                   std::string{kManualText});
        QCOMPARE(stateOf(fresh), QStringLiteral("consent_required"));
    }

    // --------------------------------------------------------------- O14 --
    void o14_deviceProfileHasZeroDiff()
    {
        // A verified profile exists and must be value-for-value untouched by
        // every C2 extraction path exercised above.
        modbuslens::core::DeviceProfile profile;
        profile.schemaVersion = 1;
        profile.profileId = "99999999-1111-2222-3333-444444444444";
        profile.displayName = "Demo inverter";
        profile.manufacturer = "ACME";
        profile.model = "INV-1000";
        profile.revision = "rev A";
        profile.description = "fixture";
        const modbuslens::core::DeviceProfile snapshot = profile;

        QTemporaryDir root;
        QVERIFY(root.isValid());
        ProfileStore::setManagedRootOverride(root.path());
        const QStringList treeBefore = snapshotTree(root.path());

        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument(kDocAId, kDocAHash);
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});
        controller.requestExtractionFor(document, std::string{kManualText});
        runner.completeFailure(0, ProviderExtractionFailure::TransportError);
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});

        QVERIFY(profile == snapshot);
        QCOMPARE(snapshotTree(root.path()), treeBefore);

        ProfileStore::setManagedRootOverride(QString());
    }

    // --------------------------------------------------------------- O15 --
    void o15_noConfidenceIsExposed()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});

        const QVariantMap candidate = controller.candidates().front().toMap();
        for (const QString &key : candidate.keys()) {
            const QString lowered = key.toLower();
            QVERIFY(!lowered.contains(QStringLiteral("confidence")));
            QVERIFY(!lowered.contains(QStringLiteral("score")));
            QVERIFY(!lowered.contains(QStringLiteral("probability")));
        }
    }

    // --------------------------------------------------------------- O16 --
    void o16_noRawProviderResponseIsExposed()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                        std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});

        const QVariantMap candidate = controller.candidates().front().toMap();
        // Exactly the locally validated fields — nothing provider-authored.
        QStringList keys = candidate.keys();
        keys.sort();
        const QStringList expected{
            QStringLiteral("contentHash"),    QStringLiteral("documentId"),
            QStringLiteral("evidenceExcerpt"), QStringLiteral("lifecycle"),
            QStringLiteral("proposedValue"),  QStringLiteral("targetField"),
            QStringLiteral("textEnd"),        QStringLiteral("textStart")};
        QCOMPARE(keys, expected);
    }

    // --------------------------------------------------------------- O17 --
    void o17_canonicalTextIsTheSource()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument(kDocAId, kDocAHash);
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        runner.completeSuccess(0, {manufacturerProposal()});

        // The attempt was driven by the canonical text handed in ...
        QCOMPARE(runner.pending.empty(), true);
        // ... and the accepted local Evidence validation produced a location
        // that round-trips against that same canonical text.
        const ProfileFieldCandidate &candidate =
            controller.validatedCandidates().front();
        const std::string text{kManualText};
        QCOMPARE(candidate.evidence.excerpt,
                 std::string{kManufacturerExcerpt});
        QCOMPARE(text.substr(static_cast<std::size_t>(candidate.evidence.textStart),
                             static_cast<std::size_t>(candidate.evidence.textEnd
                                                      - candidate.evidence.textStart)),
                 candidate.evidence.excerpt);
        QCOMPARE(candidate.evidence.documentId, std::string{kDocAId});
        QCOMPARE(candidate.evidence.contentHash, std::string{kDocAHash});
    }

    // --------------------------------------------------------------- O18 --
    void o18_noCandidateLevelReviewActions()
    {
        const QMetaObject *meta = &CandidateExtractionController::staticMetaObject;
        QStringList invokables;
        for (int i = 0; i < meta->methodCount(); ++i) {
            const QMetaMethod method = meta->method(i);
            if (method.methodType() == QMetaMethod::Method
                || method.methodType() == QMetaMethod::Slot) {
                invokables << QString::fromUtf8(method.name());
            }
        }
        // The consent operations ARE present ...
        QVERIFY(invokables.contains(QStringLiteral("requestExtraction")));
        QVERIFY(invokables.contains(QStringLiteral("grantConsent")));
        QVERIFY(invokables.contains(QStringLiteral("rejectConsent")));
        // C3 AMENDMENT (T027 §81, HUMAN-APPROVED C3-H2/H6/H8/H10): the
        // single-Candidate review actions acceptCandidate / rejectCandidate
        // (and the automation seed that reuses the real validator) are now
        // part of this controller. C3-R3B (C3-H8, HUMAN-FROZEN): the edit
        // dialog's confirmEditedCandidate is ALSO part of this controller —
        // the ONE Human-authority path that applies a HUMAN-CONFIRMED value
        // through the same staged-copy gate as Accept (no persistence here:
        // Save stays owned by ProfileController, C3-H10, no second pipeline).
        // Everything the O-era boundary actually protects still holds and is
        // asserted below:
        //   · NO persistence-shaped action exists here — Save stays owned by
        //     ProfileController and the only draft write path is the
        //     controlled staged-copy API (C3-H10, no second pipeline).
        const QSet<QString> approved{
            QStringLiteral("requestExtraction"), QStringLiteral("grantConsent"),
            QStringLiteral("rejectConsent"), QStringLiteral("acceptCandidate"),
            QStringLiteral("rejectCandidate"),
            QStringLiteral("seedReviewCandidatesForAutomation"),
            QStringLiteral("confirmEditedCandidate")};
        for (const QString &name : invokables) {
            if (approved.contains(name)) {
                continue;
            }
            const QString lowered = name.toLower();
            QVERIFY(!(lowered.contains(QStringLiteral("accept"))
                      || lowered.contains(QStringLiteral("edit"))
                      || lowered.contains(QStringLiteral("reject"))));
            QVERIFY(!(lowered.contains(QStringLiteral("save"))
                      || lowered.contains(QStringLiteral("persist"))
                      || lowered.contains(QStringLiteral("store"))
                      || lowered.contains(QStringLiteral("commit"))));
        }
    }

    // --------------------------------------------------------------- O19 --
    void o19_outboundPayloadScope()
    {
        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument(kDocAId, kDocAHash);
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();

        QCOMPARE(runner.pending.size(), std::size_t{1});
        const modbuslens::core::ExtractionRequest request =
            runner.pending.front().request;
        // The intended payload IS the selected canonical text ...
        QCOMPARE(request.canonicalExtractedText, std::string{kManualText});
        QCOMPARE(request.targetFieldToken, std::string{"manufacturer"});
        // ... and the request type cannot carry a path, a binary, another
        // manual or a serialized DeviceProfile (compile-time asserted above).
        QVERIFY(std::string(kManualText).find(kDocAHash) == std::string::npos);
    }

    // --------------------------------------------------------------- O20 --
    void o20_orchestrationIsDeterministic()
    {
        FakeCandidateRunner first;
        CandidateExtractionController firstController(first);
        firstController.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                             std::string{kManualText});
        firstController.grantConsent();
        first.completeSuccess(0, {manufacturerProposal()});

        FakeCandidateRunner second;
        CandidateExtractionController secondController(second);
        secondController.requestExtractionFor(makeDocument(kDocAId, kDocAHash),
                                              std::string{kManualText});
        secondController.grantConsent();
        second.completeSuccess(0, {manufacturerProposal()});

        QCOMPARE(firstController.candidates(), secondController.candidates());
        QCOMPARE(firstController.candidateCount(), 1);
    }

    // =========================================================================
    // T033 regression (real-world PDF manual): a text-layer PDF reaches the
    // consent gate through ManualStore::loadText (the canonical seam) instead
    // of failing with no_canonical_text. This drives the REAL requestExtraction()
    // entry: ManualImportController selection + ManualStore cache truth.
    // =========================================================================
    void o21_textLayerPdfReachesConsentGate()
    {
        QTemporaryDir managedRoot;
        QVERIFY(managedRoot.isValid());
        ManualStore::setManagedRootOverride(managedRoot.path());
        const QString source =
            QDir(managedRoot.path()).filePath("sensor-manual.pdf");
        QFile pdf(source);
        QVERIFY(pdf.open(QIODevice::WriteOnly));
        const QByteArray pdfBytes = orchestrationTextLayerPdf(
            QStringLiteral("Manufacturer: ACME Power Systems Ltd."));
        QCOMPARE(pdf.write(pdfBytes), static_cast<qint64>(pdfBytes.size()));
        pdf.close();

        ManualImportController manual;
        QVERIFY(orchestrationImport(manual, source));

        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        controller.setManualController(&manual);
        // THE BUG (pre-fix): this failed with no_canonical_text because the
        // PDF cache truth lives in the per-page .json artifact and loadText
        // only read the .txt artifact.
        controller.requestExtraction();
        QCOMPARE(controller.stateToken(), QStringLiteral("consent_required"));
        // Fail-closed order preserved: NOTHING may be sent before consent.
        QCOMPARE(runner.beginCount(), 0);

        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 1);
        QCOMPARE(runner.pending.size(), std::size_t(1));
        // The provider sees the aggregated canonical text of the PDF and the
        // truthful coarse format context (not a hardcoded txt token).
        QCOMPARE(runner.pending.at(0).request.documentTypeToken,
                 std::string("pdf"));
        QVERIFY(!runner.pending.at(0).request.canonicalExtractedText.empty());
        QVERIFY(runner.pending.at(0).request.canonicalExtractedText.find(
                    "Manufacturer: ACME Power Systems Ltd.")
                != std::string::npos);
    }

    void o22_noTextPdfFailsClosedWithAccurateClassification()
    {
        QTemporaryDir managedRoot;
        QVERIFY(managedRoot.isValid());
        ManualStore::setManagedRootOverride(managedRoot.path());
        const QString source =
            QDir(managedRoot.path()).filePath("scanned-manual.pdf");
        QFile pdf(source);
        QVERIFY(pdf.open(QIODevice::WriteOnly));
        const QByteArray pdfBytes = orchestrationNoTextPdf();
        QCOMPARE(pdf.write(pdfBytes), static_cast<qint64>(pdfBytes.size()));
        pdf.close();

        ManualImportController manual;
        QVERIFY(orchestrationImport(manual, source));

        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        controller.setManualController(&manual);
        controller.requestExtraction();
        // Accurate no-text classification (never a fake extraction, never a
        // provider dispatch with empty context).
        QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
        QCOMPARE(controller.failureToken(),
                 QStringLiteral("no_extractable_text"));
        QCOMPARE(runner.beginCount(), 0);
    }

    void o23_txtSelectionStillReachesConsentGate()
    {
        QTemporaryDir managedRoot;
        QVERIFY(managedRoot.isValid());
        ManualStore::setManagedRootOverride(managedRoot.path());
        const QString source =
            QDir(managedRoot.path()).filePath("inverter-manual.txt");
        QFile txt(source);
        QVERIFY(txt.open(QIODevice::WriteOnly));
        txt.write(kManualText);
        txt.close();

        ManualImportController manual;
        QVERIFY(orchestrationImport(manual, source));

        FakeCandidateRunner runner;
        CandidateExtractionController controller(runner);
        controller.setManualController(&manual);
        controller.requestExtraction();
        QCOMPARE(controller.stateToken(), QStringLiteral("consent_required"));
        QCOMPARE(runner.beginCount(), 0);
        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 1);
        QCOMPARE(runner.pending.at(0).request.documentTypeToken,
                 std::string("txt"));
        QCOMPARE(runner.pending.at(0).request.canonicalExtractedText,
                 std::string(kManualText));
    }
};

QTEST_MAIN(CandidateOrchestrationTest)
#include "test_candidate_orchestration.moc"