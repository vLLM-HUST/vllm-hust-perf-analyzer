# Clock-drift fixture

This controlled fixture gives profiler-host and device timestamps deliberately
different coordinates (`d = 2*p + 1000`).  The expected host-sync slice exists
only after calibration and is eroded by the fitted epsilon.  The fixture is
explicitly `synthetic_only`; it cannot be reported as real calibration.

Rebuild `clock_drift.db` with `sqlite3 clock_drift.db < fixture.sql`.
