// M12-C C2 SECOND SLICE tests (T027 §68): the ModelScope provider adapter
// boundary and the STRICT provider response contract, exercised exclusively
// through a deterministic fake transport.
//
// Determinism is structural, not a promise: this target links NO network type
// at all, so no live request, token or cost is possible from this suite.
//
// The matrix is N01..N18 from the session contract. The central acceptance
// invariant is N09: schema-valid provider output whose excerpt is NOT in the
// manual must still yield ZERO candidates — a provider response being valid
// JSON, and even schema-valid, never makes it true.
#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/candidate/CandidateExtraction.h"
#include "core/candidate/ProviderExtractionContract.h"
#include "core/profile/DeviceProfile.h"
#include "ui/ai/ExtractionTransport.h"
#include "ui/ai/ModelScopeCandidateAdapter.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::core::CandidateProposal;
using modbuslens::core::CandidateExtractionResult;
using modbuslens::core::CandidateLifecycleState;
using modbuslens::core::ExtractionRequest;
using modbuslens::core::ManualDocument;
using modbuslens::core::ManualDocumentStatus;
using modbuslens::core::ManualDocumentType;
using modbuslens::core::ProfileFieldCandidate;
using modbuslens::core::ProfileFieldTarget;
using modbuslens::core::ProviderExtractionFailure;
using modbuslens::core::ProviderExtractionResult;
using modbuslens::ui::IExtractionTransport;
using modbuslens::ui::ModelScopeAdapterConfig;
using modbuslens::ui::ModelScopeCandidateAdapter;
using modbuslens::ui::ProfileStore;

namespace {

// ---------------------------------------------------------------- fixtures --
constexpr char kDocumentId[] = "11111111-aaaa-bbbb-cccc-222222222222";
constexpr char kContentHash[] =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

constexpr char kManualText[] =
    "ACME Power Systems Ltd. - Inverter Manual\n"
    "\n"
    "1. Identity\n"
    "Manufacturer: ACME Power Systems Ltd.\n"
    "Model: INV-1000\n"
    "Revision: rev A\n";

constexpr char kManufacturerExcerpt[] = "Manufacturer: ACME Power Systems Ltd.";
constexpr char kManufacturerValue[] = "ACME Power Systems Ltd.";

ManualDocument makeDocument()
{
    ManualDocument document;
    document.schemaVersion = 1;
    document.documentId = kDocumentId;
    document.originalFileName = "inverter-manual.txt";
    document.documentType = ManualDocumentType::Txt;
    // PROVENANCE ONLY — never read again.
    document.originalPath = "Z:/provenance-only/never-read.txt";
    document.contentHash = kContentHash;
    document.byteSize = sizeof(kManualText);
    document.charCount = sizeof(kManualText) - 1;
    document.status = ManualDocumentStatus::Ready;
    return document;
}

// A ModelScope-shaped Chat Completions envelope carrying `content` as the
// assistant message. The envelope shape mirrors the accepted M6 client
// (choices[] -> message.content); no new envelope is invented.
[[nodiscard]] std::string chatCompletionsEnvelope(std::string_view content)
{
    std::string assistant;
    for (const char c : content) {
        switch (c) {
        case '"':
            assistant += "\\\"";
            break;
        case '\\':
            assistant += "\\\\";
            break;
        case '\n':
            assistant += "\\n";
            break;
        default:
            assistant.push_back(c);
            break;
        }
    }
    std::string body;
    body += "{\"id\":\"chatcmpl-1\",\"object\":\"chat.completion\",";
    body += "\"model\":\"Qwen/test\",\"choices\":[{\"index\":0,";
    body += "\"message\":{\"role\":\"assistant\",\"content\":\"";
    body += assistant;
    body += "\"},\"finish_reason\":\"stop\"}]}";
    return body;
}

// The strict, schema-VALID extraction payload for the manufacturer.
[[nodiscard]] std::string validExtractionContent()
{
    std::string content = "{\"proposals\":[{\"target\":\"manufacturer\",";
    content += "\"value\":\"";
    content += kManufacturerValue;
    content += "\",\"evidence\":\"";
    content += kManufacturerExcerpt;
    content += "\"}]}";
    return content;
}

// ------------------------------------------------------ TEST-ONLY transport --
// Deterministic fake: it captures the outbound request, returns a scripted
// exchange and counts calls. It never touches a network.
class FakeExtractionTransport : public IExtractionTransport
{
public:
    std::string responseBody;
    bool transportOk{true};
    bool statusOk{true};
    int calls{0};
    std::string lastRequestBody;

    Exchange send(std::string_view requestBody) override
    {
        ++calls;
        lastRequestBody.assign(requestBody.begin(), requestBody.end());
        Exchange exchange;
        exchange.responseBody = responseBody;
        exchange.transportOk = transportOk;
        exchange.statusOk = statusOk;
        return exchange;
    }
};

ModelScopeCandidateAdapter makeAdapter(FakeExtractionTransport &transport,
                                       std::string_view responseBody)
{
    transport.responseBody = std::string(responseBody);
    return ModelScopeCandidateAdapter(transport,
                                      ModelScopeAdapterConfig{"fake-model", 768});
}

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

template <typename T, typename = void>
struct hasConfidenceMember : std::false_type
{
};
template <typename T>
struct hasConfidenceMember<
    T, std::void_t<decltype(std::declval<T &>().confidence)>> : std::true_type
{
};
template <typename T, typename = void>
struct hasRawResponseMember : std::false_type
{
};
template <typename T>
struct hasRawResponseMember<T, std::void_t<decltype(std::declval<T &>().rawResponse)>>
    : std::true_type
{
};

// H5 / §14 as COMPILE-TIME contracts: neither a confidence member nor a raw
// provider-response member may exist on the neutral result or candidate types.
static_assert(!hasConfidenceMember<ProviderExtractionResult>::value,
              "C2 v1 forbids a confidence member on the extraction result (H5)");
static_assert(!hasConfidenceMember<ProfileFieldCandidate>::value,
              "C2 v1 forbids a confidence member on a Candidate (H5)");
static_assert(!hasRawResponseMember<ProviderExtractionResult>::value,
              "the raw provider response must not be retained (§14)");
static_assert(!hasRawResponseMember<ProfileFieldCandidate>::value,
              "the raw provider response must not be retained (§14)");

} // namespace

class CandidateAdapterTest : public QObject
{
    Q_OBJECT

private slots:
    // --------------------------------------------------------------- N01 --
    void n01_validProviderResponseYieldsPendingReviewCandidate()
    {
        const ManualDocument document = makeDocument();
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(validExtractionContent()));

        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, adapter);

        QCOMPARE(transport.calls, 1);
        QCOMPARE(result.candidates.size(), std::size_t{1});
        const ProfileFieldCandidate &candidate = result.candidates.front();
        QCOMPARE(candidate.target, ProfileFieldTarget::Manufacturer);
        QCOMPARE(candidate.proposedValue, std::string{kManufacturerValue});
        QCOMPARE(candidate.lifecycle, CandidateLifecycleState::PendingReview);
        QCOMPARE(candidate.evidence.excerpt, std::string{kManufacturerExcerpt});
        // Evidence identity is derived locally, and the location is the one
        // SESSION M's validator computed — never anything the provider sent.
        QCOMPARE(candidate.evidence.documentId, std::string{kDocumentId});
        QCOMPARE(candidate.evidence.contentHash, std::string{kContentHash});
        const std::size_t expectedAt = std::string{kManualText}.find(kManufacturerExcerpt);
        QCOMPARE(candidate.evidence.textStart,
                 static_cast<std::int64_t>(expectedAt));
    }

    // --------------------------------------------------------------- N02 --
    void n02_malformedStructuredContentFailsClosed()
    {
        const ManualDocument document = makeDocument();
        FakeExtractionTransport transport;
        // The envelope is fine; the assistant content is not JSON at all.
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope("{not json at all"));

        const ProviderExtractionResult result = adapter.extract(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                document, std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::MalformedResponse);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- N03 --
    void n03_wrongTopLevelTypeIsRejected()
    {
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope("[1,2,3]"));

        const ProviderExtractionResult result = adapter.extract(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- N04 --
    void n04_missingRequiredFieldIsRejected()
    {
        // "value" is missing from the proposal object.
        const std::string content =
            std::string{"{\"proposals\":[{\"target\":\"manufacturer\",\"evidence\":\""}
            + kManufacturerExcerpt + "\"}]}";
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const ProviderExtractionResult result = adapter.extract(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- N05 --
    void n05_wrongFieldTypeIsRejectedWithoutCoercion()
    {
        // "value" is a NUMBER, not a string. No coercion is permitted.
        const std::string content =
            "{\"proposals\":[{\"target\":\"manufacturer\",\"value\":42,"
            "\"evidence\":\"" + std::string{kManufacturerExcerpt} + "\"}]}";
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const ProviderExtractionResult result = adapter.extract(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- N06 --
    void n06_unknownExtraSchemaFieldIsRejected()
    {
        // An unexpected extra member proves the schema is STRICT rather than
        // permissive about provider drift.
        const std::string content =
            "{\"proposals\":[{\"target\":\"manufacturer\",\"value\":\""
            + std::string{kManufacturerValue} + "\",\"evidence\":\""
            + std::string{kManufacturerExcerpt}
            + "\",\"note\":\"extra\"}]}";
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const ProviderExtractionResult result = adapter.extract(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- N07 --
    void n07_unknownTargetFieldIsRejected()
    {
        const std::string content =
            std::string{"{\"proposals\":[{\"target\":\"firmware_secret\",\"value\":\""
            + std::string{kManufacturerValue} + "\",\"evidence\":\""
            + std::string{kManufacturerExcerpt} + "\"}]}"};
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const ProviderExtractionResult result = adapter.extract(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- N08 --
    void n08_emptyEvidenceIsRejected()
    {
        const std::string content =
            std::string{"{\"proposals\":[{\"target\":\"manufacturer\",\"value\":\""
            + std::string{kManufacturerValue} + "\",\"evidence\":\"\"}]}"};
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const ProviderExtractionResult result = adapter.extract(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- N09 --
    // THE CENTRAL ACCEPTANCE INVARIANT: schema-valid provider output whose
    // excerpt is NOT in the manual yields ZERO candidates. A well-formed AI
    // answer is not truth; only locally verifiable evidence makes a Candidate.
    void n09_schemaValidButFalseEvidenceYieldsNoCandidate()
    {
        const ManualDocument document = makeDocument();
        // Perfectly valid schema; the excerpt simply does not exist in the
        // supplied manual text.
        const std::string content =
            std::string{"{\"proposals\":[{\"target\":\"manufacturer\",\"value\":"
            "\"FABRICATED CORP\",\"evidence\":\"Manufacturer: FABRICATED CORP\"}]}"};
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, adapter);

        QCOMPARE(transport.calls, 1); // the provider WAS consumed
        QCOMPARE(result.candidates.size(), std::size_t{0}); // yet nothing is accepted
        QCOMPARE(result.refusedProposalCount, std::size_t{1});
    }

    // --------------------------------------------------------------- N10 --
    void n10_providerLocationIsNotAuthority()
    {
        // This slice's provider contract carries NO location hint at all, so
        // there is nothing for a provider to assert. The schema proves it: a
        // provider-supplied location member is an UNKNOWN field and is
        // refused, so it can never become authority over the evidence span.
        const std::string content =
            std::string{"{\"proposals\":[{\"target\":\"manufacturer\",\"value\":\""
            + std::string{kManufacturerValue} + "\",\"evidence\":\""
            + std::string{kManufacturerExcerpt}
            + "\",\"location\":4096}]}"};
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const ProviderExtractionResult result = adapter.extract(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- N11 --
    void n11_numericConfidenceCannotReachACandidate()
    {
        const std::string content =
            std::string{"{\"proposals\":[{\"target\":\"manufacturer\",\"value\":\""
            + std::string{kManufacturerValue} + "\",\"evidence\":\""
            + std::string{kManufacturerExcerpt} + "\",\"confidence\":0.97}]}"};
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const ProviderExtractionResult result = adapter.extract(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});

        // And the neutral proposal type still has no confidence member (H5) —
        // asserted at compile time above.
    }

    // --------------------------------------------------------------- N12 --
    void n12_rawResponseIsNotRetained()
    {
        FakeExtractionTransport transport;
        const std::string envelope = chatCompletionsEnvelope(validExtractionContent());
        ModelScopeCandidateAdapter adapter = makeAdapter(transport, envelope);

        const ProviderExtractionResult result = adapter.extract(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, true);
        QCOMPARE(result.proposals.size(), std::size_t{1});
        // The returned proposal carries the extracted fields only: the raw
        // envelope text (id / object / finish_reason) is nowhere in it.
        const CandidateProposal &proposal = result.proposals.front();
        QCOMPARE(proposal.proposedValue, std::string{kManufacturerValue});
        QVERIFY(envelope.find("chatcmpl-1") != std::string::npos);
        QVERIFY(proposal.proposedValue.find("chatcmpl-1") == std::string::npos);
        QVERIFY(proposal.evidenceExcerpt.find("chatcmpl-1") == std::string::npos);
    }

    // --------------------------------------------------------------- N13 --
    void n13_transportErrorFailsClosed()
    {
        const ManualDocument document = makeDocument();
        FakeExtractionTransport transport;
        transport.transportOk = false;
        transport.responseBody = chatCompletionsEnvelope(validExtractionContent());

        ModelScopeCandidateAdapter adapter(transport,
                                           ModelScopeAdapterConfig{"fake-model", 768});
        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, adapter);

        QCOMPARE(transport.calls, 1);
        QCOMPARE(result.candidates.size(), std::size_t{0});
        QCOMPARE(adapter.lastFailure(), ProviderExtractionFailure::TransportError);
    }

    // --------------------------------------------------------------- N14 --
    void n14_nonSuccessProviderResultFailsClosed()
    {
        const ManualDocument document = makeDocument();
        FakeExtractionTransport transport;
        transport.statusOk = false;
        transport.responseBody = chatCompletionsEnvelope(validExtractionContent());

        ModelScopeCandidateAdapter adapter(transport,
                                           ModelScopeAdapterConfig{"fake-model", 768});
        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, adapter);

        QCOMPARE(result.candidates.size(), std::size_t{0});
        QCOMPARE(adapter.lastFailure(),
                 ProviderExtractionFailure::ProviderRejectedStatus);
    }

    // --------------------------------------------------------------- N15 --
    void n15_outboundPayloadIsMinimized()
    {
        const ManualDocument document = makeDocument();
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(validExtractionContent()));

        const ExtractionRequest request =
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                document, std::string_view{kManualText});
        const std::string body = adapter.buildWireBodyForTest(request);

        // The intended extraction payload IS sent. The body is JSON, so the
        // canonical text appears with JSON escaping; the assertion is therefore
        // SEMANTIC (distinctive first / evidence / last lines all present)
        // rather than a brittle full-string comparison.
        QVERIFY(body.find("ACME Power Systems Ltd. - Inverter Manual")
                != std::string::npos);
        QVERIFY(body.find(kManufacturerExcerpt) != std::string::npos);
        QVERIFY(body.find("Revision: rev A") != std::string::npos);
        QVERIFY(body.find("manufacturer") != std::string::npos);
        // ...and nothing else about the local state is.
        QVERIFY(body.find(kDocumentId) == std::string::npos);      // local identity
        QVERIFY(body.find(kContentHash) == std::string::npos);     // content identity
        QVERIFY(body.find("inverter-manual.txt") == std::string::npos); // filename
        QVERIFY(body.find("provenance-only") == std::string::npos); // original path
        // No credential-shaped material, and no device/transaction truth.
        QVERIFY(body.find("apiKey") == std::string::npos);
        QVERIFY(body.find("api_key") == std::string::npos);
        QVERIFY(body.find("Authorization") == std::string::npos);
        QVERIFY(body.find("Bearer") == std::string::npos);
        QVERIFY(body.find("register") == std::string::npos);
        QVERIFY(body.find("transaction") == std::string::npos);
        QVERIFY(body.find("profileId") == std::string::npos);

        // The request contract itself contains only the extraction payload.
        QCOMPARE(request.targetFieldToken, std::string{"manufacturer"});
        QCOMPARE(request.canonicalExtractedText, std::string{kManualText});
    }

    // --------------------------------------------------------------- N16 --
    void n16_sourceIsTheCanonicalTextNotTheOriginalFile()
    {
        // SESSION M already proves the slice never re-reads the original file
        // (C2-A12). Here we confirm the SESSION N path adds no second read: the
        // adapter is driven purely by the canonical text handed to it, and the
        // document carries a path that does not exist.
        const ManualDocument document = makeDocument();
        QVERIFY(!QFileInfo::exists(QString::fromStdString(document.originalPath)));

        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(validExtractionContent()));

        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, adapter);

        QCOMPARE(result.candidates.size(), std::size_t{1});
        // The outbound body carries the canonical text, never a file path.
        QVERIFY(transport.lastRequestBody.find("provenance-only")
                == std::string::npos);
    }

    // --------------------------------------------------------------- N17 --
    void n17_extractionIsDeterministic()
    {
        const ManualDocument document = makeDocument();
        const std::string envelope = chatCompletionsEnvelope(validExtractionContent());

        FakeExtractionTransport first;
        ModelScopeCandidateAdapter firstAdapter = makeAdapter(first, envelope);
        const CandidateExtractionResult firstResult =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, firstAdapter);

        FakeExtractionTransport second;
        ModelScopeCandidateAdapter secondAdapter = makeAdapter(second, envelope);
        const CandidateExtractionResult secondResult =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, secondAdapter);

        QCOMPARE(firstResult.candidates.size(), secondResult.candidates.size());
        QVERIFY(firstResult.candidates == secondResult.candidates); // value equality
        // Both runs produced identical wire bodies for identical inputs.
        QCOMPARE(first.lastRequestBody, second.lastRequestBody);
    }

    // --------------------------------------------------------------- N18 --
    void n18_deviceProfileHasZeroDiff()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        ProfileStore::setManagedRootOverride(root.path());
        const QStringList before = snapshotTree(root.path());

        const ManualDocument document = makeDocument();
        FakeExtractionTransport transport;
        ModelScopeCandidateAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(validExtractionContent()));
        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, adapter);

        QCOMPARE(result.candidates.size(), std::size_t{1});
        // No ProfileStore file was created or mutated by the extraction.
        QCOMPARE(snapshotTree(root.path()), before);
        QCOMPARE(snapshotTree(ProfileStore::managedProfilesDirectory()), before);

        ProfileStore::setManagedRootOverride(QString());
    }
};

QTEST_MAIN(CandidateAdapterTest)
#include "test_candidate_adapter.moc"