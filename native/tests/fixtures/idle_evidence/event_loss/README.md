# Event-loss fixture

This controlled fixture freezes fail-closed behavior for incomplete
collection.  Empty-looking slices remain `unattributed_visible_idle`, and the
unknown event retains exact diagnostic lineage.  It is simulation/model
evidence, not runtime evidence.

Rebuild `event_loss.db` with `sqlite3 event_loss.db < fixture.sql`.
