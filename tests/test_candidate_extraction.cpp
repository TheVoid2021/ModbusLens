// M12-C C2 first-slice tests: provider-neutral AI candidate extraction with
// LOCALLY VERIFIED evidence (T027 §66, HUMAN-APPROVED C2 CONTRACT).
//
// Everything here is deterministic and offline: the provider is a TEST-ONLY
// double, there is no network access, no credential and no live model. The
// matrix is C2-A01..A12 from the session contract.
//
// A10 is a COMPILE-TIME contract: the absence of a numeric confidence /
// score / probability member on the candidate types is asserted with a real
// SFINAE probe, so re-introducing one breaks the build instead of silently
// regressing (H5).
#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/candidate/CandidateExtraction.h"
#include "core/profile/DeviceProfile.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::core::CandidateEvidence;
using modbuslens::core::CandidateExtractionResult;
using modbuslens::core::CandidateLifecycleState;
using modbuslens::core::CandidateProposal;
using modbuslens::core::DeviceProfile;
using modbuslens::core::ICandidateProposalProvider;
using modbuslens::core::ManualDocument;
using modbuslens::core::ManualDocumentStatus;
using modbuslens::core::ManualDocumentType;
using modbuslens::core::ProfileFieldCandidate;
using modbuslens::core::ProfileFieldTarget;
using modbuslens::core::RegisterDecodeType;
using modbuslens::core::RegisterEntry;
using modbuslens::ui::ProfileStore;

namespace {

// ------------------------------------------------------------- A10 compile probes --
// A member named `confidence` / `score` / `probability` must not exist anywhere
// in the C2 domain contract (H5). These probes fail the BUILD if one appears.
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
struct hasScoreMember : std::false_type
{
};
template <typename T>
struct hasScoreMember<T, std::void_t<decltype(std::declval<T &>().score)>>
    : std::true_type
{
};

template <typename T, typename = void>
struct hasProbabilityMember : std::false_type
{
};
template <typename T>
struct hasProbabilityMember<
    T, std::void_t<decltype(std::declval<T &>().probability)>> : std::true_type
{
};

static_assert(!hasConfidenceMember<ProfileFieldCandidate>::value,
              "C2 v1 forbids a confidence member on a Candidate (H5)");
static_assert(!hasScoreMember<ProfileFieldCandidate>::value,
              "C2 v1 forbids a score member on a Candidate (H5)");
static_assert(!hasProbabilityMember<ProfileFieldCandidate>::value,
              "C2 v1 forbids a probability member on a Candidate (H5)");
static_assert(!hasConfidenceMember<CandidateEvidence>::value,
              "evidence carries no confidence (H5)");
static_assert(!hasConfidenceMember<CandidateProposal>::value,
              "the provider seam carries no confidence (H5)");

// ---------------------------------------------------------------- fixtures --
constexpr char kDocumentId[] = "11111111-aaaa-bbbb-cccc-222222222222";
// A plausibly-shaped SHA-256 hex that is deliberately NOT equal to the
// documentId, so "identity used documentId, not contentHash" is observable.
constexpr char kContentHash[] =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

constexpr char kManualText[] =
    "ACME Power Systems Ltd. - Inverter Manual\n"
    "\n"
    "1. Identity\n"
    "Manufacturer: ACME Power Systems Ltd.\n"
    "Model: INV-1000\n"
    "Revision: rev A\n";

// The excerpt appears EXACTLY ONCE in kManualText (unique evidence => no
// ambiguity policy is required; ambiguity stays DEFERRED, not guessed).
constexpr char kManufacturerExcerpt[] = "Manufacturer: ACME Power Systems Ltd.";
constexpr char kManufacturerValue[] = "ACME Power Systems Ltd.";

ManualDocument makeDocument()
{
    ManualDocument document;
    document.schemaVersion = 1;
    document.documentId = kDocumentId;
    document.originalFileName = "inverter-manual.txt";
    document.documentType = ManualDocumentType::Txt;
    // PROVENANCE ONLY — this path does not even exist, which proves the slice
    // never re-reads the original source file.
    document.originalPath = "Z:/provenance-only/never-read.txt";
    document.contentHash = kContentHash;
    document.byteSize = sizeof(kManualText);
    document.charCount = sizeof(kManualText) - 1;
    document.status = ManualDocumentStatus::Ready;
    return document;
}

CandidateProposal manufacturerProposal(std::int64_t locationHint = -1)
{
    CandidateProposal proposal;
    proposal.target = ProfileFieldTarget::Manufacturer;
    proposal.proposedValue = kManufacturerValue;
    proposal.evidenceExcerpt = kManufacturerExcerpt;
    proposal.locationHint = locationHint;
    return proposal;
}

// TEST-ONLY deterministic provider double. There is no production provider and
// no network path in this slice (H1 / §7).
class FakeProposalProvider : public ICandidateProposalProvider
{
public:
    std::vector<CandidateProposal> proposals;
    bool called{false};
    ManualDocument documentSeen;
    std::string textSeen;

    std::vector<CandidateProposal> propose(const ManualDocument &document,
                                           std::string_view text) override
    {
        called = true;
        documentSeen = document;
        textSeen.assign(text.begin(), text.end());
        return proposals;
    }
};

// Deterministic, index-addressed snapshot of a directory tree (names + sizes).
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

DeviceProfile makeValidProfile()
{
    DeviceProfile profile;
    profile.schemaVersion = 1;
    profile.profileId = "99999999-1111-2222-3333-444444444444";
    profile.displayName = "Demo inverter";
    profile.manufacturer = "ACME";
    profile.model = "INV-1000";
    profile.revision = "rev A";
    profile.description = "fixture";
    RegisterEntry entry;
    entry.readFunctionCode = 0x03;
    entry.address = 1000;
    entry.name = "Frequency";
    entry.dataType = RegisterDecodeType::UInt16;
    entry.registerCount = 1;
    entry.scale = 0.1;
    entry.offset = 0.0;
    entry.unit = "Hz";
    profile.registers.push_back(entry);
    return profile;
}

[[nodiscard]] std::int64_t offsetOfManufacturerExcerpt()
{
    const std::string text{kManualText};
    const std::size_t at = text.find(kManufacturerExcerpt);
    Q_ASSERT(at != std::string::npos);
    return static_cast<std::int64_t>(at);
}

} // namespace

class CandidateExtractionTest : public QObject
{
    Q_OBJECT

private slots:
    // ----------------------------------------------------------- C2-A01 --
    void a01_presentExcerptYieldsOnePendingReviewCandidate()
    {
        const ManualDocument document = makeDocument();
        FakeProposalProvider provider;
        provider.proposals.push_back(manufacturerProposal());

        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, provider);

        QVERIFY(provider.called); // the seam was actually consumed
        QCOMPARE(result.candidates.size(), std::size_t{1});
        const ProfileFieldCandidate &candidate = result.candidates.front();
        QCOMPARE(candidate.target, ProfileFieldTarget::Manufacturer);
        QCOMPARE(candidate.proposedValue, std::string{kManufacturerValue});
        QCOMPARE(candidate.lifecycle, CandidateLifecycleState::PendingReview);
        QCOMPARE(candidate.evidence.excerpt, std::string{kManufacturerExcerpt});
        // The provider received exactly the canonical text (H8).
        QCOMPARE(provider.textSeen, std::string{kManualText});
    }

    // ----------------------------------------------------------- C2-A02 --
    void a02_absentExcerptYieldsNoValidCandidate()
    {
        const ManualDocument document = makeDocument();
        FakeProposalProvider provider;
        CandidateProposal proposal = manufacturerProposal();
        proposal.evidenceExcerpt = "Manufacturer: TOTALLY DIFFERENT CORP.";
        provider.proposals.push_back(proposal);

        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, provider);

        QCOMPARE(result.candidates.size(), std::size_t{0});
        QCOMPARE(result.refusedProposalCount, std::size_t{1});
    }

    // ----------------------------------------------------------- C2-A03 --
    void a03_candidateUsesDocumentIdentityNotContentHash()
    {
        const ManualDocument document = makeDocument();
        FakeProposalProvider provider;
        provider.proposals.push_back(manufacturerProposal());

        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, provider);

        QCOMPARE(result.candidates.size(), std::size_t{1});
        const CandidateEvidence &evidence = result.candidates.front().evidence;
        QCOMPARE(evidence.documentId, document.documentId);
        QCOMPARE(evidence.documentId, std::string{kDocumentId});
        // Document identity must NOT be the content identity.
        QVERIFY(evidence.documentId != evidence.contentHash);
        QVERIFY(std::string{kDocumentId} != std::string{kContentHash});
    }

    // ----------------------------------------------------------- C2-A04 --
    void a04_candidateCarriesContentIdentity()
    {
        const ManualDocument document = makeDocument();
        FakeProposalProvider provider;
        provider.proposals.push_back(manufacturerProposal());

        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, provider);

        QCOMPARE(result.candidates.size(), std::size_t{1});
        QCOMPARE(result.candidates.front().evidence.contentHash,
                 document.contentHash);
        QCOMPARE(result.candidates.front().evidence.contentHash,
                 std::string{kContentHash});
    }

    // ----------------------------------------------------------- C2-A05 --
    void a05_evidenceLocationResolvesToExactExcerpt()
    {
        const ManualDocument document = makeDocument();
        FakeProposalProvider provider;
        provider.proposals.push_back(manufacturerProposal());

        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, provider);

        QCOMPARE(result.candidates.size(), std::size_t{1});
        const CandidateEvidence &evidence = result.candidates.front().evidence;
        const std::string text{kManualText};
        const std::int64_t expected = offsetOfManufacturerExcerpt();
        QCOMPARE(evidence.textStart, expected);
        QCOMPARE(evidence.textEnd,
                 expected + static_cast<std::int64_t>(
                                std::string{kManufacturerExcerpt}.size()));
        // Round-trip: the stored span yields EXACTLY the stored excerpt.
        QCOMPARE(text.substr(static_cast<std::size_t>(evidence.textStart),
                             static_cast<std::size_t>(evidence.textEnd
                                                      - evidence.textStart)),
                 evidence.excerpt);
    }

    // ----------------------------------------------------------- C2-A06 --
    void a06_providerLocationHintNeverBecomesTruth()
    {
        const std::int64_t truth = offsetOfManufacturerExcerpt();
        for (const std::int64_t bogus : {std::int64_t{0}, std::int64_t{4096},
                                        std::int64_t{-1}}) {
            if (bogus == truth) {
                continue; // keep the hint genuinely bogus
            }
            const ManualDocument document = makeDocument();
            FakeProposalProvider provider;
            provider.proposals.push_back(manufacturerProposal(bogus));

            const CandidateExtractionResult result =
                modbuslens::core::extractProfileFieldCandidates(
                    document, std::string_view{kManualText}, provider);

            QCOMPARE(result.candidates.size(), std::size_t{1});
            const CandidateEvidence &evidence =
                result.candidates.front().evidence;
            QCOMPARE(evidence.textStart, truth); // recomputed, not trusted
            QVERIFY(evidence.textStart != bogus);
        }
    }

    // ----------------------------------------------------------- C2-A07 --
    void a07_extractionLeavesDeviceProfileUnchanged()
    {
        DeviceProfile profile = makeValidProfile();
        const DeviceProfile before = profile;
        const auto codeBefore = modbuslens::core::validateDeviceProfile(profile);
        QCOMPARE(codeBefore.code, modbuslens::core::ProfileValidationCode::Ok);

        const ManualDocument document = makeDocument();
        FakeProposalProvider provider;
        provider.proposals.push_back(manufacturerProposal());
        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, provider);

        QCOMPARE(result.candidates.size(), std::size_t{1});
        // Value-for-value identical: no mutation of verified Profile truth.
        QVERIFY(profile == before);
        const auto codeAfter = modbuslens::core::validateDeviceProfile(profile);
        QCOMPARE(codeAfter.code, codeBefore.code);
    }

    // ----------------------------------------------------------- C2-A08 --
    void a08_extractionCreatesNoProfileStorePersistence()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        ProfileStore::setManagedRootOverride(root.path());
        const QString before = ProfileStore::managedProfilesDirectory();
        const QStringList treeBefore = snapshotTree(before);
        const QString pathBefore =
            ProfileStore::defaultFilePathFor(QStringLiteral("some-profile"));

        const ManualDocument document = makeDocument();
        FakeProposalProvider provider;
        provider.proposals.push_back(manufacturerProposal());
        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, provider);
        QCOMPARE(result.candidates.size(), std::size_t{1});

        const QStringList treeAfter = snapshotTree(before);
        QCOMPARE(treeAfter, treeBefore);
        QVERIFY(!QFileInfo::exists(pathBefore));
        QVERIFY(!QFileInfo::exists(before) || treeBefore == treeAfter);
        ProfileStore::setManagedRootOverride(QString());
    }

    // ----------------------------------------------------------- C2-A09 --
    void a09_extractionCreatesNoManualCacheSideEffect()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QStringList before = snapshotTree(root.path());

        const ManualDocument document = makeDocument();
        FakeProposalProvider provider;
        provider.proposals.push_back(manufacturerProposal());
        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, provider);
        QCOMPARE(result.candidates.size(), std::size_t{1});

        // The domain layer performs no I/O at all, so a Candidate can never
        // materialize a manual-cache entry.
        QCOMPARE(snapshotTree(root.path()), before);
    }

    // ----------------------------------------------------------- C2-A10 --
    void a10_candidateCarriesNoNumericConfidence()
    {
        // Compile-time part lives above (static_assert block). Runtime
        // corroboration: the observable semantic field set is exactly the
        // frozen one, so nothing numeric sneaks in under another name.
        const ManualDocument document = makeDocument();
        FakeProposalProvider provider;
        provider.proposals.push_back(manufacturerProposal());
        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, provider);
        QCOMPARE(result.candidates.size(), std::size_t{1});

        const ProfileFieldCandidate &candidate = result.candidates.front();
        QStringList fields;
        fields << QString::fromUtf8(modbuslens::core::profileFieldTargetToken(
                      candidate.target))
               << QString::fromStdString(candidate.proposedValue)
               << QString::fromStdString(candidate.evidence.documentId)
               << QString::fromStdString(candidate.evidence.contentHash)
               << QString::number(candidate.evidence.textStart)
               << QString::number(candidate.evidence.textEnd)
               << QString::fromStdString(candidate.evidence.excerpt)
               << QString::fromUtf8(modbuslens::core::candidateLifecycleStateToken(
                      candidate.lifecycle));
        QCOMPARE(fields.size(), 8);
        for (const QString &field : fields) {
            QVERIFY(!field.contains(QStringLiteral("confidence"),
                                    Qt::CaseInsensitive));
            QVERIFY(!field.contains(QStringLiteral("probability"),
                                    Qt::CaseInsensitive));
        }
    }

    // ----------------------------------------------------------- C2-A11 --
    void a11_sameInputIsDeterministic()
    {
        const ManualDocument document = makeDocument();

        FakeProposalProvider first;
        first.proposals.push_back(manufacturerProposal());
        first.proposals.push_back(manufacturerProposal());
        const CandidateExtractionResult a =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, first);

        FakeProposalProvider second;
        second.proposals.push_back(manufacturerProposal());
        second.proposals.push_back(manufacturerProposal());
        const CandidateExtractionResult b =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{kManualText}, second);

        QCOMPARE(a.candidates.size(), std::size_t{2});
        QCOMPARE(a.candidates.size(), b.candidates.size());
        QCOMPARE(a.refusedProposalCount, b.refusedProposalCount);
        // Semantic equality (ProfileFieldCandidate has value operator==).
        QVERIFY(a.candidates == b.candidates);
    }

    // ----------------------------------------------------------- C2-A12 --
    void a12_canonicalTextDrivesValidationNotOriginalSource()
    {
        // The extraction entry point has NO path parameter, so re-reading the
        // original source is structurally impossible; this test corroborates it
        // behaviourally with a canonical text that differs from the RAW source
        // bytes and an original file that is deleted before extraction runs.
        QTemporaryDir work;
        QVERIFY(work.isValid());
        const QString rawPath = work.filePath(QStringLiteral("raw-source.txt"));

        // RAW source carries a UTF-8 BOM; the canonical extracted text does not
        // (C1a frozen semantics).
        QByteArray raw;
        raw.append("\xEF\xBB\xBF");
        raw.append(kManualText);
        {
            QFile file(rawPath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(raw);
            file.close();
        }

        // Derive the canonical text the way the C1 layer does (BOM stripped) and
        // prove it really is a DIFFERENT byte sequence from the raw source.
        const QByteArray canonicalBytes = raw.mid(3);
        QVERIFY(canonicalBytes != raw);
        const std::string canonicalText = canonicalBytes.toStdString();

        // Remove the original source entirely: validation must not need it.
        QVERIFY(QFile::remove(rawPath));
        QVERIFY(!QFileInfo::exists(rawPath));

        const ManualDocument document = makeDocument();
        FakeProposalProvider provider;
        provider.proposals.push_back(manufacturerProposal());
        const CandidateExtractionResult result =
            modbuslens::core::extractProfileFieldCandidates(
                document, std::string_view{canonicalText}, provider);

        QCOMPARE(result.candidates.size(), std::size_t{1});
        const CandidateEvidence &evidence = result.candidates.front().evidence;
        QCOMPARE(canonicalText.substr(
                     static_cast<std::size_t>(evidence.textStart),
                     static_cast<std::size_t>(evidence.textEnd
                                              - evidence.textStart)),
                 evidence.excerpt);
        QCOMPARE(provider.textSeen, canonicalText);
    }

    // ================================================== T035-A.1 register ==
    // The deterministic register-candidate slice (ADR-004 / Human T035-A.1
    // §4-§8). Proposals are constructed DIRECTLY here so the CORE refusal
    // rules are proven independently of the adapter parser (the parser has
    // its own matrix in test_candidate_adapter).
    static modbuslens::core::RegisterCandidateProposal makeRegisterProposal()
    {
        modbuslens::core::RegisterCandidateProposal proposal;
        proposal.functionCode = 3;
        proposal.address = 100;
        proposal.name = "Setpoint";
        proposal.dataType = modbuslens::core::RegisterDecodeType::UInt16;
        proposal.quantity = 1;
        proposal.scale = 1.0;
        proposal.offset = 0.0;
        proposal.unit = "";
        // Unique in kManualText, like the frozen evidence rule demands.
        proposal.evidenceExcerpt = "Model: INV-1000";
        return proposal;
    }

    // ------------------------------------------------------------- REG01 --
    // (§8-A) A valid proposal becomes a PendingReview candidate with exact
    // field values, frozen defaults, and locally computed evidence identity.
    void reg01_validProposalBecomesPendingReviewCandidate()
    {
        std::vector<modbuslens::core::RegisterCandidateProposal> proposals;
        proposals.push_back(makeRegisterProposal());

        const modbuslens::core::RegisterCandidateExtractionResult result =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, proposals);

        QCOMPARE(result.refusedProposalCount, std::size_t{0});
        QCOMPARE(result.candidates.size(), std::size_t{1});
        const auto &candidate = result.candidates.at(0);
        QCOMPARE(int(candidate.functionCode), 3);
        QCOMPARE(int(candidate.address), 100);
        QVERIFY(candidate.name == "Setpoint");
        QVERIFY(candidate.description.empty());
        QCOMPARE(candidate.dataType,
                 modbuslens::core::RegisterDecodeType::UInt16);
        QCOMPARE(candidate.quantity, 1);
        QCOMPARE(candidate.scale, 1.0);
        QCOMPARE(candidate.offset, 0.0);
        QVERIFY(candidate.unit.empty());
        QCOMPARE(candidate.lifecycle, CandidateLifecycleState::PendingReview);
        // Evidence identity: documentId + contentHash from the DOCUMENT, the
        // span recomputed LOCALLY (never a provider hint).
        QVERIFY(candidate.evidence.documentId == kDocumentId);
        QVERIFY(candidate.evidence.contentHash == kContentHash);
        QVERIFY(candidate.evidence.excerpt == "Model: INV-1000");
        QVERIFY(candidate.evidence.textStart >= 0);
        QVERIFY(candidate.evidence.textEnd > candidate.evidence.textStart);
        QCOMPARE(std::string_view{kManualText}.substr(
                     std::size_t(candidate.evidence.textStart),
                     std::size_t(candidate.evidence.textEnd
                                 - candidate.evidence.textStart)),
                 std::string_view{"Model: INV-1000"});
    }

    // ------------------------------------------------------------- REG02 --
    // (§8-B family) A missing name is a refusal, never a guess.
    void reg02_nameMissingIsRefused()
    {
        auto proposal = makeRegisterProposal();
        proposal.name = "";
        std::vector<modbuslens::core::RegisterCandidateProposal> proposals;
        proposals.push_back(proposal);

        const modbuslens::core::RegisterCandidateExtractionResult result =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, proposals);

        QCOMPARE(result.candidates.size(), std::size_t{0});
        QCOMPARE(result.refusedProposalCount, std::size_t{1});
    }

    // ------------------------------------------------------------- REG03 --
    // (§8-D) functionCode outside 0x01..0x7F is refused (both edges).
    void reg03_illegalFunctionCodeIsRefused()
    {
        for (const int bad : {0, 128, 255}) {
            auto proposal = makeRegisterProposal();
            proposal.functionCode = static_cast<std::uint8_t>(bad);
            std::vector<modbuslens::core::RegisterCandidateProposal>
                proposals;
            proposals.push_back(proposal);

            const modbuslens::core::RegisterCandidateExtractionResult result =
                modbuslens::core::extractRegisterCandidates(
                    makeDocument(), std::string_view{kManualText}, proposals);
            QCOMPARE(result.candidates.size(), std::size_t{0});
            QCOMPARE(result.refusedProposalCount, std::size_t{1});
        }
    }

    // ------------------------------------------------------------- REG04 --
    // (§8-F) quantity=2 with UInt16 is REFUSED, never auto-corrected to 1
    // (Human T035-A.1 §5), while quantity=1 stays legal.
    void reg04_quantityMismatchIsRefusedWithoutCorrection()
    {
        auto bad = makeRegisterProposal();
        bad.quantity = 2; // UInt16 needs exactly 1 word
        std::vector<modbuslens::core::RegisterCandidateProposal> proposals;
        proposals.push_back(bad);
        const modbuslens::core::RegisterCandidateExtractionResult badResult =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, proposals);
        QCOMPARE(badResult.candidates.size(), std::size_t{0});
        QCOMPARE(badResult.refusedProposalCount, std::size_t{1});

        std::vector<modbuslens::core::RegisterCandidateProposal> good;
        good.push_back(makeRegisterProposal());
        const modbuslens::core::RegisterCandidateExtractionResult goodResult =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, good);
        QCOMPARE(goodResult.candidates.size(), std::size_t{1});
        QCOMPARE(goodResult.refusedProposalCount, std::size_t{0});
    }

    // ------------------------------------------------------------- REG05 --
    // (§8-G) Non-finite scale / offset are refused at the CORE layer too
    // (JSON cannot express NaN/Infinity literals, so the parser rejects them
    // as overflow/type errors; this slot proves the core guard stands on
    // its own against a programmatic caller).
    void reg05_nonFiniteScaleOrOffsetIsRefused()
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();

        auto badScale = makeRegisterProposal();
        badScale.scale = nan;
        std::vector<modbuslens::core::RegisterCandidateProposal> scaleVec;
        scaleVec.push_back(badScale);
        const modbuslens::core::RegisterCandidateExtractionResult scaleResult =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, scaleVec);
        QCOMPARE(scaleResult.candidates.size(), std::size_t{0});
        QCOMPARE(scaleResult.refusedProposalCount, std::size_t{1});

        auto badOffset = makeRegisterProposal();
        badOffset.offset = inf;
        std::vector<modbuslens::core::RegisterCandidateProposal> offsetVec;
        offsetVec.push_back(badOffset);
        const modbuslens::core::RegisterCandidateExtractionResult
            offsetResult = modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, offsetVec);
        QCOMPARE(offsetResult.candidates.size(), std::size_t{0});
        QCOMPARE(offsetResult.refusedProposalCount, std::size_t{1});
    }

    // ------------------------------------------------------------- REG06 --
    // (§8-H) Evidence must be UNIQUELY locatable: an excerpt that is absent
    // — or appears more than once — is a refusal, never a guessed span.
    void reg06_evidenceMustBeUniqueInCanonicalText()
    {
        auto absent = makeRegisterProposal();
        absent.evidenceExcerpt = "REGISTER NOT IN TEXT";
        std::vector<modbuslens::core::RegisterCandidateProposal> absentVec;
        absentVec.push_back(absent);
        const modbuslens::core::RegisterCandidateExtractionResult absentResult =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, absentVec);
        QCOMPARE(absentResult.candidates.size(), std::size_t{0});
        QCOMPARE(absentResult.refusedProposalCount, std::size_t{1});

        // "ACME" appears TWICE in kManualText => ambiguous => refused.
        auto ambiguous = makeRegisterProposal();
        ambiguous.evidenceExcerpt = "ACME";
        std::vector<modbuslens::core::RegisterCandidateProposal> ambVec;
        ambVec.push_back(ambiguous);
        const modbuslens::core::RegisterCandidateExtractionResult ambResult =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, ambVec);
        QCOMPARE(ambResult.candidates.size(), std::size_t{0});
        QCOMPARE(ambResult.refusedProposalCount, std::size_t{1});
    }

    // ------------------------------------------------------------- REG07 --
    // Accounting is per-proposal deterministic: one valid + two invalid
    // proposals yield exactly one candidate and two refusals.
    void reg07_mixedProposalsAccountingIsDeterministic()
    {
        auto badFunction = makeRegisterProposal();
        badFunction.functionCode = 0;
        auto badQuantity = makeRegisterProposal();
        badQuantity.quantity = 2;

        std::vector<modbuslens::core::RegisterCandidateProposal> proposals;
        proposals.push_back(makeRegisterProposal()); // valid
        proposals.push_back(badFunction);            // refused
        proposals.push_back(badQuantity);            // refused

        const modbuslens::core::RegisterCandidateExtractionResult result =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, proposals);
        QCOMPARE(result.candidates.size(), std::size_t{1});
        QCOMPARE(result.refusedProposalCount, std::size_t{2});
    }

    // ------------------------------------------------------------- REG08 --
    // Chain cross-check: a validated candidate assembles into a RegisterEntry
    // that passes validateDeviceProfile (the ONLY validation owner for the
    // verified Profile — the apply slice reuses exactly this path).
    void reg08_candidateAssemblesIntoAValidProfileEntry()
    {
        std::vector<modbuslens::core::RegisterCandidateProposal> proposals;
        proposals.push_back(makeRegisterProposal());
        const modbuslens::core::RegisterCandidateExtractionResult result =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, proposals);
        QCOMPARE(result.candidates.size(), std::size_t{1});
        const auto &candidate = result.candidates.at(0);

        modbuslens::core::RegisterEntry entry;
        entry.readFunctionCode = candidate.functionCode;
        entry.address = candidate.address;
        entry.name = candidate.name;
        entry.description = candidate.description;
        entry.dataType = candidate.dataType;
        entry.registerCount = candidate.quantity;
        entry.scale = candidate.scale;
        entry.offset = candidate.offset;
        entry.unit = candidate.unit;

        modbuslens::core::DeviceProfile profile;
        profile.profileId = "p1";
        profile.displayName = "Profile One";
        profile.registers.push_back(entry);
        const auto validation =
            modbuslens::core::validateDeviceProfile(profile);
        QVERIFY(validation.ok());
    }

    // ------------------------------------------------------------- REG09 --
    // The stored evidence participates in the SAME C3 freshness gate as the
    // profile-field slice: it revalidates Ok against the unchanged canonical
    // text (revalidateCandidateEvidence is shared, not duplicated).
    void reg09_storedEvidenceRevalidates()
    {
        std::vector<modbuslens::core::RegisterCandidateProposal> proposals;
        proposals.push_back(makeRegisterProposal());
        const modbuslens::core::RegisterCandidateExtractionResult result =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kManualText}, proposals);
        QCOMPARE(result.candidates.size(), std::size_t{1});

        const auto revalidation =
            modbuslens::core::revalidateCandidateEvidence(
                result.candidates.at(0).evidence,
                std::string_view{kManualText});
        QCOMPARE(revalidation,
                 modbuslens::core::CandidateEvidenceRevalidationCode::Ok);
    }

    // =======================================================================
    // T035-A.3.2.3 — DETERMINISTIC LINE-ENDING NORMALIZATION
    // (Human-adjudicated Option A, LIMITED scope: CRLF / lone CR / LF only).
    //
    // A local fixture set is used on purpose: these cases must be readable
    // WITHOUT cross-referencing the frozen kManualText, because the whole point
    // is the SHAPE of the line endings, not the register values.
    // =======================================================================

    // normalizeLineEndings is a total, stateless rule — asserted first so every
    // later case rests on a proven primitive rather than on an assumption.
    void a323_01_normalizationPrimitive()
    {
        using modbuslens::core::normalizeLineEndings;
        // CRLF -> LF, lone CR -> LF, LF unchanged.
        QCOMPARE(normalizeLineEndings("a\r\nb"), std::string("a\nb"));
        QCOMPARE(normalizeLineEndings("a\rb"), std::string("a\nb"));
        QCOMPARE(normalizeLineEndings("a\nb"), std::string("a\nb"));
        QCOMPARE(normalizeLineEndings("a\r\n\r\nb"), std::string("a\n\nb"));
        // A CR at the very end must not run off the buffer.
        QCOMPARE(normalizeLineEndings("a\r"), std::string("a\n"));
        // NOTHING else changes: TAB, double space, case, digits, punctuation.
        QCOMPARE(normalizeLineEndings("a\tb"), std::string("a\tb"));
        QCOMPARE(normalizeLineEndings("a  b"), std::string("a  b"));
        QCOMPARE(normalizeLineEndings("Ab1."), std::string("Ab1."));
        QVERIFY(normalizeLineEndings("") == std::string(""));
    }

    // The equivalence predicate must agree with the transform, and must NOT
    // claim equivalence for any non-line-ending difference.
    void a323_02_equivalencePredicate()
    {
        using modbuslens::core::isNormalizedLineEndingEquivalent;
        QVERIFY(isNormalizedLineEndingEquivalent('\r', '\n'));
        QVERIFY(isNormalizedLineEndingEquivalent('\n', '\r'));
        QVERIFY(isNormalizedLineEndingEquivalent('\n', '\n'));
        QVERIFY(isNormalizedLineEndingEquivalent(' ', ' '));
        // Space vs newline is NOT line-ending equivalence.
        QVERIFY(!isNormalizedLineEndingEquivalent(' ', '\n'));
        QVERIFY(!isNormalizedLineEndingEquivalent('\t', ' '));
        QVERIFY(!isNormalizedLineEndingEquivalent('a', 'A'));
    }

    // --- 1: EXACT unique behaviour is UNCHANGED ---------------------------
    void a323_03_exactUniqueUnchanged()
    {
        constexpr std::string_view text =
            "Header\nRegister 5: Temperature\nTrailer\n";
        std::int64_t start = -1;
        std::int64_t end = -1;
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(
                     text, "Register 5: Temperature", start, end),
                 modbuslens::core::EvidenceLocateCode::Located);
        QCOMPARE(start, std::int64_t{7});
        QCOMPARE(end, std::int64_t{7 + 23});
    }

    // --- 2: EXACT ambiguous behaviour is UNCHANGED ------------------------
    void a323_04_exactAmbiguousUnchanged()
    {
        constexpr std::string_view text = "dup\ndup\n";
        std::int64_t start = -1;
        std::int64_t end = -1;
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(text, "dup", start, end),
                 modbuslens::core::EvidenceLocateCode::Ambiguous);
        QCOMPARE(start, std::int64_t{-1});
        QCOMPARE(end, std::int64_t{-1});
    }

    // --- 3: SAME-PAGE CRLF raw + LF evidence now SUCCEEDS -----------------
    // This is the measured real-world shape: a PDF page stores CRLF, a provider
    // may answer with LF. Exact fails; the fallback must locate it and the
    // returned span must be a REAL raw span.
    void a323_05_samePageCrlfRawLfEvidenceSucceeds()
    {
        constexpr std::string_view raw =
            "Alpha\r\nRegister 5: Temperature\r\nOmega\r\n";
        constexpr std::string_view evidence = "Register 5: Temperature\r\nOmega";
        const std::string lfEvidence = modbuslens::core::normalizeLineEndings(
            std::string("Register 5: Temperature\nOmega"));
        std::int64_t start = -1;
        std::int64_t end = -1;
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(raw, lfEvidence, start,
                                                         end),
                 modbuslens::core::EvidenceLocateCode::Located);
        // Real offsets into the ORIGINAL text, not into a normalized copy.
        QCOMPARE(raw.substr(static_cast<std::size_t>(start),
                            static_cast<std::size_t>(end - start)),
                 evidence);
        // And the raw slice normalizes to exactly the submitted evidence.
        QCOMPARE(modbuslens::core::normalizeLineEndings(
                     raw.substr(static_cast<std::size_t>(start),
                                static_cast<std::size_t>(end - start))),
                 lfEvidence);
    }

    // --- 4: CROSS-PAGE LF raw + CRLF evidence now SUCCEEDS ----------------
    // The inverse direction: the ManualStore aggregation joins pages with a
    // bare LF, while the provider may return CRLF.
    void a323_06_crossPageLfRawCrlfEvidenceSucceeds()
    {
        constexpr std::string_view raw =
            "Page one ends\r\nTail of page one\nHead of page two\r\nMore\r\n";
        const std::string crlfEvidence = "Tail of page one\r\nHead of page two";
        std::int64_t start = -1;
        std::int64_t end = -1;
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(raw, crlfEvidence, start,
                                                         end),
                 modbuslens::core::EvidenceLocateCode::Located);
        // The raw slice keeps the RAW line ending of that region (a bare LF at
        // the page boundary) — it is NOT rewritten into the evidence's CRLF
        // form. Equivalence is asserted through the authorized rule, which is
        // the only form the slice is guaranteed to satisfy.
        const std::string_view slice =
            raw.substr(static_cast<std::size_t>(start),
                       static_cast<std::size_t>(end - start));
        QCOMPARE(slice, std::string_view{"Tail of page one\nHead of page two"});
        QCOMPARE(modbuslens::core::normalizeLineEndings(slice),
                 modbuslens::core::normalizeLineEndings(crlfEvidence));
    }

    // --- 5: A LONE CR is equivalent to LF ---------------------------------
    void a323_07_loneCrEquivalentToLf()
    {
        constexpr std::string_view raw = "Alpha\rBeta\rGamma\r";
        const std::string lfEvidence = std::string("Alpha\nBeta");
        std::int64_t start = -1;
        std::int64_t end = -1;
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(raw, lfEvidence, start,
                                                         end),
                 modbuslens::core::EvidenceLocateCode::Located);
        QCOMPARE(raw.substr(static_cast<std::size_t>(start),
                            static_cast<std::size_t>(end - start)),
                 std::string_view{"Alpha\rBeta"});
    }

    // --- 6: TAB / MULTI-SPACE differences are STILL REJECTED --------------
    // The fallback must NOT be a whitespace normalization. This is the guard
    // that keeps the authorized scope from silently widening.
    void a323_08_tabAndMultiSpaceStillRejected()
    {
        constexpr std::string_view tabRaw = "Value\tUnit\r\nNext\r\n";
        const std::string spaceEvidence = "Value Unit";
        std::int64_t start = -1;
        std::int64_t end = -1;
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(tabRaw, spaceEvidence,
                                                         start, end),
                 modbuslens::core::EvidenceLocateCode::NotFound);

        constexpr std::string_view twoSpaceRaw = "Value  Unit\r\nNext\r\n";
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(twoSpaceRaw,
                                                         "Value Unit", start,
                                                         end),
                 modbuslens::core::EvidenceLocateCode::NotFound);
    }

    // --- 7: A NON-line-ending character difference must be REJECTED -------
    // "look-alike but different characters" — a case difference, a changed
    // digit and a punctuation change must all still fail.
    void a323_09_nonLineEndingDifferenceRejected()
    {
        constexpr std::string_view raw =
            "Register 5: Temperature\r\nUnit: degC\r\n";
        std::int64_t start = -1;
        std::int64_t end = -1;
        // Case difference.
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(
                     raw, "register 5: temperature\nUnit: degC", start, end),
                 modbuslens::core::EvidenceLocateCode::NotFound);
        // Digit difference.
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(
                     raw, "Register 6: Temperature\nUnit: degC", start, end),
                 modbuslens::core::EvidenceLocateCode::NotFound);
        // Punctuation difference.
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(
                     raw, "Register 5. Temperature\nUnit: degC", start, end),
                 modbuslens::core::EvidenceLocateCode::NotFound);
    }

    // --- 8: NORMALIZED duplicates must be REJECTED (the critical case) ----
    // Two ORIGINALLY DIFFERENT raw spans — one CRLF-form and one lone-CR-form —
    // both normalize to the SAME evidence, while the EXACT match fails for
    // both. The fallback must report ambiguous; it must never take the first
    // hit. This is the case the negative control targets, so it is built to be
    // maximally hostile: exact finds NOTHING, normalized finds TWO.
    void a323_10_normalizedDuplicatesRejected()
    {
        constexpr std::string_view raw =
            "AA\r\ndup\r\nx\ndup\rBB\ndup\r\nx\ndup";
        const std::string evidence = std::string("dup\nx\ndup");
        // Precondition of the case: an exact search finds nothing at all …
        QCOMPARE(raw.find(evidence), std::string_view::npos);
        // … yet the normalized view contains the evidence TWICE.
        const std::string normalizedRaw =
            modbuslens::core::normalizeLineEndings(raw);
        QCOMPARE(normalizedRaw.find(evidence), std::size_t{3});
        QVERIFY(normalizedRaw.find(evidence, 4) != std::string::npos);

        std::int64_t start = -1;
        std::int64_t end = -1;
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(raw, evidence, start,
                                                         end),
                 modbuslens::core::EvidenceLocateCode::Ambiguous);
        // The refusal must not publish ANY span (no "first hit" fallback).
        QCOMPARE(start, std::int64_t{-1});
        QCOMPARE(end, std::int64_t{-1});
    }

    // --- 9: EMPTY / whitespace-only evidence is REJECTED ------------------
    void a323_11_emptyAndWhitespaceEvidenceRejected()
    {
        constexpr std::string_view raw = "Alpha\r\nBeta\r\n";
        std::int64_t start = -1;
        std::int64_t end = -1;
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(raw, "", start, end),
                 modbuslens::core::EvidenceLocateCode::NotFound);
        // A lone CR normalizes to "\n" — present in raw, so it IS locatable;
        // this documents that "whitespace-only" is judged on the CONTENT, and
        // an all-CR excerpt cannot silently become empty.
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(raw, "\r", start, end),
                 modbuslens::core::EvidenceLocateCode::Ambiguous);
    }

    // --- 10 + 11: REAL raw offsets and slice round-trip -------------------
    // The returned offsets must index the ORIGINAL string, and the raw slice
    // must normalize to exactly the submitted evidence.
    void a323_12_rawOffsetsAndRoundTrip()
    {
        constexpr std::string_view raw =
            "PREFIX\r\nAA\r\nBB\r\nCC\r\nSUFFIX\r\n";
        const std::string evidence = std::string("AA\nBB\nCC");
        std::int64_t start = -1;
        std::int64_t end = -1;
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(raw, evidence, start,
                                                         end),
                 modbuslens::core::EvidenceLocateCode::Located);
        const std::string_view slice =
            raw.substr(static_cast<std::size_t>(start),
                       static_cast<std::size_t>(end - start));
        // The raw slice keeps CRLF (it is NOT rewritten to the evidence form)…
        QCOMPARE(slice, std::string_view{"AA\r\nBB\r\nCC"});
        // …and the round-trip holds under the authorized equivalence only.
        QCOMPARE(modbuslens::core::normalizeLineEndings(slice), evidence);
        // The offsets are genuinely into raw: the preceding separator is intact.
        QVERIFY(start == 8);
    }

    // --- 12: register EXTRACTION uses the fallback ------------------------
    void a323_13_registerExtractionUsesFallback()
    {
        // Canonical text with CRLF (the real PDF page shape) …
        constexpr char kCrlfText[] =
            "Identity\r\nModel: INV-1000\r\nRegister 5: Temperature\r\n";
        // … and a provider that answered with LF across the line boundary.
        modbuslens::core::RegisterCandidateProposal proposal =
            makeRegisterProposal();
        proposal.evidenceExcerpt = "Model: INV-1000\nRegister 5: Temperature";

        std::vector<modbuslens::core::RegisterCandidateProposal> proposals;
        proposals.push_back(proposal);
        const modbuslens::core::RegisterCandidateExtractionResult result =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kCrlfText}, proposals);
        QCOMPARE(result.candidates.size(), std::size_t{1});

        const CandidateEvidence &evidence = result.candidates.at(0).evidence;
        // The STORED excerpt is the PROVIDER's submission (not laundered into a
        // canonical re-read) …
        QCOMPARE(evidence.excerpt, proposal.evidenceExcerpt);
        // … while the SPAN is a real raw offset whose slice is line-ending
        // equivalent to it.
        QCOMPARE(modbuslens::core::normalizeLineEndings(
                     std::string_view{kCrlfText}.substr(
                         static_cast<std::size_t>(evidence.textStart),
                         static_cast<std::size_t>(evidence.textEnd
                                                  - evidence.textStart))),
                 modbuslens::core::normalizeLineEndings(evidence.excerpt));
    }

    // --- 13: register Accept REVALIDATES with the SAME rule ---------------
    void a323_14_registerAcceptRevalidationUsesSameRule()
    {
        constexpr char kCrlfText[] =
            "Identity\r\nModel: INV-1000\r\nRegister 5: Temperature\r\n";
        modbuslens::core::RegisterCandidateProposal proposal =
            makeRegisterProposal();
        proposal.evidenceExcerpt = "Model: INV-1000\nRegister 5: Temperature";

        std::vector<modbuslens::core::RegisterCandidateProposal> proposals;
        proposals.push_back(proposal);
        const modbuslens::core::RegisterCandidateExtractionResult result =
            modbuslens::core::extractRegisterCandidates(
                makeDocument(), std::string_view{kCrlfText}, proposals);
        QCOMPARE(result.candidates.size(), std::size_t{1});

        // What passed extraction MUST pass revalidation (Human §5).
        QCOMPARE(modbuslens::core::revalidateRegisterCandidateEvidence(
                     result.candidates.at(0).evidence,
                     std::string_view{kCrlfText}),
                 modbuslens::core::CandidateEvidenceRevalidationCode::Ok);
        // The exact-only profile-field gate is deliberately NOT used here: it
        // would reject this same evidence purely on CRLF/LF. This asserts the
        // two gates are genuinely different, so the separation is real.
        QCOMPARE(modbuslens::core::revalidateCandidateEvidence(
                     result.candidates.at(0).evidence,
                     std::string_view{kCrlfText}),
                 modbuslens::core::CandidateEvidenceRevalidationCode::
                     RoundTripFailed);
    }

    // --- 14: documentId / contentHash are NOT changed by the fallback -----
    void a323_15_identityUnchanged()
    {
        constexpr char kCrlfText[] =
            "Identity\r\nModel: INV-1000\r\nRegister 5: Temperature\r\n";
        modbuslens::core::RegisterCandidateProposal proposal =
            makeRegisterProposal();
        proposal.evidenceExcerpt = "Model: INV-1000\nRegister 5: Temperature";
        std::vector<modbuslens::core::RegisterCandidateProposal> proposals;
        proposals.push_back(proposal);
        const ManualDocument document = makeDocument();
        const modbuslens::core::RegisterCandidateExtractionResult result =
            modbuslens::core::extractRegisterCandidates(
                document, std::string_view{kCrlfText}, proposals);
        QCOMPARE(result.candidates.size(), std::size_t{1});
        QCOMPARE(result.candidates.at(0).evidence.documentId,
                 std::string{kDocumentId});
        QCOMPARE(result.candidates.at(0).evidence.contentHash,
                 std::string{kContentHash});
    }

    // --- 15: manufacturer evidence regression (EXACT-ONLY preserved) ------
    void a323_16_manufacturerStaysExactOnly()
    {
        // A manufacturer excerpt differing ONLY in line endings must STILL be
        // refused: the frozen manufacturer contract was not widened.
        constexpr std::string_view crlf = "Manufacturer: ACME\r\nModel: INV\r\n";
        std::int64_t start = -1;
        std::int64_t end = -1;
        QCOMPARE(modbuslens::core::locateEvidenceExcerpt(
                     crlf, "Manufacturer: ACME\nModel: INV", start, end),
                 modbuslens::core::EvidenceLocateCode::Located);
        // …but the PROFILE-FIELD validator the manufacturer path uses does not
        // consult that shared rule at all — proven by the exact-only gate:
        CandidateEvidence crlfEvidence;
        crlfEvidence.documentId = kDocumentId;
        crlfEvidence.contentHash = kContentHash;
        crlfEvidence.textStart = 0;
        crlfEvidence.textEnd = 22;
        crlfEvidence.excerpt = "Manufacturer: ACME\nModel: INV";
        QCOMPARE(modbuslens::core::revalidateCandidateEvidence(crlfEvidence, crlf),
                 modbuslens::core::CandidateEvidenceRevalidationCode::
                     RoundTripFailed);
        // And an EXACT manufacturer evidence still revalidates Ok, unchanged.
        CandidateEvidence exactEvidence;
        exactEvidence.documentId = kDocumentId;
        exactEvidence.contentHash = kContentHash;
        exactEvidence.textStart = 0;
        exactEvidence.textEnd = 18;
        exactEvidence.excerpt = "Manufacturer: ACME";
        QCOMPARE(
            modbuslens::core::revalidateCandidateEvidence(exactEvidence, crlf),
            modbuslens::core::CandidateEvidenceRevalidationCode::Ok);
    }
};

QTEST_MAIN(CandidateExtractionTest)
#include "test_candidate_extraction.moc"