package store

import (
	"fmt"
	"math/rand"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Ping mirrors tools/telemetry-worker/schema.sql pings table.
type Ping struct {
	ID        int    `json:"id"`
	TS        string `json:"ts"`  // ISO-8601
	Day       string `json:"day"` // YYYY-MM-DD
	InstallID string `json:"install_id"`
	Event     string `json:"event"` // install | active
	Version   string `json:"version"`
	Channel   string `json:"channel"` // stable | beta
	OS        string `json:"os"`
	Country   string `json:"country"`
	Email     string `json:"email"`
}

// User mirrors tools/accounts-worker/schema.sql users table.
type User struct {
	Auth0Sub  string `json:"auth0_sub"`
	Email     string `json:"email"`
	Tier      string `json:"tier"` // free | pro | enterprise
	CreatedAt string `json:"created_at"`
}

// Stats is the overview aggregation (mirrors queries.sql).
type Stats struct {
	InstallsTotal int           `json:"installs_total"`
	Installs30d   int           `json:"installs_30d"`
	Active7d      int           `json:"active_7d"`
	Active30d     int           `json:"active_30d"`
	UsersTotal    int           `json:"users_total"`
	DAU           []DayCount    `json:"dau"`
	InstallsDaily []DayCount    `json:"installs_daily"`
	Versions      []GroupCount  `json:"versions"`
	Channels      []GroupCount  `json:"channels"`
	Countries     []GroupCount  `json:"countries"`
	Retention     RetentionStat `json:"retention"`
}

type DayCount struct {
	Day   string `json:"day"`
	Count int    `json:"count"`
}

type GroupCount struct {
	Key   string `json:"key"`
	Count int    `json:"count"`
	Pct   int    `json:"pct"` // bar width: share of the largest group, 0-100
}

// withPct fills Pct relative to the largest count (the list is sorted desc or by key).
func withPct(g []GroupCount) []GroupCount {
	max := 0
	for _, x := range g {
		if x.Count > max {
			max = x.Count
		}
	}
	for i := range g {
		if max > 0 {
			g[i].Pct = g[i].Count * 100 / max
		}
	}
	return g
}

type RetentionStat struct {
	Cohort      int     `json:"cohort"`
	StillActive int     `json:"still_active"`
	Percent     float64 `json:"percent"`
}

// Store holds the dataset the dashboard renders. Reads never block on the
// network: live D1 data is fetched in the background (see live.go) and swapped
// in atomically by setData, which also pre-sorts, pre-indexes and pre-aggregates
// so a page view is just a copy of already-computed values.
type Store struct {
	mu       sync.RWMutex
	pings    []Ping   // newest first
	users    []User   // newest first
	search   []string // lower-cased haystack per ping, parallel to pings
	stats    Stats
	statsDay string // UTC day the cached stats were computed for
	versions []string
	updated  time.Time
	mode     string // "mock" | "live"
}

// New starts the live store: empty ("loading") until the first D1 fetch lands,
// which happens in the background so the server is up immediately. With MOCK=1
// it serves seeded demo data instead.
func New() *Store {
	if !LiveEnabled() {
		return NewMock()
	}
	s := &Store{mode: "loading"}
	go s.liveLoop()
	return s
}

// NewMock returns a store seeded with deterministic demo data and no network use.
func NewMock() *Store {
	s := &Store{mode: "mock"}
	s.seedMock()
	return s
}

func (s *Store) Mode() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.mode
}

// Updated is when the data currently being served was loaded.
func (s *Store) Updated() time.Time {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.updated
}

// setData installs a new dataset. Callers must not hold s.mu.
func (s *Store) setData(pings []Ping, users []User, mode string) {
	sort.SliceStable(pings, func(a, b int) bool { return pings[a].ID > pings[b].ID })
	sort.SliceStable(users, func(a, b int) bool { return users[a].CreatedAt > users[b].CreatedAt })
	search := make([]string, len(pings))
	for i, p := range pings {
		search[i] = strings.ToLower(p.InstallID + "|" + p.Email + "|" + p.Version + "|" + p.Country)
	}
	day := time.Now().UTC().Format("2006-01-02")
	stats := computeStats(pings, users)
	versions := DistinctVersions(pings)
	s.mu.Lock()
	s.pings, s.users, s.search = pings, users, search
	s.stats, s.statsDay, s.versions = stats, day, versions
	s.updated = time.Now()
	if mode != "" {
		s.mode = mode
	}
	s.mu.Unlock()
}

func (s *Store) seedMock() {
	r := rand.New(rand.NewSource(42))
	now := time.Now().UTC()

	// Real released versions only — project is at 0.5.6 (see CMakeLists.txt project(VERSION 0.5.6)).
	// Seed data uses a distribution that looks like upgrade lag, not future versions.
	versions := []string{"0.5.6", "0.5.5", "0.5.4", "0.5.3", "0.4.0"}
	channels := []string{"stable", "beta"}
	countries := []string{"US", "CA", "GB", "DE", "AU", "NZ", "FR", "JP", ""}
	installIDs := make([]string, 240)
	for i := range installIDs {
		installIDs[i] = fmt.Sprintf("%08x%08x%08x%08x", r.Uint32(), r.Uint32(), r.Uint32(), r.Uint32())[:32]
	}

	// Generate installs spread over last 90 days
	pings := []Ping{}
	id := 1
	for i, iid := range installIDs {
		dayOffset := r.Intn(90)
		ts := now.AddDate(0, 0, -dayOffset).Add(time.Duration(r.Intn(86400)) * time.Second)
		ver := versions[r.Intn(len(versions))]
		ch := channels[0]
		if r.Float64() < 0.18 {
			ch = channels[1]
		}
		country := countries[r.Intn(len(countries))]
		email := ""
		if r.Float64() < 0.35 {
			email = fmt.Sprintf("user%d@example.com", i%60)
		}
		pings = append(pings, Ping{
			ID:        id,
			TS:        ts.Format(time.RFC3339),
			Day:       ts.Format("2006-01-02"),
			InstallID: iid,
			Event:     "install",
			Version:   ver,
			Channel:   ch,
			OS:        "windows",
			Country:   country,
			Email:     email,
		})
		id++
	}
	// Generate active pings: each install has 0..30 active pings scattered
	for _, iid := range installIDs {
		n := r.Intn(18)
		// 12% churn: never returned
		if r.Float64() < 0.12 {
			n = 0
		}
		for j := 0; j < n; j++ {
			dayOffset := r.Intn(30)
			ts := now.AddDate(0, 0, -dayOffset).Add(time.Duration(r.Intn(86400)) * time.Second)
			ver := versions[r.Intn(len(versions))]
			ch := channels[0]
			if r.Float64() < 0.18 {
				ch = channels[1]
			}
			country := countries[r.Intn(len(countries))]
			email := ""
			if r.Float64() < 0.35 {
				email = fmt.Sprintf("user%d@example.com", r.Intn(60))
			}
			pings = append(pings, Ping{
				ID:        id,
				TS:        ts.Format(time.RFC3339),
				Day:       ts.Format("2006-01-02"),
				InstallID: iid,
				Event:     "active",
				Version:   ver,
				Channel:   ch,
				OS:        "windows",
				Country:   country,
				Email:     email,
			})
			id++
		}
	}

	// Users
	tiers := []string{"free", "free", "free", "pro", "enterprise"}
	users := []User{}
	for i := 0; i < 68; i++ {
		sub := fmt.Sprintf("auth0|%08x%08x", r.Uint32(), r.Uint32())
		email := fmt.Sprintf("user%d@example.com", i)
		if i == 0 {
			email = "chet@example.com"
		}
		tier := tiers[r.Intn(len(tiers))]
		createdAt := now.AddDate(0, 0, -r.Intn(120)).Format(time.RFC3339)
		users = append(users, User{Auth0Sub: sub, Email: email, Tier: tier, CreatedAt: createdAt})
	}
	s.setData(pings, users, "mock")
}

// pageOf returns the [start,end) window of n items for a 1-based page.
func pageOf(n, page, perPage int) (int, int) {
	if perPage <= 0 {
		perPage = 20
	}
	if page < 1 {
		page = 1
	}
	start := (page - 1) * perPage
	if start > n {
		start = n
	}
	return start, min(start+perPage, n)
}

// QueryPings filters, sorts and paginates. sortKey "" means newest first; dir is "asc" or "desc".
func (s *Store) QueryPings(event, channel, version, search, sortKey, dir string, page, perPage int) ([]Ping, int) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	needle := strings.ToLower(strings.TrimSpace(search))
	var idx []int
	for i := range s.pings {
		p := &s.pings[i]
		if (event != "" && p.Event != event) || (channel != "" && p.Channel != channel) || (version != "" && p.Version != version) {
			continue
		}
		if needle != "" && !strings.Contains(s.search[i], needle) {
			continue
		}
		idx = append(idx, i)
	}
	if less := pingLess(sortKey); less != nil {
		desc := dir == "desc"
		sort.SliceStable(idx, func(a, b int) bool {
			pa, pb := &s.pings[idx[a]], &s.pings[idx[b]]
			if desc {
				return less(pb, pa)
			}
			return less(pa, pb)
		})
	}
	start, end := pageOf(len(idx), page, perPage)
	out := make([]Ping, 0, end-start)
	for _, i := range idx[start:end] {
		out = append(out, s.pings[i])
	}
	return out, len(idx)
}

func pingLess(key string) func(a, b *Ping) bool {
	switch key {
	case "when":
		return func(a, b *Ping) bool { return a.ID < b.ID }
	case "event":
		return func(a, b *Ping) bool { return a.Event < b.Event }
	case "version":
		return func(a, b *Ping) bool { return versionLess(a.Version, b.Version) }
	case "channel":
		return func(a, b *Ping) bool { return a.Channel < b.Channel }
	case "os":
		return func(a, b *Ping) bool { return a.OS < b.OS }
	case "country":
		return func(a, b *Ping) bool { return a.Country < b.Country }
	case "email":
		return func(a, b *Ping) bool { return strings.ToLower(a.Email) < strings.ToLower(b.Email) }
	case "install":
		return func(a, b *Ping) bool { return a.InstallID < b.InstallID }
	}
	return nil
}

// versionLess orders "0.9.0" < "0.10.0" numerically, falling back to string order.
func versionLess(a, b string) bool {
	pa, pb := strings.Split(a, "."), strings.Split(b, ".")
	for i := 0; i < len(pa) && i < len(pb); i++ {
		na, ea := strconv.Atoi(pa[i])
		nb, eb := strconv.Atoi(pb[i])
		if ea != nil || eb != nil {
			if pa[i] != pb[i] {
				return pa[i] < pb[i]
			}
			continue
		}
		if na != nb {
			return na < nb
		}
	}
	return len(pa) < len(pb)
}

func (s *Store) QueryUsers(search, tier, sortKey, dir string, page, perPage int) ([]User, int) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	needle := strings.ToLower(strings.TrimSpace(search))
	var idx []int
	for i := range s.users {
		u := &s.users[i]
		if tier != "" && u.Tier != tier {
			continue
		}
		if needle != "" && !strings.Contains(strings.ToLower(u.Email), needle) && !strings.Contains(strings.ToLower(u.Auth0Sub), needle) {
			continue
		}
		idx = append(idx, i)
	}
	var less func(a, b *User) bool
	switch sortKey {
	case "email":
		less = func(a, b *User) bool { return strings.ToLower(a.Email) < strings.ToLower(b.Email) }
	case "tier":
		less = func(a, b *User) bool { return a.Tier < b.Tier }
	case "joined":
		less = func(a, b *User) bool { return a.CreatedAt < b.CreatedAt }
	case "sub":
		less = func(a, b *User) bool { return a.Auth0Sub < b.Auth0Sub }
	}
	if less != nil {
		desc := dir == "desc"
		sort.SliceStable(idx, func(a, b int) bool {
			ua, ub := &s.users[idx[a]], &s.users[idx[b]]
			if desc {
				return less(ub, ua)
			}
			return less(ua, ub)
		})
	}
	start, end := pageOf(len(idx), page, perPage)
	out := make([]User, 0, end-start)
	for _, i := range idx[start:end] {
		out = append(out, s.users[i])
	}
	return out, len(idx)
}

// RecentPings returns up to n newest pings.
func (s *Store) RecentPings(n int) []Ping {
	s.mu.RLock()
	defer s.mu.RUnlock()
	if n > len(s.pings) {
		n = len(s.pings)
	}
	return append([]Ping(nil), s.pings[:n]...)
}

// Versions lists every version seen, for the telemetry filter.
func (s *Store) Versions() []string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.versions
}

func (s *Store) AllPings() []Ping {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return append([]Ping(nil), s.pings...)
}

func (s *Store) AllUsers() []User {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return append([]User(nil), s.users...)
}

// ComputeStats returns the cached aggregation, recomputing only when the data
// changed (setData) or the UTC day rolled over (the 7d/30d windows move).
func (s *Store) ComputeStats() Stats {
	today := time.Now().UTC().Format("2006-01-02")
	s.mu.RLock()
	if s.statsDay == today {
		st := s.stats
		s.mu.RUnlock()
		return st
	}
	s.mu.RUnlock()
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.statsDay != today {
		s.stats = computeStats(s.pings, s.users)
		s.statsDay = today
	}
	return s.stats
}

func computeStats(pings []Ping, users []User) Stats {
	installsTotal := 0
	for _, p := range pings {
		if p.Event == "install" {
			installsTotal++
		}
	}
	// active distinct
	cut7 := time.Now().UTC().AddDate(0, 0, -7).Format("2006-01-02")
	cut30 := time.Now().UTC().AddDate(0, 0, -30).Format("2006-01-02")
	set7 := map[string]struct{}{}
	set30 := map[string]struct{}{}
	for _, p := range pings {
		if p.Event != "active" {
			continue
		}
		if p.Day >= cut7 {
			set7[p.InstallID] = struct{}{}
		}
		if p.Day >= cut30 {
			set30[p.InstallID] = struct{}{}
		}
	}
	// DAU last 30
	dauMap := map[string]map[string]struct{}{}
	installsDaily := map[string]int{}
	for _, p := range pings {
		if p.Day < cut30 {
			continue
		}
		if p.Event == "active" {
			if dauMap[p.Day] == nil {
				dauMap[p.Day] = map[string]struct{}{}
			}
			dauMap[p.Day][p.InstallID] = struct{}{}
		}
		if p.Event == "install" {
			installsDaily[p.Day]++
		}
	}
	dau := []DayCount{}
	for i := 29; i >= 0; i-- {
		day := time.Now().UTC().AddDate(0, 0, -i).Format("2006-01-02")
		c := 0
		if m, ok := dauMap[day]; ok {
			c = len(m)
		}
		dau = append(dau, DayCount{Day: day, Count: c})
	}
	installsDailyArr := []DayCount{}
	for i := 29; i >= 0; i-- {
		day := time.Now().UTC().AddDate(0, 0, -i).Format("2006-01-02")
		installsDailyArr = append(installsDailyArr, DayCount{Day: day, Count: installsDaily[day]})
	}
	// versions last 30
	verMap := map[string]map[string]struct{}{}
	chMap := map[string]map[string]struct{}{}
	countryMap := map[string]map[string]struct{}{}
	for _, p := range pings {
		if p.Day < cut30 {
			continue
		}
		if verMap[p.Version] == nil {
			verMap[p.Version] = map[string]struct{}{}
		}
		verMap[p.Version][p.InstallID] = struct{}{}
		if chMap[p.Channel] == nil {
			chMap[p.Channel] = map[string]struct{}{}
		}
		chMap[p.Channel][p.InstallID] = struct{}{}
		key := p.Country
		if key == "" {
			key = "unknown"
		}
		if countryMap[key] == nil {
			countryMap[key] = map[string]struct{}{}
		}
		countryMap[key][p.InstallID] = struct{}{}
	}
	versions := []GroupCount{}
	for k, v := range verMap {
		versions = append(versions, GroupCount{Key: k, Count: len(v)})
	}
	sort.Slice(versions, func(a, b int) bool { return versions[a].Count > versions[b].Count })
	channels := []GroupCount{}
	for k, v := range chMap {
		channels = append(channels, GroupCount{Key: k, Count: len(v)})
	}
	sort.Slice(channels, func(a, b int) bool { return channels[a].Key < channels[b].Key })
	countries := []GroupCount{}
	for k, v := range countryMap {
		countries = append(countries, GroupCount{Key: k, Count: len(v)})
	}
	sort.Slice(countries, func(a, b int) bool { return countries[a].Count > countries[b].Count })
	if len(countries) > 8 {
		countries = countries[:8]
	}
	// retention
	cut30Old := time.Now().UTC().AddDate(0, 0, -30).Format("2006-01-02")
	cohortSet := map[string]struct{}{}
	for _, p := range pings {
		if p.Event == "install" && p.Day <= cut30Old {
			cohortSet[p.InstallID] = struct{}{}
		}
	}
	still := 0
	active30Set := map[string]struct{}{}
	for _, p := range pings {
		if p.Event == "active" && p.Day >= cut30 {
			active30Set[p.InstallID] = struct{}{}
		}
	}
	for id := range cohortSet {
		if _, ok := active30Set[id]; ok {
			still++
		}
	}
	percent := 0.0
	if len(cohortSet) > 0 {
		percent = float64(still) / float64(len(cohortSet)) * 100
	}
	installs30d := 0
	for _, d := range installsDailyArr {
		installs30d += d.Count
	}
	return Stats{
		InstallsTotal: installsTotal,
		Installs30d:   installs30d,
		Active7d:      len(set7),
		Active30d:     len(set30),
		UsersTotal:    len(users),
		DAU:           dau,
		InstallsDaily: installsDailyArr,
		Versions:      withPct(versions),
		Channels:      withPct(channels),
		Countries:     withPct(countries),
		Retention:     RetentionStat{Cohort: len(cohortSet), StillActive: still, Percent: percent},
	}
}

func DistinctVersions(pings []Ping) []string {
	m := map[string]struct{}{}
	for _, p := range pings {
		m[p.Version] = struct{}{}
	}
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}
