#include <QtTest>

#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariantMap>

#include "core/profile/DeviceProfile.h"
#include "ui/profile/ActiveProfileController.h"
#include "ui/profile/ProfileController.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::core::DeviceProfile;
using modbuslens::core::RegisterDecodeType;
using modbuslens::core::RegisterEntry;
using modbuslens::ui::ActiveProfileController;
using modbuslens::ui::ProfileController;
using modbuslens::ui::ProfileStore;

namespace {

// A valid profile with a two-entry cross-FC register map (FC03 + FC04 at the
// SAME PDU address — legal by contract and exactly the shape the active
// content must preserve).
DeviceProfile makeProfile(const QString &profileId, const QString &displayName,
                          const QString &manufacturer = "ACME")
{
    DeviceProfile profile;
    profile.schemaVersion = 1;
    profile.profileId = profileId.toStdString();
    profile.displayName = displayName.toStdString();
    profile.manufacturer = manufacturer.toStdString();
    profile.model = "INV-1000";
    RegisterEntry fc03;
    fc03.readFunctionCode = 0x03;
    fc03.address = 1000;
    fc03.name = "Frequency";
    fc03.dataType = RegisterDecodeType::UInt16;
    fc03.registerCount = 1;
    fc03.scale = 0.1;
    fc03.unit = "Hz";
    profile.registers.push_back(fc03);
    RegisterEntry fc04;
    fc04.readFunctionCode = 0x04;
    fc04.address = 1000;
    fc04.name = "Ambient";
    fc04.dataType = RegisterDecodeType::Int16;
    fc04.registerCount = 1;
    fc04.scale = 1.0;
    fc04.offset = 0.0;
    fc04.unit = "\u00B0C";
    profile.registers.push_back(fc04);
    return profile;
}

QString persist(const DeviceProfile &profile)
{
    const QString path = ProfileStore::defaultFilePathFor(
        QString::fromStdString(profile.profileId));
    if (!ProfileStore::saveToFile(profile, path).ok()) {
        return QString();
    }
    return path;
}

QVariantMap activeRegisters(const ActiveProfileController &active)
{
    return active.activeProfile();
}

} // namespace

class ActiveProfileControllerTest : public QObject
{
    Q_OBJECT

    // A fresh profile-owning pair: catalog scanned, nothing active.
    struct Fixture
    {
        ProfileController profiles;
        ActiveProfileController active;
        explicit Fixture()
        {
            active.setProfileController(&profiles);
        }
    };

private slots:
    void init()
    {
        // Every test runs against its OWN injected temporary managed root:
        // automated tests must never touch a real user's profile data.
        const QString root = QDir::temp().filePath(
            QStringLiteral("m12b-b3-test-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QDir().mkpath(root);
        ProfileStore::setManagedRootOverride(root);
    }

    void cleanup()
    {
        ProfileStore::setManagedRootOverride(QString());
    }

    void b3c01_initialActiveIsNone()
    {
        Fixture fx;
        QVERIFY(!fx.active.hasActiveProfile());
        QCOMPARE(fx.active.activeProfileId(), QString());
        QVERIFY(fx.active.activeProfile().isEmpty());
        QVERIFY(fx.active.activeError().isEmpty());
    }

    void b3c02_selectValidProfileById()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        QVERIFY(fx.active.hasActiveProfile());
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
        QVERIFY(fx.active.activeError().isEmpty());
    }

    void b3c03_identityIsTheFullProfileId()
    {
        QVERIFY(!persist(makeProfile("id-alpha-first", "Alpha")).isEmpty());
        Fixture fx;
        // Selecting by the EXACT full id of the second profile must land on
        // that identity — not on a displayName match or a prefix.
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-alpha-first")));
        QCOMPARE(fx.active.activeProfileId(),
                 QStringLiteral("id-alpha-first"));
    }

    void b3c04_clearActive()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.active.clearActive();
        QVERIFY(!fx.active.hasActiveProfile());
        QCOMPARE(fx.active.activeProfileId(), QString());
        QVERIFY(fx.active.activeProfile().isEmpty());
    }

    void b3c05_selectNonexistentRejected()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(!fx.active.selectProfile(QStringLiteral("id-missing")));
        QCOMPARE(fx.active.activeError(),
                 QStringLiteral("设备档案不存在或无法加载"));
    }

    void b3c06_failedSelectionInventsNothing()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(!fx.active.selectProfile(QStringLiteral("id-missing")));
        // No fake active state appeared…
        QVERIFY(!fx.active.hasActiveProfile());
        // …and a previously active profile survives a failed re-selection.
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        QVERIFY(!fx.active.selectProfile(QStringLiteral("id-missing")));
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
    }

    void b3c07_duplicateDisplayNamesSelectableIndependently()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        QVERIFY(!persist(makeProfile("id-b", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-b")));
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-b"));
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
    }

    void b3c08_activeSurvivesUnrelatedStateChanges()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        // Workspace-equivalent churn: catalog rescans and unrelated editor
        // error states never touch the session selection.
        fx.profiles.refreshCatalog();
        fx.profiles.newProfile();
        fx.profiles.discardCurrentChanges();
        QVERIFY(fx.active.hasActiveProfile());
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
    }

    void b3c09_openForEditOtherDoesNotChangeActive()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        QVERIFY(!persist(makeProfile("id-b", "Beta")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        // The editor opens profile B — the ACTIVE identity must stay A.
        QVERIFY(fx.profiles.openProfile(QStringLiteral("id-b")));
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
    }

    void b3c10_newEditorProfileDoesNotChangeActive()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.profiles.newProfile();
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
    }

    void b3c11_unsavedEditorDraftDoesNotMutateActiveContent()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        // A dirty (unsaved) edit of the SAME profile changes NOTHING the
        // active accessor exposes: active content = persisted truth.
        QVERIFY(fx.profiles.openProfile(QStringLiteral("id-a")));
        fx.profiles.setDisplayName(QStringLiteral("Dirty rename"));
        QVERIFY(fx.profiles.dirty());
        QCOMPARE(fx.active.activeProfile().value("displayName").toString(),
                 QStringLiteral("Alpha"));
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
    }

    void b3c12_saveNonActiveDoesNotChangeActive()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        QVERIFY(!persist(makeProfile("id-b", "Beta")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        QVERIFY(fx.profiles.openProfile(QStringLiteral("id-b")));
        fx.profiles.setDisplayName(QStringLiteral("Beta renamed"));
        QVERIFY(fx.profiles.saveCurrent());
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
    }

    void b3c13_saveActiveKeepsIdentity()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        QVERIFY(fx.profiles.openProfile(QStringLiteral("id-a")));
        fx.profiles.setDisplayName(QStringLiteral("Alpha renamed"));
        QVERIFY(fx.profiles.saveCurrent());
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
    }

    void b3c14_saveActiveRefreshesPersistedContent()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        QCOMPARE(fx.active.activeProfile().value("displayName").toString(),
                 QStringLiteral("Alpha"));
        QVERIFY(fx.profiles.openProfile(QStringLiteral("id-a")));
        fx.profiles.setDisplayName(QStringLiteral("Alpha renamed"));
        QVERIFY(fx.profiles.saveCurrent());
        // Same identity, NEW persisted version — the content followed.
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
        QCOMPARE(fx.active.activeProfile().value("displayName").toString(),
                 QStringLiteral("Alpha renamed"));
    }

    void b3c15_deleteNonActiveKeepsActive()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        QVERIFY(!persist(makeProfile("id-b", "Beta")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        QVERIFY(fx.profiles.deleteProfile(QStringLiteral("id-b")));
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
        QVERIFY(fx.active.hasActiveProfile());
    }

    void b3c16_successfulDeleteActiveClears()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        QVERIFY(!persist(makeProfile("id-b", "Beta")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        // Deleting the ACTIVE profile succeeds → immediately No Profile
        // Selected; no fallback to B, no stale cache.
        QVERIFY(fx.profiles.deleteProfile(QStringLiteral("id-a")));
        QVERIFY(!fx.active.hasActiveProfile());
        QCOMPARE(fx.active.activeProfileId(), QString());
        QVERIFY(fx.active.activeProfile().isEmpty());
    }

    void b3c17_failedDeleteActiveKeepsActive()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        {
            // Hold the managed file open: on Windows QFile locks the path
            // without FILE_SHARE_DELETE, so the removal fails and the delete
            // reports failure — the active selection MUST survive it.
            QFile lock(ProfileStore::defaultFilePathFor(
                QStringLiteral("id-a")));
            QVERIFY(lock.open(QIODevice::ReadOnly));
            QVERIFY(!fx.profiles.deleteProfile(QStringLiteral("id-a")));
            QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
            QVERIFY(fx.active.hasActiveProfile());
        }
        // After the lock is released the delete succeeds and clears.
        QVERIFY(fx.profiles.deleteProfile(QStringLiteral("id-a")));
        QVERIFY(!fx.active.hasActiveProfile());
    }

    void b3c18_refreshMissingActiveClears()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        // The file vanishes outside the app (§31.3 Selected Profile Missing).
        QVERIFY(QFile::remove(ProfileStore::defaultFilePathFor(
            QStringLiteral("id-a"))));
        fx.profiles.refreshCatalog();
        QVERIFY(!fx.active.hasActiveProfile());
        QVERIFY(fx.active.activeProfile().isEmpty());
    }

    void b3c19_refreshInvalidActiveClears()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        // The persisted file becomes unloadable: the id drops out of the
        // valid catalog → the active selection clears (no stale cache).
        QFile bad(ProfileStore::defaultFilePathFor(QStringLiteral("id-a")));
        QVERIFY(bad.open(QIODevice::WriteOnly | QIODevice::Truncate));
        bad.write("{\"schemaVersion\":1,\"profileId\":");
        bad.close();
        fx.profiles.refreshCatalog();
        QVERIFY(!fx.active.hasActiveProfile());
    }

    void b3c20_refreshRenamedActiveKeepsIdentity()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        QVERIFY(fx.profiles.openProfile(QStringLiteral("id-a")));
        fx.profiles.setDisplayName(QStringLiteral("Totally new name"));
        QVERIFY(fx.profiles.saveCurrent());
        fx.profiles.refreshCatalog();
        // Metadata changed, identity did not: selection stays, the selector
        // label follows the catalog refresh through the same roles.
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
        QVERIFY(fx.active.hasActiveProfile());
    }

    void b3c21_activeRegisterMapMatchesPersisted()
    {
        DeviceProfile profile = makeProfile("id-a", "Alpha");
        const QString path = persist(profile);
        QVERIFY(!path.isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        const QVariantMap active = fx.active.activeProfile();
        const QVariantList registers =
            active.value("registers").toList();
        QCOMPARE(registers.size(), 2);
        // Every metadata field matches the persisted profile exactly.
        const QVariantMap r0 = registers.at(0).toMap();
        QCOMPARE(r0.value("readFunctionCode").toInt(), 0x03);
        QCOMPARE(r0.value("address").toInt(), 1000);
        QCOMPARE(r0.value("name").toString(), QStringLiteral("Frequency"));
        QCOMPARE(r0.value("dataType").toString(), QStringLiteral("UInt16"));
        QCOMPARE(r0.value("registerCount").toInt(), 1);
        QCOMPARE(r0.value("byteOrder").toString(), QStringLiteral("Normal"));
        QCOMPARE(r0.value("scale").toDouble(), 0.1);
        QCOMPARE(r0.value("unit").toString(), QStringLiteral("Hz"));
        const QVariantMap r1 = registers.at(1).toMap();
        QCOMPARE(r1.value("readFunctionCode").toInt(), 0x04);
        QCOMPARE(r1.value("name").toString(), QStringLiteral("Ambient"));
        QCOMPARE(r1.value("dataType").toString(), QStringLiteral("Int16"));
        QCOMPARE(r1.value("unit").toString(), QStringLiteral("\u00B0C"));
        // And the identity fields too.
        QCOMPARE(active.value("profileId").toString(),
                 QStringLiteral("id-a"));
        QCOMPARE(active.value("displayName").toString(),
                 QStringLiteral("Alpha"));
    }

    void b3c22_crossFcSameAddressEntriesPreservedInActive()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        const QVariantList registers =
            fx.active.activeProfile().value("registers").toList();
        // FC03@1000 and FC04@1000 both survive (different register spaces).
        QCOMPARE(registers.size(), 2);
        QCOMPARE(registers.at(0).toMap().value("address").toInt(), 1000);
        QCOMPARE(registers.at(1).toMap().value("address").toInt(), 1000);
        QVERIFY(registers.at(0).toMap().value("readFunctionCode").toInt()
                != registers.at(1).toMap().value("readFunctionCode").toInt());
    }

    void b3c23_newSessionDoesNotRestorePreviousActive()
    {
        QVERIFY(!persist(makeProfile("id-a", "Alpha")).isEmpty());
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        // A brand-new session (fresh controllers) starts at No Profile
        // Selected — nothing was ever persisted about the selection.
        ProfileController freshProfiles;
        ActiveProfileController freshActive;
        freshActive.setProfileController(&freshProfiles);
        QVERIFY(!freshActive.hasActiveProfile());
        QCOMPARE(freshActive.activeProfileId(), QString());
        QVERIFY(freshActive.activeProfile().isEmpty());
    }

    void b3c24_noComBindingState()
    {
        // Architectural proof via the metaobject: the active-profile surface
        // exposes EXACTLY the frozen properties — there is no COM/port
        // binding state to auto-link a profile to a connection.
        Fixture fx;
        const QMetaObject *meta = fx.active.metaObject();
        QSet<QString> properties;
        for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i)
            properties.insert(QString::fromLatin1(meta->property(i).name()));
        QCOMPARE(properties, (QSet<QString>{
            QStringLiteral("profileController"),
            QStringLiteral("hasActiveProfile"),
            QStringLiteral("activeProfileId"),
            QStringLiteral("activeProfile"),
            QStringLiteral("activeError")}));
    }

    void b3c25_noSlaveBindingState()
    {
        // Same discipline on the invokable surface: only select/clear exist —
        // no slave-address binding, no auto-derive, no persist-restore.
        Fixture fx;
        const QMetaObject *meta = fx.active.metaObject();
        QSet<QString> invokables;
        for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
            const QByteArray signature = meta->method(i).methodSignature();
            const QString name = QString::fromLatin1(
                signature.left(signature.indexOf('(')));
            invokables.insert(name);
        }
        // Every public method name must be one of the frozen surface calls
        // (or a QObject infra name the moc reports).
        for (const QString &name : invokables) {
            QVERIFY2(
                name == QStringLiteral("selectProfile")
                    || name == QStringLiteral("clearActive")
                    || name == QStringLiteral("profileControllerChanged")
                    || name == QStringLiteral("activeChanged")
                    || name == QStringLiteral("deleteLater")
                    || name == QStringLiteral("metaObject")
                    || name.startsWith(QStringLiteral("_q_")),
                qPrintable(QStringLiteral("unexpected method: %1").arg(name)));
        }
        QVERIFY(invokables.contains(QStringLiteral("selectProfile")));
        QVERIFY(invokables.contains(QStringLiteral("clearActive")));
    }
};

QTEST_GUILESS_MAIN(ActiveProfileControllerTest)
#include "test_active_profile_controller.moc"
