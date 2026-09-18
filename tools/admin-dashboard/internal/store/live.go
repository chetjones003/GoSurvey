package store

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"
)

// WranglerResult mirrors the JSON wrangler --json emits for D1.
type wranglerResult struct {
	Results []map[string]any `json:"results"`
	Meta    any              `json:"meta"`
}

// liveState manages periodic refresh when LIVE_D1=1.
type liveState struct {
	mu        sync.Mutex
	enabled   bool
	lastFetch time.Time
	lastErr   string
	ttl       time.Duration
}

var live = &liveState{ttl: 30 * time.Second}

func init() {
	if os.Getenv("LIVE_D1") == "1" || os.Getenv("LIVE") == "1" {
		live.enabled = true
	}
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

// shouldRefresh returns true if live is enabled and ttl expired.
func shouldRefresh() bool {
	if !live.enabled {
		return false
	}
	live.mu.Lock()
	defer live.mu.Unlock()
	return time.Since(live.lastFetch) > live.ttl
}

func markFetched() {
	live.mu.Lock()
	live.lastFetch = time.Now()
	live.mu.Unlock()
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
	// Prefer npx wrangler, fallback to wrangler
	// Build command
	// On Windows cmd, npx is npx.cmd — exec will resolve via PATH with extension.
	npx := "npx"
	if runtime.GOOS == "windows" {
		// exec will find npx.cmd via PATHEXT, but be explicit for logging
		npx = "npx"
	}
	args := []string{"wrangler", "d1", "execute", db, "--remote", "--json", "--config", cfgAbs, "--command", sql}
	// Try npx wrangler first
	out, err := execCommand(npx, args, root)
	if err != nil {
		// Try bare wrangler
		out2, err2 := execCommand("wrangler", args[1:], root)
		if err2 != nil {
			return nil, fmt.Errorf("wrangler failed (%v / %v): %s", err, err2, firstLine(string(out)+string(out2)))
		}
		out = out2
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
	cmd := exec.Command(name, args...)
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

// RefreshLive attempts to pull live data and update the store; returns error if live disabled or fetch fails.
func (s *Store) RefreshLive() error {
	if !live.enabled {
		return fmt.Errorf("live not enabled (set LIVE_D1=1)")
	}
	pings, err := FetchLivePings(1000)
	if err != nil {
		setLiveError(err.Error())
		return err
	}
	users, err := FetchLiveUsers(1000)
	if err != nil {
		// pings succeeded but users failed — still use pings
		setLiveError("users: " + err.Error())
		// keep users as before, but update pings
		s.mu.Lock()
		s.pings = pings
		s.mode = "live"
		s.mu.Unlock()
		markFetched()
		return err
	}
	s.mu.Lock()
	s.pings = pings
	s.users = users
	s.mode = "live"
	s.mu.Unlock()
	setLiveError("")
	markFetched()
	return nil
}

// EnsureLive tries refresh if stale; returns live error if any.
func (s *Store) EnsureLive() {
	if !shouldRefresh() {
		return
	}
	_ = s.RefreshLive()
}
