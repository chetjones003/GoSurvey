package handlers

import (
	"bytes"
	"encoding/json"
	"html/template"
	"log"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	"github.com/chetjones003/GoSurvey/tools/admin-dashboard/internal/store"
)

const perPage = 20

// Handlers serves pages, HTMX partials and the JSON API. Templates are parsed
// once at startup: Pages maps a page name to layout+page, Partials holds the
// table-row fragments.
type Handlers struct {
	Store    *store.Store
	Pages    map[string]*template.Template
	Partials *template.Template
	Asset    string // cache-busting token for /static URLs
}

// page renders layout+page into a buffer first so a template error is a clean 500.
func (h *Handlers) page(w http.ResponseWriter, name, title, sub string, extra map[string]any) {
	data := map[string]any{
		"Title": title, "PageTitle": title, "PageSub": sub, "ActivePage": name,
		"Mode": h.Store.Mode(), "LiveError": store.LiveError(), "Live": store.LiveEnabled(),
		"Updated": h.Store.Updated(), "Asset": h.Asset, "Now": time.Now(),
	}
	for k, v := range extra {
		data[k] = v
	}
	var buf bytes.Buffer
	if err := h.Pages[name].ExecuteTemplate(&buf, "layout.html", data); err != nil {
		log.Printf("render %s: %v", name, err)
		http.Error(w, "render "+name+": "+err.Error(), http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-cache")
	_, _ = buf.WriteTo(w)
}

func (h *Handlers) partial(w http.ResponseWriter, name string, data any) {
	var buf bytes.Buffer
	if err := h.Partials.ExecuteTemplate(&buf, name, data); err != nil {
		log.Printf("render %s: %v", name, err)
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_, _ = buf.WriteTo(w)
}

func (h *Handlers) Overview(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	h.page(w, "overview", "Overview",
		"Installs, activity and versions — last 30 days.",
		map[string]any{"Stats": h.Store.ComputeStats(), "Pings": h.Store.RecentPings(8)})
}

func (h *Handlers) TelemetryPage(w http.ResponseWriter, r *http.Request) {
	h.page(w, "telemetry", "Telemetry",
		"Anonymous install and active pings from gosurvey-telemetry.",
		map[string]any{"Versions": h.Store.Versions(), "Rows": h.pingsView(r.URL.Query())})
}

func (h *Handlers) AccountsPage(w http.ResponseWriter, r *http.Request) {
	h.page(w, "accounts", "Accounts",
		"Registered users from gosurvey-accounts.",
		map[string]any{"Stats": h.Store.ComputeStats(), "Rows": h.usersView(r.URL.Query())})
}

func (h *Handlers) AnalyticsPage(w http.ResponseWriter, r *http.Request) {
	h.page(w, "analytics", "Analytics",
		"The shipped queries.sql blocks, with their current results.",
		map[string]any{"Stats": h.Store.ComputeStats()})
}

// pager is the shared paging state for the table partials.
type pager struct {
	Total, Page, TotalPages, From, To, Cols int
	PrevURL, NextURL                        string // empty = no such page
}

func newPager(base string, cols int, q url.Values, page, total int) pager {
	totalPages := (total + perPage - 1) / perPage
	if totalPages < 1 {
		totalPages = 1
	}
	link := func(p int) string {
		v := url.Values{}
		for k, vals := range q {
			if k != "page" && len(vals) > 0 && vals[0] != "" {
				v.Set(k, vals[0])
			}
		}
		v.Set("page", strconv.Itoa(p))
		return base + "?" + v.Encode()
	}
	p := pager{Total: total, Page: page, TotalPages: totalPages, Cols: cols}
	if total > 0 {
		p.From = (page-1)*perPage + 1
		p.To = min(page*perPage, total)
	}
	if page > 1 {
		p.PrevURL = link(page - 1)
	}
	if page < totalPages {
		p.NextURL = link(page + 1)
	}
	return p
}

func pageParam(q url.Values) int {
	if n, err := strconv.Atoi(q.Get("page")); err == nil && n > 0 {
		return n
	}
	return 1
}

func (h *Handlers) pingsView(q url.Values) map[string]any {
	page := pageParam(q)
	rows, total := h.Store.QueryPings(q.Get("event"), q.Get("channel"), q.Get("version"), q.Get("q"), q.Get("sort"), q.Get("dir"), page, perPage)
	return map[string]any{"Rows": rows, "Pager": newPager("/partials/pings", 8, q, page, total), "Now": time.Now()}
}

func (h *Handlers) usersView(q url.Values) map[string]any {
	page := pageParam(q)
	rows, total := h.Store.QueryUsers(q.Get("q"), q.Get("tier"), q.Get("sort"), q.Get("dir"), page, perPage)
	return map[string]any{"Rows": rows, "Pager": newPager("/partials/users", 4, q, page, total), "Now": time.Now()}
}

// PartialPings returns table rows for HTMX.
func (h *Handlers) PartialPings(w http.ResponseWriter, r *http.Request) {
	h.partial(w, "pings_rows.html", h.pingsView(r.URL.Query()))
}

func (h *Handlers) PartialUsers(w http.ResponseWriter, r *http.Request) {
	h.partial(w, "users_rows.html", h.usersView(r.URL.Query()))
}

func (h *Handlers) PartialStats(w http.ResponseWriter, r *http.Request) {
	h.partial(w, "stats_cards.html", h.Store.ComputeStats())
}

// Refresh re-pulls D1 on demand (the Refresh button); without LIVE_D1 it is a no-op.
func (h *Handlers) Refresh(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}
	if store.LiveEnabled() {
		if err := h.Store.RefreshLive(); err != nil {
			http.Error(w, err.Error(), http.StatusBadGateway)
			return
		}
	}
	w.WriteHeader(http.StatusNoContent)
}

// API endpoints (JSON)
func (h *Handlers) APIPings(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	rows, total := h.Store.QueryPings(q.Get("event"), q.Get("channel"), q.Get("version"), q.Get("q"), q.Get("sort"), q.Get("dir"), atoi(q.Get("page"), 1), atoi(q.Get("per_page"), 20))
	writeJSON(w, map[string]any{"rows": rows, "total": total})
}

func (h *Handlers) APIUsers(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	rows, total := h.Store.QueryUsers(q.Get("q"), q.Get("tier"), q.Get("sort"), q.Get("dir"), atoi(q.Get("page"), 1), atoi(q.Get("per_page"), 20))
	writeJSON(w, map[string]any{"rows": rows, "total": total})
}

func (h *Handlers) APIStats(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, h.Store.ComputeStats())
}

func (h *Handlers) APIHealth(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, map[string]any{
		"status":      "ok",
		"mode":        h.Store.Mode(),
		"liveEnabled": store.LiveEnabled(),
		"loaded":      !h.Store.Updated().IsZero(),
		"error":       store.LiveError(),
		"updated":     h.Store.Updated().UTC().Format(time.RFC3339),
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
