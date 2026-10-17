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
#include "ui/ai/CandidateProviderAdapter.h"
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
using modbuslens::ui::CandidateAdapterConfig;
using modbuslens::ui::CandidateProviderAdapter;
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

CandidateProviderAdapter makeAdapter(FakeExtractionTransport &transport,
                                       std::string_view responseBody)
{
    transport.responseBody = std::string(responseBody);
    return CandidateProviderAdapter(transport,
                                      CandidateAdapterConfig{modbuslens::ui::AiProviderKind::ModelScope, "fake-model", 768});
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter = makeAdapter(transport, envelope);

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

        CandidateProviderAdapter adapter(transport,
                                           CandidateAdapterConfig{modbuslens::ui::AiProviderKind::ModelScope, "fake-model", 768});
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

        CandidateProviderAdapter adapter(transport,
                                           CandidateAdapterConfig{modbuslens::ui::AiProviderKind::ModelScope, "fake-model", 768});
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter adapter =
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
        CandidateProviderAdapter firstAdapter = makeAdapter(first, envelope);
        const CandidateExtractionResult firstResult =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, firstAdapter);

        FakeExtractionTransport second;
        CandidateProviderAdapter secondAdapter = makeAdapter(second, envelope);
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
        CandidateProviderAdapter adapter =
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

    // ================================================== T035-A.1 register ==
    // The strict register-candidate parser (ADR-004 / Human T035-A.1 §5-§7).
    // These slots exercise parseStrictRegisterCandidateProposals DIRECTLY:
    // A.1 is the parser foundation; wiring it into extract()/prompt belongs
    // to the next slice, so the manufacturer path above stays untouched.

    // --------------------------------------------------------------- R01 --
    // (§8-A) A legal register proposal parses with exact field values and
    // frozen defaults for absent optional members.
    void r01_validRegisterProposalParses()
    {
        // All fields present.
        const std::string full =
            "{\"registerProposals\":[{\"functionCode\":4,\"address\":5,"
            "\"name\":\"Temperature\",\"dataType\":\"Float32\",\"quantity\":2,"
            "\"evidence\":\"Register 5 Temperature Float32 scale 0.1\","
            "\"description\":\"Process temperature\",\"scale\":0.1,"
            "\"offset\":0,\"unit\":\"degC\"}]}";
        const modbuslens::ui::RegisterProviderExtractionResult result =
            modbuslens::ui::parseStrictRegisterCandidateProposals(full);
        QCOMPARE(result.ok, true);
        QCOMPARE(result.failure, ProviderExtractionFailure::None);
        QCOMPARE(result.proposals.size(), std::size_t{1});
        QCOMPARE(int(result.proposals.at(0).functionCode), 4);
        QCOMPARE(int(result.proposals.at(0).address), 5);
        QVERIFY(result.proposals.at(0).name == "Temperature");
        QVERIFY(result.proposals.at(0).description == "Process temperature");
        QCOMPARE(result.proposals.at(0).dataType,
                 modbuslens::core::RegisterDecodeType::Float32);
        QCOMPARE(result.proposals.at(0).quantity, 2);
        QCOMPARE(result.proposals.at(0).scale, 0.1);
        QCOMPARE(result.proposals.at(0).offset, 0.0);
        QVERIFY(result.proposals.at(0).unit == "degC");
        QVERIFY(result.proposals.at(0).evidenceExcerpt
                == "Register 5 Temperature Float32 scale 0.1");

        // Optional members absent => frozen defaults (scale 1.0 / offset 0.0
        // / unit "" / description "").
        const std::string minimal =
            "{\"registerProposals\":[{\"functionCode\":3,\"address\":100,"
            "\"name\":\"Setpoint\",\"dataType\":\"UInt16\",\"quantity\":1,"
            "\"evidence\":\"Register 100 Setpoint UInt16\"}]}";
        const modbuslens::ui::RegisterProviderExtractionResult defaults =
            modbuslens::ui::parseStrictRegisterCandidateProposals(minimal);
        QCOMPARE(defaults.ok, true);
        QCOMPARE(defaults.proposals.size(), std::size_t{1});
        QCOMPARE(defaults.proposals.at(0).scale, 1.0);
        QCOMPARE(defaults.proposals.at(0).offset, 0.0);
        QVERIFY(defaults.proposals.at(0).unit.empty());
        QVERIFY(defaults.proposals.at(0).description.empty());
        QCOMPARE(defaults.proposals.at(0).dataType,
                 modbuslens::core::RegisterDecodeType::UInt16);
    }

    // --------------------------------------------------------------- R02 --
    // (§8-B) A missing required member is a schema violation.
    void r02_missingRequiredMemberIsRejected()
    {
        // "address" is missing.
        const std::string content =
            "{\"registerProposals\":[{\"functionCode\":4,"
            "\"name\":\"Temperature\",\"dataType\":\"UInt16\",\"quantity\":1,"
            "\"evidence\":\"e\"}]}";
        const modbuslens::ui::RegisterProviderExtractionResult result =
            modbuslens::ui::parseStrictRegisterCandidateProposals(content);
        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- R03 --
    // (§8-C) Unknown members are rejected AND the two envelopes never mix:
    // a response carrying both "proposals" and "registerProposals" is a
    // violation on BOTH parsers (no implicit routing, §6).
    void r03_unknownMemberAndMixedEnvelopeAreRejected()
    {
        // (a) Extra member inside a register proposal.
        const std::string extra =
            "{\"registerProposals\":[{\"functionCode\":4,\"address\":5,"
            "\"name\":\"T\",\"dataType\":\"UInt16\",\"quantity\":1,"
            "\"evidence\":\"e\",\"note\":\"extra\"}]}";
        const modbuslens::ui::RegisterProviderExtractionResult extraResult =
            modbuslens::ui::parseStrictRegisterCandidateProposals(extra);
        QCOMPARE(extraResult.ok, false);
        QCOMPARE(extraResult.failure,
                 ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(extraResult.proposals.size(), std::size_t{0});

        // (b) Mixed envelope: register parser refuses "proposals".
        const std::string mixed =
            std::string{
                "{\"proposals\":[{\"target\":\"manufacturer\",\"value\":\"v\","
                "\"evidence\":\"e\"}],\"registerProposals\":[]}"};
        const modbuslens::ui::RegisterProviderExtractionResult mixedResult =
            modbuslens::ui::parseStrictRegisterCandidateProposals(mixed);
        QCOMPARE(mixedResult.ok, false);
        QCOMPARE(mixedResult.failure,
                 ProviderExtractionFailure::SchemaViolation);

        // (c) Symmetry: the PROFILE-FIELD parser equally refuses the
        // register envelope (unknown member "registerProposals").
        const ProviderExtractionResult profileResult =
            modbuslens::ui::parseStrictCandidateProposals(
                "{\"registerProposals\":[]}");
        QCOMPARE(profileResult.ok, false);
        QCOMPARE(profileResult.failure,
                 ProviderExtractionFailure::SchemaViolation);
    }

    // --------------------------------------------------------------- R04 --
    // (§8-D) Illegal functionCode: 0x00, the exception space, fractional and
    // string forms are all refusals — never coerced, never defaulted.
    void r04_illegalFunctionCodeIsRejected()
    {
        for (const char *bad :
             {"0", "128", "255", "4.5", "\"4\""}) {
            const std::string content =
                std::string{"{\"registerProposals\":[{\"functionCode\":"}
                + bad
                + ",\"address\":5,\"name\":\"T\",\"dataType\":\"UInt16\","
                  "\"quantity\":1,\"evidence\":\"e\"}]}";
            const modbuslens::ui::RegisterProviderExtractionResult result =
                modbuslens::ui::parseStrictRegisterCandidateProposals(content);
            QCOMPARE(result.ok, false);
            QCOMPARE(result.failure,
                     ProviderExtractionFailure::SchemaViolation);
            QCOMPARE(result.proposals.size(), std::size_t{0});
        }
    }

    // --------------------------------------------------------------- R05 --
    // (§8-E) Illegal address: above the PDU top, negative and fractional
    // forms are refusals.
    void r05_illegalAddressIsRejected()
    {
        for (const char *bad : {"65536", "-1", "1.5"}) {
            const std::string content =
                std::string{"{\"registerProposals\":[{\"functionCode\":4,"
                            "\"address\":"}
                + bad
                + ",\"name\":\"T\",\"dataType\":\"UInt16\",\"quantity\":1,"
                  "\"evidence\":\"e\"}]}";
            const modbuslens::ui::RegisterProviderExtractionResult result =
                modbuslens::ui::parseStrictRegisterCandidateProposals(content);
            QCOMPARE(result.ok, false);
            QCOMPARE(result.failure,
                     ProviderExtractionFailure::SchemaViolation);
            QCOMPARE(result.proposals.size(), std::size_t{0});
        }
    }

    // --------------------------------------------------------------- R06 --
    // (§8-F) quantity must equal the dataType word count — the mismatch is
    // REJECTED, never auto-corrected (Human T035-A.1 §5).
    void r06_quantityDataTypeMismatchIsRejectedWithoutCorrection()
    {
        const std::string uint16Three =
            "{\"registerProposals\":[{\"functionCode\":3,\"address\":100,"
            "\"name\":\"T\",\"dataType\":\"UInt16\",\"quantity\":3,"
            "\"evidence\":\"e\"}]}";
        QCOMPARE(
            modbuslens::ui::parseStrictRegisterCandidateProposals(uint16Three)
                .ok,
            false);
        const std::string uint16Two =
            "{\"registerProposals\":[{\"functionCode\":3,\"address\":100,"
            "\"name\":\"T\",\"dataType\":\"UInt16\",\"quantity\":2,"
            "\"evidence\":\"e\"}]}";
        QCOMPARE(
            modbuslens::ui::parseStrictRegisterCandidateProposals(uint16Two).ok,
            false);
        const std::string float32One =
            "{\"registerProposals\":[{\"functionCode\":3,\"address\":100,"
            "\"name\":\"T\",\"dataType\":\"Float32\",\"quantity\":1,"
            "\"evidence\":\"e\"}]}";
        QCOMPARE(
            modbuslens::ui::parseStrictRegisterCandidateProposals(float32One)
                .ok,
            false);
        // The matching pairs stay legal (positive controls).
        const std::string uint16One =
            "{\"registerProposals\":[{\"functionCode\":3,\"address\":100,"
            "\"name\":\"T\",\"dataType\":\"UInt16\",\"quantity\":1,"
            "\"evidence\":\"e\"}]}";
        QCOMPARE(
            modbuslens::ui::parseStrictRegisterCandidateProposals(uint16One)
                .ok,
            true);
        const std::string float32Two =
            "{\"registerProposals\":[{\"functionCode\":3,\"address\":100,"
            "\"name\":\"T\",\"dataType\":\"Float32\",\"quantity\":2,"
            "\"evidence\":\"e\"}]}";
        QCOMPARE(
            modbuslens::ui::parseStrictRegisterCandidateProposals(float32Two)
                .ok,
            true);
    }

    // --------------------------------------------------------------- R07 --
    // (§8-G) Non-finite scale: JSON has no NaN/Infinity literals, so an
    // overflowing literal ("1e999") fails the finite check via from_chars
    // out_of_range, and a string "0.1" is a type error. Same for offset.
    void r07_nonFiniteOrWrongTypeScaleIsRejected()
    {
        for (const char *badScale : {"1e999", "-1e999", "\"0.1\""}) {
            const std::string content =
                std::string{"{\"registerProposals\":[{\"functionCode\":4,"
                            "\"address\":5,\"name\":\"T\","
                            "\"dataType\":\"UInt16\",\"quantity\":1,"
                            "\"evidence\":\"e\",\"scale\":"}
                + badScale + "}]}";
            const modbuslens::ui::RegisterProviderExtractionResult result =
                modbuslens::ui::parseStrictRegisterCandidateProposals(content);
            QCOMPARE(result.ok, false);
            QCOMPARE(result.failure,
                     ProviderExtractionFailure::SchemaViolation);
            QCOMPARE(result.proposals.size(), std::size_t{0});
        }
        for (const char *badOffset : {"1e999", "\"0\""}) {
            const std::string content =
                std::string{"{\"registerProposals\":[{\"functionCode\":4,"
                            "\"address\":5,\"name\":\"T\","
                            "\"dataType\":\"UInt16\",\"quantity\":1,"
                            "\"evidence\":\"e\",\"offset\":"}
                + badOffset + "}]}";
            const modbuslens::ui::RegisterProviderExtractionResult result =
                modbuslens::ui::parseStrictRegisterCandidateProposals(content);
            QCOMPARE(result.ok, false);
            QCOMPARE(result.failure,
                     ProviderExtractionFailure::SchemaViolation);
        }
    }

    // --------------------------------------------------------------- R08 --
    // (§8-H) Evidence is REQUIRED: an empty or absent "evidence" member is a
    // refusal (the candidate can never exist without its exact excerpt).
    void r08_missingOrEmptyEvidenceIsRejected()
    {
        const std::string empty =
            "{\"registerProposals\":[{\"functionCode\":4,\"address\":5,"
            "\"name\":\"T\",\"dataType\":\"UInt16\",\"quantity\":1,"
            "\"evidence\":\"\"}]}";
        QCOMPARE(
            modbuslens::ui::parseStrictRegisterCandidateProposals(empty).ok,
            false);
        const std::string absent =
            "{\"registerProposals\":[{\"functionCode\":4,\"address\":5,"
            "\"name\":\"T\",\"dataType\":\"UInt16\",\"quantity\":1}]}";
        QCOMPARE(
            modbuslens::ui::parseStrictRegisterCandidateProposals(absent).ok,
            false);
    }

    // --------------------------------------------------------------- R09 --
    // An empty "registerProposals" array is legal (mirror of the frozen
    // profile-field rule: "the source supports nothing" is a valid outcome).
    void r09_emptyRegisterProposalsArrayIsLegal()
    {
        const modbuslens::ui::RegisterProviderExtractionResult result =
            modbuslens::ui::parseStrictRegisterCandidateProposals(
                "{\"registerProposals\":[]}");
        QCOMPARE(result.ok, true);
        QCOMPARE(result.failure, ProviderExtractionFailure::None);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- R10 --
    // (§8-I) Manufacturer-path regression marker: the profile-field parser
    // still accepts exactly its own frozen contract, untouched by the
    // register parser addition (full suite = the remaining slots above).
    void r10_manufacturerParserStillAcceptsItsOwnContract()
    {
        const std::string content =
            std::string{"{\"proposals\":[{\"target\":\"manufacturer\",\"value\":\""}
            + kManufacturerValue + "\",\"evidence\":\"" + kManufacturerExcerpt
            + "\"}]}";
        const ProviderExtractionResult result =
            modbuslens::ui::parseStrictCandidateProposals(content);
        QCOMPARE(result.ok, true);
        QCOMPARE(result.proposals.size(), std::size_t{1});
        QCOMPARE(result.proposals.at(0).target,
                 ProfileFieldTarget::Manufacturer);
    }

    // ================================================== T035-A.2 wiring ==
    // The register_map request contract, prompt branch and parser
    // integration (ADR-004 / Human T035-A.2 §3-§7). extract() itself is
    // untouched: manufacturer slots above ARE the compatibility proof.

    // --------------------------------------------------------------- R11 --
    // (§7-A) The register-map request is generated with the frozen token and
    // the SAME H2 payload discipline; the wrong-mode guard refuses closed.
    void r11_registerMapRequestIsGenerated()
    {
        const ExtractionRequest request =
            modbuslens::core::buildRegisterMapExtractionRequest(
                makeDocument(), std::string_view{kManualText});
        QVERIFY(request.targetFieldToken
                == modbuslens::core::kRegisterMapTargetToken);
        QVERIFY(request.targetFieldToken == "register_map");
        QVERIFY(request.documentTypeToken == "txt");
        QVERIFY(request.canonicalExtractedText == kManualText);

        // Wrong-mode dispatch is refused closed (caller bug, never guessed).
        FakeExtractionTransport transport;
        CandidateProviderAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope("{}"));
        const modbuslens::ui::RegisterProviderExtractionResult wrongMode =
            adapter.extractRegister(
                modbuslens::core::buildC2FirstSliceExtractionRequest(
                    makeDocument(), std::string_view{kManualText}));
        QCOMPARE(wrongMode.ok, false);
        QCOMPARE(wrongMode.failure,
                 ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(wrongMode.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- R12 --
    // (§7-B) The register prompt branch: the wire body carries the register
    // schema and the manual text, and NEVER the profile-field envelope key.
    void r12_registerPromptBranchDrivesTheWireBody()
    {
        FakeExtractionTransport transport;
        CandidateProviderAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope("{}"));
        const std::string body = adapter.buildWireBodyForTest(
            modbuslens::core::buildRegisterMapExtractionRequest(
                makeDocument(), std::string_view{kManualText}));

        QVERIFY(body.find("registerProposals") != std::string::npos);
        QVERIFY(body.find("\"proposals\"") == std::string::npos);
        QVERIFY(body.find("Target: register_map") != std::string::npos);
        QVERIFY(body.find("functionCode") != std::string::npos);
        QVERIFY(body.find("quantity") != std::string::npos);
        QVERIFY(body.find("MANUAL TEXT BEGIN") != std::string::npos);
        // The manual text is embedded, but JSON-escaped: assert a SINGLE-LINE
        // excerpt (real newlines become \n sequences inside the JSON string).
        QVERIFY(body.find("Model: INV-1000") != std::string::npos);
        // The manufacturer schema key must not leak into the register prompt.
        QVERIFY(body.find("\"value\":\"...\"") == std::string::npos);
    }

    // --------------------------------------------------------------- R13 --
    // (§7-C) A legal registerProposals response flows through the FULL
    // adapter path (transport -> envelope -> strict register parser).
    void r13_legalRegisterProposalsFlowThroughTheAdapter()
    {
        const std::string content =
            "{\"registerProposals\":[{\"functionCode\":4,\"address\":5,"
            "\"name\":\"Temperature\",\"dataType\":\"Float32\",\"quantity\":2,"
            "\"evidence\":\"Register 5 Temperature Float32 scale 0.1\","
            "\"scale\":0.1,\"unit\":\"degC\"}]}";
        FakeExtractionTransport transport;
        CandidateProviderAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const modbuslens::ui::RegisterProviderExtractionResult result =
            adapter.extractRegister(
                modbuslens::core::buildRegisterMapExtractionRequest(
                    makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, true);
        QCOMPARE(result.failure, ProviderExtractionFailure::None);
        QCOMPARE(result.proposals.size(), std::size_t{1});
        QCOMPARE(int(result.proposals.at(0).functionCode), 4);
        QCOMPARE(int(result.proposals.at(0).address), 5);
        QVERIFY(result.proposals.at(0).name == "Temperature");
        QCOMPARE(result.proposals.at(0).dataType,
                 modbuslens::core::RegisterDecodeType::Float32);
        QCOMPARE(result.proposals.at(0).quantity, 2);
        QCOMPARE(result.proposals.at(0).scale, 0.1);
        QVERIFY(result.proposals.at(0).unit == "degC");
    }

    // --------------------------------------------------------------- R14 --
    // (§7-D) A MIXED envelope (proposals + registerProposals) through the
    // register path is a whole-response rejection.
    void r14_mixedEnvelopeViaRegisterPathIsRejected()
    {
        const std::string mixed =
            std::string{
                "{\"proposals\":[{\"target\":\"manufacturer\",\"value\":\"v\","
                "\"evidence\":\"e\"}],\"registerProposals\":[]}"};
        FakeExtractionTransport transport;
        CandidateProviderAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(mixed));

        const modbuslens::ui::RegisterProviderExtractionResult result =
            adapter.extractRegister(
                modbuslens::core::buildRegisterMapExtractionRequest(
                    makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- R15 --
    // (§7-E) An unknown member inside a register proposal is rejected.
    void r15_unknownFieldViaRegisterPathIsRejected()
    {
        const std::string content =
            "{\"registerProposals\":[{\"functionCode\":4,\"address\":5,"
            "\"name\":\"T\",\"dataType\":\"UInt16\",\"quantity\":1,"
            "\"evidence\":\"e\",\"confidence\":0.9}]}";
        FakeExtractionTransport transport;
        CandidateProviderAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const modbuslens::ui::RegisterProviderExtractionResult result =
            adapter.extractRegister(
                modbuslens::core::buildRegisterMapExtractionRequest(
                    makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- R16 --
    // (§7-F) A missing required member is rejected through the full path.
    void r16_missingFieldViaRegisterPathIsRejected()
    {
        const std::string content =
            "{\"registerProposals\":[{\"functionCode\":4,"
            "\"name\":\"T\",\"dataType\":\"UInt16\",\"quantity\":1,"
            "\"evidence\":\"e\"}]}";
        FakeExtractionTransport transport;
        CandidateProviderAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const modbuslens::ui::RegisterProviderExtractionResult result =
            adapter.extractRegister(
                modbuslens::core::buildRegisterMapExtractionRequest(
                    makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }

    // --------------------------------------------------------------- R17 --
    // (§7-G) An unknown dataType token is rejected — the M11 frozen token
    // set is the only authority.
    void r17_unknownDataTypeViaRegisterPathIsRejected()
    {
        const std::string content =
            "{\"registerProposals\":[{\"functionCode\":4,\"address\":5,"
            "\"name\":\"T\",\"dataType\":\"Uint64\",\"quantity\":1,"
            "\"evidence\":\"e\"}]}";
        FakeExtractionTransport transport;
        CandidateProviderAdapter adapter =
            makeAdapter(transport, chatCompletionsEnvelope(content));

        const modbuslens::ui::RegisterProviderExtractionResult result =
            adapter.extractRegister(
                modbuslens::core::buildRegisterMapExtractionRequest(
                    makeDocument(), std::string_view{kManualText}));

        QCOMPARE(result.ok, false);
        QCOMPARE(result.failure, ProviderExtractionFailure::SchemaViolation);
        QCOMPARE(result.proposals.size(), std::size_t{0});
    }
};

QTEST_MAIN(CandidateAdapterTest)
#include "test_candidate_adapter.moc"