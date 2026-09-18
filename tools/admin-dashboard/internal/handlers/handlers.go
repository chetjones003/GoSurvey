package handlers

import (
	"encoding/json"
	"html/template"
	"net/http"
	"strconv"
	"strings"

	"github.com/chetjones003/GoSurvey/tools/admin-dashboard/internal/store"
)

type Handlers struct {
	Store *store.Store
	Tmpl  *template.Template
}

func (h *Handlers) Overview(w http.ResponseWriter, r *http.Request) {
	stats := h.Store.ComputeStats()
	data := map[string]any{
		"Stats":      stats,
		"Mode":       h.Store.Mode(),
		"ActivePage": "overview",
		"Pings":      h.Store.AllPings(),
		"Users":      h.Store.AllUsers(),
	}
	_ = h.Tmpl.ExecuteTemplate(w, "overview.html", data)
}

func (h *Handlers) TelemetryPage(w http.ResponseWriter, r *http.Request) {
	versions := store.DistinctVersions(h.Store.AllPings())
	data := map[string]any{
		"ActivePage": "telemetry",
		"Mode":       h.Store.Mode(),
		"Versions":   versions,
		"Stats":      h.Store.ComputeStats(),
	}
	_ = h.Tmpl.ExecuteTemplate(w, "telemetry.html", data)
}

func (h *Handlers) AccountsPage(w http.ResponseWriter, r *http.Request) {
	data := map[string]any{
		"ActivePage": "accounts",
		"Mode":       h.Store.Mode(),
		"Stats":      h.Store.ComputeStats(),
	}
	_ = h.Tmpl.ExecuteTemplate(w, "accounts.html", data)
}

func (h *Handlers) AnalyticsPage(w http.ResponseWriter, r *http.Request) {
	stats := h.Store.ComputeStats()
	data := map[string]any{
		"ActivePage": "analytics",
		"Mode":       h.Store.Mode(),
		"Stats":      stats,
	}
	_ = h.Tmpl.ExecuteTemplate(w, "analytics.html", data)
}

// PartialPings returns table rows for HTMX.
func (h *Handlers) PartialPings(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	event := q.Get("event")
	channel := q.Get("channel")
	version := q.Get("version")
	search := q.Get("q")
	page, _ := strconv.Atoi(q.Get("page"))
	if page < 1 {
		page = 1
	}
	perPage := 20
	rows, total := h.Store.QueryPings(event, channel, version, search, page, perPage)
	totalPages := (total + perPage - 1) / perPage
	if totalPages < 1 {
		totalPages = 1
	}
	data := map[string]any{
		"Rows":       rows,
		"Total":      total,
		"Page":       page,
		"TotalPages": totalPages,
		"HasPrev":    page > 1,
		"HasNext":    page < totalPages,
		"PrevPage":   page - 1,
		"NextPage":   page + 1,
		"Query":      q,
	}
	_ = h.Tmpl.ExecuteTemplate(w, "pings_rows.html", data)
}

func (h *Handlers) PartialUsers(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	search := q.Get("q")
	tier := q.Get("tier")
	page, _ := strconv.Atoi(q.Get("page"))
	if page < 1 {
		page = 1
	}
	perPage := 20
	rows, total := h.Store.QueryUsers(search, tier, page, perPage)
	totalPages := (total + perPage - 1) / perPage
	if totalPages < 1 {
		totalPages = 1
	}
	data := map[string]any{
		"Rows":       rows,
		"Total":      total,
		"Page":       page,
		"TotalPages": totalPages,
		"HasPrev":    page > 1,
		"HasNext":    page < totalPages,
		"PrevPage":   page - 1,
		"NextPage":   page + 1,
		"Query":      q,
	}
	_ = h.Tmpl.ExecuteTemplate(w, "users_rows.html", data)
}

func (h *Handlers) PartialStats(w http.ResponseWriter, r *http.Request) {
	stats := h.Store.ComputeStats()
	_ = h.Tmpl.ExecuteTemplate(w, "stats_cards.html", stats)
}

// API endpoints (JSON)
func (h *Handlers) APIPings(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	rows, total := h.Store.QueryPings(q.Get("event"), q.Get("channel"), q.Get("version"), q.Get("q"), atoi(q.Get("page"), 1), atoi(q.Get("per_page"), 20))
	writeJSON(w, map[string]any{"rows": rows, "total": total})
}

func (h *Handlers) APIUsers(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	rows, total := h.Store.QueryUsers(q.Get("q"), q.Get("tier"), atoi(q.Get("page"), 1), atoi(q.Get("per_page"), 20))
	writeJSON(w, map[string]any{"rows": rows, "total": total})
}

func (h *Handlers) APIStats(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, h.Store.ComputeStats())
}

func (h *Handlers) APIHealth(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, map[string]any{
		"status": "ok",
		"mode":   h.Store.Mode(),
		"databases": map[string]string{
			"gosurvey-telemetry": "pings — " + strings.Join([]string{"id", "ts", "day", "install_id", "event", "version", "channel", "os", "country", "email"}, ", "),
			"gosurvey-accounts":  "users — auth0_sub, email, tier, created_at",
		},
	})
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(v)
}

func atoi(s string, def int) int {
	if s == "" {
		return def
	}
	n, err := strconv.Atoi(s)
	if err != nil {
		return def
	}
	return n
}
