package store

import (
	"strings"
	"testing"
)

func TestMockVersionsAreRealReleases(t *testing.T) {
	s := &Store{mode: "mock"}
	s.seedMock()
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
	s := &Store{mode: "mock"}
	s.seedMock()
	// Filter by event should only return that event
	rows, _ := s.QueryPings("install", "", "", "", 1, 1000)
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
	// Ensure that without LIVE_D1, EnsureLive is a no-op and mode stays mock
	orig := live.enabled
	live.enabled = false
	defer func() { live.enabled = orig }()
	s := &Store{mode: "mock"}
	s.seedMock()
	countBefore := len(s.pings)
	s.EnsureLive()
	if len(s.pings) != countBefore {
		t.Fatalf("EnsureLive mutated mock when live disabled")
	}
	if s.Mode() != "mock" {
		t.Fatalf("mode changed to %q without live", s.Mode())
	}
}
