// M12-C ML-2 tests (Manual Library maintenance, T027 §91): DELETE of one
// selected Manual under the frozen P0-ML-A/B/C/D/F/G/H policies.
//
// Determinism is structural: delete never touches a provider (the runner
// below only counts dispatches), the managed roots are injected temporary
// directories, and every delete goes through the ONE authoritative
// application path: guards -> command -> ManualStore -> controller refresh.
// All new entry points are exercised through the Qt meta-object (by name),
// matching the QML call boundary exactly.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <string>

#include "core/manual/ManualDocument.h"
#include "core/profile/DeviceProfile.h"
#include "ui/candidate/CandidateExtractionController.h"
#include "ui/candidate/CandidateExtractionRunner.h"
#include "ui/manual/ManualImportController.h"
#include "ui/manual/ManualStore.h"
#include "ui/profile/ProfileController.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::ui::CandidateExtractionController;
using modbuslens::ui::ICandidateExtractionRunner;
using modbuslens::ui::ManualImportController;
using modbuslens::ui::ManualStore;
using modbuslens::ui::ProfileController;
using modbuslens::ui::ProfileStore;

namespace {

constexpr auto kTokenBlockedPending = "manual_delete_blocked_pending";
constexpr auto kTokenBlockedRunning = "manual_delete_blocked_running";
constexpr auto kTokenNotFound = "manual_delete_not_found";
constexpr auto kTokenMetadataFailed = "manual_delete_metadata_failed";
constexpr auto kTokenCleanupWarning = "manual_delete_cleanup_warning";

constexpr char kManualA[] = "NovaDrive NDV-200 VFD\n"
                            "Manufacturer: NovaDrive Automation Ltd.\n";
constexpr char kManualB[] = "Huachen HCA-500 Thermostat\n"
                            "Manufacturer: Huachen Automation Equipment Co., Ltd.\n";
constexpr char kManualC[] = "Orion OMX-32 Servo Controller\n"
                            "Manufacturer: Orion Motion Technologies\n";

// Counts dispatches ONLY to prove review/delete actions never reach a
// provider (ML2-22). begin() starts an attempt that NEVER completes, which
// is what makes the RUNNING state deterministic for the guard tests.
class HangingCountingRunner : public ICandidateExtractionRunner
{
public:
    bool begin(const modbuslens::core::ExtractionRequest & /*request*/,
               std::uint64_t /*generation*/,
               const CompletionHandler & /*onDone*/) override
    {
        ++calls_;
        return true; // attempt starts and stays Running (no completion)
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

[[nodiscard]] bool deleteOk(const QVariantMap &result)
{
    return !result.isEmpty() && result.value("ok").toBool();
}

[[nodiscard]] QString resultToken(const QVariantMap &result)
{
    return result.value("token").toString();
}

[[nodiscard]] QString writeSource(const QString &dir, const QString &name,
                                  const QByteArray &bytes)
{
    const QString path = QDir(dir).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    file.write(bytes);
    file.close();
    return path;
}

[[nodiscard]] QByteArray readFileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// Imports `bytes` as one manual through the authoritative controller route
// and returns the installed record map (from the refreshed list).
// Imports `bytes` and returns the installed record; an empty map means the
// import failed (the caller's QVERIFY catches it with context).
[[nodiscard]] QVariantMap importManual(ManualImportController &controller,
                                       const QString &rootDir,
                                       const QString &fileName,
                                       const QByteArray &bytes)
{
    const QString path = writeSource(rootDir, fileName, bytes);
    if (path.isEmpty()) {
        return {};
    }
    if (!controller.importManualFile(QUrl::fromLocalFile(path))) {
        return {};
    }
    for (const QVariant &entry : controller.manualDocuments()) {
        if (entry.toMap().value("originalFileName").toString() == fileName) {
            return entry.toMap();
        }
    }
    return {};
}

[[nodiscard]] modbuslens::core::ManualDocument documentFrom(
    const QVariantMap &record)
{
    modbuslens::core::ManualDocument document;
    document.documentId = record.value("documentId").toString().toStdString();
    document.contentHash = record.value("contentHash").toString().toStdString();
    document.originalFileName =
        record.value("originalFileName").toString().toStdString();
    document.originalPath =
        record.value("originalPath").toString().toStdString();
    document.documentType = modbuslens::core::ManualDocumentType::Txt;
    return document;
}

// Installs ONE validated PendingReview Candidate referencing `record` through
// the automation seed (the REAL deterministic C2 validator).
[[nodiscard]] bool seedPendingCandidate(
    CandidateExtractionController &controller, const QVariantMap &record,
    const QString &canonicalText)
{
    QVariantMap document;
    document.insert("documentId", record.value("documentId"));
    document.insert("contentHash", record.value("contentHash"));
    document.insert("originalFileName", record.value("originalFileName"));
    QVariantMap proposal;
    proposal.insert("target", QStringLiteral("manufacturer"));
    proposal.insert("proposedValue",
                    QStringLiteral("NovaDrive Automation Ltd."));
    proposal.insert("evidenceExcerpt",
                    QStringLiteral("NovaDrive Automation Ltd."));
    return controller.seedReviewCandidatesForAutomation(
        document, canonicalText, QVariantList{proposal});
}

} // namespace

class ManualDeleteTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        qunsetenv("MODELSCOPE_API_KEY");
        qunsetenv("MODBUSLENS_MODELSCOPE_MODEL");
        const QString testDir = QDir(tempRoot_.path()).filePath(
            QTest::currentTestFunction() ? QTest::currentTestFunction()
                                         : QStringLiteral("case"));
        QDir(tempRoot_.path()).mkpath(testDir);
        ManualStore::setManagedRootOverride(testDir);
        ProfileStore::setManagedRootOverride(testDir);
        ManualStore::setRemoveInterposerForAutomation({});
    }

    void cleanup()
    {
        ManualStore::setRemoveInterposerForAutomation({});
        ManualStore::setManagedRootOverride(QString());
        ProfileStore::setManagedRootOverride(QString());
    }

    // ----------------------------------------------------- RED anchor ----
    // The frozen ML-2 delete command must exist on the orchestration owner
    // before any behavioral assertion can mean anything.
    void ml2_red_deleteApisExist()
    {
        CandidateExtractionController controller;
        QVERIFY(hasMethod(&controller, "deleteManualDocument(QVariantMap)"));
        QVERIFY(hasMethod(&controller,
                          "checkManualDeleteAllowed(QVariantMap)"));
        QVERIFY(controller.metaObject()->indexOfProperty("lastDeleteNotice")
                >= 0);
        QVERIFY(controller.metaObject()->indexOfProperty("lastDeleteNoticeToken")
                >= 0);
    }

    // ----------------------------------------------------- ML2-01 --------
    void ml2_01_noManualCannotDestructivelyProceed()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        wire(&controller, &manuals);
        QVariantMap ghost;
        ghost.insert("documentId", QStringLiteral("no-such-document"));
        const QVariantMap result = invokeDelete(&controller, ghost);
        QVERIFY(!deleteOk(result));
        QCOMPARE(resultToken(result), QString::fromLatin1(kTokenNotFound));
        QCOMPARE(runner.callCount(), 0);
    }

    // --------------------------------------- ML2-04/05/06/07/08 + 22 -----
    void ml2_deleteSelectedManualEndToEnd()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        wire(&controller, &manuals);

        const QVariantMap a = importManual(manuals, tempRoot_.path(),
                                           "delete-me.txt",
                                           QByteArray("Delete me\n"));
        QVERIFY(!a.isEmpty());
        QCOMPARE(manuals.manualDocuments().size(), 1);
        manuals.selectDocument(0);
        QCOMPARE(manuals.selectedIndex(), 0);
        QCOMPARE(manuals.previewText(), QStringLiteral("Delete me\n"));
        QVERIFY(invokeCheckAllowed(&controller, a)
                    .value("allowed")
                    .toBool());

        const QString metadataPath =
            ManualStore::documentsDirectory() + QStringLiteral("/")
            + a.value("documentId").toString() + QStringLiteral(".json");
        QVERIFY(QFile::exists(metadataPath));

        // ML2-04: the authoritative metadata record is removed.
        const QVariantMap result = invokeDelete(&controller, a);
        QVERIFY2(deleteOk(result),
                 qPrintable(QStringLiteral("delete failed: ")
                            + resultToken(result)));
        QVERIFY(!QFile::exists(metadataPath));

        // ML2-05/06: selection and preview are cleared (P0-ML-A), which also
        // disables AI extraction until an explicit new selection (ML2-07
        // structural via selectedIndex).
        QCOMPARE(manuals.selectedIndex(), -1);
        QVERIFY(manuals.selectedDocument().isEmpty());
        QVERIFY(manuals.previewText().isEmpty());

        // ML2-08: a fresh controller still sees the record gone.
        ManualImportController reopened;
        QCOMPARE(reopened.manualDocuments().size(), 0);

        // ML2-22: zero provider dispatches for the whole delete path.
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- ML2-03 --------
    // Cancel semantics are QML-dialog behavior (the Cancel button performs NO
    // call at all); the deterministic unit equivalent: the guard CHECK is a
    // pure read and never mutates the library.
    void ml2_03_checkAllowedIsPureRead()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        wire(&controller, &manuals);
        const QVariantMap a = importManual(manuals, tempRoot_.path(),
                                           "keep.txt", QByteArray("Keep\n"));
        QVERIFY(!a.isEmpty());
        const int countBefore = manuals.manualDocuments().size();

        const QVariantMap check = invokeCheckAllowed(&controller, a);
        QVERIFY(check.value("allowed").toBool());

        QCOMPARE(manuals.manualDocuments().size(), countBefore);
        QCOMPARE(runner.callCount(), 0);
    }

    // --------------------------------------------- ML2-09/10/24 ----------
    void ml2_otherRecordsOriginalSourceAndSelectionIdentity()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        wire(&controller, &manuals);

        const QVariantMap a = importManual(manuals, tempRoot_.path(),
                                           "remove-me.txt",
                                           QByteArray("Remove me\n"));
        const QString sourcePathB = writeSource(tempRoot_.path(),
                                                "original-B.txt",
                                                QByteArray("Keep me\n"));
        const QVariantMap b = importManual(manuals, tempRoot_.path(),
                                           "original-B.txt",
                                           QByteArray("Keep me\n"));
        QVERIFY(!a.isEmpty());
        QVERIFY(!b.isEmpty());
        QCOMPARE(manuals.manualDocuments().size(), 2);
        QVERIFY(a.value("documentId") != b.value("documentId"));

        // Select B, then delete A: the selection must NOT silently move
        // (ML2-24) - it stays on B BY IDENTITY.
        const int indexB = manuals.manualDocuments().indexOf(b);
        QVERIFY(indexB >= 0);
        manuals.selectDocument(indexB);

        QVERIFY(deleteOk(invokeDelete(&controller, a)));

        // ML2-09: exactly one record remains and it is B.
        QCOMPARE(manuals.manualDocuments().size(), 1);
        QCOMPARE(manuals.manualDocuments().front().toMap().value("documentId"),
                 b.value("documentId"));
        // ML2-24: the selection still points at B BY IDENTITY (the numeric
        // index may legitimately shift when the list shrinks).
        QCOMPARE(manuals.selectedIndex(), manuals.manualDocuments()
                                              .indexOf(b));
        QCOMPARE(manuals.selectedDocument().value("documentId"),
                 b.value("documentId"));
        // ML2-10: the ORIGINAL external source file B is untouched.
        QVERIFY(QFile::exists(sourcePathB));
        QCOMPARE(readFileBytes(sourcePathB), QByteArray("Keep me\n"));
        QCOMPARE(runner.callCount(), 0);
    }

    // ------------------------------------------------ ML2-11/12 ----------
    void ml2_sharedContentSurvivesThenLastReferenceCleansUp()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        wire(&controller, &manuals);

        // Two DIFFERENT records with the SAME content: identical bytes from
        // two different original paths.
        const QByteArray shared = "Shared content that must survive\n";
        const QVariantMap recordOne =
            importManual(manuals, tempRoot_.path(), "one.txt", shared);
        QVERIFY(!recordOne.isEmpty());
        const QDir secondDir(tempRoot_.path() + QStringLiteral("/two"));
        QVERIFY(secondDir.mkpath(QStringLiteral(".")));
        const QVariantMap recordTwo =
            importManual(manuals, secondDir.path(), "two.txt", shared);
        QVERIFY(!recordTwo.isEmpty());
        QCOMPARE(manuals.manualDocuments().size(), 2);
        QVERIFY(recordOne.value("documentId") != recordTwo.value("documentId"));
        QCOMPARE(recordOne.value("contentHash"), recordTwo.value("contentHash"));

        const QString managedSource = ManualStore::sourceDirectory()
            + QStringLiteral("/")
            + recordOne.value("contentHash").toString()
            + QStringLiteral(".bin");
        const QString textCache = ManualStore::textDirectory()
            + QStringLiteral("/")
            + recordOne.value("contentHash").toString()
            + QStringLiteral(".txt");
        QVERIFY(QFile::exists(managedSource));
        QVERIFY(QFile::exists(textCache));

        // Delete the FIRST record: the shared artifacts are still referenced
        // by the second record and MUST survive (P0-ML-F).
        QVERIFY(deleteOk(invokeDelete(&controller, recordOne)));
        QCOMPARE(manuals.manualDocuments().size(), 1);
        QVERIFY(QFile::exists(managedSource));
        QVERIFY(QFile::exists(textCache));

        // The surviving record still previews its managed text. The delete
        // removed the OTHER record, so the selection is KEPT on the
        // survivor by identity (P0-ML-A clears only a deleted selection).
        QCOMPARE(manuals.selectedIndex(), 0);
        QCOMPARE(manuals.selectedDocument().value("documentId"),
                 recordTwo.value("documentId"));
        QCOMPARE(manuals.previewText(), QString::fromUtf8(shared));

        // Delete the LAST reference: the eligible managed artifacts are
        // cleaned (ML2-12).
        QVERIFY(deleteOk(invokeDelete(&controller, recordTwo)));
        QCOMPARE(manuals.manualDocuments().size(), 0);
        QVERIFY(!QFile::exists(managedSource));
        QVERIFY(!QFile::exists(textCache));
        QCOMPARE(runner.callCount(), 0);
    }

    // ----------------------------------------------------- ML2-13 --------
    void ml2_missingManagedArtifactStillSafe()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        wire(&controller, &manuals);

        const QVariantMap a = importManual(manuals, tempRoot_.path(),
                                           "missing-artifact.txt",
                                           QByteArray("Artifact gone\n"));
        QVERIFY(!a.isEmpty());
        QVERIFY(QFile::remove(
            ManualStore::textDirectory() + QStringLiteral("/")
            + a.value("contentHash").toString() + QStringLiteral(".txt")));

        QVERIFY(deleteOk(invokeDelete(&controller, a)));
        QCOMPARE(manuals.manualDocuments().size(), 0);
        QCOMPARE(runner.callCount(), 0);
    }

    // --------------------------------------- ML2-16/17/23 (RED anchor) ---
    void ml2_pendingCandidateBlocksOnlyItsOwnManual()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        wire(&controller, &manuals);

        const QVariantMap a = importManual(manuals, tempRoot_.path(),
                                           "target.txt",
                                           QByteArray("Target manual\n"
                                                      "NovaDrive Automation "
                                                      "Ltd.\n"));
        const QVariantMap b = importManual(manuals, tempRoot_.path(),
                                           "other.txt",
                                           QByteArray("Other manual\n"));

        // Seed a REAL validated PendingReview Candidate referencing A.
        QVERIFY(seedPendingCandidate(
            controller, a,
            QStringLiteral("Target manual\nNovaDrive Automation Ltd.\n")));
        QCOMPARE(controller.property("candidateCount").toInt(), 1);

        // ML2-16: deleting A is BLOCKED while its Candidate is pending.
        const QVariantMap blocked = invokeDelete(&controller, a);
        QVERIFY(!deleteOk(blocked));
        QCOMPARE(resultToken(blocked),
                 QString::fromLatin1(kTokenBlockedPending));
        QCOMPARE(controller.property("lastDeleteNoticeToken").toString(),
                 QString::fromLatin1(kTokenBlockedPending));
        QCOMPARE(manuals.manualDocuments().size(), 2); // nothing removed
        QCOMPARE(controller.property("candidateCount").toInt(), 1);

        // ML2-17: the pending Candidate for A does NOT block deleting B.
        QVERIFY(deleteOk(invokeDelete(&controller, b)));
        QCOMPARE(manuals.manualDocuments().size(), 1);
        // ML2-23: the Candidate was NOT auto-consumed by the delete.
        QCOMPARE(controller.property("candidateCount").toInt(), 1);
        QCOMPARE(runner.callCount(), 0);
    }

    // --------------------------------------- ML2-18/19/22 (RED anchor) ---
    void ml2_runningExtractionBlocksOnlyItsOwnManual()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        wire(&controller, &manuals);

        const QVariantMap a = importManual(manuals, tempRoot_.path(),
                                           "running.txt",
                                           QByteArray("Running target\n"));
        const QVariantMap b = importManual(manuals, tempRoot_.path(),
                                           "idle.txt",
                                           QByteArray("Idle manual\n"));

        // Drive the REAL extraction state machine to Running for A (consent
        // granted through the accepted flow; the hanging runner never
        // completes, so the attempt stays Running).
        controller.requestExtractionFor(
            documentFrom(a),
            QStringLiteral("Running target\n").toStdString());
        controller.grantConsent();
        QCOMPARE(controller.property("stateToken").toString(),
                 QStringLiteral("running"));

        // ML2-18: deleting A is BLOCKED while its extraction is Running.
        const QVariantMap blocked = invokeDelete(&controller, a);
        QVERIFY(!deleteOk(blocked));
        QCOMPARE(resultToken(blocked),
                 QString::fromLatin1(kTokenBlockedRunning));
        QCOMPARE(manuals.manualDocuments().size(), 2);
        QCOMPARE(controller.property("stateToken").toString(),
                 QStringLiteral("running")); // extraction state untouched

        // ML2-19: a DIFFERENT manual is not blocked by A's Running attempt.
        QVERIFY(deleteOk(invokeDelete(&controller, b)));
        QCOMPARE(manuals.manualDocuments().size(), 1);
        QCOMPARE(controller.property("stateToken").toString(),
                 QStringLiteral("running")); // still running, untouched
        // ML2-22: the delete path caused ZERO additional runner dispatches.
        QCOMPARE(runner.callCount(), 1);
    }

    // --------------------------------------- ML2-20 (RED anchor) ---------
    void ml2_consumedCandidateDoesNotBlock()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        ProfileController profiles;
        wire(&controller, &manuals);
        // C3 Accept requires the controlled draft target (C3-H4).
        QVERIFY(controller.setProperty("profileController",
                                       QVariant::fromValue(&profiles)));
        profiles.newProfile();
        QVERIFY(profiles.setProperty("displayName",
                                     QStringLiteral("Review Profile")));

        const QVariantMap a = importManual(
            manuals, tempRoot_.path(), "consumed.txt",
            QByteArray("Consumed candidate manual\n"
                       "NovaDrive Automation Ltd.\n"));
        // Seed a pending Candidate, then CONSUME it through the accepted C3
        // review path (Accept with valid evidence + open profile).
        QVERIFY(seedPendingCandidate(
            controller, a,
            QStringLiteral("Consumed candidate manual\n"
                           "NovaDrive Automation Ltd.\n")));
        const QVariantList pending = controller.property("candidates").toList();
        QCOMPARE(pending.size(), 1);
        bool accepted = false;
        QVERIFY(QMetaObject::invokeMethod(
            &controller, "acceptCandidate", Q_RETURN_ARG(bool, accepted),
            Q_ARG(QVariantMap, pending.front().toMap())));
        QVERIFY(accepted);
        QCOMPARE(controller.property("candidateCount").toInt(), 0);

        // Only PENDING matters: the consumed Candidate cannot block.
        QVERIFY(deleteOk(invokeDelete(&controller, a)));
        QCOMPARE(manuals.manualDocuments().size(), 0);
        QCOMPARE(runner.callCount(), 0);
    }

    // --------------------------------------- ML2-21/30 (RED anchor) ------
    void ml2_savedProfileTruthUnchangedByDelete()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        ProfileController profiles;
        wire(&controller, &manuals);
        profiles.newProfile();
        QVERIFY(profiles.setProperty("displayName",
                                     QStringLiteral("Saved Profile")));
        QVERIFY(profiles.setProperty("manufacturer",
                                     QStringLiteral("ACME Ltd.")));
        QVERIFY(profiles.saveCurrent());
        const QString profilePath = ProfileStore::defaultFilePathFor(
            profiles.currentProfileId());
        const QByteArray persistedBefore = readFileBytes(profilePath);

        const QVariantMap a = importManual(manuals, tempRoot_.path(),
                                           "profile-source.txt",
                                           QByteArray("Profile source\n"));
        QVERIFY(deleteOk(invokeDelete(&controller, a)));

        // ML2-21/30: the saved DeviceProfile truth is byte-identical and the
        // accepted Manufacturer value survives the manual deletion.
        QCOMPARE(readFileBytes(profilePath), persistedBefore);
        const auto loaded = ProfileStore::loadFromFile(profilePath);
        QVERIFY(loaded.ok());
        QCOMPARE(QString::fromStdString(loaded.profile.manufacturer),
                 QStringLiteral("ACME Ltd."));
        QCOMPARE(runner.callCount(), 0);
    }

    // --------------------------------- ML2-14/15 (fault injection) --------
    // Deterministic failures through the smallest test-only remove seam: the
    // production default stays direct QFile::remove. No real directory is
    // sabotaged and no permission is touched.
    void ml2_14_metadataRemoveFailureFailsAtomically()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        wire(&controller, &manuals);

        const QVariantMap a = importManual(manuals, tempRoot_.path(),
                                           "metadata-fail.txt",
                                           QByteArray("Metadata fail"));
        QVERIFY(!a.isEmpty());
        const QString metadataPath =
            ManualStore::documentsDirectory() + QStringLiteral("/")
            + a.value("documentId").toString() + QStringLiteral(".json");
        const QString managedSource =
            ManualStore::sourceDirectory() + QStringLiteral("/")
            + a.value("contentHash").toString() + QStringLiteral(".bin");

        // Force ONLY the authoritative metadata removal to fail.
        ManualStore::setRemoveInterposerForAutomation(
            [&metadataPath](const QString &path) {
                return path != metadataPath && QFile::remove(path);
            });

        const QVariantMap result = invokeDelete(&controller, a);
        QVERIFY(!deleteOk(result));
        QCOMPARE(resultToken(result),
                 QString::fromLatin1(kTokenMetadataFailed));
        // P0-ML-D: the record REMAINS visible and no GC ran.
        QVERIFY(QFile::exists(metadataPath));
        QCOMPARE(manuals.manualDocuments().size(), 1);
        QVERIFY(QFile::exists(managedSource));
        ManualStore::setRemoveInterposerForAutomation({});
        QCOMPARE(runner.callCount(), 0);
    }

    void ml2_15_cleanupFailureWarnsAfterMetadataSuccess()
    {
        HangingCountingRunner runner;
        CandidateExtractionController controller(runner);
        ManualImportController manuals;
        wire(&controller, &manuals);

        const QVariantMap a = importManual(manuals, tempRoot_.path(),
                                           "cleanup-fail.txt",
                                           QByteArray("Cleanup fail"));
        const QVariantMap keep = importManual(manuals, tempRoot_.path(),
                                              "keep-cleanup.txt",
                                              QByteArray("Keep"));
        QVERIFY(!a.isEmpty());
        QVERIFY(!keep.isEmpty());
        const QString metadataPath =
            ManualStore::documentsDirectory() + QStringLiteral("/")
            + a.value("documentId").toString() + QStringLiteral(".json");
        const QString managedSource =
            ManualStore::sourceDirectory() + QStringLiteral("/")
            + a.value("contentHash").toString() + QStringLiteral(".bin");
        QVERIFY(QFile::exists(managedSource));

        // Force ONLY the eligible managed-source cleanup to fail.
        ManualStore::setRemoveInterposerForAutomation(
            [&managedSource](const QString &path) {
                return path != managedSource && QFile::remove(path);
            });

        const QVariantMap result = invokeDelete(&controller, a);
        // P0-ML-F: SUCCESS WITH WARNING - the record stays deleted and the
        // warning is visible; never a fake full-cleanup success, never a
        // rollback that recreates the metadata.
        QVERIFY2(deleteOk(result),
                 qPrintable(QStringLiteral("delete failed: ")
                            + resultToken(result)));
        QCOMPARE(resultToken(result),
                 QString::fromLatin1(kTokenCleanupWarning));
        QCOMPARE(controller.property("lastDeleteNoticeToken").toString(),
                 QString::fromLatin1(kTokenCleanupWarning));
        QVERIFY(!QFile::exists(metadataPath));
        QCOMPARE(manuals.manualDocuments().size(), 1); // only `keep` remains
        QVERIFY(QFile::exists(managedSource)); // leftover is visible state
        ManualStore::setRemoveInterposerForAutomation({});
        QCOMPARE(runner.callCount(), 0);
    }

private:
    static void wire(CandidateExtractionController *controller,
                     ManualImportController *manuals)
    {
        QVERIFY(controller->setProperty("manualController",
                                        QVariant::fromValue(manuals)));
    }

    [[nodiscard]] static modbuslens::core::ManualDocument documentFrom(
        const QVariantMap &record)
    {
        modbuslens::core::ManualDocument document;
        document.documentId = record.value("documentId").toString().toStdString();
        document.contentHash = record.value("contentHash").toString().toStdString();
        document.originalFileName =
            record.value("originalFileName").toString().toStdString();
        document.originalPath =
            record.value("originalPath").toString().toStdString();
        document.documentType = modbuslens::core::ManualDocumentType::Txt;
        return document;
    }

    [[nodiscard]] static QByteArray readFileBytes(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll();
    }

    QTemporaryDir tempRoot_;
};

QTEST_GUILESS_MAIN(ManualDeleteTest)

#include "test_manual_delete.moc"
