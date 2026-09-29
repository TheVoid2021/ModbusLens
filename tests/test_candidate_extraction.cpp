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
};

QTEST_MAIN(CandidateExtractionTest)
#include "test_candidate_extraction.moc"