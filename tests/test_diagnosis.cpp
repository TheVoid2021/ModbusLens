#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/analysis/TransactionAnalysis.h"
#include "core/diagnosis/DiagnosisContext.h"
#include "core/diagnosis/RuleBasedDiagnosis.h"

using modbuslens::core::DiagnosisActionCode;
using modbuslens::core::DiagnosisContext;
using modbuslens::core::DiagnosisFinding;
using modbuslens::core::DiagnosisFindingCode;
using modbuslens::core::DiagnosisReport;
using modbuslens::core::DiagnosisSeverity;
using modbuslens::core::DiagnosisTransaction;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionStatus;
using modbuslens::core::buildDiagnosisContext;
using modbuslens::core::diagnoseTransactions;

namespace {

using ms = std::chrono::milliseconds;

DiagnosisTransaction tx(
    std::uint8_t address, TransactionStatus status, long long elapsedMs,
    std::optional<std::uint8_t> exceptionCode = std::nullopt,
    std::vector<modbuslens::core::TransactionRequestIssue> requestIssues = {})
{
    return DiagnosisTransaction{
        .deviceAddress = address,
        .functionCode = 0x03,
        .analysis = TransactionAnalysis{
            .status = status,
            .elapsed = ms{elapsedMs},
            .exceptionCode = exceptionCode,
            .issue = std::nullopt,
        },
        .requestIssues = std::move(requestIssues),
    };
}

DiagnosisContext contextOf(std::initializer_list<DiagnosisTransaction> transactions)
{
    std::vector<DiagnosisTransaction> input{transactions};
    return buildDiagnosisContext(input);
}

} // namespace

class DiagnosisTest : public QObject
{
    Q_OBJECT

private slots:
    // DIAG-A01 (P0): empty context -> exactly one NoData finding, no Healthy.
    void a01_empty();
    // DIAG-A02 (P0): all success -> Healthy with the success count.
    void a02_allSuccess();
    // DIAG-A03 (P0): one Timeout -> TimeoutObserved + 4 checks, no Healthy.
    void a03_timeout();
    // DIAG-A04 (P0): one CrcError -> CrcErrorObserved + settings/wiring/noise.
    void a04_crc();
    // DIAG-A05 (P0): Exception 0x02 -> code carried + register map check.
    void a05_exception02();
    // DIAG-A06 (P0): ProtocolError -> Error severity + consistency checks.
    void a06_protocol();
    // DIAG-A07 (P0): mixed golden -> three failure findings, fixed order,
    // statistics derived via summarizeTransactions.
    void a07_mixedGolden();
    // DIAG-A08 (P1): exception grouping (0x02,0x03,0x02), ascending order.
    void a08_exceptionGrouping();
    // DIAG-A09 (P1): only Pending -> PendingObserved, no Healthy.
    void a09_pending();
    // DIAG-A10 (P1): determinism — same context, identical reports.
    void a10_determinism();
    // DIAG-A11 (P0, T014): DiagnosisContext preserves the analyzer's issue
    // verbatim; the baseline does NOT re-derive it and keeps its finding.
    void a11_protocolIssuePreservedInContext();
    // DIAG-A12 (P0, T015): broadcast observations surface as an Info finding
    // in the fixed order, with no suggested checks and never Healthy.
    void a12_expectedNoResponseObservation();
    // DIAG-A13 (P0, T015): a broadcast must never, on its own, allow a batch
    // to be declared Healthy — even when every answered transaction succeeded.
    void a13_broadcastNeverHealthy();
    // DIAG-A14 (P0, T015 Part C): Success + requestIssues must NOT be
    // Healthy; a deterministic RequestIssueObserved finding appears.
    void a14_requestIssueObserved();
    // DIAG-A15 (P1): finding order includes RequestIssueObserved after
    // ExpectedNoResponseObserved and before Healthy/NoData.
    void a15_requestIssueOrder();
};

void DiagnosisTest::a01_empty()
{
    const DiagnosisContext empty = contextOf({});
    QVERIFY(empty.transactions.empty());
    QCOMPARE(empty.statistics.observedCount, std::size_t{0});

    const DiagnosisReport report = diagnoseTransactions(empty);
    QCOMPARE(report.findings.size(), std::size_t{1});
    const DiagnosisFinding expected{
        .code = DiagnosisFindingCode::NoData,
        .severity = DiagnosisSeverity::Info,
        .affectedCount = 0,
        .exceptionCode = std::nullopt,
        .recommendedActions = {},
    };
    QCOMPARE(report.findings[0], expected);
}

void DiagnosisTest::a02_allSuccess()
{
    const auto context = contextOf({
        tx(0x01, TransactionStatus::Success, 25),
        tx(0x01, TransactionStatus::Success, 30),
    });
    const DiagnosisReport report = diagnoseTransactions(context);

    QCOMPARE(report.findings.size(), std::size_t{1});
    QCOMPARE(report.findings[0].code, DiagnosisFindingCode::Healthy);
    QCOMPARE(report.findings[0].severity, DiagnosisSeverity::Info);
    QCOMPARE(report.findings[0].affectedCount, std::size_t{2});
    QVERIFY(report.findings[0].recommendedActions.empty());
}

void DiagnosisTest::a03_timeout()
{
    const auto context = contextOf({tx(0x01, TransactionStatus::Timeout, 1000)});
    const DiagnosisReport report = diagnoseTransactions(context);

    QCOMPARE(report.findings.size(), std::size_t{1});
    const auto& f = report.findings[0];
    QCOMPARE(f.code, DiagnosisFindingCode::TimeoutObserved);
    QCOMPARE(f.severity, DiagnosisSeverity::Warning);
    QCOMPARE(f.affectedCount, std::size_t{1});
    QVERIFY(!f.exceptionCode.has_value());
    const std::vector<DiagnosisActionCode> expectedActions = {
        DiagnosisActionCode::CheckDevicePower,
        DiagnosisActionCode::CheckSlaveAddress,
        DiagnosisActionCode::CheckSerialSettings,
        DiagnosisActionCode::CheckWiring,
    };
    QCOMPARE(f.recommendedActions, expectedActions);
}

void DiagnosisTest::a04_crc()
{
    const auto context = contextOf({tx(0x01, TransactionStatus::CrcError, 17)});
    const DiagnosisReport report = diagnoseTransactions(context);

    QCOMPARE(report.findings.size(), std::size_t{1});
    const auto& f = report.findings[0];
    QCOMPARE(f.code, DiagnosisFindingCode::CrcErrorObserved);
    QCOMPARE(f.affectedCount, std::size_t{1});
    const std::vector<DiagnosisActionCode> expectedActions = {
        DiagnosisActionCode::CheckSerialSettings,
        DiagnosisActionCode::CheckWiring,
        DiagnosisActionCode::CheckNoiseAndGrounding,
    };
    QCOMPARE(f.recommendedActions, expectedActions);
}

void DiagnosisTest::a05_exception02()
{
    const auto context = contextOf({
        tx(0x01, TransactionStatus::Exception, 18, std::uint8_t{0x02}),
    });
    const DiagnosisReport report = diagnoseTransactions(context);

    QCOMPARE(report.findings.size(), std::size_t{1});
    const auto& f = report.findings[0];
    QCOMPARE(f.code, DiagnosisFindingCode::ExceptionObserved);
    QCOMPARE(f.severity, DiagnosisSeverity::Warning);
    QCOMPARE(f.affectedCount, std::size_t{1});
    QVERIFY(f.exceptionCode.has_value());
    QCOMPARE(*f.exceptionCode, std::uint8_t{0x02});
    const std::vector<DiagnosisActionCode> expectedActions = {
        DiagnosisActionCode::CheckRegisterMap,
    };
    QCOMPARE(f.recommendedActions, expectedActions);
}

void DiagnosisTest::a06_protocol()
{
    const auto context = contextOf({tx(0x01, TransactionStatus::ProtocolError, 30)});
    const DiagnosisReport report = diagnoseTransactions(context);

    QCOMPARE(report.findings.size(), std::size_t{1});
    const auto& f = report.findings[0];
    QCOMPARE(f.code, DiagnosisFindingCode::ProtocolErrorObserved);
    QCOMPARE(f.severity, DiagnosisSeverity::Error);
    QCOMPARE(f.affectedCount, std::size_t{1});
    const std::vector<DiagnosisActionCode> expectedActions = {
        DiagnosisActionCode::InspectProtocolConsistency,
        DiagnosisActionCode::CheckDeviceDocumentation,
    };
    QCOMPARE(f.recommendedActions, expectedActions);
}

void DiagnosisTest::a07_mixedGolden()
{
    // The T008/T009 golden facts: Success / Exception 0x02 / Crc / Timeout.
    const auto context = contextOf({
        tx(0x01, TransactionStatus::Success, 25),
        tx(0x01, TransactionStatus::Exception, 18, std::uint8_t{0x02}),
        tx(0x01, TransactionStatus::CrcError, 17),
        tx(0x01, TransactionStatus::Timeout, 1000),
    });

    // Context self-consistency: statistics derived ONLY from the batch.
    QCOMPARE(context.statistics.observedCount, std::size_t{4});
    QCOMPARE(context.statistics.completedCount, std::size_t{4});
    QCOMPARE(context.statistics.pendingCount, std::size_t{0});
    QCOMPARE(context.statistics.successCount, std::size_t{1});
    QCOMPARE(context.statistics.exceptionCount, std::size_t{1});
    QCOMPARE(context.statistics.crcErrorCount, std::size_t{1});
    QCOMPARE(context.statistics.timeoutCount, std::size_t{1});
    QCOMPARE(context.statistics.protocolErrorCount, std::size_t{0});
    QVERIFY(context.statistics.successRate.has_value());
    QCOMPARE(*context.statistics.successRate, 0.25);
    QVERIFY(context.statistics.averageSuccessLatencyMs.has_value());
    QCOMPARE(*context.statistics.averageSuccessLatencyMs, 25.0);

    const DiagnosisReport report = diagnoseTransactions(context);

    // No Healthy in a mixed batch; three independent failure findings in
    // the archived fixed order: CRC -> Timeout -> Exception.
    QCOMPARE(report.findings.size(), std::size_t{3});
    QCOMPARE(report.findings[0].code, DiagnosisFindingCode::CrcErrorObserved);
    QCOMPARE(report.findings[1].code, DiagnosisFindingCode::TimeoutObserved);
    QCOMPARE(report.findings[2].code, DiagnosisFindingCode::ExceptionObserved);
    for (const auto& f : report.findings) {
        QCOMPARE(f.affectedCount, std::size_t{1});
    }
}

void DiagnosisTest::a08_exceptionGrouping()
{
    const auto context = contextOf({
        tx(0x01, TransactionStatus::Exception, 10, std::uint8_t{0x02}),
        tx(0x01, TransactionStatus::Exception, 10, std::uint8_t{0x03}),
        tx(0x01, TransactionStatus::Exception, 10, std::uint8_t{0x02}),
    });
    const DiagnosisReport report = diagnoseTransactions(context);

    QCOMPARE(report.findings.size(), std::size_t{2});
    // Ascending by exception code: 0x02 first, then 0x03.
    QCOMPARE(report.findings[0].exceptionCode, std::optional<std::uint8_t>{0x02});
    QCOMPARE(report.findings[0].affectedCount, std::size_t{2});
    QCOMPARE(report.findings[1].exceptionCode, std::optional<std::uint8_t>{0x03});
    QCOMPARE(report.findings[1].affectedCount, std::size_t{1});
    // Per-code standard recommendation mapping.
    QCOMPARE(report.findings[0].recommendedActions,
             std::vector<DiagnosisActionCode>{DiagnosisActionCode::CheckRegisterMap});
    QCOMPARE(report.findings[1].recommendedActions,
             std::vector<DiagnosisActionCode>{DiagnosisActionCode::CheckRequestParameters});
}

void DiagnosisTest::a09_pending()
{
    const auto context = contextOf({tx(0x01, TransactionStatus::Pending, 10)});
    QCOMPARE(context.statistics.pendingCount, std::size_t{1});

    const DiagnosisReport report = diagnoseTransactions(context);
    QCOMPARE(report.findings.size(), std::size_t{1});
    QCOMPARE(report.findings[0].code, DiagnosisFindingCode::PendingObserved);
    QCOMPARE(report.findings[0].severity, DiagnosisSeverity::Info);
    QCOMPARE(report.findings[0].affectedCount, std::size_t{1});
    QCOMPARE(report.findings[0].recommendedActions,
             std::vector<DiagnosisActionCode>{DiagnosisActionCode::WaitForCompletion});
}

void DiagnosisTest::a10_determinism()
{
    const auto context = contextOf({
        tx(0x01, TransactionStatus::Success, 25),
        tx(0x01, TransactionStatus::Exception, 18, std::uint8_t{0x02}),
        tx(0x01, TransactionStatus::CrcError, 17),
        tx(0x01, TransactionStatus::Timeout, 1000),
    });
    const auto first = diagnoseTransactions(context);
    const auto second = diagnoseTransactions(context);
    QVERIFY(first == second);
}

void DiagnosisTest::a11_protocolIssuePreservedInContext()
{
    // Analyzer-produced ProtocolError (address mismatch). The context must
    // carry the issue through by value — DiagnosisContext adds NOTHING and
    // the baseline must not re-derive it (single-authority chain).
    const modbuslens::core::ModbusRtuFrame request{
        .address = 0x01, .functionCode = 0x03, .data = {0x00, 0x00, 0x00, 0x02}};
    const modbuslens::core::ModbusRtuFrame foreign{
        .address = 0x02, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
    const auto analysis = modbuslens::core::analyzeFunction03Transaction(
        request, modbuslens::core::ResponseObservation{foreign}, ms{25}, ms{1000});

    const std::vector<DiagnosisTransaction> batch = {
        DiagnosisTransaction{
            .deviceAddress = 0x01, .functionCode = 0x03, .analysis = analysis,
            .requestIssues = {}},
    };
    const DiagnosisContext context = buildDiagnosisContext(batch);

    QCOMPARE(context.transactions.size(), std::size_t{1});
    QVERIFY(context.transactions[0].analysis.issue.has_value());
    QCOMPARE(context.transactions[0].analysis.issue->code,
             modbuslens::core::TransactionIssueCode::ResponseAddressMismatch);
    QCOMPARE(*context.transactions[0].analysis.issue->expectedAddress,
             std::uint8_t{0x01});
    QCOMPARE(*context.transactions[0].analysis.issue->actualAddress,
             std::uint8_t{0x02});

    // Baseline finding itself stays the pre-T014 shape (no issue deduction,
    // no new finding codes) — the DIAG-A06 contract is not weakened.
    const DiagnosisReport report = diagnoseTransactions(context);
    QCOMPARE(report.findings.size(), std::size_t{1});
    QCOMPARE(report.findings[0].code, DiagnosisFindingCode::ProtocolErrorObserved);
}

void DiagnosisTest::a12_expectedNoResponseObservation()
{
    const auto context = contextOf({
        tx(0x00, TransactionStatus::ExpectedNoResponse, 0),
        tx(0x01, TransactionStatus::Timeout, 1000),
        tx(0x01, TransactionStatus::Pending, 10),
    });
    const DiagnosisReport report = diagnoseTransactions(context);

    // Fixed deterministic order: Protocol -> CRC -> Timeout -> Exception ->
    // Pending -> ExpectedNoResponse -> Healthy/NoData.
    QCOMPARE(report.findings.size(), std::size_t{3});
    QCOMPARE(report.findings[0].code, DiagnosisFindingCode::TimeoutObserved);
    QCOMPARE(report.findings[1].code, DiagnosisFindingCode::PendingObserved);
    QCOMPARE(report.findings[2].code,
             DiagnosisFindingCode::ExpectedNoResponseObserved);
    QCOMPARE(report.findings[2].severity, DiagnosisSeverity::Info);
    QCOMPARE(report.findings[2].affectedCount, std::size_t{1});
    QVERIFY(report.findings[2].recommendedActions.empty());
    for (const auto& finding : report.findings) {
        QVERIFY(finding.code != DiagnosisFindingCode::Healthy);
    }
}

void DiagnosisTest::a13_broadcastNeverHealthy()
{
    // Every ANSWERED transaction succeeded, but the batch also observed a
    // broadcast: Healthy must NOT be declared (a broadcast proves nothing
    // about device state by itself — ADR-003).
    const auto context = contextOf({
        tx(0x01, TransactionStatus::Success, 20),
        tx(0x00, TransactionStatus::ExpectedNoResponse, 0),
    });
    const DiagnosisReport report = diagnoseTransactions(context);
    QCOMPARE(report.findings.size(), std::size_t{1});
    QCOMPARE(report.findings[0].code,
             DiagnosisFindingCode::ExpectedNoResponseObserved);
    QCOMPARE(report.findings[0].affectedCount, std::size_t{1});

    // Control: the same batch WITHOUT the broadcast IS Healthy.
    const auto healthyContext = contextOf({
        tx(0x01, TransactionStatus::Success, 20),
    });
    const DiagnosisReport healthyReport = diagnoseTransactions(healthyContext);
    QCOMPARE(healthyReport.findings.size(), std::size_t{1});
    QCOMPARE(healthyReport.findings[0].code, DiagnosisFindingCode::Healthy);
}

void DiagnosisTest::a14_requestIssueObserved()
{
    // Success means "matching normal response observed" — it proves NOTHING
    // about the request's own protocol validity (orthogonal dimensions).
    modbuslens::core::TransactionRequestIssue issue;
    issue.code = modbuslens::core::TransactionRequestIssueCode::InvalidRequestQuantity;
    issue.observedQuantity = std::uint16_t{126};
    issue.minAllowedQuantity = std::uint16_t{1};
    issue.maxAllowedQuantity = std::uint16_t{125};

    const auto context = contextOf({
        tx(0x01, TransactionStatus::Success, 20, std::nullopt, {issue}),
    });
    const DiagnosisReport report = diagnoseTransactions(context);

    // NOT Healthy: request issues are first-class orthogonal facts.
    for (const auto& finding : report.findings) {
        QVERIFY(finding.code != DiagnosisFindingCode::Healthy);
    }
    QCOMPARE(report.findings.size(), std::size_t{1});
    QCOMPARE(report.findings[0].code, DiagnosisFindingCode::RequestIssueObserved);
    // affectedCount = TRANSACTIONS carrying issues (1), never the issue count.
    QCOMPARE(report.findings[0].affectedCount, std::size_t{1});
    QCOMPARE(report.findings[0].severity, DiagnosisSeverity::Warning);

    // Statistics orthogonality proof (audit §4): the request issues never
    // become an eighth TransactionStatus — the batch is still 1 Success,
    // 1 completed, rate 100% — while Baseline correctly refuses Healthy.
    QCOMPARE(context.statistics.successCount, std::size_t{1});
    QCOMPARE(context.statistics.completedCount, std::size_t{1});
    QVERIFY(context.statistics.successRate.has_value());
    QCOMPARE(*context.statistics.successRate, 1.0);
}

void DiagnosisTest::a15_requestIssueOrder()
{
    modbuslens::core::TransactionRequestIssue issue;
    issue.code = modbuslens::core::TransactionRequestIssueCode::InvalidBroadcastFunction;

    const auto context = contextOf({
        tx(0x00, TransactionStatus::ExpectedNoResponse, 0, std::nullopt, {issue}),
        tx(0x01, TransactionStatus::Timeout, 1000),
    });
    const DiagnosisReport report = diagnoseTransactions(context);

    // Fixed order: Timeout -> ExpectedNoResponse -> RequestIssue -> (no Healthy).
    QCOMPARE(report.findings.size(), std::size_t{3});
    QCOMPARE(report.findings[0].code, DiagnosisFindingCode::TimeoutObserved);
    QCOMPARE(report.findings[1].code,
             DiagnosisFindingCode::ExpectedNoResponseObserved);
    QCOMPARE(report.findings[2].code, DiagnosisFindingCode::RequestIssueObserved);
}

QTEST_GUILESS_MAIN(DiagnosisTest)
#include "test_diagnosis.moc"