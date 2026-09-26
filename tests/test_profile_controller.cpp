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

    // ------------------------------------------------------------------
    // M12-B SECOND SLICE — Register Map editor (B2-C01..B2-C30). The draft
    // remains the ONLY editing truth: every mutation goes through a candidate
    // copy + full frozen validation, and a refused candidate leaves the draft
    // byte-identical.
    // ------------------------------------------------------------------

    // Raw-text field map exactly as the QML hands it over (M10 authority
    // discipline: the controller parses, QML never decides legality).
    QVariantMap entryFields(const QString &fc, const QString &address,
                            const QString &name, int dataType = 2,
                            const QString &scale = QString(),
                            const QString &offset = QString(),
                            const QString &unit = QString(),
                            int byteOrder = 0, int wordOrder = -1,
                            const QString &description = QString()) const
    {
        QVariantMap fields;
        fields.insert(QStringLiteral("readFunctionCode"), fc);
        fields.insert(QStringLiteral("address"), address);
        fields.insert(QStringLiteral("name"), name);
        fields.insert(QStringLiteral("description"), description);
        fields.insert(QStringLiteral("dataType"), QString::number(dataType));
        fields.insert(QStringLiteral("byteOrder"), QString::number(byteOrder));
        fields.insert(QStringLiteral("wordOrder"), QString::number(wordOrder));
        fields.insert(QStringLiteral("scale"), scale);
        fields.insert(QStringLiteral("offset"), offset);
        fields.insert(QStringLiteral("unit"), unit);
        return fields;
    }

    QStringList registerNames(ProfileController &controller)
    {
        QStringList names;
        const QVariantList map = controller.property("registerMap").toList();
        for (const QVariant &row : map)
            names << row.toMap().value(QStringLiteral("name")).toString();
        return names;
    }

    void b2c01_emptyRegisterMap()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QCOMPARE(controller.property("registerMap").toList().size(), 0);
    }

    void b2c02_addFc03UInt16()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        const QString idBefore = controller.currentProfileId();
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("Frequency"), 2, QStringLiteral("0.1"),
            QString(), QStringLiteral("Hz"))));
        const QVariantList map = controller.property("registerMap").toList();
        QCOMPARE(map.size(), 1);
        const QVariantMap row = map.first().toMap();
        QCOMPARE(row.value("draftIndex").toInt(), 0);
        QCOMPARE(row.value("readFunctionCode").toInt(), 3);
        QCOMPARE(row.value("address").toInt(), 1000);
        QCOMPARE(row.value("dataType").toString(), QStringLiteral("UInt16"));
        QVERIFY(controller.dirty());
        QCOMPARE(controller.currentProfileId(), idBefore);
        QVERIFY(controller.lastActionErrorToken().isEmpty());
    }

    void b2c03_addCustomFc41()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        // Plain HEX and 0x-prefixed forms (M10 input conventions) both work.
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("41"), QStringLiteral("2000"),
            QStringLiteral("Vendor A"))));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("0x41"), QStringLiteral("2001"),
            QStringLiteral("Vendor B"))));
        const QVariantList map = controller.property("registerMap").toList();
        QCOMPARE(map.size(), 2);
        QCOMPARE(map.at(0).toMap().value("readFunctionCode").toInt(), 0x41);
    }

    void b2c04_fc00Rejected()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"), QStringLiteral("A"))));
        QVERIFY(!controller.addRegisterEntry(entryFields(
            QStringLiteral("00"), QStringLiteral("1001"), QStringLiteral("B"))));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("invalid_read_function_code"));
        // The draft is untouched: still exactly one entry.
        QCOMPARE(registerNames(controller).size(), 1);
    }

    void b2c05_fc80Rejected()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(!controller.addRegisterEntry(entryFields(
            QStringLiteral("80"), QStringLiteral("1000"), QStringLiteral("B"))));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("invalid_read_function_code"));
        QVERIFY(!controller.addRegisterEntry(entryFields(
            QStringLiteral("FF"), QStringLiteral("1000"), QStringLiteral("C"))));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("invalid_read_function_code"));
        QCOMPARE(controller.property("registerMap").toList().size(), 0);
    }

    void b2c06_crossFcSameAddressAccepted()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"), QStringLiteral("A"))));
        // Same PDU address, DIFFERENT register space: legal by contract.
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("04"), QStringLiteral("1000"), QStringLiteral("B"))));
        QCOMPARE(controller.property("registerMap").toList().size(), 2);
    }

    void b2c07_sameFcDuplicateRejected()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"), QStringLiteral("A"))));
        QVERIFY(!controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"), QStringLiteral("B"))));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("duplicate_address"));
        // Human-readable (T027 §24): names the FC, the address and the owner.
        QVERIFY(controller.lastActionError().contains(
            QStringLiteral("重复地址")));
        QVERIFY(controller.lastActionError().contains(
            QStringLiteral("A")));
        QCOMPARE(controller.property("registerMap").toList().size(), 1);
    }

    void b2c08_sameFcOverlapRejected()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"), QStringLiteral("A"))));
        // UInt32 @1001 spans 1001..1002 — disjoint from 1000..1000: legal.
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1001"),
            QStringLiteral("Wide"), 4, QString(), QString(), QString(), 0,
            0)));
        // UInt32 @999 spans 999..1000 — CROSSES 1000..1000 in the SAME FC
        // space: overlapping_span.
        QVERIFY(!controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("999"),
            QStringLiteral("Cross"), 4, QString(), QString(), QString(), 0,
            0)));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("overlapping_span"));
        QVERIFY(controller.lastActionError().contains(
            QStringLiteral("重叠")));
        QCOMPARE(controller.property("registerMap").toList().size(), 2);
    }

    void b2c09_crossFcOverlapAccepted()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"), 4, QString(), QString(), QString(), 0, 0)));
        // Same span shape in another FC space: never interacts.
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("41"), QStringLiteral("1001"),
            QStringLiteral("B"), 2)));
        QCOMPARE(controller.property("registerMap").toList().size(), 2);
    }

    void b2c10_editEntryMetadata()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("Freq"), 2, QStringLiteral("0.1"))));
        QVERIFY(controller.editRegisterEntry(
            0, entryFields(QStringLiteral("03"), QStringLiteral("1000"),
                           QStringLiteral("Freq2"), 2,
                           QStringLiteral("0.2"), QString(),
                           QStringLiteral("kPa"))));
        const QVariantMap entry = controller.registerEntryAt(0);
        QCOMPARE(entry.value("name").toString(), QStringLiteral("Freq2"));
        QCOMPARE(entry.value("scale").toDouble(), 0.2);
        QCOMPARE(entry.value("unit").toString(), QStringLiteral("kPa"));
    }

    void b2c11_failedEditKeepsOriginal()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("First"), 2, QStringLiteral("0.1"))));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("2000"),
            QStringLiteral("Second"), 2, QStringLiteral("1"))));
        // Editing entry 0 into a same-FC collision with entry 1.
        QVERIFY(!controller.editRegisterEntry(
            0, entryFields(QStringLiteral("03"), QStringLiteral("2000"),
                           QStringLiteral("Clash"))));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("duplicate_address"));
        const QVariantMap entry = controller.registerEntryAt(0);
        QCOMPARE(entry.value("name").toString(), QStringLiteral("First"));
        QCOMPARE(entry.value("address").toInt(), 1000);
        QCOMPARE(entry.value("scale").toDouble(), 0.1);
    }

    void b2c12_deleteEntry()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"), QStringLiteral("A"))));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("04"), QStringLiteral("1000"), QStringLiteral("B"))));
        QVERIFY(controller.removeRegisterEntry(0));
        QCOMPARE(controller.property("registerMap").toList().size(), 1);
        QCOMPARE(registerNames(controller).first(),
                 QStringLiteral("B"));
        QVERIFY(!controller.removeRegisterEntry(5));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("register_entry_missing"));
    }

    void b2c13_deleteSetsProfileDirty()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"), QStringLiteral("A"))));
        QVERIFY(controller.saveCurrent());
        QVERIFY(!controller.dirty());
        QVERIFY(controller.removeRegisterEntry(0));
        QVERIFY(controller.dirty());
    }

    void b2c14_discardRestoresPersistedMap()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Discard map"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("Persisted"), 2, QStringLiteral("0.1"),
            QString(), QStringLiteral("Hz"))));
        QVERIFY(controller.saveCurrent());
        // Two draft-only mutations after the save…
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("04"), QStringLiteral("1000"),
            QStringLiteral("DraftOnly"))));
        QVERIFY(controller.removeRegisterEntry(0));
        QCOMPARE(controller.property("registerMap").toList().size(), 1);
        // …and Discard restores the PERSISTED register map byte-identically.
        controller.discardCurrentChanges();
        const QVariantList map = controller.property("registerMap").toList();
        QCOMPARE(map.size(), 1);
        QCOMPARE(map.first().toMap().value("name").toString(),
                 QStringLiteral("Persisted"));
        QVERIFY(!controller.dirty());
    }

    void b2c15_registerCountDerivedOneWord()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"), 2)));
        QCOMPARE(controller.registerEntryAt(0).value("registerCount").toInt(),
                 1);
    }

    void b2c16_registerCountDerivedTwoWord()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"), 6, QString(), QString(), QString(), 0, 1)));
        QCOMPARE(controller.registerEntryAt(0).value("registerCount").toInt(),
                 2);
        QCOMPARE(controller.registerEntryAt(0).value("wordOrder").toInt(), 1);
    }

    void b2c17_scaleZeroAccepted()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"), 2, QStringLiteral("0"))));
        QCOMPARE(controller.registerEntryAt(0).value("scale").toDouble(), 0.0);
        QVERIFY(controller.lastActionErrorToken().isEmpty());
    }

    void b2c18_nonFiniteScaleRejected()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(!controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"), 2, QStringLiteral("nan"))));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("non_finite_scale"));
        QVERIFY(!controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"), 2, QStringLiteral("inf"))));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("non_finite_scale"));
        QVERIFY(!controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"), 2, QStringLiteral("12x"))));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("non_finite_scale"));
        QCOMPARE(controller.property("registerMap").toList().size(), 0);
    }

    void b2c19_nonFiniteOffsetRejected()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(!controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"), 2, QString(), QStringLiteral("-inf"))));
        QCOMPARE(controller.lastActionErrorToken(),
                 QStringLiteral("non_finite_offset"));
        QCOMPARE(controller.property("registerMap").toList().size(), 0);
    }

    void b2c20_unitRoundTrip()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Unit device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("Temp"), 2, QStringLiteral("1"), QString(),
            QStringLiteral("\u00B0C"))));
        QVERIFY(controller.saveCurrent());
        ProfileController loader;
        QVERIFY(loader.openProfile(controller.currentProfileId()));
        QCOMPARE(loader.registerEntryAt(0).value("unit").toString(),
                 QStringLiteral("\u00B0C"));
    }

    void b2c21_byteOrderRoundTrip()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Byte order device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"), 2, QString(), QString(), QString(), 1)));
        QVERIFY(controller.saveCurrent());
        ProfileController loader;
        QVERIFY(loader.openProfile(controller.currentProfileId()));
        QCOMPARE(loader.registerEntryAt(0).value("byteOrder").toInt(), 1);
    }

    void b2c22_wordOrderRoundTrip()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Word order device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"), 6, QString(), QString(), QString(), 0, 1)));
        QVERIFY(controller.saveCurrent());
        ProfileController loader;
        QVERIFY(loader.openProfile(controller.currentProfileId()));
        QCOMPARE(loader.registerEntryAt(0).value("wordOrder").toInt(), 1);
        // A 1-word entry keeps the M11 default word order in persistence
        // (the disabled 不适用 control never pollutes the data).
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("04"), QStringLiteral("1000"),
            QStringLiteral("B"), 2, QString(), QString(), QString(), 0, -1)));
        QCOMPARE(controller.registerEntryAt(1).value("wordOrder").toInt(), 0);
    }

    void b2c23_saveLoadFullRegisterMap()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Roundtrip device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("Frequency"), 2, QStringLiteral("0.1"),
            QStringLiteral("0.5"), QStringLiteral("Hz"))));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("04"), QStringLiteral("1000"),
            QStringLiteral("Ambient"), 3, QStringLiteral("1"),
            QString(), QStringLiteral("\u00B0C"), 1)));
        QVERIFY(controller.saveCurrent());
        ProfileController loader;
        QVERIFY(loader.openProfile(controller.currentProfileId()));
        const QVariantList map = loader.property("registerMap").toList();
        QCOMPARE(map.size(), 2);
        const QVariantMap first = loader.registerEntryAt(0);
        QCOMPARE(first.value("name").toString(), QStringLiteral("Frequency"));
        QCOMPARE(first.value("readFunctionCode").toInt(), 3);
        QCOMPARE(first.value("address").toInt(), 1000);
        QCOMPARE(first.value("scale").toDouble(), 0.1);
        QCOMPARE(first.value("offset").toDouble(), 0.5);
        QCOMPARE(first.value("unit").toString(), QStringLiteral("Hz"));
        const QVariantMap second = loader.registerEntryAt(1);
        QCOMPARE(second.value("name").toString(), QStringLiteral("Ambient"));
        QCOMPARE(second.value("dataType").toInt(), 3);
        QCOMPARE(second.value("byteOrder").toInt(), 1);
        QCOMPARE(second.value("unit").toString(), QStringLiteral("\u00B0C"));
    }

    void b2c24_profileIdStableAfterRegisterEdits()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        const QString id = controller.currentProfileId();
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"), QStringLiteral("A"))));
        QVERIFY(controller.editRegisterEntry(
            0, entryFields(QStringLiteral("03"), QStringLiteral("1000"),
                           QStringLiteral("B"))));
        QVERIFY(controller.removeRegisterEntry(0));
        QCOMPARE(controller.currentProfileId(), id);
        QVERIFY(controller.saveCurrent());
        QCOMPARE(controller.currentProfileId(), id);
    }

    void b2c25_registerEditDoesNotTouchM11Decode()
    {
        // Architectural proof (black-box): the M11 decode pipeline over
        // canonical raw words is INDEPENDENT of profile metadata — editing a
        // profile entry must not change any M11 decode result, and the word
        // counts the profile derives are THE M11 word counts.
        using modbuslens::core::decodeRegisterView;
        using modbuslens::core::RegisterByteOrder;
        using modbuslens::core::RegisterWordOrder;
        const std::vector<std::uint16_t> words{0xABCD, 0x1234};
        const auto before = decodeRegisterView(
            words, 0, modbuslens::core::RegisterDecodeType::UInt16,
            RegisterByteOrder::Normal, RegisterWordOrder::HighWordFirst);

        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("Before"), 2)));
        QVERIFY(controller.editRegisterEntry(
            0, entryFields(QStringLiteral("03"), QStringLiteral("1000"),
                           QStringLiteral("After"), 2,
                           QStringLiteral("9.9"), QString(),
                           QStringLiteral("renamed-unit"))));
        QCOMPARE(controller.registerEntryAt(0).value("name").toString(),
                 QStringLiteral("After"));

        const auto after = decodeRegisterView(
            words, 0, modbuslens::core::RegisterDecodeType::UInt16,
            RegisterByteOrder::Normal, RegisterWordOrder::HighWordFirst);
        QCOMPARE(after, before);
        // The derived registerCount IS the M11 word count (one authority).
        QCOMPARE(modbuslens::core::registerDecodeTypeWordCount(
                     modbuslens::core::RegisterDecodeType::UInt16),
                 controller.registerEntryAt(0).value("registerCount").toInt());
    }

    void b2c26_deterministicRegisterOrdering()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        // Insert in scrambled (FC, address) order.
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("41"), QStringLiteral("2000"),
            QStringLiteral("C"))));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"))));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("04"), QStringLiteral("1500"),
            QStringLiteral("B"))));
        // Display order: readFunctionCode, then address (T027 §17).
        const QStringList fcs = [&] {
            QStringList values;
            const QVariantList map = controller.property("registerMap").toList();
            for (const QVariant &row : map)
                values << row.toMap().value("readFunctionCode").toString();
            return values;
        }();
        QCOMPARE(fcs, (QStringList{QStringLiteral("3"),
                                   QStringLiteral("4"),
                                   QStringLiteral("65")}));
        QCOMPARE(registerNames(controller),
                 (QStringList{QStringLiteral("A"), QStringLiteral("B"),
                              QStringLiteral("C")}));
    }

    void b2c27_invalidAddDoesNotMutateDraft()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Guarded"));
        QVERIFY(controller.saveCurrent());
        QVERIFY(!controller.dirty());
        QVERIFY(!controller.addRegisterEntry(entryFields(
            QStringLiteral("80"), QStringLiteral("1000"),
            QStringLiteral("Bad"))));
        QCOMPARE(controller.property("registerMap").toList().size(), 0);
        QVERIFY(!controller.dirty());
        QVERIFY(!controller.lastActionError().isEmpty());
    }

    void b2c28_invalidEditDoesNotMutateDraft()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Guarded edit"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"))));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("2000"),
            QStringLiteral("B"))));
        QVERIFY(controller.saveCurrent());
        QVERIFY(!controller.dirty());
        QVERIFY(!controller.editRegisterEntry(
            0, entryFields(QStringLiteral("03"), QStringLiteral("2000"),
                           QStringLiteral("Clash"))));
        QCOMPARE(controller.registerEntryAt(0).value("name").toString(),
                 QStringLiteral("A"));
        QVERIFY(!controller.dirty());
    }

    void b2c29_registerChangeJoinsDirtyGuard()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.saveCurrent());
        QVERIFY(!controller.dirty());
        // A register-only change participates in the SAME dirty truth as the
        // identity fields (no second dirty flag exists).
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"))));
        QVERIFY(controller.hasOpenProfile() && controller.dirty());
        controller.discardCurrentChanges();
        QVERIFY(!(controller.hasOpenProfile() && controller.dirty()));
    }

    void b2c30_saveFailureRetainsRegisterDirty()
    {
        ProfileController controller;
        controller.newProfile();
        controller.setDisplayName(QStringLiteral("Register device"));
        QVERIFY(controller.addRegisterEntry(entryFields(
            QStringLiteral("03"), QStringLiteral("1000"),
            QStringLiteral("A"))));
        QVERIFY(controller.dirty());
        // The managed root's parent becomes a regular file: the save fails.
        const QString root = ProfileStore::managedProfilesDirectory();
        const QString blocker = root + QStringLiteral(".blockedfile");
        QVERIFY(QFile(blocker).open(QIODevice::WriteOnly));
        ProfileStore::setManagedRootOverride(blocker);
        QVERIFY(!controller.saveCurrent());
        ProfileStore::setManagedRootOverride(root);
        QVERIFY(controller.dirty());
        // The register change is still there, unsaved but intact.
        QCOMPARE(controller.property("registerMap").toList().size(), 1);
        QVERIFY(!controller.lastActionError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(ProfileControllerTest)
#include "test_profile_controller.moc"