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
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <string>

#include "core/profile/DeviceProfile.h"
#include "ui/candidate/CandidateExtractionController.h"
#include "ui/candidate/CandidateExtractionRunner.h"
#include "ui/manual/ManualImportController.h"
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

// Starts attempts that NEVER complete: the deterministic RUNNING state for
// the delete-guard tests (ML2-18/19) and any future Running-state coverage.
class HangingCountingRunner : public ICandidateExtractionRunner
{
public:
    bool begin(const modbuslens::core::ExtractionRequest & /*request*/,
               std::uint64_t /*generation*/,
               const CompletionHandler & /*onDone*/) override
    {
        ++calls_;
        return true;
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

[[nodiscard]] bool deleteOk(const QVariantMap &result)
{
    return !result.isEmpty() && result.value("ok").toBool();
}

[[nodiscard]] QString resultToken(const QVariantMap &result)
{
    return result.value("token").toString();
}

[[nodiscard]] QVariantMap invokeDelete(QObject *controller,
                                       const QVariantMap &document)
{
    QVariantMap result;
    QMetaObject::invokeMethod(controller, "deleteManualDocument",
                              Q_RETURN_ARG(QVariantMap, result),
                              Q_ARG(QVariantMap, document));
    return result;
}

[[nodiscard]] QVariantMap invokeCheckAllowed(QObject *controller,
                                             const QVariantMap &document)
{
    QVariantMap result;
    QMetaObject::invokeMethod(controller, "checkManualDeleteAllowed",
                              Q_RETURN_ARG(QVariantMap, result),
                              Q_ARG(QVariantMap, document));
    return result;
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

// T035-A.3.1: the register review-model seed, reached exactly the way the
// automation boundary reaches it (same invokable discipline as above).
[[nodiscard]] bool invokeSeedRegister(QObject *controller,
                                      const QVariantMap &document,
                                      const QString &canonicalText,
                                      const QVariantList &proposals)
{
    return QMetaObject::invokeMethod(
        controller, "seedRegisterReviewCandidatesForAutomation",
        Q_ARG(QVariantMap, document), Q_ARG(QString, canonicalText),
        Q_ARG(QVariantList, proposals));
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
        : ReviewHarness(rootDir, kManualText, QStringLiteral("review-manual.txt"))
    {
    }

    // T035-A.3.2.3: the same real managed-store transaction, but the imported
    // source bytes are supplied by the caller so a CRLF-shaped manual can be
    // exercised through the genuine import path (no hand-built canonical text).
    ReviewHarness(const QString &rootDir, const char *sourceText,
                  const QString &fileName)
    {
        const QString sourcePath =
            QDir(rootDir).filePath(QStringLiteral("manual-src.txt"));
        QFile source(sourcePath);
        if (source.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            source.write(sourceText);
            source.close();
        }
        const auto imported =
            ManualStore::importSourceFile(sourcePath, fileName);
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

// T035-A.3.1: a legal register seed proposal. The excerpt is a UNIQUE
// single line of kManualText (the same conservative evidence rule as the
// profile-field slice — the register validator enforces it identically).
[[nodiscard]] QVariantMap registerProposal()
{
    QVariantMap proposal;
    proposal.insert(QStringLiteral("functionCode"), 4);
    proposal.insert(QStringLiteral("address"), 5);
    proposal.insert(QStringLiteral("name"), QStringLiteral("Temperature"));
    proposal.insert(QStringLiteral("dataType"), QStringLiteral("Float32"));
    proposal.insert(QStringLiteral("quantity"), 2);
    proposal.insert(QStringLiteral("scale"), 0.1);
    proposal.insert(QStringLiteral("offset"), 0);
    proposal.insert(QStringLiteral("unit"), QStringLiteral("degC"));
    proposal.insert(QStringLiteral("evidenceExcerpt"),
                    QStringLiteral("Model: INV-1000"));
    return proposal;
}

// T035-A.3.2.3: a review-active manual whose canonical text uses CRLF line
// endings INSIDE its lines and a bare LF at the page-style boundary — the
// measured real shape of a PDF import (see the task file §3B-D5-B). Register
// evidence submitted with plain LF must seed AND accept through the shared
// line-ending rule.
constexpr char kCrlfManualText[] =
    "ACME Power Systems Ltd. - Inverter Manual\r\n"
    "\r\n"
    "1. Identity\r\n"
    "Manufacturer: ACME Power Systems Ltd.\r\n"
    "Model: INV-1000\n"
    "Register 5: Temperature degC\r\n";

// The provider answered with LF where the canonical text holds CRLF: the exact
// match fails, the line-ending fallback must locate it uniquely.
[[nodiscard]] QVariantMap crlfRegisterProposal()
{
    QVariantMap proposal;
    proposal.insert(QStringLiteral("functionCode"), 4);
    proposal.insert(QStringLiteral("address"), 5);
    proposal.insert(QStringLiteral("name"), QStringLiteral("Temperature"));
    proposal.insert(QStringLiteral("dataType"), QStringLiteral("UInt16"));
    proposal.insert(QStringLiteral("quantity"), 1);
    proposal.insert(QStringLiteral("evidenceExcerpt"),
                    QStringLiteral("Model: INV-1000\n"
                                   "Register 5: Temperature degC"));
    return proposal;
}

[[nodiscard]] QVariantMap crlfManufacturerProposal()
{
    QVariantMap proposal;
    proposal.insert(QStringLiteral("target"), QStringLiteral("manufacturer"));
    proposal.insert(QStringLiteral("proposedValue"), QString::fromUtf8(kProposed));
    // LF where the canonical text holds CRLF → the EXACT-ONLY manufacturer
    // path must still REFUSE this (the frozen contract is not widened).
    proposal.insert(QStringLiteral("evidenceExcerpt"),
                    QStringLiteral("Manufacturer: ACME Power Systems Ltd.\n"
                                   "Model: INV-1000"));
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

    // ---- M12-C C3-R3B: EDIT (C3-H8 Human-authored override) ----
    // edited-confirm = ONE explicit Human authority action: the same
    // evidence freshness gate as Accept, the same full-profile validation,
    // success writes the HUMAN value to the draft only (no auto-save) and
    // consumes the Candidate. The edited value may differ from what the
    // Evidence literally states: authority = the Human, never the Evidence.

    [[nodiscard]] QVariantMap invokeConfirmEdit(
        QObject *controller, const QVariantMap &candidate,
        const QString &editedValue)
    {
        QVariantMap result;
        QMetaObject::invokeMethod(controller, "confirmEditedCandidate",
                                  Q_RETURN_ARG(QVariantMap, result),
                                  Q_ARG(QVariantMap, candidate),
                                  Q_ARG(QString, editedValue));
        return result;
    }

    [[nodiscard]] bool confirmOk(const QVariantMap &result)
    {
        return !result.isEmpty() && result.value("ok").toBool();
    }

    void edit_00_apisExist()
    {
        CandidateExtractionController controller;
        QVERIFY(hasMethod(&controller,
                          "confirmEditedCandidate(QVariantMap,QString)"));
    }

    void edit_01_confirmAppliesHumanValueToDraft()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);

        const QVariantMap result =
            invokeConfirmEdit(&controller, candidate,
                              QStringLiteral("Human-Verified-Co"));
        QVERIFY2(confirmOk(result), qPrintable(resultToken(result)));

        // EDIT-09: the HUMAN value reached the draft - not the AI proposal.
        QCOMPARE(profiles.manufacturer(), QStringLiteral("Human-Verified-Co"));
        // EDIT-10: consumed exactly once.
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
        // EDIT-11: draft only - a never-saved profile has no file yet.
        QVERIFY(!QFile::exists(ProfileStore::defaultFilePathFor(
            profiles.currentProfileId())));
        // EDIT-22: zero provider dispatches.
        QCOMPARE(runner.callCount(), 0);
    }

    void edit_02_confirmConsumesExactlyOnce()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(confirmOk(invokeConfirmEdit(&controller, candidate,
                                            QStringLiteral("Human-Verified-Co"))));

        // A stale/repeated confirm must be refused and must not re-apply.
        QVERIFY(!confirmOk(invokeConfirmEdit(&controller, candidate,
                                             QStringLiteral("Second Value"))));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
        QCOMPARE(profiles.manufacturer(), QStringLiteral("Human-Verified-Co"));
    }

    void edit_03_cancelLeavesEverythingUntouched()
    {
        // Unit equivalent of dialog-Cancel: no confirm call is made, so the
        // Candidate stays pending and the draft stays byte-identical
        // (EDIT-06; the dialog interaction itself is gate-covered).
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QStringList before = draftSnapshot(&profiles);

        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        QCOMPARE(draftSnapshot(&profiles), before);
        QCOMPARE(profiles.manufacturer(), QString::fromUtf8(kOldManufacturer));
    }

    void edit_04_editedValueMayDifferFromEvidence()
    {
        // EDIT-20: the Human value differs from what the Evidence literally
        // states; authority = the Human - freshness+validation are the only
        // gates, and none of them may refuse the edit for "not matching the
        // evidence text".
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);

        QVERIFY(confirmOk(invokeConfirmEdit(&controller, candidate,
                                            QStringLiteral("Human-Verified-Co"))));
        QCOMPARE(profiles.manufacturer(), QStringLiteral("Human-Verified-Co"));
    }

    void edit_05_evidenceFreshnessBlocksConfirm()
    {
        // EDIT-15: the managed text cache changed UNDER the pending
        // Candidate - the stored excerpt can no longer round-trip.
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(writeTextCache(
            QString::fromStdString(harness.document.contentHash),
                               QStringLiteral("CORRUPTED CACHE CONTENT\n")));
        const QStringList before = draftSnapshot(&profiles);

        const QVariantMap result =
            invokeConfirmEdit(&controller, candidate,
                              QStringLiteral("Human-Verified-Co"));
        QVERIFY(!confirmOk(result));
        QCOMPARE(resultToken(result),
                 QStringLiteral("edit_evidence_freshness_failed"));
        // Atomic: Candidate stays pending, draft untouched.
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        QCOMPARE(draftSnapshot(&profiles), before);
        QCOMPARE(runner.callCount(), 0);
    }

    void edit_06_documentMissingBlocksConfirm()
    {
        // EDIT-16: the referenced Manual record disappears before confirm.
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(QFile::remove(
            ManualStore::documentsDirectory() + QStringLiteral("/")
            + candidate.value("documentId").toString()
            + QStringLiteral(".json")));

        const QVariantMap result =
            invokeConfirmEdit(&controller, candidate,
                              QStringLiteral("Human-Verified-Co"));
        QVERIFY(!confirmOk(result));
        QCOMPARE(resultToken(result),
                 QStringLiteral("edit_evidence_freshness_failed"));
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        QCOMPARE(profiles.manufacturer(), QString::fromUtf8(kOldManufacturer));
    }

    void edit_07_contentHashMismatchBlocksConfirm()
    {
        // EDIT-17: the record's content identity no longer matches the
        // Candidate's bound evidence identity.
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(rewriteDocumentContentHash(
            candidate.value("documentId").toString(),
            QString(64, QChar(u'0'))));

        const QVariantMap result =
            invokeConfirmEdit(&controller, candidate,
                              QStringLiteral("Human-Verified-Co"));
        QVERIFY(!confirmOk(result));
        QCOMPARE(resultToken(result),
                 QStringLiteral("edit_evidence_freshness_failed"));
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        QCOMPARE(profiles.manufacturer(), QString::fromUtf8(kOldManufacturer));
    }

    // ------------------------------------------------ EDIT-19 strong case -
    // The whole-profile validation inside the controlled write must be
    // load-bearing for edited-confirm too: an unrelated invalid draft state
    // (missing displayName) makes the confirm fail atomically even though
    // the Candidate and its evidence are perfectly valid.
    void edit_08_invalidWholeProfileBlocksEditedConfirm()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);
        QVERIFY(profiles.saveCurrent());

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        // An EXISTING legal editor seam makes the draft invalid: the frozen
        // rule display_name_missing now fails on the draft.
        QVERIFY(profiles.setProperty("displayName", QString()));
        QCOMPARE(profiles.validationText(),
                 QStringLiteral("设备名称必填"));
        const QStringList before = draftSnapshot(&profiles);

        const QVariantMap result =
            invokeConfirmEdit(&controller, candidate,
                              QStringLiteral("Human-Verified-Co"));
        QVERIFY(!confirmOk(result));
        // The error token identifies the ACTUAL validation failure.
        QCOMPARE(resultToken(result),
                 QStringLiteral("display_name_missing"));
        // Atomic: Candidate stays pending, draft untouched.
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        QCOMPARE(draftSnapshot(&profiles), before);
        QCOMPARE(runner.callCount(), 0);
    }

    void edit_09_explicitSavePersistsEditedValue()
    {
        // EDIT-13: persistence stays MANUAL SAVE - after a successful
        // edited-confirm the explicit Save persists the HUMAN value.
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap confirmOne =
            invokeConfirmEdit(&controller, pendingCandidateAt(&controller, 0),
                              QStringLiteral("Human-Verified-Co"));
        QVERIFY(confirmOk(confirmOne));
        QVERIFY(profiles.dirty());
        QVERIFY(profiles.saveCurrent());

        const auto loaded = ProfileStore::loadFromFile(
            ProfileStore::defaultFilePathFor(profiles.currentProfileId()));
        QVERIFY(loaded.ok());
        QCOMPARE(QString::fromStdString(loaded.profile.manufacturer),
                 QStringLiteral("Human-Verified-Co"));
    }

    void edit_10_discardAfterConfirmRestoresBaseline()
    {
        // EDIT-14: the EXISTING authoritative Discard restores the persisted
        // baseline; the consumed Candidate does NOT resurrect.
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);
        QVERIFY(profiles.saveCurrent());

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap confirmTwo =
            invokeConfirmEdit(&controller, pendingCandidateAt(&controller, 0),
                              QStringLiteral("Human-Verified-Co"));
        QVERIFY(confirmOk(confirmTwo));
        QCOMPARE(profiles.manufacturer(), QStringLiteral("Human-Verified-Co"));

        profiles.discardCurrentChanges();
        QCOMPARE(profiles.manufacturer(), QString::fromUtf8(kOldManufacturer));
        QCOMPARE(profiles.dirty(), false);
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
    }

    void edit_11_acceptAndRejectUnchanged()
    {
        // EDIT-24/25: Accept/Reject semantics are untouched by Edit.
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profiles;
        wire(&controller, &profiles);
        openProfileWithOldManufacturer(&profiles);

        const ReviewHarness harness(tempRoot_.path());
        // EDIT-24: the plain Accept path still applies the AI proposal.
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(profiles.manufacturer(), QString::fromUtf8(kProposed));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);

        // EDIT-25: the plain Reject path still consumes without mutation.
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(invokeReject(&controller, pendingCandidateAt(&controller, 0)));
        QCOMPARE(controller.property("candidateCount").toInt(), 0);
        QCOMPARE(profiles.manufacturer(), QString::fromUtf8(kProposed));
    }

    void edit_12_pendingEditedCandidateStillBlocksManualDelete()
    {
        // EDIT-26 controller-equivalent: the ML-2 delete guard still blocks
        // while the Candidate (possibly edited in a dialog) is pending.
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        modbuslens::ui::ManualImportController manuals;
        QVERIFY(controller.setProperty("manualController",
                                        QVariant::fromValue(&manuals)));

        const ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        const QVariantMap blocked = invokeDelete(&controller, harness.documentMap);
        QVERIFY(!deleteOk(blocked));
        QCOMPARE(resultToken(blocked),
                 QStringLiteral("manual_delete_blocked_pending"));
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
    }

private:

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

// T035-A.3.1: a dedicated slots section — moc collects ONLY functions that
// live in a slots section, so the register review tests get their own one
// instead of relying on insertion position inside the helper section.
private slots:
    // ============================================ T035-A.3.1 register ==

    // -------------------------------------------------- A31-01 (§4-A/B) --
    // Register candidate session flow + variant conversion: a valid seed
    // proposal becomes ONE pending register candidate whose variant carries
    // kind + all v1 fields + locally computed evidence identity, and the
    // evidence span round-trips against the canonical text.
    void a31_01_registerSessionFlowAndVariantConversion()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ReviewHarness harness(tempRoot_.path());

        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{registerProposal()}));

        const QVariantList pending = pendingCandidates(&controller);
        QCOMPARE(pending.size(), 1);
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        const QVariantMap candidate = pending.at(0).toMap();
        QCOMPARE(candidate.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("register"));
        QCOMPARE(candidate.value(QStringLiteral("functionCode")).toInt(), 4);
        QCOMPARE(candidate.value(QStringLiteral("address")).toInt(), 5);
        QCOMPARE(candidate.value(QStringLiteral("name")).toString(),
                 QStringLiteral("Temperature"));
        QCOMPARE(candidate.value(QStringLiteral("dataType")).toString(),
                 QStringLiteral("Float32"));
        QCOMPARE(candidate.value(QStringLiteral("quantity")).toInt(), 2);
        QCOMPARE(candidate.value(QStringLiteral("scale")).toDouble(), 0.1);
        QCOMPARE(candidate.value(QStringLiteral("offset")).toDouble(), 0.0);
        QCOMPARE(candidate.value(QStringLiteral("unit")).toString(),
                 QStringLiteral("degC"));
        // T035-A.3.2.2: the optional-member presence flags ride the variant —
        // the fixture stated scale/offset/unit but NOT description.
        QCOMPARE(candidate.value(QStringLiteral("hasScale")).toBool(), true);
        QCOMPARE(candidate.value(QStringLiteral("hasOffset")).toBool(), true);
        QCOMPARE(candidate.value(QStringLiteral("hasUnit")).toBool(), true);
        QCOMPARE(candidate.value(QStringLiteral("hasDescription")).toBool(),
                 false);
        QCOMPARE(candidate.value(QStringLiteral("evidenceExcerpt")).toString(),
                 QStringLiteral("Model: INV-1000"));
        QCOMPARE(candidate.value(QStringLiteral("lifecycle")).toString(),
                 QStringLiteral("pending_review"));
        QCOMPARE(candidate.value(QStringLiteral("documentId")).toString(),
                 QString::fromStdString(harness.document.documentId));
        QCOMPARE(candidate.value(QStringLiteral("contentHash")).toString(),
                 QString::fromStdString(harness.document.contentHash));
        // The locally computed span round-trips to the excerpt.
        const qsizetype start = candidate.value(QStringLiteral("textStart"))
                                    .value<qlonglong>();
        const qsizetype end =
            candidate.value(QStringLiteral("textEnd")).value<qlonglong>();
        QVERIFY(start >= 0 && end > start);
        QCOMPARE(harness.canonicalText.mid(start, end - start),
                 QStringLiteral("Model: INV-1000"));
    }

    // -------------------------------------------------- A31-02 ----------
    // [EVOLVED by T035-A.3.2.1 — declared in the task doc, not a silent
    // assertion edit] Under A.3.1 this slot asserted that register Accept
    // refuses outright (register_candidate_apply_not_available). A.3.2.1
    // AUTHORIZED the controlled staged-draft apply, so the CURRENT
    // fail-closed boundary for a register accept WITHOUT an open profile
    // target is profile_target_missing — the candidate still stays
    // PendingReview and nothing is written. The old outright-refusal surface
    // remains covered by the register edited-confirm path and the
    // validation-failure slots below.
    void a31_02_acceptWithoutProfileTargetFailsClosed()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{registerProposal()}));

        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(!invokeAccept(&controller, candidate));
        QCOMPARE(controller.property("lastReviewErrorToken").toString(),
                 QStringLiteral("profile_target_missing"));
        // The candidate is STILL pending, unchanged.
        QCOMPARE(pendingCandidates(&controller).size(), 1);
        QCOMPARE(pendingCandidateAt(&controller, 0), candidate);
    }

    // -------------------------------------------------- A31-03 (§4-D) ---
    // A consumed register candidate can never be acted on again: the second
    // reject is a refusal (stale/repeated action), the first consumed it.
    void a31_03_staleRegisterCandidateRejectFails()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{registerProposal()}));

        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(invokeReject(&controller, candidate));
        QCOMPARE(pendingCandidates(&controller).size(), 0);
        QVERIFY(!invokeReject(&controller, candidate));
        QCOMPARE(controller.property("lastReviewErrorToken").toString(),
                 QStringLiteral("candidate_not_pending"));
        QCOMPARE(pendingCandidates(&controller).size(), 0);
    }

    // -------------------------------------------------- A31-04 (§4-E) ---
    // A seed proposal WITHOUT evidence is refused by the REAL deterministic
    // register validator — it never lands in the review set.
    void a31_04_missingEvidenceProposalNeverLands()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ReviewHarness harness(tempRoot_.path());

        QVariantMap noEvidence = registerProposal();
        noEvidence.insert(QStringLiteral("evidenceExcerpt"), QString());
        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{noEvidence}));
        QCOMPARE(pendingCandidates(&controller).size(), 0);

        // And an excerpt that is NOT in the manual is equally refused.
        QVariantMap falseEvidence = registerProposal();
        falseEvidence.insert(QStringLiteral("evidenceExcerpt"),
                             QStringLiteral("REGISTER NOT IN TEXT"));
        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{falseEvidence}));
        QCOMPARE(pendingCandidates(&controller).size(), 0);
    }

    // -------------------------------------------------- A31-05 (§4-C) ---
    // Manufacturer regression inside the COMBINED review model: the
    // profile-field queue keeps its kind/value, the register queue rides the
    // same projection after it, and both review paths still work end to end.
    void a31_05_mixedKindsProjectionAndManufacturerRegression()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        ReviewHarness harness(tempRoot_.path());

        QVERIFY(invokeSeed(&controller, harness.documentMap,
                           harness.canonicalText,
                           QVariantList{manufacturerProposal()}));
        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{registerProposal()}));

        const QVariantList pending = pendingCandidates(&controller);
        QCOMPARE(pending.size(), 2);
        QCOMPARE(controller.property("candidateCount").toInt(), 2);
        // The profile-field projection keeps its FROZEN key set (o16): no
        // kind key there — the register variant is the only kind carrier.
        QVERIFY(!pending.at(0).toMap().contains(QStringLiteral("kind")));
        QCOMPARE(pending.at(0).toMap().value(QStringLiteral("proposedValue"))
                     .toString(),
                 QString::fromUtf8(kProposed));
        QCOMPARE(pending.at(1).toMap().value(QStringLiteral("kind")).toString(),
                 QStringLiteral("register"));

        // The register reject path works in the combined projection...
        QVERIFY(invokeReject(&controller, pending.at(1).toMap()));
        QCOMPARE(pendingCandidates(&controller).size(), 1);
        // ...and the profile-field reject path is untouched.
        QVERIFY(invokeReject(&controller, pendingCandidates(&controller)
                                                 .at(0)
                                                 .toMap()));
        QCOMPARE(pendingCandidates(&controller).size(), 0);
        // The draft was never touched by any of it (no apply happened).
        QCOMPARE(profileController.manufacturer(), QString::fromUtf8(kOldManufacturer));
    }

    // ================================== T035-A.3.2.1 controlled apply ==

    // -------------------------------------------------- A32-01 (§4-A) ---
    // Human Accept on a register candidate: the CONTROLLED staged-draft
    // write succeeds, the candidate is consumed, and the DRAFT (never the
    // store) carries the entry with exact values.
    void a32_01_registerAcceptConsumesAndStagesDraft()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        ReviewHarness harness(tempRoot_.path());
        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{registerProposal()}));

        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(invokeAccept(&controller, candidate));

        // Consumed + staged into the draft (staged draft ONLY).
        QCOMPARE(pendingCandidates(&controller).size(), 0);
        QCOMPARE(profileController.registerMap().size(), 1);
        const QVariantMap row = profileController.registerMap().at(0).toMap();
        QCOMPARE(row.value(QStringLiteral("readFunctionCode")).toInt(), 4);
        QCOMPARE(row.value(QStringLiteral("address")).toInt(), 5);
        QCOMPARE(row.value(QStringLiteral("name")).toString(),
                 QStringLiteral("Temperature"));
        QCOMPARE(row.value(QStringLiteral("dataType")).toString(),
                 QStringLiteral("Float32"));
        QVERIFY(profileController.dirty());
        // Staged draft ONLY: no persistence behind the test's back.
        QVERIFY(!QFile::exists(ProfileStore::defaultFilePathFor(
            profileController.currentProfileId())));
    }

    // -------------------------------------------------- A32-02 (§4-B/D) -
    // Two individually-valid candidates for the SAME FC+address: the first
    // accept stages, the second is a duplicate_address fail-closed refusal —
    // the refused candidate stays PendingReview and the draft is untouched.
    void a32_02_duplicateAddressAcrossCandidatesFailsClosed()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        ReviewHarness harness(tempRoot_.path());

        QVariantMap first = registerProposal();
        first.insert(QStringLiteral("functionCode"), 3);
        first.insert(QStringLiteral("address"), 100);
        first.insert(QStringLiteral("dataType"), QStringLiteral("UInt16"));
        first.insert(QStringLiteral("quantity"), 1);
        first.insert(QStringLiteral("name"), QStringLiteral("Setpoint"));
        // "Model: INV-1000" — unique single line.
        QVariantMap second = first;
        second.insert(QStringLiteral("name"), QStringLiteral("Setpoint Copy"));
        second.insert(QStringLiteral("evidenceExcerpt"),
                      QStringLiteral("1. Identity")); // the other unique line

        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{first, second}));
        QCOMPARE(pendingCandidates(&controller).size(), 2);

        const QVariantMap firstMap = pendingCandidateAt(&controller, 0);
        QVERIFY(invokeAccept(&controller, firstMap));
        QCOMPARE(profileController.registerMap().size(), 1);

        // The projection order: profile-field queue is empty, so the SECOND
        // register candidate is now pendingCandidateAt(0).
        const QVariantMap secondMap = pendingCandidateAt(&controller, 0);
        QVERIFY(!invokeAccept(&controller, secondMap));
        QCOMPARE(controller.property("lastReviewErrorToken").toString(),
                 QStringLiteral("candidate_apply_failed"));
        QCOMPARE(profileController.lastActionErrorToken(),
                 QStringLiteral("duplicate_address"));
        // Fail-closed: the refused candidate stays pending, draft unchanged.
        QCOMPARE(pendingCandidates(&controller).size(), 1);
        QCOMPARE(pendingCandidateAt(&controller, 0), secondMap);
        QCOMPARE(profileController.registerMap().size(), 1);
    }

    // -------------------------------------------------- A32-03 (§4-C/E) -
    // Overlapping span against a MANUAL draft entry (FC03@100) — the
    // candidate FC03@99..100 is refused; draft and pending set unchanged.
    void a32_03_overlappingSpanAgainstManualEntryFailsClosed()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        QVERIFY(profileController.addRegisterEntry(registerEntryFields()));
        ReviewHarness harness(tempRoot_.path());

        QVariantMap overlapping = registerProposal();
        overlapping.insert(QStringLiteral("functionCode"), 3);
        overlapping.insert(QStringLiteral("address"), 99);
        overlapping.insert(QStringLiteral("dataType"), QStringLiteral("Float32"));
        overlapping.insert(QStringLiteral("quantity"), 2);
        overlapping.insert(QStringLiteral("name"), QStringLiteral("Overlaps"));
        overlapping.insert(QStringLiteral("evidenceExcerpt"),
                           QStringLiteral("Model: INV-1000"));
        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{overlapping}));

        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(!invokeAccept(&controller, candidate));
        QCOMPARE(profileController.lastActionErrorToken(),
                 QStringLiteral("overlapping_span"));
        QCOMPARE(profileController.registerMap().size(), 1);
        QCOMPARE(pendingCandidates(&controller).size(), 1);
    }

    // -------------------------------------------------- A32-04 (§7-K) ---
    // The apply-side forensics records the machine token: a register accept
    // that fails profile validation produces a
    // "[register validation forensics]" block with
    // stage=validation:duplicate_address (Human T035-A.3.2.2 §6).
    void a32_04_forensicsValidationTokenRecorded()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        ReviewHarness harness(tempRoot_.path());

        qputenv("MODBUSLENS_PROVIDER_FORENSICS", "1");
        const QString logPath =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/modbuslens_provider_forensics.log");
        QFile::remove(logPath);

        QVariantMap first = registerProposal();
        first.insert(QStringLiteral("functionCode"), 3);
        first.insert(QStringLiteral("address"), 100);
        first.insert(QStringLiteral("dataType"), QStringLiteral("UInt16"));
        first.insert(QStringLiteral("quantity"), 1);
        first.insert(QStringLiteral("name"), QStringLiteral("Setpoint"));
        QVariantMap duplicate = first;
        duplicate.insert(QStringLiteral("name"), QStringLiteral("Copy"));

        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{first, duplicate}));
        QVERIFY(invokeAccept(&controller, pendingCandidateAt(&controller, 0)));
        QVERIFY(!invokeAccept(&controller, pendingCandidateAt(&controller, 0)));

        QFile log(logPath);
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(log.readAll());
        QVERIFY(content.contains(
            QStringLiteral("[register validation forensics]")));
        QVERIFY(content.contains(
            QStringLiteral("stage=validation:duplicate_address")));
        log.close();
        QFile::remove(logPath);
        qunsetenv("MODBUSLENS_PROVIDER_FORENSICS");
    }

    // =======================================================================
    // T035-A.3.2.3 — LINE-ENDING EVIDENCE, END TO END THROUGH THE REVIEW MODEL
    // =======================================================================

    // A-323-01: a register proposal whose evidence differs from the canonical
    // text ONLY in line endings now SEEDS a PendingReview candidate (the
    // extraction-time fallback), and the stored excerpt stays the PROVIDER's
    // submission (no laundering).
    void a323_01_registerSeedAcceptsLineEndingEvidence()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        ReviewHarness harness(tempRoot_.path(), kCrlfManualText,
                              QStringLiteral("crlf-manual.txt"));
        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{crlfRegisterProposal()}));
        QCOMPARE(pendingCandidates(&controller).size(), 1);
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QCOMPARE(candidate.value(QStringLiteral("evidenceExcerpt")).toString(),
                 QStringLiteral("Model: INV-1000\n"
                                "Register 5: Temperature degC"));
    }

    // A-323-02 (Human §5 — the critical one): extraction passed, so ACCEPT must
    // pass too. The register freshness gate uses the SAME rule as generation.
    void a323_02_registerAcceptRevalidatesWithLineEndingRule()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        ReviewHarness harness(tempRoot_.path(), kCrlfManualText,
                              QStringLiteral("crlf-manual.txt"));
        QVERIFY(invokeSeedRegister(&controller, harness.documentMap,
                                   harness.canonicalText,
                                   QVariantList{crlfRegisterProposal()}));
        const QVariantMap candidate = pendingCandidateAt(&controller, 0);
        QVERIFY(invokeAccept(&controller, candidate));
        QCOMPARE(pendingCandidates(&controller).size(), 0);
        QCOMPARE(profileController.registerMap().size(), 1);
        const QVariantMap row = profileController.registerMap().at(0).toMap();
        QCOMPARE(row.value(QStringLiteral("address")).toInt(), 5);
        QCOMPARE(row.value(QStringLiteral("name")).toString(),
                 QStringLiteral("Temperature"));
        // Still staged-draft only.
        QVERIFY(!QFile::exists(ProfileStore::defaultFilePathFor(
            profileController.currentProfileId())));
    }

    // A-323-03 (Human §5/§10): the MANUFACTURER path is NOT widened. The same
    // CRLF manual with an LF manufacturer excerpt is refused — extraction and
    // accept both, so the frozen contract is provably intact.
    void a323_03_manufacturerEvidenceStaysExactOnly()
    {
        CountingRefusingRunner runner;
        CandidateExtractionController controller(runner);
        ProfileController profileController;
        wire(&controller, &profileController);
        openProfileWithOldManufacturer(&profileController);
        ReviewHarness harness(tempRoot_.path(), kCrlfManualText,
                              QStringLiteral("crlf-manual.txt"));
        QVERIFY(invokeSeed(&controller, harness.documentMap, harness.canonicalText,
                           QVariantList{crlfManufacturerProposal()}));
        // Refused by the exact-only manufacturer validator: nothing lands.
        QCOMPARE(pendingCandidates(&controller).size(), 0);
    }

    // A-323-04 NOTE: forensics for the EVIDENCE stage is emitted by the
    // production COMPLETION path (completeRegisterAttempt), which the
    // automation seed deliberately does not go through. The forensics
    // consistency assertion therefore lives in the orchestration suite
    // (reg06_forensicsAgreesWithLineEndingJudgment), where the real completion
    // seam is reached. Keeping a copy here would only assert the absence of a
    // log file — a vacuous test.

private:
    // NOTE (T035-A.3.1 lesson): a member VARIABLE must never live inside a
    // "private slots:" section — moc rejects it ("Not a signal or slot
    // declaration") and silently emits a partial moc table, which drops the
    // new slots from test discovery while the build still succeeds.
    QTemporaryDir tempRoot_;
};

QTEST_MAIN(CandidateReviewTest)

#include "test_candidate_review.moc"
