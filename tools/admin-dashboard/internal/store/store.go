package store

import (
	"fmt"
	"math/rand"
	"os"
	"sort"
	"strings"
	"sync"
	"time"
)

// Ping mirrors tools/telemetry-worker/schema.sql pings table.
type Ping struct {
	ID        int    `json:"id"`
	TS        string `json:"ts"`         // ISO-8601
	Day       string `json:"day"`        // YYYY-MM-DD
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
	InstallsTotal int            `json:"installs_total"`
	Active7d      int            `json:"active_7d"`
	Active30d     int            `json:"active_30d"`
	UsersTotal    int            `json:"users_total"`
	DAU           []DayCount     `json:"dau"`
	InstallsDaily []DayCount     `json:"installs_daily"`
	Versions      []GroupCount   `json:"versions"`
	Channels      []GroupCount   `json:"channels"`
	Countries     []GroupCount   `json:"countries"`
	Retention     RetentionStat  `json:"retention"`
}

type DayCount struct {
	Day   string `json:"day"`
	Count int    `json:"count"`
}

type GroupCount struct {
	Key   string `json:"key"`
	Count int    `json:"count"`
}

type RetentionStat struct {
	Cohort      int `json:"cohort"`
	StillActive int `json:"still_active"`
	Percent     float64 `json:"percent"`
}

// Store holds mock data and optional live D1 config.
type Store struct {
	mu    sync.RWMutex
	pings []Ping
	users []User
	mode  string // "mock" | "live"
}

func New() *Store {
	s := &Store{mode: "mock"}
	if LiveEnabled() {
		// Try live immediately; fall back to mock on error but keep mode as live-error
		if err := s.RefreshLive(); err != nil {
			// still seed mock so UI works, but mode stays mock until success
			s.seedMock()
			// Keep error for banner
			fmt.Fprintf(os.Stderr, "[admin] live fetch failed, using mock: %v\n", err)
			return s
		}
		return s
	}
	s.seedMock()
	return s
}

func (s *Store) Mode() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.mode
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
	sort.Slice(pings, func(a, b int) bool { return pings[a].ID < pings[b].ID })
	s.pings = pings

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
	sort.Slice(users, func(a, b int) bool { return users[a].CreatedAt > users[b].CreatedAt })
	s.users = users
}

// QueryPings filters and paginates.
func (s *Store) QueryPings(event, channel, version, search string, page, perPage int) ([]Ping, int) {
	s.EnsureLive()
	s.mu.RLock()
	defer s.mu.RUnlock()
	filtered := make([]Ping, 0, len(s.pings))
	for _, p := range s.pings {
		if event != "" && p.Event != event {
			continue
		}
		if channel != "" && p.Channel != channel {
			continue
		}
		if version != "" && p.Version != version {
			continue
		}
		if search != "" {
			needle := strings.ToLower(search)
			if !strings.Contains(strings.ToLower(p.InstallID), needle) &&
				!strings.Contains(strings.ToLower(p.Email), needle) &&
				!strings.Contains(strings.ToLower(p.Version), needle) &&
				!strings.Contains(strings.ToLower(p.Country), needle) {
				continue
			}
		}
		filtered = append(filtered, p)
	}
	// newest first
	sort.Slice(filtered, func(a, b int) bool { return filtered[a].ID > filtered[b].ID })
	total := len(filtered)
	if perPage <= 0 {
		perPage = 20
	}
	if page < 1 {
		page = 1
	}
	start := (page - 1) * perPage
	if start >= total {
		return []Ping{}, total
	}
	end := start + perPage
	if end > total {
		end = total
	}
	return filtered[start:end], total
}

func (s *Store) QueryUsers(search, tier string, page, perPage int) ([]User, int) {
	s.EnsureLive()
	s.mu.RLock()
	defer s.mu.RUnlock()
	filtered := make([]User, 0, len(s.users))
	for _, u := range s.users {
		if tier != "" && u.Tier != tier {
			continue
		}
		if search != "" {
			needle := strings.ToLower(search)
			if !strings.Contains(strings.ToLower(u.Email), needle) &&
				!strings.Contains(strings.ToLower(u.Auth0Sub), needle) {
				continue
			}
		}
		filtered = append(filtered, u)
	}
	total := len(filtered)
	if perPage <= 0 {
		perPage = 20
	}
	if page < 1 {
		page = 1
	}
	start := (page - 1) * perPage
	if start >= total {
		return []User{}, total
	}
	end := start + perPage
	if end > total {
		end = total
	}
	return filtered[start:end], total
}

func (s *Store) AllPings() []Ping {
	s.EnsureLive()
	s.mu.RLock()
	defer s.mu.RUnlock()
	cp := make([]Ping, len(s.pings))
	copy(cp, s.pings)
	return cp
}
func (s *Store) AllUsers() []User {
	s.EnsureLive()
	s.mu.RLock()
	defer s.mu.RUnlock()
	cp := make([]User, len(s.users))
	copy(cp, s.users)
	return cp
}

func (s *Store) ComputeStats() Stats {
	s.EnsureLive()
	s.mu.RLock()
	defer s.mu.RUnlock()
	installsTotal := 0
	for _, p := range s.pings {
		if p.Event == "install" {
			installsTotal++
		}
	}
	// active distinct
	cut7 := time.Now().UTC().AddDate(0, 0, -7).Format("2006-01-02")
	cut30 := time.Now().UTC().AddDate(0, 0, -30).Format("2006-01-02")
	set7 := map[string]struct{}{}
	set30 := map[string]struct{}{}
	for _, p := range s.pings {
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
	for _, p := range s.pings {
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
	for _, p := range s.pings {
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
	for _, p := range s.pings {
		if p.Event == "install" && p.Day <= cut30Old {
			cohortSet[p.InstallID] = struct{}{}
		}
	}
	still := 0
	active30Set := map[string]struct{}{}
	for _, p := range s.pings {
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
	return Stats{
		InstallsTotal: installsTotal,
		Active7d:      len(set7),
		Active30d:     len(set30),
		UsersTotal:    len(s.users),
		DAU:           dau,
		InstallsDaily: installsDailyArr,
		Versions:      versions,
		Channels:      channels,
		Countries:     countries,
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
