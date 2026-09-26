#include <QtTest>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "core/profile/DeviceProfile.h"
#include "ui/profile/ProfileController.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::core::DeviceProfile;
using modbuslens::core::RegisterDecodeType;
using modbuslens::core::RegisterEntry;
using modbuslens::ui::ProfileController;
using modbuslens::ui::ProfileStore;

namespace {

// A valid single-register profile written directly into the managed root.
DeviceProfile makeValidProfile(const QString &profileId,
                               const QString &displayName = "Inverter A")
{
    DeviceProfile profile;
    profile.schemaVersion = 1;
    profile.profileId = profileId.toStdString();
    profile.displayName = displayName.toStdString();
    profile.manufacturer = "ACME";
    profile.model = "INV-1000";
    RegisterEntry entry;
    entry.readFunctionCode = 0x03;
    entry.address = 1000;
    entry.name = "Frequency";
    entry.dataType = RegisterDecodeType::UInt16;
    entry.registerCount = 1;
    entry.scale = 0.1;
    entry.unit = "Hz";
    profile.registers.push_back(entry);
    return profile;
}

QString writeManaged(const DeviceProfile &profile)
{
    const QString path = ProfileStore::defaultFilePathFor(
        QString::fromStdString(profile.profileId));
    const auto saved = ProfileStore::saveToFile(profile, path);
    if (!saved.ok()) {
        return QString();
    }
    return path;
}

} // namespace

class ProfileControllerTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;

private slots:
    void init()
    {
        // Every test runs against its OWN injected temporary managed root:
        // automated tests must never touch a real user's profile data.
        QVERIFY(QTemporaryDir().isValid());
        const QString root = QDir::temp().filePath(
            QStringLiteral("m12b-test-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QDir().mkpath(root);
        ProfileStore::setManagedRootOverride(root);
    }

    void cleanup()
    {
        ProfileStore::setManagedRootOverride(QString());
    }

    void injectionProof()
    {
        // B1-C20: the injected root must never equal the production
        // AppData location.
        QVERIFY(!ProfileStore::managedProfilesDirectory().isEmpty());
        QVERIFY(ProfileStore::managedRootOverride().isEmpty()
                || ProfileStore::managedProfilesDirectory()
                       == ProfileStore::managedRootOverride());
    }

    void b1c01_emptyCatalog()
    {
        ProfileController controller;
        controller.refreshCatalog();
        QCOMPARE(controller.profileCatalog().size(), 0);
        QCOMPARE(controller.catalogIssueCount(), 0);
        QVERIFY(!controller.hasOpenProfile());
    }

    void b1c02_validProfileAppears()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-1")).isEmpty());
        ProfileController controller;
        controller.refreshCatalog();
        QCOMPARE(controller.profileCatalog().size(), 1);
        const QVariantMap entry = controller.profileCatalog().first().toMap();
        QCOMPARE(entry.value("profileId").toString(), QStringLiteral("id-1"));
        QCOMPARE(entry.value("displayPrimary").toString(),
                 QStringLiteral("Inverter A"));
    }

    void b1c03_deterministicListing()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-b", "Beta")).isEmpty());
        QVERIFY(!writeManaged(makeValidProfile("id-a", "Alpha")).isEmpty());
        QVERIFY(!writeManaged(makeValidProfile("id-c", "Beta")).isEmpty());
        ProfileController controller;
        controller.refreshCatalog();
        const QVariantList catalog = controller.profileCatalog();
        QCOMPARE(catalog.size(), 3);
        QCOMPARE(catalog.at(0).toMap().value("displayPrimary").toString(),
                 QStringLiteral("Alpha"));
        QCOMPARE(catalog.at(1).toMap().value("displayPrimary").toString(),
                 QStringLiteral("Beta"));
        QCOMPARE(catalog.at(2).toMap().value("displayPrimary").toString(),
                 QStringLiteral("Beta"));
        // Same displayPrimary: ordered by profileId.
        QVERIFY(catalog.at(1).toMap().value("profileId").toString()
                < catalog.at(2).toMap().value("profileId").toString());
    }

    void b1c04_duplicateDisplayNameIdentityStaysProfileId()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-11111111", "Same")).isEmpty());
        QVERIFY(!writeManaged(makeValidProfile("id-22222222", "Same")).isEmpty());
        ProfileController controller;
        controller.refreshCatalog();
        const QVariantList catalog = controller.profileCatalog();
        QCOMPARE(catalog.size(), 2);
        // Internal identity is always the full profileId; both are listed.
        const QString id1 = catalog.at(0).toMap().value("profileId").toString();
        const QString id2 = catalog.at(1).toMap().value("profileId").toString();
        QVERIFY(id1 != id2);
        // The secondary text disambiguates with a short profileId.
        QVERIFY(!catalog.at(0).toMap().value("displaySecondary").toString()
                     .isEmpty());
        QVERIFY(!catalog.at(1).toMap().value("displaySecondary").toString()
                     .isEmpty());
        QVERIFY(catalog.at(0).toMap().value("displaySecondary").toString()
                != catalog.at(1).toMap().value("displaySecondary").toString());
    }

    void b1c05_malformedDoesNotBlockValidProfiles()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-good")).isEmpty());
        // A corrupt managed file lands in the same directory.
        const QString badPath = ProfileStore::defaultFilePathFor("id-bad");
        QFile bad(badPath);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{\"schemaVersion\":1,\"profileId\":");
        bad.close();
        ProfileController controller;
        controller.refreshCatalog();
        QCOMPARE(controller.profileCatalog().size(), 1);
        QCOMPARE(controller.profileCatalog().first().toMap()
                     .value("profileId").toString(),
                 QStringLiteral("id-good"));
    }

    void b1c06_malformedFileProducesIssue()
    {
        const QString badPath = ProfileStore::defaultFilePathFor("id-bad");
        QFile bad(badPath);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not json");
        bad.close();
        ProfileController controller;
        controller.refreshCatalog();
        QCOMPARE(controller.catalogIssueCount(), 1);
        QVERIFY(!controller.catalogIssueText().isEmpty());
        // The bad file still exists (never auto-deleted or repaired).
        QVERIFY(QFile::exists(badPath));
    }

    void b1c07_newGeneratesStableProfileId()
    {
        ProfileController controller;
        controller.newProfile();
        QVERIFY(controller.hasOpenProfile());
        QVERIFY(!controller.currentProfileId().isEmpty());
        QCOMPARE(controller.displayName(), QString());
        // Untouched new draft is not dirty (nothing was modified).
        QVERIFY(!controller.dirty());
    }

    void b1c08_newIdsDiffer()
    {
        ProfileController controller;
        controller.newProfile();
        const QString first = controller.currentProfileId();
        controller.discardCurrentChanges();
        controller.newProfile();
        QVERIFY(controller.currentProfileId() != first);
    }

    void b1c09_displayNameEditKeepsProfileId()
    {
        ProfileController controller;
        controller.newProfile();
        const QString id = controller.currentProfileId();
        controller.setDisplayName(QStringLiteral("My device"));
        QCOMPARE(controller.currentProfileId(), id);
    }

    void b1c10_optionalEditKeepsProfileId()
    {
        ProfileController controller;
        controller.newProfile();
        const QString id = controller.currentProfileId();
        controller.setManufacturer(QStringLiteral("ACME"));
        controller.setModel(QStringLiteral("M-1"));
        controller.setRevision(QStringLiteral("rev B"));
        controller.setDescription(QStringLiteral("desc"));
        QCOMPARE(controller.currentProfileId(), id);
    }

    void b1c11_identityEditSetsDirty()
    {
        ProfileController controller;
        controller.newProfile();
        QVERIFY(!controller.dirty());
        controller.setDisplayName(QStringLiteral("Named"));
        QVERIFY(controller.dirty());
    }

    void b1c12_saveClearsDirty()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Saved device"));
        QVERIFY(controller.dirty());
        QVERIFY(controller.saveCurrent());
        QVERIFY(!controller.dirty());
        // The managed file exists in the injected root.
        QVERIFY(QFile::exists(ProfileStore::defaultFilePathFor(
            controller.currentProfileId())));
    }

    void b1c13_saveInvalidDisplayNameFails()
    {
        ProfileController controller;
        controller.newProfile();
        QVERIFY(!controller.saveCurrent());
        QVERIFY(!controller.lastActionError().isEmpty());
    }

    void b1c14_saveFailureKeepsDirty()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Named"));
        // Corrupt the managed root: the file cannot be written (the target
        // "directory" is now a regular file).
        const QString root = ProfileStore::managedProfilesDirectory();
        const QString blocker = root + QStringLiteral(".lockfile");
        QVERIFY(QFile(blocker).open(QIODevice::WriteOnly));
        // A nested path whose parent is a file makes mkpath fail.
        ProfileStore::setManagedRootOverride(blocker);
        controller.setDisplayName(QStringLiteral("Named 2"));
        QVERIFY(controller.dirty());
        QVERIFY(!controller.saveCurrent());
        QVERIFY(controller.dirty());
        QVERIFY(!controller.lastActionError().isEmpty());
        ProfileStore::setManagedRootOverride(root);
    }

    void b1c15_openValidProfile()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-open")).isEmpty());
        ProfileController controller;
        controller.openProfile(QStringLiteral("id-open"));
        QVERIFY(controller.hasOpenProfile());
        QCOMPARE(controller.displayName(), QStringLiteral("Inverter A"));
        QVERIFY(!controller.dirty());
    }

    void b1c16_openFailureKeepsDraft()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Current draft"));
        QVERIFY(!controller.openProfile(QStringLiteral("missing-id")));
        QVERIFY(!controller.lastActionError().isEmpty());
        // The draft is untouched: still open, still the same identity/text.
        QVERIFY(controller.hasOpenProfile());
        QCOMPARE(controller.displayName(), QStringLiteral("Current draft"));
    }

    void b1c17_deleteRemovesManagedFile()
    {
        const QString path =
            !writeManaged(makeValidProfile("id-del")).isEmpty()
                ? ProfileStore::defaultFilePathFor(QStringLiteral("id-del"))
                : QString();
        QVERIFY(!path.isEmpty());
        QVERIFY(QFile::exists(path));
        ProfileController controller;
        QVERIFY(controller.deleteProfile(QStringLiteral("id-del")));
        QVERIFY(!QFile::exists(path));
        controller.refreshCatalog();
        QCOMPARE(controller.profileCatalog().size(), 0);
    }

    void b1c18_deleteFailureNotReportedAsSuccess()
    {
        ProfileController controller;
        // Deleting a profile that does not exist must fail loudly.
        QVERIFY(!controller.deleteProfile(QStringLiteral("id-missing")));
        QVERIFY(!controller.lastActionError().isEmpty());
    }

    void b1c19_catalogRefreshAfterSaveAndDelete()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Lifecycle"));
        QVERIFY(controller.saveCurrent());
        controller.refreshCatalog();
        QCOMPARE(controller.profileCatalog().size(), 1);
        const QString id = controller.currentProfileId();
        QVERIFY(controller.deleteProfile(id));
        controller.refreshCatalog();
        QCOMPARE(controller.profileCatalog().size(), 0);
        // Deleting the open profile cleared the editor.
        QVERIFY(!controller.hasOpenProfile());
    }

    void b1c20_injectedRootIsTestRoot()
    {
        // The controller operations land in the INJECTED root, not the
        // production user-data location: saving here, then checking the
        // production location stays profile-free.
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Injected root only"));
        QVERIFY(controller.saveCurrent());
        QVERIFY(controller.profileCatalog().size() == 1);
        // The production user-data location would normally contain the
        // managed root; the injected root is somewhere else entirely.
        const QString production =
            QDir(QStandardPaths::writableLocation(
                     QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("profiles"));
        QVERIFY(ProfileStore::managedProfilesDirectory() != production);
        // And the production location has no managed files from this test.
        const QFileInfoList productionFiles =
            QDir(production).entryInfoList(
                QStringList{QStringLiteral("profile-*.json")}, QDir::Files);
        QCOMPARE(int(productionFiles.size()), 0);
    }

    // ------------------------------------------------------------------
    // Notification contract. The QML workspace binds the dirty cue, the
    // validation label and the editor fields directly to these properties,
    // so a state change without its NOTIFY signal is invisible in the UI even
    // though every getter already returns the new value. This group is the
    // automated regression protection for exactly that failure mode (the
    // committed first slice shipped setDisplayName() without its emit; only
    // the QML gate could see it).
    // ------------------------------------------------------------------
    void b1c21_displayNameEditNotifiesEditorChanged()
    {
        ProfileController controller;
        controller.newProfile();
        QSignalSpy spy(&controller, &ProfileController::editorChanged);
        controller.setDisplayName(QStringLiteral("Named"));
        QCOMPARE(spy.count(), 1);
        // Idempotent write: no notification, no spurious binding refresh.
        controller.setDisplayName(QStringLiteral("Named"));
        QCOMPARE(spy.count(), 1);
    }

    void b1c22_everyIdentitySetterNotifies()
    {
        ProfileController controller;
        controller.newProfile();
        QSignalSpy spy(&controller, &ProfileController::editorChanged);
        controller.setDisplayName(QStringLiteral("n"));
        controller.setManufacturer(QStringLiteral("m"));
        controller.setModel(QStringLiteral("mo"));
        controller.setRevision(QStringLiteral("r"));
        controller.setDescription(QStringLiteral("d"));
        QCOMPARE(spy.count(), 5);
        // Lifecycle commands notify too (the fields must follow the new
        // profile after New/Open/Discard).
        controller.discardCurrentChanges();
        controller.newProfile();
        QVERIFY(spy.count() >= 7);
    }

    // ------------------------------------------------------------------
    // Dirty-action state machine (T027 §33.2). The QML dialog resolves
    // Save / Discard / Cancel through exactly these controller calls; the UI
    // wiring is gated by --qml-profile-editor-check, the semantics below are
    // gated here, deterministically and headless.
    // ------------------------------------------------------------------
    void b1c23_dirtyOpenCancelKeepsDraft()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-cancel", "Target")).isEmpty());
        ProfileController controller;
        QVERIFY(controller.openProfile(QStringLiteral("id-cancel")));
        controller.setDisplayName(QStringLiteral("Edited draft"));
        QVERIFY(controller.dirty());
        // Cancel performs NO controller call: everything the user typed is
        // still there and nothing was written.
        QCOMPARE(controller.displayName(), QStringLiteral("Edited draft"));
        QCOMPARE(controller.currentProfileId(), QStringLiteral("id-cancel"));
        QVERIFY(controller.dirty());
        const auto onDisk = ProfileStore::loadFromFile(
            ProfileStore::defaultFilePathFor(QStringLiteral("id-cancel")));
        QVERIFY(onDisk.ok());
        QCOMPARE(QString::fromStdString(onDisk.profile.displayName),
                 QStringLiteral("Target"));
    }

    void b1c24_dirtyOpenDiscardOpensTarget()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-a", "First")).isEmpty());
        QVERIFY(!writeManaged(makeValidProfile("id-b", "Second")).isEmpty());
        ProfileController controller;
        QVERIFY(controller.openProfile(QStringLiteral("id-a")));
        controller.setDisplayName(QStringLiteral("Discarded edit"));
        QVERIFY(controller.dirty());
        controller.discardCurrentChanges();
        QVERIFY(controller.openProfile(QStringLiteral("id-b")));
        QCOMPARE(controller.currentProfileId(), QStringLiteral("id-b"));
        QCOMPARE(controller.displayName(), QStringLiteral("Second"));
        QVERIFY(!controller.dirty());
        // Discard is not Save: the abandoned edit never reached disk.
        const auto first = ProfileStore::loadFromFile(
            ProfileStore::defaultFilePathFor(QStringLiteral("id-a")));
        QVERIFY(first.ok());
        QCOMPARE(QString::fromStdString(first.profile.displayName),
                 QStringLiteral("First"));
    }

    void b1c25_dirtyOpenSaveSuccessSavesThenOpensTarget()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-a", "First")).isEmpty());
        QVERIFY(!writeManaged(makeValidProfile("id-b", "Second")).isEmpty());
        ProfileController controller;
        QVERIFY(controller.openProfile(QStringLiteral("id-a")));
        controller.setDisplayName(QStringLiteral("Renamed"));
        QVERIFY(controller.dirty());
        // Save branch: the save must land BEFORE the pending action runs.
        QVERIFY(controller.saveCurrent());
        QVERIFY(!controller.dirty());
        const auto saved = ProfileStore::loadFromFile(
            ProfileStore::defaultFilePathFor(QStringLiteral("id-a")));
        QVERIFY(saved.ok());
        QCOMPARE(QString::fromStdString(saved.profile.displayName),
                 QStringLiteral("Renamed"));
        QVERIFY(controller.openProfile(QStringLiteral("id-b")));
        QCOMPARE(controller.displayName(), QStringLiteral("Second"));
        QVERIFY(controller.profileCatalog().size() == 2);
    }

    void b1c26_dirtyOpenSaveFailureBlocksPendingAction()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-a", "First")).isEmpty());
        QVERIFY(!writeManaged(makeValidProfile("id-b", "Second")).isEmpty());
        ProfileController controller;
        QVERIFY(controller.openProfile(QStringLiteral("id-a")));
        // Invalid draft (required displayName empty): the save cannot succeed.
        controller.setDisplayName(QString());
        QVERIFY(controller.dirty());
        QVERIFY(!controller.saveCurrent());
        QVERIFY(!controller.lastActionError().isEmpty());
        // The pending action is blocked: still on the dirty original profile.
        QCOMPARE(controller.currentProfileId(), QStringLiteral("id-a"));
        QVERIFY(controller.dirty());
        QVERIFY(controller.hasOpenProfile());
        const auto untouched = ProfileStore::loadFromFile(
            ProfileStore::defaultFilePathFor(QStringLiteral("id-a")));
        QVERIFY(untouched.ok());
        QCOMPARE(QString::fromStdString(untouched.profile.displayName),
                 QStringLiteral("First"));
    }

    void b1c27_saveIoFailureKeepsDraftAndFile()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-a", "First")).isEmpty());
        ProfileController controller;
        QVERIFY(controller.openProfile(QStringLiteral("id-a")));
        controller.setDisplayName(QStringLiteral("Not written"));
        QVERIFY(controller.dirty());
        // A valid draft that cannot be written: the managed root's parent is
        // now a regular file, so mkpath/save fail.
        const QString root = ProfileStore::managedProfilesDirectory();
        const QString blocker = root + QStringLiteral(".blockedfile");
        QVERIFY(QFile(blocker).open(QIODevice::WriteOnly));
        ProfileStore::setManagedRootOverride(blocker);
        QVERIFY(!controller.saveCurrent());
        ProfileStore::setManagedRootOverride(root);
        QVERIFY(controller.dirty());
        QVERIFY(!controller.lastActionError().isEmpty());
        QCOMPARE(controller.displayName(), QStringLiteral("Not written"));
        const auto onDisk = ProfileStore::loadFromFile(
            ProfileStore::defaultFilePathFor(QStringLiteral("id-a")));
        QVERIFY(onDisk.ok());
        QCOMPARE(QString::fromStdString(onDisk.profile.displayName),
                 QStringLiteral("First"));
    }

    void b1c28_deleteRequiresExactExistingId()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-a", "First")).isEmpty());
        QVERIFY(!writeManaged(makeValidProfile("id-b", "Second")).isEmpty());
        ProfileController controller;
        QVERIFY(controller.openProfile(QStringLiteral("id-a")));
        QCOMPARE(controller.profileCatalog().size(), 2);
        // The confirmation dialog passes the EXACT selected id; anything else
        // must fail loudly and delete nothing.
        QVERIFY(!controller.deleteProfile(QStringLiteral("id-a ")));
        QVERIFY(!controller.lastActionError().isEmpty());
        QCOMPARE(controller.profileCatalog().size(), 2);
        QVERIFY(controller.hasOpenProfile());
    }

    void b1c29_deleteSuccessRemovesFileAndClearsEditor()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-a", "First")).isEmpty());
        QVERIFY(!writeManaged(makeValidProfile("id-b", "Second")).isEmpty());
        const QString path = ProfileStore::defaultFilePathFor(
            QStringLiteral("id-a"));
        ProfileController controller;
        QVERIFY(controller.openProfile(QStringLiteral("id-a")));
        QVERIFY(controller.deleteProfile(QStringLiteral("id-a")));
        QVERIFY(!QFile::exists(path));
        QCOMPARE(controller.profileCatalog().size(), 1);
        // Deleting the open profile returns the editor to "No Profile Opened".
        QVERIFY(!controller.hasOpenProfile());
        QVERIFY(controller.lastActionError().isEmpty());
    }

    void b1c30_deleteFailureKeepsProfile()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-a", "First")).isEmpty());
        // A managed entry that cannot be removed: the path is a directory.
        const QString blocked =
            ProfileStore::defaultFilePathFor(QStringLiteral("id-dir"));
        QVERIFY(QDir().mkpath(blocked));
        ProfileController controller;
        QVERIFY(!controller.deleteProfile(QStringLiteral("id-dir")));
        QVERIFY(!controller.lastActionError().isEmpty());
        QVERIFY(QFileInfo(blocked).isDir());
        // The valid profile is untouched by the failed delete.
        QCOMPARE(controller.profileCatalog().size(), 1);
        QVERIFY(QFile::exists(ProfileStore::defaultFilePathFor(
            QStringLiteral("id-a"))));
    }

    void b1c31_exitGuardCancelKeepsDenial()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-a", "First")).isEmpty());
        ProfileController controller;
        QVERIFY(controller.openProfile(QStringLiteral("id-a")));
        controller.setDisplayName(QStringLiteral("Exit draft"));
        // The Main.qml onClosing guard predicate.
        QVERIFY(controller.hasOpenProfile() && controller.dirty());
        // Cancel performs no controller call: a second close request is still
        // denied.
        QVERIFY(controller.hasOpenProfile() && controller.dirty());
        // Only Discard (or a successful Save) releases the guard.
        controller.discardCurrentChanges();
        QVERIFY(!(controller.hasOpenProfile() && controller.dirty()));
    }

    void b1c32_exitGuardSaveFailureKeepsDenial()
    {
        QVERIFY(!writeManaged(makeValidProfile("id-a", "First")).isEmpty());
        ProfileController controller;
        QVERIFY(controller.openProfile(QStringLiteral("id-a")));
        controller.setDisplayName(QString());
        QVERIFY(controller.hasOpenProfile() && controller.dirty());
        // A failed save must NOT release the exit guard.
        QVERIFY(!controller.saveCurrent());
        QVERIFY(!controller.lastActionError().isEmpty());
        QVERIFY(controller.hasOpenProfile() && controller.dirty());
        QVERIFY(controller.profileCatalog().size() == 1);
    }
};

QTEST_GUILESS_MAIN(ProfileControllerTest)
#include "test_profile_controller.moc"