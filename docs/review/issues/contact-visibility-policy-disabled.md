# P2 · [Rule invalid] Nearby node visibility annotation is inconsistent with the actual query behavior

Status: **acknowledged**
Category: **Code Quality/Domain Rules**

## Conclusion

The interface annotation of `ContactService::getNearby` states that it only returns non-contact nodes visible within six days, `formatTimeStatus` can also display Offline after six days; but the actual filter function `isNodeVisible(uint32_t)` ignores arguments and returns `true` unconditionally.

So expired nodes will not exit nearby/ignored queries as commented. This is not a rule that the document can fill in on its own. The product design must confirm the retention period and contact exceptions before modifying the code.

## Evidence

- `modules/core_chat/include/chat/usecase/contact_service.h`: Interface description of the six-day visibility period of nearby nodes.
- `modules/core_chat/src/usecase/contact_service.cpp`: `isNodeVisible` currently returns true directly.
-Same file `formatTimeStatus`: Return Offline after six days, but this function does not become a query filtering rule.

## Rules requiring confirmation

1. Is the retention/visibility of non-contact nodes indeed six days?
2. Are ignored nodes also expired?
3. Are saved contacts retained permanently, even if wireless observation expires?
4. How to calculate the freshness of a device without valid epoch?
5. Should cleaning be hidden during query, or should it be deleted from the persistence directory?

## Acceptance

After the rule is confirmed, a testable visibility policy should simultaneously drive the nearby query, ignored query, status copy, and cleanup strategy; comments, display text, and storage behavior cannot continue to define freshness separately.
