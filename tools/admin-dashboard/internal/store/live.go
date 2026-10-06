package store

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// WranglerResult mirrors the JSON wrangler --json emits for D1.
type wranglerResult struct {
	Results []map[string]any `json:"results"`
	Meta    any              `json:"meta"`
}

// liveState tracks the background D1 refresh (LIVE_D1=1).
type liveState struct {
	mu       sync.Mutex
	enabled  bool
	lastErr  string
	interval time.Duration
	running  sync.Mutex // serialises refreshes so a manual one can't overlap the loop
}

var live = &liveState{interval: 30 * time.Second}

func init() {
	// Live D1 is the default. Set MOCK=1 (or LIVE_D1=0) to run on demo data offline.
	live.enabled = os.Getenv("MOCK") != "1" && os.Getenv("LIVE_D1") != "0"
}

func LiveEnabled() bool { return live.enabled }
func LiveError() string {
	live.mu.Lock()
	defer live.mu.Unlock()
	return live.lastErr
}
func setLiveError(err string) {
	live.mu.Lock()
	live.lastErr = err
	live.mu.Unlock()
}

// liveLoop refreshes immediately and then on every interval. It runs in its own
// goroutine so neither startup nor page loads ever wait on wrangler.
func (s *Store) liveLoop() {
	for {
		_ = s.RefreshLive()
		time.Sleep(live.interval)
	}
}

// repoRoot finds GoSurvey repo root by walking up from executable or cwd looking for tools/telemetry-worker/wrangler.toml
func repoRoot() string {
	// Try cwd
	cwd, _ := os.Getwd()
	for d := cwd; d != "/" && d != "."; d = filepath.Dir(d) {
		if _, err := os.Stat(filepath.Join(d, "tools", "telemetry-worker", "wrangler.toml")); err == nil {
			return d
		}
		if filepath.Dir(d) == d {
			break
		}
	}
	// Try relative to this binary's expected location tools/admin-dashboard
	if exe, err := os.Executable(); err == nil {
		d := filepath.Dir(exe)
		for i := 0; i < 5; i++ {
			if _, err := os.Stat(filepath.Join(d, "tools", "telemetry-worker", "wrangler.toml")); err == nil {
				return d
			}
			if _, err := os.Stat(filepath.Join(d, "wrangler.toml")); err == nil {
				return filepath.Dir(d)
			}
			d = filepath.Dir(d)
		}
	}
	// Fallback: parent of admin-dashboard
	if wd, err := os.Getwd(); err == nil {
		return filepath.Dir(filepath.Dir(wd))
	}
	return "."
}

func runWrangler(db, sql, configPath string) ([]map[string]any, error) {
	root := repoRoot()
	cfgAbs := configPath
	if !filepath.IsAbs(cfgAbs) {
		cfgAbs = filepath.Join(root, configPath)
	}
	args := []string{"d1", "execute", db, "--remote", "--json", "--config", cfgAbs, "--command", sql}
	// A globally installed wrangler starts in milliseconds; npx can spend
	// seconds resolving the package first, so only fall back to it.
	var out []byte
	var err error
	if path, lookErr := exec.LookPath("wrangler"); lookErr == nil {
		out, err = execCommand(path, args, root)
	} else {
		out, err = execCommand("npx", append([]string{"wrangler"}, args...), root)
	}
	if err != nil {
		return nil, fmt.Errorf("wrangler failed (%v): %s", err, firstLine(string(out)))
	}
	// Wrangler prints banner before JSON — find first '['
	idx := bytes.Index(out, []byte("["))
	if idx == -1 {
		return nil, fmt.Errorf("no JSON array in wrangler output: %s", firstLine(string(out)))
	}
	jsonBytes := out[idx:]
	var data []wranglerResult
	if err := json.Unmarshal(jsonBytes, &data); err != nil {
		// Try single object case (some wrangler versions)
		var single wranglerResult
		if err2 := json.Unmarshal(jsonBytes, &single); err2 == nil {
			return single.Results, nil
		}
		return nil, fmt.Errorf("unmarshal wrangler JSON: %w (raw: %s)", err, firstLine(string(jsonBytes)))
	}
	if len(data) == 0 {
		return []map[string]any{}, nil
	}
	// If multiple statements were sent, wrangler returns multiple result objects.
	// For fetch paths we send single SELECT, so return first.
	return data[0].Results, nil
}

func execCommand(name string, args []string, dir string) ([]byte, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, name, args...)
	cmd.Dir = dir
	// Ensure wrangler creds are found — inherit env
	cmd.Env = os.Environ()
	var out bytes.Buffer
	var errBuf bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &errBuf
	err := cmd.Run()
	combined := append(out.Bytes(), errBuf.Bytes()...)
	if err != nil {
		return combined, err
	}
	return combined, nil
}

func firstLine(s string) string {
	s = strings.TrimSpace(s)
	if idx := strings.Index(s, "\n"); idx != -1 {
		return s[:idx]
	}
	if len(s) > 300 {
		return s[:300]
	}
	return s
}

// FetchLivePings hits gosurvey-telemetry
func FetchLivePings(limit int) ([]Ping, error) {
	if limit <= 0 {
		limit = 500
	}
	sql := fmt.Sprintf("SELECT id, ts, day, install_id, event, version, channel, os, country, email FROM pings ORDER BY id DESC LIMIT %d", limit)
	rows, err := runWrangler("gosurvey-telemetry", sql, "tools/telemetry-worker/wrangler.toml")
	if err != nil {
		return nil, err
	}
	pings := make([]Ping, 0, len(rows))
	for _, r := range rows {
		p := Ping{
			ID:        int(toFloat(r["id"])),
			TS:        toString(r["ts"]),
			Day:       toString(r["day"]),
			InstallID: toString(r["install_id"]),
			Event:     toString(r["event"]),
			Version:   toString(r["version"]),
			Channel:   toString(r["channel"]),
			OS:        toString(r["os"]),
			Country:   toString(r["country"]),
			Email:     toString(r["email"]),
		}
		pings = append(pings, p)
	}
	return pings, nil
}

func FetchLiveUsers(limit int) ([]User, error) {
	if limit <= 0 {
		limit = 500
	}
	sql := fmt.Sprintf("SELECT auth0_sub, email, tier, created_at FROM users ORDER BY created_at DESC LIMIT %d", limit)
	rows, err := runWrangler("gosurvey-accounts", sql, "tools/accounts-worker/wrangler.toml")
	if err != nil {
		return nil, err
	}
	users := make([]User, 0, len(rows))
	for _, r := range rows {
		u := User{
			Auth0Sub:  toString(r["auth0_sub"]),
			Email:     toString(r["email"]),
			Tier:      toString(r["tier"]),
			CreatedAt: toString(r["created_at"]),
		}
		if u.Tier == "" {
			u.Tier = "free"
		}
		users = append(users, u)
	}
	return users, nil
}

func toString(v any) string {
	if v == nil {
		return ""
	}
	switch t := v.(type) {
	case string:
		return t
	case float64:
		// integers from D1 come as float64 via json
		if t == float64(int(t)) {
			return fmt.Sprintf("%d", int(t))
		}
		return fmt.Sprintf("%v", t)
	default:
		return fmt.Sprintf("%v", t)
	}
}
func toFloat(v any) float64 {
	switch t := v.(type) {
	case float64:
		return t
	case int:
		return float64(t)
	case int64:
		return float64(t)
	case string:
		var f float64
		fmt.Sscan(t, &f)
		return f
	default:
		return 0
	}
}

// RefreshLive pulls pings and users from D1 in parallel and swaps them in.
// If only one query fails the other is still applied and the error is surfaced.
func (s *Store) RefreshLive() error {
	if !live.enabled {
		return fmt.Errorf("live is disabled (MOCK=1 / LIVE_D1=0)")
	}
	live.running.Lock()
	defer live.running.Unlock()

	var (
		pings            []Ping
		users            []User
		pingErr, userErr error
		wg               sync.WaitGroup
	)
	wg.Add(2)
	go func() { defer wg.Done(); pings, pingErr = FetchLivePings(1000) }()
	go func() { defer wg.Done(); users, userErr = FetchLiveUsers(1000) }()
	wg.Wait()

	if pingErr != nil {
		setLiveError(pingErr.Error())
		return pingErr
	}
	if userErr != nil {
		// pings succeeded but users failed: keep the previous users
		setLiveError("users: " + userErr.Error())
		s.mu.RLock()
		users = append([]User(nil), s.users...)
		s.mu.RUnlock()
		s.setData(pings, users, "live")
		return userErr
	}
	s.setData(pings, users, "live")
	setLiveError("")
	return nil
}
