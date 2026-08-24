# Rule consistency

Status: **has_unresolved** · 1 finding

- P2: [Nearby node visibility annotation is inconsistent with actual query behavior](../issues/contact-visibility-policy-disabled.md)

Interface description and status text imply six-day freshness, but `isNodeVisible()` Currently always returns true. Before the rules are confirmed, the model document only describes the real behavior and does not pretend that the expired policy has been implemented by code.
