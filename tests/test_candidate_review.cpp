// M12-C C3 FIRST BEHAVIOR SLICE tests (T027 §81, C3-H1..H10): the Human
// authority boundary between PendingReview Candidates and the verified
// Device Profile.
//
// Determinism is structural: Accept/Reject require ZERO provider/network
// activity (the runner below only counts that nothing is dispatched), the
// managed roots are injected temporary directories, and every review decision
// is a pure local computation against REAL managed manual artifacts.
//
// Every C3 review entry point is exercised through the Qt meta-object (by
// name), matching the QML call boundary exactly — including the automation
// seed, which reuses the REAL deterministic C2 evidence validator rather than
// introducing a fake-AI mode.
//
// The matrix is C3-R2-01..20 from the session contract. Cases whose only
// reachable path is structural (r2_19/r2_20) carry their evidence inline.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <string>

#include "core/profile/DeviceProfile.h"
#include "ui/candidate/CandidateExtractionController.h"
#include "ui/candidate/CandidateExtractionRunner.h"
#include "ui/manual/ManualStore.h"
#include "ui/profile/ProfileController.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::ui::CandidateExtractionController;
using modbuslens::ui::ICandidateExtractionRunner;
using modbuslens::ui::ManualStore;
using modbuslens::ui::ProfileController;
using modbuslens::ui::ProfileStore;

namespace {

constexpr char kManualText[] =
    "ACME Power Systems Ltd. - Inverter Manual\n"
    "\n"
    "1. Identity\n"
    "Manufacturer: ACME Power Systems Ltd.\n"
    "Model: INV-1000\n";
constexpr char kExcerpt[] = "Manufacturer: ACME Power Systems Ltd.";
constexpr char kProposed[] = "ACME Power Systems Ltd.";
constexpr char kOldManufacturer[] = "Old Co.";

// Counts dispatches ONLY to prove review actions never reach a provider
// (C3-R2-17). It refuses every begin() so no extraction can ever start.
class CountingRefusingRunner : public ICandidateExtractionRunner
{
public:
    bool begin(const modbuslens::core::ExtractionRequest & /*request*/,
               std::uint64_t /*generation*/,
               const CompletionHandler & /*onDone*/) override
    {
        ++calls_;
        return false;
    }

    [[nodiscard]] int beginCount() const override { return calls_; }
    [[nodiscard]] int callCount() const { return calls_; }

private:
    int calls_{0};
};

[[nodiscard]] bool hasMethod(const QObject *object, const char *signature)
{
    return object != nullptr
        && object->metaObject()->indexOfMethod(signature) >= 0;
}

// Invokable-shaped helpers: the review API is reached exactly the way QML
// reaches it, so the tests cannot drift from the production call boundary.

[[nodiscard]] bool invokeSeed(QObject *controller, const QVariantMap &document,
                              const QString &canonicalText,
                              const QVariantList &proposals)
{
    return QMetaObject::invokeMethod(
        controller, "seedReviewCandidatesForAutomation", Q_ARG(QVariantMap, document),
        Q_ARG(QString, canonicalText), Q_ARG(QVariantList, proposals));
}

[[nodiscard]] bool invokeAccept(QObject *controller, const QVariantMap &candidate)
{
    bool ok = false;
    QMetaObject::invokeMethod(controller, "acceptCandidate", Q_RETURN_ARG(bool, ok),
                              Q_ARG(QVariantMap, candidate));
    return ok;
}

[[nodiscard]] bool invokeReject(QObject *controller, const QVariantMap &candidate)
{
    bool ok = false;
    QMetaObject::invokeMethod(controller, "rejectCandidate", Q_RETURN_ARG(bool, ok),
                              Q_ARG(QVariantMap, candidate));
    return ok;
}

[[nodiscard]] bool invokeApply(ProfileController *controller,
                               const QString &field, const QString &value)
{
    bool ok = false;
    QMetaObject::invokeMethod(controller, "applyCandidateField",
                              Q_RETURN_ARG(bool, ok), Q_ARG(QString, field),
                              Q_ARG(QString, value));
    return ok;
}

[[nodiscard]] QVariantList pendingCandidates(QObject *controller)
{
    return controller->property("candidates").toList();
}

[[nodiscard]] QVariantMap pendingCandidateAt(QObject *controller, int index)
{
    const QVariantList pending = pendingCandidates(controller);
    if (index < 0 || index >= pending.size()) {
        return {};
    }
    return pending.at(index).toMap();
}

[[nodiscard]] QStringList draftSnapshot(ProfileController *controller)
{
    return {controller->displayName(), controller->manufacturer(),
            controller->model(),       controller->revision(),
            controller->description(), QString::number(controller->registerMap().size()),
            controller->validationText()};
}

// A REAL manual through the real managed-store transaction: the evidence
// round-trip must run against genuine managed artifacts.
struct ReviewHarness
{
    explicit ReviewHarness(const QString &rootDir)
    {
        const QString sourcePath =
            QDir(rootDir).filePath(QStringLiteral("manual-src.txt"));
        QFile source(sourcePath);
        if (source.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            source.write(kManualText);
            source.close();
        }
        const auto imported =
            ManualStore::importSourceFile(sourcePath, QStringLiteral("review-manual.txt"));
        Q_ASSERT(imported.ok());
        document = imported.document;
        canonicalText = ManualStore::loadText(
            QString::fromStdString(document.contentHash));
        Q_ASSERT(!canonicalText.isEmpty());

        documentMap.insert(QStringLiteral("documentId"),
                           QString::fromStdString(document.documentId));
        documentMap.insert(QStringLiteral("contentHash"),
                           QString::fromStdString(document.contentHash));
        documentMap.insert(QStringLiteral("originalFileName"),
                           QString::fromStdString(document.originalFileName));
        documentMap.insert(QStringLiteral("originalPath"),
                           QString::fromStdString(document.originalPath));
    }

    modbuslens::core::ManualDocument document;
    QString canonicalText;
    QVariantMap documentMap;
};

[[nodiscard]] QVariantMap manufacturerProposal()
{
    QVariantMap proposal;
    proposal.insert(QStringLiteral("target"), QStringLiteral("manufacturer"));
    proposal.insert(QStringLiteral("proposedValue"), QString::fromUtf8(kProposed));
    proposal.insert(QStringLiteral("evidenceExcerpt"), QString::fromUtf8(kExcerpt));
    return proposal;
}

} // namespace

class CandidateReviewTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Deterministic, credential-free, injected roots for every test.
        qunsetenv("MODELSCOPE_API_KEY");
        qunsetenv("MODBUSLENS_MODELSCOPE_MODEL");
        const QString testDir = QDir(tempRoot_.path()).filePath(
            QTest::currentTestFunction() ? QTest::currentTestFunction()
                                         : QStringLiteral("case"));
        QDir(tempRoot_.path()).mkpath(testDir);
        ManualStore::setManagedRootOverride(testDir);
        ProfileStore::setManagedRootOverride(testDir);
    }

    void cleanup()
    {
        ManualStore::setManagedRootOverride(QString());
        ProfileStore::setManagedRootOverride(QString());
    }

    // ----------------------------------------------------- RED anchor ----
    // The frozen C3 review API must exist on the production objects before
    // any behavioral assertion can mean anything.
    void r00_reviewApiExists()
    {
        CandidateExtractionController controller;
        ProfileController profileController;

        QVERIFY(hasMethod(&controller, "acceptCandidate(QVariantMap)"));
        QVERIFY(hasMethod(&controller, "rejectCandidate(QVariantMap)"));
        QVERIFY(hasMethod(&controller,
                          "seedReviewCandidatesForAutomation(QVariantMap,QString,QVariantList)"));
        QVERIFY(controller.metaObject()->indexOfProperty("lastReviewError") >= 0);
        QVERIFY(controller.metaObject()->indexOfProperty("lastReviewErrorToken") >= 0);
        QVERIFY(controller.metaObject()->indexOfProperty("profileController") >= 0);
        QVERIFY(hasMethod(&profileController, "applyCandidateField(QString,QString)"));
    }

    // ----------------------------------------------------- C3-R2-01 ------
    void r2_01_pendingCandidateAloneCausesZeroDraftMutation()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        const QStringList before = draftSnapshot(&profileController);
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        const QStringList after = draftSnapshot(&profileController);
        QCOMPARE(after, before);
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-02 ------
    void r2_02_rejectConsumesWithoutAnyProfileMutation()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(!candidate.isEmpty());

        const QStringList before = draftSnapshot(&profileController);
        QVERIFY(invokeReject(&controller, candidate));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
        QCOMPARE(draftSnapshot(&profileController), before);
        QCOMPARE(controller.property("lastReviewErrorToken").toString(), QString());

        // Consumed: the same Candidate cannot be reviewed again (C3-H6).
        QVERIFY(!invokeReject(&controller, candidate));
        QVERIFY(!invokeAccept(&controller, candidate));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
        QCOMPARE(runner.callCount(), 0);
    }

    // ------------------------------------------------ C3-R2-03/04 --------
    void r2_03_acceptAppliesOnlyManufacturerToDraft()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        QVERIFY((profileController.setProperty("model", QStringLiteral("Old Model"))));
        QVERIFY((profileController.setProperty("description", QStringLiteral("Old description"))));

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);

        QVERIFY(invokeAccept(&controller, candidate));

        // The proposed value reached the draft Manufacturer, and ONLY it.
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kProposed));
        QCOMPARE(profileController.model(), QStringLiteral("Old Model"));
        QCOMPARE(profileController.description(), QStringLiteral("Old description"));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
        QCOMPARE(controller.property("lastReviewErrorToken").toString(), QString());
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-05 ------
    void r2_05_registerMapUnchanged()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        QVERIFY(profileController.addRegisterEntry(registerEntryFields()));

        const QVariantList registersBefore = profileController.registerMap();
        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(invokeAccept(&controller, pendingCandidateAt(&controller, 0)));

        const QVariantList registersAfter = profileController.registerMap();
        QCOMPARE(registersAfter.size(), registersBefore.size());
        QCOMPARE(registersAfter, registersBefore);
        QCOMPARE(runner.callCount(), 0);
    }

    // ------------------------------------------------ C3-R2-06/07 --------
    void r2_06_acceptDoesNotAutoSave_then_r2_07_explicitSavePersists()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        QVERIFY(profileController.saveCurrent());

        const QString profilePath = ProfileStore::defaultFilePathFor(
            profileController.currentProfileId());
        const QByteArray persistedBefore = readBytes(profilePath);
        QVERIFY(!persistedBefore.isEmpty());

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(invokeAccept(&controller, pendingCandidateAt(&controller, 0)));

        // C3-R2-06: Accept must NOT auto-save — the managed JSON is
        // byte-identical right after a successful Accept.
        const QByteArray persistedAfterAccept = readBytes(profilePath);
        QCOMPARE(persistedAfterAccept, persistedBefore);
        QCOMPARE(profileController.dirty(), true);

        // C3-R2-07: the EXPLICIT Save goes through the existing
        // ProfileController/ProfileStore path and persists the new value.
        QVERIFY(profileController.saveCurrent());
        const auto loaded = ProfileStore::loadFromFile(profilePath);
        QVERIFY(loaded.ok());
        QCOMPARE(QString::fromStdString(loaded.profile.manufacturer),
                 QString::fromUtf8(kProposed));
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-08 ------
    void r2_08_contentHashMismatchFailsAtomically()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));

        // The managed document record now claims a DIFFERENT content identity:
        // the Candidate's evidence no longer matches the canonical truth.
        QVERIFY(rewriteDocumentContentHash(
            QString::fromStdString(harness.document.documentId),
            QString(64, QChar(u'0'))));
        const QStringList before = draftSnapshot(&profileController);
        QVERIFY(!invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QVERIFY(!controller.property("lastReviewErrorToken").toString().isEmpty());
        QCOMPARE(controller.property("candidateCount").toInt(), 1); // stays pending
        QCOMPARE(draftSnapshot(&profileController), before);
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-09 ------
    void r2_09_excerptRoundTripFailureFailsAtomically()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));

        // The managed text cache changed under the Candidate: the recorded
        // excerpt/location can no longer round-trip.
        QVERIFY(writeTextCache(
            QString::fromStdString(harness.document.contentHash),
                               QStringLiteral("CORRUPTED CACHE CONTENT\n")));
        const QStringList before = draftSnapshot(&profileController);
        QVERIFY(!invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(controller.property("lastReviewErrorToken").toString(),
                 QStringLiteral("evidence_invalid"));
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        QCOMPARE(draftSnapshot(&profileController), before);
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-10 ------
    void r2_10_missingDocumentAndMissingSourceFailAtomically()
    {
        // (a) the document record disappears under a pending Candidate.
        {
            CountingRefusingRunner runner;
            CandidateExtractionController controller(runner);
            ProfileController profileController;
            wire(&controller, &profileController);
            openProfileWithOldManufacturer(&profileController);

            const ReviewHarness harness(tempRoot_.path());
            QVERIFY(invokeSeed(&controller, harness.documentMap,
                               harness.canonicalText,
                               QVariantList{manufacturerProposal()}));
            QVERIFY(QFile::remove(
                ManualStore::documentsDirectory() + QStringLiteral("/")
                + QString::fromStdString(harness.document.documentId)
                + QStringLiteral(".json")));
            const QStringList before = draftSnapshot(&profileController);
            QVERIFY(!invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
            QCOMPARE(controller.property("lastReviewErrorToken").toString(),
                     QStringLiteral("evidence_document_missing"));
            QCOMPARE(controller.property("candidateCount").toInt(), 1);
            QCOMPARE(draftSnapshot(&profileController), before);
            QCOMPARE(runner.callCount(), 0);
        }
        // (b) the record exists but the canonical text cache is unresolvable.
        {
            CountingRefusingRunner runner;
            CandidateExtractionController controller(runner);
            ProfileController profileController;
            wire(&controller, &profileController);
            openProfileWithOldManufacturer(&profileController);

            const ReviewHarness harness(tempRoot_.path());
            QVERIFY(invokeSeed(&controller, harness.documentMap,
                               harness.canonicalText,
                               QVariantList{manufacturerProposal()}));
            QVERIFY(QFile::remove(
                ManualStore::textDirectory() + QStringLiteral("/")
                + QString::fromStdString(harness.document.contentHash)
                + QStringLiteral(".txt")));
            QVERIFY(!invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
            QCOMPARE(controller.property("lastReviewErrorToken").toString(),
                     QStringLiteral("evidence_source_missing"));
            QCOMPARE(controller.property("candidateCount").toInt(), 1);
            QCOMPARE(runner.callCount(), 0);
        }
    }

    // ----------------------------------------------------- C3-R2-11 ------
    void r2_11_noProfileTargetFails()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(!invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(controller.property("lastReviewErrorToken").toString(),
                 QStringLiteral("profile_target_missing"));
        QCOMPARE(controller.property("candidateCount").toInt(), 1);

        // Wired, but nothing is open: still no valid current target.
        ProfileController profileController;
        QVERIFY((controller.setProperty("profileController", QVariant::fromValue(&profileController))));
        QVERIFY(!invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(controller.property("lastReviewErrorToken").toString(),
                 QStringLiteral("profile_target_missing"));
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-12 ------
    void r2_12_acceptOverwritesCurrentDraftValue()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        // The draft value changes AFTER the Candidate was generated: Accept is
        // the Human's explicit "overwrite the currently displayed value".
        QVERIFY((profileController.setProperty("manufacturer", QStringLiteral("Other Co."))));
        QCOMPARE(profileController.manufacturer(), QStringLiteral("Other Co."));

        QVERIFY(invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kProposed));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-13 ------
    void r2_13_unsupportedTargetRejectedExplicitly()
    {
        ProfileController profileController;
        openProfileWithOldManufacturer(&profileController);

        // The whitelist lives at the controlled write API: anything beyond the
        // first-slice target is an explicit, mutation-free refusal.
        QVERIFY(!invokeApply(&profileController, QStringLiteral("display_name"),
                             QStringLiteral("Whatever")));
        QCOMPARE(profileController.lastActionErrorToken(),
                 QStringLiteral("unsupported_candidate_field"));
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kOldManufacturer));
        QVERIFY(!invokeApply(&profileController, QStringLiteral("not_a_field"),
                             QStringLiteral("Whatever")));
        QCOMPARE(profileController.lastActionErrorToken(),
                 QStringLiteral("unsupported_candidate_field"));
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kOldManufacturer));

        // The supported target applies.
        QVERIFY(invokeApply(&profileController, QStringLiteral("manufacturer"),
                            QString::fromUtf8(kProposed)));
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kProposed));

        // And a non-manufacturer Candidate can never even enter the pending
        // review set: the deterministic validator refuses it at generation.
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        const ReviewHarness harness(tempRoot_.path());
        QVariantMap otherProposal = manufacturerProposal();
        otherProposal.insert(QStringLiteral("target"), QStringLiteral("display_name"));
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{otherProposal}));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
    }

    // ----------------------------------------------------- C3-R2-14 ------
    void r2_14_acceptCannotBeAppliedTwice()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(invokeAccept(&controller, candidate));
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kProposed));

        // A repeated/stale action must not act on any Candidate again.
        QVERIFY(!invokeAccept(&controller, candidate));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kProposed));
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-15 ------
    void r2_15_rejectedCandidateCannotMutateProfile()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(invokeReject(&controller, candidate));
        const QStringList before = draftSnapshot(&profileController);

        // A rejected Candidate can never reach the draft through the review
        // path — it is consumed, so every review action on it is refused.
        QVERIFY(!invokeAccept(&controller, candidate));
        QVERIFY(!invokeReject(&controller, candidate));
        QCOMPARE(draftSnapshot(&profileController), before);
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-16 ------
    void r2_16_rejectDoesNotBlockFreshExtraction()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(invokeReject(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);

        // A fresh extraction (replay-seeded here through the same accepted
        // validator) produces a NEW pending Candidate.
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        QVERIFY(invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kProposed));
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-17 ------
    void r2_17_acceptRejectCauseZeroRunnerCalls()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(invokeReject(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(runner.callCount(), 0);
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-18 ------
    void r2_18_profileJsonStaysCandidateFree()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QVERIFY(profileController.saveCurrent());

        const QString profilePath = ProfileStore::defaultFilePathFor(
            profileController.currentProfileId());
        const QJsonObject object =
            QJsonDocument::fromJson(readBytes(profilePath)).object();
        const QStringList allowed{QStringLiteral("schemaVersion"),
                                  QStringLiteral("profileId"),
                                  QStringLiteral("displayName"),
                                  QStringLiteral("manufacturer"),
                                  QStringLiteral("model"),
                                  QStringLiteral("revision"),
                                  QStringLiteral("description"),
                                  QStringLiteral("registers")};
        for (const QString &key : object.keys()) {
            QVERIFY2(allowed.contains(key),
                     qPrintable(QStringLiteral("unexpected profile key: ") + key));
        }
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-19 ------
    // Structural: the C3 path never touches the M10/M11/raw decode machinery —
    // it calls validateDeviceProfile and writes ONE identity draft field. The
    // behavioral evidence here = the register map survives Accept + Save
    // intact and the loaded profile still validates; the protected-diff audit
    // of the behavior commit completes it.
    void r2_19_registerDecodeTruthUntouched()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        QVERIFY(profileController.addRegisterEntry(registerEntryFields()));

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QVERIFY(profileController.saveCurrent());

        const auto loaded = ProfileStore::loadFromFile(ProfileStore::defaultFilePathFor(
            profileController.currentProfileId()));
        QVERIFY(loaded.ok());
        QCOMPARE(loaded.profile.registers.size(), std::size_t{1});
        QCOMPARE(loaded.profile.registers.front().readFunctionCode, std::uint8_t{3});
        QCOMPARE(loaded.profile.registers.front().address, std::uint16_t{100});
        const auto validation =
            modbuslens::core::validateDeviceProfile(loaded.profile);
        QVERIFY(validation.ok());
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- C3-R2-20 ------
    // Structural: the review surface has no Q&A/question surface at all
    // (C3/M12-D hard boundary). The QML side asserts the same in its gate.
    void r2_20_m12dSurfaceAbsent()
    {
        CandidateExtractionController controller;
        const QMetaObject *meta = controller.metaObject();
        for (int i = 0; i < meta->methodCount(); ++i) {
            const QString name = QString::fromLatin1(meta->method(i).name());
            QVERIFY2(!name.contains(QStringLiteral("question"), Qt::CaseInsensitive)
                         && !name.contains(QStringLiteral("ask"), Qt::CaseInsensitive)
                         && !name.contains(QStringLiteral("answer"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("unexpected Q&A method: ") + name));
        }
    }

    // ------------------------------------------------- C3-R2A GAP A ------
    // The full-profile validation inside the controlled Accept path must be
    // LOAD-BEARING: with a draft that violates an ACTUAL frozen rule
    // (display_name_missing), a perfectly valid Candidate with perfectly
    // valid evidence must still be refused atomically (C3-H2/H10, T027 §82
    // acceptance-evidence addendum). This fails if the full-profile
    // validation call is bypassed.
    void r2a_01_invalidDraftAcceptFailsAtomically()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        QVERIFY(profileController.saveCurrent());
        const QString profilePath = ProfileStore::defaultFilePathFor(
            profileController.currentProfileId());
        const QByteArray persistedBefore = readBytes(profilePath);

        // Make the CURRENT DRAFT invalid through the accepted editing seam.
        QVERIFY(profileController.setProperty("displayName", QString()));
        // The controller projection reports the draft as invalid (human text,
        // non-empty), and the AUTHORITATIVE core validator names the frozen
        // rule on the equivalent logical state (persisted profile with the
        // cleared displayName) - validateDeviceProfile, not a test invention.
        QVERIFY(!profileController.validationText().isEmpty());
        {
            auto equivalent =
                ProfileStore::loadFromFile(
                    ProfileStore::defaultFilePathFor(
                        profileController.currentProfileId()))
                    .profile;
            equivalent.displayName.clear();
            QCOMPARE(modbuslens::core::validateDeviceProfile(equivalent).code,
                     modbuslens::core::ProfileValidationCode::DisplayNameMissing);
        }

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        const QStringList invalidBefore = draftSnapshot(&profileController);

        // Accept: valid Candidate, valid evidence — but the FULL staged
        // profile validation must refuse.
        QVERIFY(!invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(controller.property("lastReviewErrorToken").toString(),
                 QStringLiteral("candidate_apply_failed"));
        QCOMPARE(profileController.lastActionErrorToken(),
                 QStringLiteral("display_name_missing"));
        QCOMPARE(controller.property("candidateCount").toInt(), 1); // stays pending
        QCOMPARE(draftSnapshot(&profileController), invalidBefore);
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kOldManufacturer));
        QCOMPARE(readBytes(profilePath), persistedBefore); // no Save
        QCOMPARE(runner.callCount(), 0);
    }

    // ------------------------------------------------- C3-R2A GAP B ------
    // Accept -> Discard: restoration goes through the EXISTING Discard
    // workflow, and a consumed Candidate never re-enters PendingReview merely
    // because the Profile draft was discarded (C3-H5/H9).
    void r2a_02_acceptThenDiscardRestoresBaseline()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        QVERIFY(profileController.saveCurrent());

        const QString profilePath = ProfileStore::defaultFilePathFor(
            profileController.currentProfileId());
        const QByteArray persistedBefore = readBytes(profilePath);
        const QStringList persistedSnapshot = draftSnapshot(&profileController);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(invokeAccept(&controller, candidate));
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kProposed));
        QCOMPARE(profileController.dirty(), true);
        QCOMPARE(readBytes(profilePath), persistedBefore); // no auto-save

        // The EXISTING authoritative Discard workflow.
        QMetaObject::invokeMethod(&profileController, "discardCurrentChanges",
                                  Qt::DirectConnection);
        QCOMPARE(draftSnapshot(&profileController), persistedSnapshot);
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kOldManufacturer));
        QCOMPARE(profileController.dirty(), false);
        QCOMPARE(readBytes(profilePath), persistedBefore);

        // The Candidate stays consumed; it does not come back with the draft.
        QVERIFY(!invokeAccept(&controller, candidate));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
        QCOMPARE(runner.callCount(), 0);
    }

private:
    static void wire(CandidateExtractionController *controller,
                     ProfileController *profileController)
    {
        QVERIFY((controller->setProperty("profileController", QVariant::fromValue(profileController))));
    }

    static void openProfileWithOldManufacturer(ProfileController *controller)
    {
        controller->newProfile();
        QVERIFY((controller->setProperty("displayName", QStringLiteral("Review Profile"))));
        QVERIFY((controller->setProperty("manufacturer", QString::fromUtf8(kOldManufacturer))));
    }

    [[nodiscard]] static QVariantMap registerEntryFields()
    {
        QVariantMap fields;
        fields.insert(QStringLiteral("readFunctionCode"), QStringLiteral("3"));
        fields.insert(QStringLiteral("address"), QStringLiteral("100"));
        fields.insert(QStringLiteral("name"), QStringLiteral("Power"));
        fields.insert(QStringLiteral("description"), QString());
        fields.insert(QStringLiteral("dataType"), QStringLiteral("0"));
        fields.insert(QStringLiteral("byteOrder"), QStringLiteral("0"));
        fields.insert(QStringLiteral("wordOrder"), QStringLiteral("-1"));
        fields.insert(QStringLiteral("scale"), QString());
        fields.insert(QStringLiteral("offset"), QString());
        fields.insert(QStringLiteral("unit"), QString());
        return fields;
    }

    [[nodiscard]] static QByteArray readBytes(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll();
    }

    [[nodiscard]] static bool writeTextCache(const QString &contentHash,
                                             const QString &content)
    {
        QFile file(ManualStore::textDirectory() + QStringLiteral("/") + contentHash
                   + QStringLiteral(".txt"));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        return file.write(content.toUtf8()) != -1;
    }

    [[nodiscard]] static bool rewriteDocumentContentHash(const QString &documentId,
                                                         const QString &newHash)
    {
        const QString path = ManualStore::documentsDirectory() + QStringLiteral("/")
                             + documentId + QStringLiteral(".json");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        if (object.isEmpty()) {
            return false;
        }
        object.insert(QStringLiteral("contentHash"), newHash);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        return file.write(QJsonDocument(object).toJson()) != -1;
    }

    QTemporaryDir tempRoot_;
};

QTEST_MAIN(CandidateReviewTest)

#include "test_candidate_review.moc"
