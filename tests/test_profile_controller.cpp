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
};

QTEST_GUILESS_MAIN(ProfileControllerTest)
#include "test_profile_controller.moc"