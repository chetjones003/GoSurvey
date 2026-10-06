package store

import (
	"strings"
	"testing"
)

func TestMockVersionsAreRealReleases(t *testing.T) {
	s := NewMock()
	allowed := map[string]bool{"0.5.6": true, "0.5.5": true, "0.5.4": true, "0.5.3": true, "0.4.0": true}
	for _, p := range s.AllPings() {
		if !allowed[p.Version] {
			t.Fatalf("mock ping has unreal version %q; allowed are 0.5.x/0.4.0", p.Version)
		}
	}
	// Ensure no 0.7.x leaked
	for _, v := range DistinctVersions(s.AllPings()) {
		if strings.HasPrefix(v, "0.7") || strings.HasPrefix(v, "0.6") {
			t.Fatalf("distinct versions contains future %q", v)
		}
	}
}

func TestQueryPingsFiltering(t *testing.T) {
	s := NewMock()
	// Filter by event should only return that event
	rows, _ := s.QueryPings("install", "", "", "", "", "", 1, 1000)
	for _, r := range rows {
		if r.Event != "install" {
			t.Fatalf("expected install, got %s", r.Event)
		}
	}
	if len(rows) == 0 {
		t.Fatal("expected some install rows")
	}
}

func TestLiveDisabledDoesNotRefresh(t *testing.T) {
	// Without LIVE_D1, RefreshLive refuses and the mock data stays untouched.
	orig := live.enabled
	live.enabled = false
	defer func() { live.enabled = orig }()
	s := NewMock()
	countBefore := len(s.pings)
	if err := s.RefreshLive(); err == nil {
		t.Fatal("RefreshLive should fail when live is disabled")
	}
	if len(s.pings) != countBefore {
		t.Fatalf("RefreshLive mutated mock when live disabled")
	}
	if s.Mode() != "mock" {
		t.Fatalf("mode changed to %q without live", s.Mode())
	}
}

func TestQueryPingsPagingAndOrder(t *testing.T) {
	s := NewMock()
	page1, total := s.QueryPings("", "", "", "", "", "", 1, 10)
	page2, total2 := s.QueryPings("", "", "", "", "", "", 2, 10)
	if total != total2 || total != len(s.pings) {
		t.Fatalf("total mismatch: %d %d %d", total, total2, len(s.pings))
	}
	if len(page1) != 10 || len(page2) != 10 {
		t.Fatalf("page sizes %d %d", len(page1), len(page2))
	}
	if page1[0].ID <= page1[9].ID || page1[9].ID <= page2[0].ID {
		t.Fatal("pings are not newest-first across pages")
	}
	if rows, _ := s.QueryPings("", "", "", "", "", "", 9999, 10); len(rows) != 0 {
		t.Fatal("page past the end should be empty")
	}
}

func TestStatsCachedAndConsistent(t *testing.T) {
	s := NewMock()
	a, b := s.ComputeStats(), s.ComputeStats()
	if a.InstallsTotal != b.InstallsTotal || a.InstallsTotal != 240 {
		t.Fatalf("installs total %d / %d, want 240", a.InstallsTotal, b.InstallsTotal)
	}
	for _, g := range a.Versions {
		if g.Pct < 0 || g.Pct > 100 {
			t.Fatalf("version %s pct %d out of range", g.Key, g.Pct)
		}
	}
}

func TestRecentPingsFewerThanRequested(t *testing.T) {
	s := &Store{mode: "mock"}
	s.setData([]Ping{{ID: 1}, {ID: 2}}, nil, "live")
	if got := s.RecentPings(8); len(got) != 2 || got[0].ID != 2 {
		t.Fatalf("RecentPings = %+v", got)
	}
}

func TestQuerySorting(t *testing.T) {
	s := NewMock()
	rows, _ := s.QueryPings("", "", "", "", "version", "asc", 1, 500)
	for i := 1; i < len(rows); i++ {
		if versionLess(rows[i].Version, rows[i-1].Version) {
			t.Fatalf("version asc out of order at %d: %s after %s", i, rows[i].Version, rows[i-1].Version)
		}
	}
	rows, _ = s.QueryPings("", "", "", "", "when", "asc", 1, 5)
	if rows[0].ID > rows[4].ID {
		t.Fatal("when asc should be oldest first")
	}
	users, _ := s.QueryUsers("", "", "email", "desc", 1, 100)
	for i := 1; i < len(users); i++ {
		if strings.ToLower(users[i].Email) > strings.ToLower(users[i-1].Email) {
			t.Fatal("email desc out of order")
		}
	}
}

func TestVersionLessNumeric(t *testing.T) {
	if !versionLess("0.9.0", "0.10.0") || versionLess("0.10.0", "0.9.0") {
		t.Fatal("versions must compare numerically")
	}
}

func TestNewIsLoadingUntilFetched(t *testing.T) {
	orig := live.enabled
	live.enabled = false // keep New() from starting the network loop; NewMock path is covered elsewhere
	defer func() { live.enabled = orig }()
	if New().Mode() != "mock" {
		t.Fatal("MOCK mode should serve demo data")
	}
	s := &Store{mode: "loading"}
	if st := s.ComputeStats(); st.InstallsTotal != 0 || len(s.RecentPings(8)) != 0 {
		t.Fatal("a loading store must render empty, not crash")
	}
}
