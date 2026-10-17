# R8_03 FIX-02c DIFF

```diff
         if (const auto* analysis =
                 std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
             const auto finished = *pending_;
             pending_.reset();
+            // FIX-02c (SOURCE-REPRO-R8): the LOCAL session completed this
+            // request, but startActiveRequest() had begun the MEMBER session —
+            // without this, the member stays in AwaitingResponse forever and
+            // the NEXT read's beginActiveRequest() fails (C35/R16 root cause).
+            // Order matters: cancel BEFORE emit, because Qt delivers this
+            // signal synchronously on this thread and a completion callback
+            // may legitimately start a new request that must not be cancelled.
+            session_.cancel();
             emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
                 .request = finished,
                 .responseAdu = response,
                 .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                 .analysis = *analysis,
             });
@@ -5121,12 +5206,17 @@ public:
         }
         const auto result = session.onResponseTimeout(pending_->intent.timeout);
         if (const auto *analysis =
                 std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
             const auto finished = *pending_;
             pending_.reset();
+            // FIX-02c (SOURCE-REPRO-R8): same cleanup as completeRead() — the
+            // member session begun by startActiveRequest() must leave
+            // AwaitingResponse before the completion signal is emitted
+            // (Qt delivers it synchronously; a callback may start a new request).
+            session_.cancel();
             emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
                 .request = finished,
                 .responseAdu = {},
                 .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                 .analysis = *analysis,
             });
@@ -5150,26 +5240,43 @@ public:
 
         if (!pending_.has_value()
             || pending_->intent.function
                 != modbuslens::core::ActiveFunction::ReadHoldingRegisters) {
             return;
         }
-        SerialTransactionSession session;
-        const auto begin = session.beginActiveRequest(*pending_);
-        if (std::get_if<SerialTransactionError>(&begin) != nullptr) {
```
