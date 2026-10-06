package handlers

import (
	"encoding/json"
	"html/template"
	"strconv"
	"strings"
	"time"
)

// FuncMap holds the template helpers that turn raw D1 values into readable ones.
var FuncMap = template.FuncMap{
	"toJSON": func(v any) template.JS {
		b, _ := json.Marshal(v)
		return template.JS(b)
	},
	"percent": func(n, denom int) int {
		if denom <= 0 {
			return 0
		}
		return min(100, n*100/denom)
	},
	"num":     num,
	"ago":     ago,
	"country": countryName,
	"short":   short,
	"date":    dateOnly,
	"stamp":   stamp,
}

// num formats 12345 as "12,345".
func num(n int) string {
	s := strconv.Itoa(n)
	neg := strings.HasPrefix(s, "-")
	if neg {
		s = s[1:]
	}
	for i := len(s) - 3; i > 0; i -= 3 {
		s = s[:i] + "," + s[i:]
	}
	if neg {
		s = "-" + s
	}
	return s
}

func parseTS(ts string) (time.Time, bool) {
	for _, layout := range []string{time.RFC3339, "2006-01-02 15:04:05", "2006-01-02"} {
		if t, err := time.Parse(layout, ts); err == nil {
			return t, true
		}
	}
	return time.Time{}, false
}

// ago turns an ISO timestamp into "5m ago" / "3d ago"; older than 30 days it
// falls back to the date. Unparseable input is returned unchanged.
func ago(ts string) string {
	t, ok := parseTS(ts)
	if !ok {
		return ts
	}
	d := time.Since(t)
	switch {
	case d < 0, d < time.Minute:
		return "just now"
	case d < time.Hour:
		return strconv.Itoa(int(d.Minutes())) + "m ago"
	case d < 24*time.Hour:
		return strconv.Itoa(int(d.Hours())) + "h ago"
	case d < 30*24*time.Hour:
		return strconv.Itoa(int(d.Hours()/24)) + "d ago"
	}
	return t.Format("2 Jan 2006")
}

func dateOnly(ts string) string {
	if t, ok := parseTS(ts); ok {
		return t.Format("2 Jan 2006")
	}
	return ts
}

// short truncates an identifier for table display (the full value stays in a tooltip).
func short(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "…"
}

var countries = map[string]string{
	"US": "United States", "CA": "Canada", "GB": "United Kingdom", "DE": "Germany",
	"AU": "Australia", "NZ": "New Zealand", "FR": "France", "JP": "Japan",
	"MX": "Mexico", "BR": "Brazil", "IN": "India", "ES": "Spain", "IT": "Italy",
	"NL": "Netherlands", "SE": "Sweden", "NO": "Norway", "IE": "Ireland",
	"ZA": "South Africa", "CN": "China", "KR": "South Korea",
}

func countryName(code string) string {
	switch {
	case code == "" || code == "unknown":
		return "Unknown"
	case countries[code] != "":
		return countries[code]
	}
	return code
}

// stamp renders an ISO timestamp as "2026-10-06 22:33:32" (UTC).
func stamp(ts string) string {
	if t, ok := parseTS(ts); ok {
		return t.UTC().Format("2006-01-02 15:04:05")
	}
	return ts
}
